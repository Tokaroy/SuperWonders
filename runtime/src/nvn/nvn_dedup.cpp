// Per-submit upload reuse (docs/nvn/DEBUG.md #30). A draw's buffers are copied from guest memory into the upload
// ring when it is replayed; the same camera block, scene constants and vertex buffers were copied again for every
// draw that bound them (~130 KB per draw, 50-70 MB per frame). Within one submit guest memory only changes through
// inline uniform updates (UpdateUniformBuffer, which invalidates what it overlaps) and compute results (ranges a
// dispatch writes are read from their written copy, nvn_written.cpp, and invalidated here): a copy made once can
// serve every later draw of the submit that binds the same bytes.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "nvn_draw_state.h"

namespace nx::nvn::replay {

namespace {

struct Entry {
    uint64_t size = 0, padded = 0;
    Upload upload;
};

constexpr uint32_t kTableSize = 16384;
constexpr uint32_t kPageBuckets = 4096;

struct Slot {
    const uint8_t* p = nullptr;
    uint64_t generation = 0;
    bool valid = false;
    Entry entry;
};

struct PageBucket {
    uint64_t generation = 0;
    std::vector<uint32_t> slots;
};

Slot g_table[kTableSize];
PageBucket g_pages[kPageBuckets];
uint64_t g_generation = 1;
uint32_t g_table_used = 0;
uint64_t g_saved = 0, g_copied = 0;

uint32_t slot_hash(const uint8_t* p) {
    uintptr_t v = uintptr_t(p);
    return uint32_t(((v >> 4) ^ ((v >> 14) * 0x9E3779B9u)) & (kTableSize - 1));
}

void register_pages(const uint8_t* p, uint64_t size, uint32_t idx) {
    const uintptr_t p0 = uintptr_t(p) >> 16;
    const uintptr_t p1 = uintptr_t(p + (size ? size - 1 : 0)) >> 16;
    const uintptr_t span = std::min<uintptr_t>(p1 - p0, kPageBuckets - 1);
    for (uintptr_t k = 0; k <= span; ++k) {
        PageBucket& b = g_pages[(p0 + k) & (kPageBuckets - 1)];
        if (b.generation != g_generation) {
            b.generation = g_generation;
            b.slots.clear();
        }
        b.slots.push_back(idx);
    }
}

}  // namespace

Upload dedup_upload(const uint8_t* p, uint64_t size, uint64_t padded, uint64_t align) {
    if (!p || !size) {
        Upload u = upload_alloc(padded, align);
        std::memset(u.ptr, 0, padded);
        return u;
    }
    static const bool off = std::getenv("NX_NO_DEDUP") != nullptr;  // bisecting: a fresh copy every time
    uint32_t target_idx = UINT32_MAX;
    if (!off) {
        if (g_table_used >= kTableSize / 2) dedup_reset();
        const uint32_t h = slot_hash(p);
        for (uint32_t probe = 0; probe < kTableSize; ++probe) {
            const uint32_t idx = (h + probe) & (kTableSize - 1);
            Slot& s = g_table[idx];
            if (s.generation != g_generation) {
                if (target_idx == UINT32_MAX) target_idx = idx;
                break;
            }
            if (!s.valid) {
                if (target_idx == UINT32_MAX) target_idx = idx;
                continue;
            }
            if (s.p == p) {
                if (s.entry.size == size && s.entry.padded >= padded && s.entry.upload.offset % align == 0) {
                    g_saved += padded;
                    return s.entry.upload;
                }
                target_idx = idx;
                break;
            }
        }
    }
    Upload u = upload_alloc(padded, align);
    std::memcpy(u.ptr, p, size);
    if (padded > size) std::memset(u.ptr + size, 0, padded - size);
    if (!off && target_idx != UINT32_MAX) {
        Slot& s = g_table[target_idx];
        if (s.generation != g_generation) ++g_table_used;
        s = {p, g_generation, true, {size, padded, u}};
        register_pages(p, size, target_idx);
    }
    g_copied += padded;
    return u;
}

void dedup_reset() {
    ++g_generation;
    g_table_used = 0;
}

void dedup_invalidate(const uint8_t* p, uint64_t size) {
    if (!p || !size) return;
    const uintptr_t p0 = uintptr_t(p) >> 16;
    const uintptr_t p1 = uintptr_t(p + size - 1) >> 16;
    const uintptr_t span = std::min<uintptr_t>(p1 - p0, kPageBuckets - 1);
    for (uintptr_t k = 0; k <= span; ++k) {
        PageBucket& b = g_pages[(p0 + k) & (kPageBuckets - 1)];
        if (b.generation != g_generation) continue;
        for (uint32_t idx : b.slots) {
            Slot& s = g_table[idx];
            if (s.generation == g_generation && s.valid && s.p < p + size && p < s.p + s.entry.size)
                s.valid = false;
        }
    }
}

// c[0], the NVN driver's constants, as far as shaders read them (docs/nvn/TRANSLATOR.md): buffer descriptors
// {u64 GPU address; u32 size} of what is bound, which compute shaders search to find the buffer an address falls in
// (they wrote garbage with a zero table), and the maximum point size.
std::array<uint8_t, 0x1000> driver_constants(ShaderStage stage, int st) {
    std::array<uint8_t, 0x1000> c0{};
    auto put = [&](uint32_t at, const Range& r) {
        if (at + 12 > c0.size()) return;
        std::memcpy(c0.data() + at, &r.address, 8);
        uint32_t size = uint32_t(std::min<uint64_t>(r.size, 0xFFFFFFFFu));
        std::memcpy(c0.data() + at + 8, &size, 4);
    };
    if (stage == ShaderStage::Compute) {
        for (int n = 0; n < 16; n++) put(0x210 + 0x10 * n, g_state.uniforms[st][n]);
        for (int n = 0; n < 16; n++) put(0x310 + 0x10 * n, g_state.storage[st][n]);
    } else {
        int hw = stage == ShaderStage::Fragment ? 4 : 0;  // Maxwell stage numbers: vertex 0, fragment 4
        for (int n = 0; n < 16; n++) put(0x110 + 0x100 * hw + 0x10 * n, g_state.storage[st][n]);
        float max_point = 64.0f;
        std::memcpy(c0.data() + 0x610, &max_point, 4);
    }
    return c0;
}

void dedup_log() {
    if (g_copied + g_saved)
        log("nvn: uploads copied %.1f MB, reused %.1f MB", double(g_copied) / 1e6, double(g_saved) / 1e6);
    g_copied = g_saved = 0;
}

}  // namespace nx::nvn::replay
