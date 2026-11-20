// acc:u0 (accounts) and the storage objects am hands out (launch parameters).
#include "ipc.h"
#include "user_profiles.h"
#include "profile_image.h"

#include <algorithm>
#include <fstream>
#include <filesystem>

namespace nx {

// The launcher selects one local profile before the game starts.
const profiles::User& user() { return profiles::current(); }

// IStorage / IStorageAccessor over a byte blob (am's storage objects).
std::shared_ptr<Service> make_storage(std::vector<uint8_t> data) {
    auto blob = std::make_shared<std::vector<uint8_t>>(std::move(data));
    auto s = std::make_shared<Service>("IStorage");
    s->cmds[0] = [blob](IpcCtx& c) {  // Open -> IStorageAccessor
        auto a = std::make_shared<Service>("IStorageAccessor");
        a->cmds[0] = [blob](IpcCtx& c) { c.push<int64_t>(int64_t(blob->size())); return 0u; };  // GetSize
        a->cmds[11] = [blob](IpcCtx& c) {                                                       // Read(offset)
            int64_t off = c.arg<int64_t>(0);
            Buffer out = c.out_buf(0);
            if (off < 0 || uint64_t(off) > blob->size()) return 0x3E880u;  // out of bounds
            size_t n = std::min<size_t>(out.size, blob->size() - size_t(off));
            std::memcpy(reinterpret_cast<void*>(out.addr), blob->data() + off, n);
            return 0u;
        };
        c.push_object(a);
        return 0u;
    };
    return s;
}

std::shared_ptr<Service> preselected_user_storage() {
    std::vector<uint8_t> d(0x88, 0);
    uint32_t magic = 0xC79497CA, selected = 1;
    std::memcpy(d.data(), &magic, 4);
    std::memcpy(d.data() + 4, &selected, 4);
    std::memcpy(d.data() + 8, &user().lo, 8);
    std::memcpy(d.data() + 16, &user().hi, 8);
    return make_storage(std::move(d));
}

namespace {

void push_uid(IpcCtx& c) {
    c.push<uint64_t>(user().lo);
    c.push<uint64_t>(user().hi);
}

void write_uid_list(IpcCtx& c) {
    Buffer out = c.out_buf(0);
    if (out.addr && out.size >= 16) {
        std::memset(reinterpret_cast<void*>(out.addr), 0, out.size);
        st<uint64_t>(out.addr, user().lo);
        st<uint64_t>(out.addr + 8, user().hi);
    }
}

std::shared_ptr<Service> profile() {
    auto s = std::make_shared<Service>("IProfile");
    auto base = [](IpcCtx& c) {  // ProfileBase {Uid; u64 last_edit; char nickname[0x20]}
        push_uid(c);
        c.push<uint64_t>(0);
        char name[0x20] = {};
        std::memcpy(name, user().name.data(), std::min(user().name.size(), sizeof(name) - 1));
        for (char ch : name) c.push<char>(ch);
    };
    s->cmds[0] = [base](IpcCtx& c) {  // Get(out UserData buffer) -> ProfileBase
        Buffer out = c.out_buf(0);
        if (out.addr) std::memset(reinterpret_cast<void*>(out.addr), 0, out.size);
        base(c);
        return 0u;
    };
    s->cmds[1] = [base](IpcCtx& c) { base(c); return 0u; };            // GetBase
    auto image = [] {
        std::vector<uint8_t> bytes;
        if (user().icon.empty()) return bytes;
        if (user().icon.rfind("builtin:", 0) == 0 && user().icon.size() == 9)
            return profiles::builtin_icon(user().icon[8] - '0');
        std::ifstream f(std::filesystem::u8path(user().icon), std::ios::binary | std::ios::ate);
        if (!f) return bytes;
        const auto size = f.tellg();
        if (size <= 0 || size > 0x20000) return bytes;
        bytes.resize(size_t(size));
        f.seekg(0);
        if (!f.read(reinterpret_cast<char*>(bytes.data()), size) || !profiles::valid_icon(bytes)) bytes.clear();
        return bytes;
    };
    s->cmds[10] = [image](IpcCtx& c) { c.push<uint32_t>(uint32_t(image().size())); return 0u; };
    s->cmds[11] = [image](IpcCtx& c) {
        const auto bytes = image();
        const Buffer out = c.out_buf(0);
        const size_t n = out.addr ? std::min<size_t>(bytes.size(), out.size) : 0;
        if (n) std::memcpy(reinterpret_cast<void*>(out.addr), bytes.data(), n);
        c.push<uint32_t>(uint32_t(n));
        return 0u;
    };
    return s;
}

std::shared_ptr<Service> manager_for_application() {
    auto s = std::make_shared<Service>("IManagerForApplication");
    s->cmds[0] = [](IpcCtx& c) { c.push<uint8_t>(0); return 0u; };  // CheckAvailability
    s->cmds[1] = [](IpcCtx& c) { c.push<uint64_t>(0x1234ABCD5678EF90ull); return 0u; };  // GetAccountId
    return s;
}

void account(Service& s) {
    s.cmds[0] = [](IpcCtx& c) { c.push<int32_t>(1); return 0u; };  // GetUserCount
    s.cmds[1] = [](IpcCtx& c) {                                     // GetUserExistence(uid)
        c.push<uint8_t>(c.arg<uint64_t>(0) == user().lo && c.arg<uint64_t>(8) == user().hi);
        return 0u;
    };
    s.cmds[2] = [](IpcCtx& c) { write_uid_list(c); return 0u; };  // ListAllUsers
    s.cmds[3] = [](IpcCtx& c) { write_uid_list(c); return 0u; };  // ListOpenUsers
    s.cmds[4] = [](IpcCtx& c) { push_uid(c); return 0u; };        // GetLastOpenedUser
    s.cmds[5] = [](IpcCtx& c) { c.push_object(profile()); return 0u; };  // GetProfile
    s.cmds[50] = [](IpcCtx& c) { c.push<uint8_t>(0); return 0u; };  // IsUserRegistrationRequestPermitted
    s.cmds[51] = [](IpcCtx& c) { push_uid(c); return 0u; };         // TrySelectUserWithoutInteraction
    s.cmds[100] = [](IpcCtx&) { return 0u; };                       // InitializeApplicationInfo (old)
    s.cmds[101] = [](IpcCtx& c) { c.push_object(manager_for_application()); return 0u; };
    s.cmds[130] = [](IpcCtx& c) {                                   // ActivateOpenContextHolder
        c.push_object(std::make_shared<Service>("IOpenContext"));
        return 0u;
    };
    s.cmds[131] = [](IpcCtx& c) {                                   // ListOpenContextStoredUsers
        Buffer out = c.out_buf(0);
        if (out.addr && out.size) std::memset(reinterpret_cast<void*>(out.addr), 0, out.size);
        return 0u;
    };
    s.cmds[140] = [](IpcCtx&) { return 0u; };  // InitializeApplicationInfo
    s.cmds[141] = [](IpcCtx& c) { write_uid_list(c); return 0u; };  // ListQualifiedUsers (SDK 16)
    s.cmds[160] = [](IpcCtx&) { return 0u; };                       // InitializeApplicationInfoV2 (SDK 16)
    s.cmds[150] = [](IpcCtx& c) { c.push<uint8_t>(0); return 0u; };  // IsUserAccountSwitchLocked
}

ServiceRegistrar r_acc_u0("acc:u0", [] { auto s = std::make_shared<Service>("acc:u0"); account(*s); return s; });
ServiceRegistrar r_acc_u1("acc:u1", [] { auto s = std::make_shared<Service>("acc:u1"); account(*s); return s; });

}  // namespace

}  // namespace nx
