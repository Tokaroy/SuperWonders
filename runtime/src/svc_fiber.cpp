// nn::os::Fiber HLE backed by native Win32 Fibers (Windows) so NPLN/gRPC fibers context-switch cleanly on x86_64.
#include <nx/runtime.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace nx {

void backtrace(CPU&);

namespace {

struct HostFiber {
    uint64_t guest_fiber = 0;
    uint64_t entry = 0;
    uint64_t arg = 0;
    uint64_t stack_top = 0;
    std::atomic<bool> active{false};
    CPU cpu{};
#if defined(_WIN32)
    void* win_fiber = nullptr;
#endif
};

std::mutex g_fiber_mu;
std::unordered_map<uint64_t, HostFiber*> g_fibers;

#if defined(_WIN32)
struct ThreadFiberState {
    void* root_fiber = nullptr;
    uint64_t current_guest = 0;
    HostFiber* current_host = nullptr;
    HostFiber* prev_to_release = nullptr;
};

DWORD fiber_tls_slot() {
    static const DWORD slot = TlsAlloc();
    return slot;
}

__attribute__((noinline)) ThreadFiberState& thread_fiber_state() {
    const DWORD slot = fiber_tls_slot();
    auto* st = static_cast<ThreadFiberState*>(TlsGetValue(slot));
    if (!st) {
        st = new ThreadFiberState();
        TlsSetValue(slot, st);
    }
    return *st;
}

__attribute__((noinline)) void read_thread_tls_ptrs(uint64_t& tpidr, uint64_t& tpidrro) {
    asm volatile("" ::: "memory");
    CPU& c = current_cpu();
    tpidr = c.tpidr;
    tpidrro = c.tpidrro;
}

__attribute__((noinline)) void win_switch_fiber(void* target_win) {
    asm volatile("" ::: "memory");
    ::SwitchToFiber(target_win);
    asm volatile("" ::: "memory");
}

void release_prev_fiber() {
    auto& st = thread_fiber_state();
    if (st.prev_to_release) {
        st.prev_to_release->active.store(false, std::memory_order_release);
        st.prev_to_release = nullptr;
    }
}
#endif

void acquire_target_fiber(HostFiber* hf) {
    if (!hf) return;
    while (hf->active.exchange(true, std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

void write_thread_current_fiber(uint64_t tpidrro, uint64_t guest_fiber) {
    if (!tpidrro) return;
    const uint64_t thread_ptr = ld<uint64_t>(tpidrro + 0x1F8);
    if (thread_ptr) st<uint64_t>(thread_ptr + 0x70, guest_fiber);
}

#if defined(_WIN32)
VOID WINAPI fiber_entry_proc(PVOID param) {
    release_prev_fiber();
    auto* hf = static_cast<HostFiber*>(param);
    read_thread_tls_ptrs(hf->cpu.tpidr, hf->cpu.tpidrro);
    hf->cpu.x[0] = hf->arg;
    hf->cpu.x[29] = 0;
    hf->cpu.x[30] = 0;
    hf->cpu.x[31] = hf->stack_top;
    call(hf->cpu, hf->entry);
    const uint64_t next_guest = hf->cpu.x[0];
    static int ret_logs = 0;
    if (ret_logs < 25) {
        ++ret_logs;
        log("fiber: [tid=%lu] entry_return %llx -> %llx", (unsigned long)GetCurrentThreadId(),
            (unsigned long long)hf->guest_fiber, (unsigned long long)next_guest);
    }
    HostFiber* target_hf = nullptr;
    void* target_win = thread_fiber_state().root_fiber;
    if (next_guest) {
        std::lock_guard lk(g_fiber_mu);
        auto it = g_fibers.find(next_guest);
        if (it != g_fibers.end() && it->second->win_fiber) {
            target_hf = it->second;
            target_win = target_hf->win_fiber;
        }
    }
    acquire_target_fiber(target_hf);
    uint64_t tpidr = 0, tpidrro = 0;
    read_thread_tls_ptrs(tpidr, tpidrro);
    {
        auto& st = thread_fiber_state();
        st.current_guest = next_guest;
        st.current_host = target_hf;
        st.prev_to_release = hf;
    }
    if (target_hf) {
        target_hf->cpu.tpidr = tpidr;
        target_hf->cpu.tpidrro = tpidrro;
    }
    write_thread_current_fiber(tpidrro, next_guest);
    if (target_win) win_switch_fiber(target_win);
}
#endif

void hle_initialize_fiber(CPU& c) {
    const uint64_t fiber_ptr = c.x[0];
    const uint64_t entry = c.x[1];
    const uint64_t arg = c.x[2];
    const uint64_t stack = c.x[3];
    const uint64_t stack_size = c.x[4];
    if (!fiber_ptr || !entry) return;
    st<uint64_t>(fiber_ptr + 0x40, stack);
    st<uint64_t>(fiber_ptr + 0x48, stack_size);

    static int init_logs = 0;
    if (init_logs < 200) {
        ++init_logs;
        log("fiber: [tid=%lu] InitializeFiber ptr=%llx entry=%llx arg=%llx lr=%s",
            (unsigned long)GetCurrentThreadId(),
            (unsigned long long)fiber_ptr, (unsigned long long)entry,
            (unsigned long long)arg, describe(c.x[30]).c_str());
    }
    auto* hf = new HostFiber();
    hf->guest_fiber = fiber_ptr;
    hf->entry = entry;
    hf->arg = arg;
    hf->stack_top = (stack + stack_size) & ~15ull;
#if defined(_WIN32)
    const SIZE_T host_stack = SIZE_T(std::max<uint64_t>(stack_size, 2ull * 1024 * 1024));
    hf->win_fiber = CreateFiber(host_stack, fiber_entry_proc, hf);
#endif
    HostFiber* old_hf = nullptr;
    {
        std::lock_guard lk(g_fiber_mu);
        if (auto it = g_fibers.find(fiber_ptr); it != g_fibers.end()) {
            old_hf = it->second;
        }
        g_fibers[fiber_ptr] = hf;
    }
    if (old_hf) {
        while (old_hf->active.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
#if defined(_WIN32)
        if (old_hf->win_fiber && old_hf->win_fiber != GetCurrentFiber()) DeleteFiber(old_hf->win_fiber);
#endif
        delete old_hf;
    }
}

void hle_switch_to_fiber(CPU& c) {
#if defined(_WIN32)
    const uint64_t target_guest = c.x[0];
    void* root_fiber = nullptr;
    uint64_t cur_guest = 0;
    HostFiber* cur_host = nullptr;
    {
        auto& st = thread_fiber_state();
        if (!st.root_fiber) {
            st.root_fiber = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(nullptr);
        }
        root_fiber = st.root_fiber;
        cur_guest = st.current_guest;
        cur_host = st.current_host;
    }
    HostFiber* target_hf = nullptr;
    void* target_win = nullptr;
    if (target_guest == 0) {
        target_win = root_fiber;
    } else {
        std::lock_guard lk(g_fiber_mu);
        auto it = g_fibers.find(target_guest);
        if (it != g_fibers.end()) {
            target_hf = it->second;
            target_win = target_hf->win_fiber;
        }
    }
    static int sw_logs = 0;
    if (sw_logs < 80) {
        ++sw_logs;
        log("fiber: [tid=%lu] SwitchToFiber %llx -> %llx", (unsigned long)GetCurrentThreadId(),
            (unsigned long long)cur_guest, (unsigned long long)target_guest);
        if (sw_logs <= 15 && cur_guest != 0 && target_guest == 0) {
            backtrace(c);
        }
    }
    if (target_guest == cur_guest) return;
    if (!target_win) return;
    // Each fiber owns its own HostFiber::cpu and an `active` handoff flag so cross-thread
    // fiber migration never runs or frees the same Win32 fiber on two threads concurrently.
    // Never cache ThreadFiberState& or thread_local across win_switch_fiber().
    acquire_target_fiber(target_hf);
    uint64_t tpidr = 0, tpidrro = 0;
    read_thread_tls_ptrs(tpidr, tpidrro);
    {
        auto& st = thread_fiber_state();
        st.current_guest = target_guest;
        st.current_host = target_hf;
        st.prev_to_release = cur_host;
    }
    if (target_hf && target_hf->cpu.x[30] == 0) {
        static int fr_logs = 0;
        if (fr_logs < 200) {
            ++fr_logs;
            log("fiber: [tid=%lu] first_run ptr=%llx arg=%llx", (unsigned long)GetCurrentThreadId(),
                (unsigned long long)target_guest, (unsigned long long)target_hf->arg);
        }
    }
    if (cur_host) {
        cur_host->cpu.x[29] = c.x[29];
        cur_host->cpu.x[30] = c.x[30];
    }
    if (target_hf) {
        target_hf->cpu.tpidr = tpidr;
        target_hf->cpu.tpidrro = tpidrro;
    }
    write_thread_current_fiber(tpidrro, target_guest);
    win_switch_fiber(target_win);
    release_prev_fiber();
    {
        auto& st = thread_fiber_state();
        st.current_guest = cur_guest;
        st.current_host = cur_host;
    }
    read_thread_tls_ptrs(c.tpidr, c.tpidrro);
    write_thread_current_fiber(c.tpidrro, cur_guest);
#else
    (void)c;
#endif
}

void hle_finalize_fiber(CPU& c) {
    const uint64_t fiber_ptr = c.x[0];
    HostFiber* hf = nullptr;
    {
        std::lock_guard lk(g_fiber_mu);
        auto it = g_fibers.find(fiber_ptr);
        if (it != g_fibers.end()) {
            hf = it->second;
            g_fibers.erase(it);
        }
    }
    if (hf) {
        static int fin_logs = 0;
        if (fin_logs < 200) {
            ++fin_logs;
            log("fiber: [tid=%lu] FinalizeFiber %llx", (unsigned long)GetCurrentThreadId(),
                (unsigned long long)fiber_ptr);
        }
        while (hf->active.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
#if defined(_WIN32)
        if (hf->win_fiber && hf->win_fiber != GetCurrentFiber()) DeleteFiber(hf->win_fiber);
#endif
        delete hf;
    }
}

void hle_get_current_fiber(CPU& c) {
#if defined(_WIN32)
    c.x[0] = thread_fiber_state().current_guest;
#else
    c.x[0] = 0;
#endif
}

}  // namespace

void dump_active_fibers(std::string (*describe)(uint64_t)) {
    std::lock_guard lk(g_fiber_mu);
    for (const auto& [ptr, hf] : g_fibers) {
        if (!hf) continue;
        const bool is_active = hf->active.load(std::memory_order_acquire);
        std::string chain = describe(hf->cpu.x[30]);
        uint64_t fp = hf->cpu.x[29];
        for (int k = 0; k < 12 && fp && (fp & 7) == 0 && fp >= kModuleBase; k++) {
            uint64_t lr = ld<uint64_t>(fp + 8);
            if (!lr) break;
            chain += " < " + describe(lr);
            uint64_t next = ld<uint64_t>(fp);
            if (next <= fp) break;
            fp = next;
        }
        log("fibers: ptr=%llx active=%d entry=%s %s", (unsigned long long)ptr, is_active ? 1 : 0,
            describe(hf->entry).c_str(), chain.c_str());
    }
}

NX_HLE("_ZN2nn2os15InitializeFiberEPNS0_9FiberTypeEPFS2_PvES3_S3_mi", hle_initialize_fiber);
NX_HLE("_ZN2nn2os13SwitchToFiberEPNS0_9FiberTypeE", hle_switch_to_fiber);
NX_HLE("_ZN2nn2os13FinalizeFiberEPNS0_9FiberTypeE", hle_finalize_fiber);
NX_HLE("_ZN2nn2os15GetCurrentFiberEv", hle_get_current_fiber);

}  // namespace nx
