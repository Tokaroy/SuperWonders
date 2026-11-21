// Nextendo Network OAuth 2.0 PKCE client, nex-token manager, BAAS id_token RS256 signer and session heartbeat.
#include "nextendo_client.h"

#include <nx/runtime.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>
#endif

namespace nx::nextendo {

namespace {

constexpr const char* kDefaultClientId = "nxc_d91GmCAGSfSp";
constexpr uint16_t kCallbackPort = 47823;
constexpr const char* kRedirectUri = "http://127.0.0.1:47823/callback";

std::mutex g_mu;
std::string g_data_root;
bool g_enabled = false;
std::string g_client_id = kDefaultClientId;
std::string g_access_token;
std::string g_refresh_token;
std::string g_nex_token;
uint64_t g_pid = 0;
std::string g_username;
std::string g_friend_code;
int64_t g_nex_expires_at = 0;
std::string g_last_error;
std::atomic<bool> g_login_busy{false};
std::atomic<bool> g_heartbeat_stop{false};
std::thread g_heartbeat_thread;
std::string g_cached_jwt;
int64_t g_cached_jwt_exp = 0;

std::string api_base_host() {
    if (const char* ov = std::getenv("NEXTENDO_API"); ov && *ov) {
        std::string s = ov;
        if (s.rfind("https://", 0) == 0) s = s.substr(8);
        else if (s.rfind("http://", 0) == 0) s = s.substr(7);
        while (!s.empty() && s.back() == '/') s.pop_back();
        return s;
    }
    return "nextendo.network";
}

std::string base64url(const uint8_t* data, size_t len) {
    static constexpr char kTbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = (i + 1 < len) ? data[i + 1] : 0;
        const uint32_t c = (i + 2 < len) ? data[i + 2] : 0;
        const uint32_t triple = (a << 16) | (b << 8) | c;
        out.push_back(kTbl[(triple >> 18) & 0x3F]);
        out.push_back(kTbl[(triple >> 12) & 0x3F]);
        if (i + 1 < len) out.push_back(kTbl[(triple >> 6) & 0x3F]);
        if (i + 2 < len) out.push_back(kTbl[triple & 0x3F]);
    }
    return out;
}

std::string base64url_str(const std::string& s) {
    return base64url(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

std::string url_encode(const std::string& s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char ch : s) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out.push_back(char(ch));
        } else {
            out.push_back('%');
            out.push_back(kHex[(ch >> 4) & 0xF]);
            out.push_back(kHex[ch & 0xF]);
        }
    }
    return out;
}

std::string json_get_string(const std::string& json, const char* key) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return {};
    p = json.find(':', p + pat.size());
    if (p == std::string::npos) return {};
    p = json.find_first_not_of(" \t\r\n", p + 1);
    if (p == std::string::npos || json[p] != '"') return {};
    ++p;
    std::string out;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) {
            ++p;
        }
        out.push_back(json[p++]);
    }
    return out;
}

int64_t json_get_int(const std::string& json, const char* key) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return 0;
    p = json.find(':', p + pat.size());
    if (p == std::string::npos) return 0;
    p = json.find_first_not_of(" \t\r\n", p + 1);
    if (p == std::string::npos) return 0;
    return std::strtoll(json.c_str() + p, nullptr, 10);
}

void save_locked() {
    if (g_data_root.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(g_data_root), ec);
    const auto p = std::filesystem::u8path(g_data_root) / "nextendo.ini";
    std::ofstream f(p, std::ios::trunc);
    if (!f) return;
    f << "enabled=" << (g_enabled ? 1 : 0) << "\n";
    f << "client_id=" << g_client_id << "\n";
    f << "access_token=" << g_access_token << "\n";
    f << "refresh_token=" << g_refresh_token << "\n";
    f << "nex_token=" << g_nex_token << "\n";
    f << "pid=" << g_pid << "\n";
    f << "username=" << g_username << "\n";
    f << "friend_code=" << g_friend_code << "\n";
    f << "nex_expires_at=" << g_nex_expires_at << "\n";
}

void load_locked() {
    if (g_data_root.empty()) return;
    const auto p = std::filesystem::u8path(g_data_root) / "nextendo.ini";
    std::ifstream f(p);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq);
        const std::string v = line.substr(eq + 1);
        if (k == "enabled") g_enabled = (v == "1");
        else if (k == "client_id" && !v.empty()) g_client_id = v;
        else if (k == "access_token") g_access_token = v;
        else if (k == "refresh_token") g_refresh_token = v;
        else if (k == "nex_token") g_nex_token = v;
        else if (k == "pid") g_pid = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "username") g_username = v;
        else if (k == "friend_code") g_friend_code = v;
        else if (k == "nex_expires_at") g_nex_expires_at = std::strtoll(v.c_str(), nullptr, 10);
    }
    if (const char* env_cid = std::getenv("NX_NEXTENDO_CLIENT_ID"); env_cid && *env_cid) g_client_id = env_cid;
}

#if defined(_WIN32)
bool winhttp_request(const std::string& method, const std::string& path, const std::string& headers,
                     const std::string& body, DWORD& out_status, std::string& out_body) {
    out_status = 0;
    out_body.clear();
    const std::string host = api_base_host();
    std::wstring whost(host.begin(), host.end());
    std::wstring wmethod(method.begin(), method.end());
    std::wstring wpath(path.begin(), path.end());
    std::wstring wheaders(headers.begin(), headers.end());

    HINTERNET ses = WinHttpOpen(L"SuperWonders/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return false;
    HINTERNET con = WinHttpConnect(ses, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, wmethod.c_str(), wpath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                        : nullptr;
    bool ok = false;
    if (req && WinHttpSendRequest(req, wheaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wheaders.c_str(),
                                  wheaders.empty() ? 0 : DWORD(wheaders.size()),
                                  body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                                  DWORD(body.size()), DWORD(body.size()), 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD sz = sizeof(out_status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &out_status, &sz, WINHTTP_NO_HEADER_INDEX);
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(req, chunk.data(), avail, &read) || read == 0) break;
            chunk.resize(read);
            out_body += chunk;
        }
        ok = true;
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return ok;
}

bool sha256_bytes(const uint8_t* in, size_t len, uint8_t out[32]) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
    bool ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0 &&
              BCryptHashData(hash, const_cast<PUCHAR>(in), ULONG(len), 0) == 0 &&
              BCryptFinishHash(hash, out, 32, 0) == 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

std::string random_hex(size_t bytes_count) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::vector<uint8_t> buf(bytes_count, 0);
    BCryptGenRandom(nullptr, buf.data(), ULONG(buf.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    std::string out;
    out.reserve(bytes_count * 2);
    for (uint8_t b : buf) {
        out.push_back(kHex[(b >> 4) & 0xF]);
        out.push_back(kHex[b & 0xF]);
    }
    return out;
}

std::string sign_rs256_jwt(const std::string& header_and_payload) {
    BCRYPT_ALG_HANDLE rsa_alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    if (BCryptOpenAlgorithmProvider(&rsa_alg, BCRYPT_RSA_ALGORITHM, nullptr, 0) != 0) return {};

    const auto key_path = std::filesystem::u8path(g_data_root) / "nextendo-rsa.bin";
    std::vector<uint8_t> blob;
    {
        std::ifstream kf(key_path, std::ios::binary | std::ios::ate);
        if (kf && kf.tellg() > 0) {
            blob.resize(size_t(kf.tellg()));
            kf.seekg(0);
            kf.read(reinterpret_cast<char*>(blob.data()), std::streamsize(blob.size()));
        }
    }
    bool loaded = !blob.empty() &&
                  BCryptImportKeyPair(rsa_alg, nullptr, BCRYPT_RSAPRIVATE_BLOB, &key, blob.data(), ULONG(blob.size()),
                                      0) == 0;
    if (!loaded) {
        if (BCryptGenerateKeyPair(rsa_alg, &key, 2048, 0) == 0 && BCryptFinalizeKeyPair(key, 0) == 0) {
            ULONG sz = 0;
            if (BCryptExportKey(key, nullptr, BCRYPT_RSAPRIVATE_BLOB, nullptr, 0, &sz, 0) == 0 && sz > 0) {
                blob.resize(sz);
                if (BCryptExportKey(key, nullptr, BCRYPT_RSAPRIVATE_BLOB, blob.data(), sz, &sz, 0) == 0) {
                    std::ofstream kf(key_path, std::ios::binary | std::ios::trunc);
                    if (kf) kf.write(reinterpret_cast<const char*>(blob.data()), std::streamsize(blob.size()));
                }
            }
            loaded = true;
        }
    }
    std::string sig_b64;
    uint8_t digest[32]{};
    if (loaded &&
        sha256_bytes(reinterpret_cast<const uint8_t*>(header_and_payload.data()), header_and_payload.size(), digest)) {
        BCRYPT_PKCS1_PADDING_INFO pad{BCRYPT_SHA256_ALGORITHM};
        ULONG sig_len = 0;
        if (BCryptSignHash(key, &pad, digest, sizeof(digest), nullptr, 0, &sig_len, BCRYPT_PAD_PKCS1) == 0 &&
            sig_len > 0) {
            std::vector<uint8_t> sig(sig_len);
            if (BCryptSignHash(key, &pad, digest, sizeof(digest), sig.data(), sig_len, &sig_len, BCRYPT_PAD_PKCS1) == 0)
                sig_b64 = base64url(sig.data(), sig_len);
        }
    }
    if (key) BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(rsa_alg, 0);
    return sig_b64;
}
#endif

bool refresh_oauth_token_locked() {
#if defined(_WIN32)
    if (g_refresh_token.empty() || g_client_id.empty()) return false;
    const std::string form =
        "grant_type=refresh_token&refresh_token=" + url_encode(g_refresh_token) +
        "&client_id=" + url_encode(g_client_id);
    DWORD st = 0;
    std::string body;
    const std::string hdrs =
        "Content-Type: application/x-www-form-urlencoded\r\nX-Nextendo-Client-Id: " + g_client_id + "\r\n";
    if (winhttp_request("POST", "/api/oauth/token", hdrs, form, st, body) && st == 200) {
        const std::string at = json_get_string(body, "access_token");
        const std::string rt = json_get_string(body, "refresh_token");
        if (!at.empty()) g_access_token = at;
        if (!rt.empty()) g_refresh_token = rt;
        save_locked();
        log("nextendo: refreshed OAuth access_token");
        return !at.empty();
    }
    log("nextendo: OAuth refresh failed (HTTP %lu)", (unsigned long)st);
#endif
    return false;
}

bool fetch_nex_token_locked() {
#if defined(_WIN32)
    if (g_access_token.empty() || g_client_id.empty()) return false;
    DWORD st = 0;
    std::string body;
    const std::string hdrs =
        "Authorization: Bearer " + g_access_token + "\r\nX-Nextendo-Client-Id: " + g_client_id + "\r\n";
    if (!winhttp_request("GET", "/api/nex-token", hdrs, "", st, body)) {
        g_last_error = "Network error contacting Nextendo /api/nex-token";
        return false;
    }
    if (st == 200) {
        const std::string tok = json_get_string(body, "nex_token");
        const int64_t pid = json_get_int(body, "pid");
        if (!tok.empty() && pid > 0) {
            g_nex_token = tok;
            g_pid = uint64_t(pid);
            g_username = json_get_string(body, "username");
            g_friend_code = json_get_string(body, "friend_code");
            const int64_t exp_in = json_get_int(body, "expires_in");
            g_nex_expires_at = std::time(nullptr) + (exp_in > 0 ? exp_in : 86400);
            g_cached_jwt.clear();
            g_last_error.clear();
            save_locked();
            log("nextendo: signed in as %s (pid=%llu, friend_code=%s)",
                g_username.c_str(), (unsigned long long)g_pid, g_friend_code.c_str());
            return true;
        }
    }
    g_last_error = "Nextendo /api/nex-token failed (HTTP " + std::to_string(st) + ")";
#endif
    return false;
}

}  // namespace

void init(const std::string& data_root) {
    std::lock_guard lk(g_mu);
    g_data_root = data_root;
    load_locked();
    if (g_enabled) _putenv_s("NX_ONLINE_MODE", "1");
}

void shutdown() {
    g_heartbeat_stop = true;
    if (g_heartbeat_thread.joinable()) g_heartbeat_thread.join();
}

Status status() {
    std::lock_guard lk(g_mu);
    return {g_enabled, !g_nex_token.empty() && g_pid != 0, g_login_busy.load(),
            g_client_id, g_pid, g_username, g_friend_code, g_last_error};
}

void set_enabled(bool enabled) {
    std::lock_guard lk(g_mu);
    g_enabled = enabled;
    _putenv_s("NX_ONLINE_MODE", enabled ? "1" : "0");
    save_locked();
}

void set_client_id(const std::string& client_id) {
    std::lock_guard lk(g_mu);
    g_client_id = client_id.empty() ? kDefaultClientId : client_id;
    save_locked();
}

void sign_out() {
    std::lock_guard lk(g_mu);
    g_access_token.clear();
    g_refresh_token.clear();
    g_nex_token.clear();
    g_pid = 0;
    g_username.clear();
    g_friend_code.clear();
    g_nex_expires_at = 0;
    g_cached_jwt.clear();
    g_last_error.clear();
    save_locked();
}

bool begin_oauth_login() {
#if defined(_WIN32)
    if (g_login_busy.exchange(true)) return false;
    std::string client_id;
    {
        std::lock_guard lk(g_mu);
        client_id = g_client_id;
    }
    std::thread([client_id]() {
        uint8_t verifier_raw[32]{};
        BCryptGenRandom(nullptr, verifier_raw, sizeof(verifier_raw), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        const std::string verifier = base64url(verifier_raw, sizeof(verifier_raw));
        uint8_t challenge_raw[32]{};
        sha256_bytes(reinterpret_cast<const uint8_t*>(verifier.data()), verifier.size(), challenge_raw);
        const std::string challenge = base64url(challenge_raw, sizeof(challenge_raw));

        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
        SOCKET srv = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        BOOL yes = TRUE;
        if (srv != INVALID_SOCKET) setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(kCallbackPort);
        if (srv == INVALID_SOCKET || ::bind(srv, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0 ||
            ::listen(srv, 1) != 0) {
            if (srv != INVALID_SOCKET) closesocket(srv);
            std::lock_guard lk(g_mu);
            g_last_error = "Could not open local OAuth callback listener on 127.0.0.1:47823";
            g_login_busy = false;
            return;
        }

        const std::string auth_url = "https://" + api_base_host() + "/oauth/authorize?client_id=" +
                                     url_encode(client_id) + "&redirect_uri=" + url_encode(kRedirectUri) +
                                     "&response_type=code&scope=" + url_encode("identity friends game.matchmaking") +
                                     "&code_challenge=" + url_encode(challenge) + "&code_challenge_method=S256";
        ShellExecuteA(nullptr, "open", auth_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

        DWORD timeout_ms = 180000;
        setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
        SOCKET cli = ::accept(srv, nullptr, nullptr);
        closesocket(srv);
        std::string code;
        if (cli != INVALID_SOCKET) {
            char req_buf[4096]{};
            int n = ::recv(cli, req_buf, sizeof(req_buf) - 1, 0);
            if (n > 0) {
                std::string req(req_buf, n);
                const size_t cpos = req.find("code=");
                if (cpos != std::string::npos) {
                    size_t end = req.find_first_of(" &\r\n", cpos + 5);
                    code = req.substr(cpos + 5, end == std::string::npos ? std::string::npos : end - (cpos + 5));
                }
            }
            const char* html =
                "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
                "<html><body style='font-family:sans-serif;background:#111;color:#eee;padding:40px'>"
                "<h2>Nextendo login complete</h2><p>You can close this tab and return to SuperWonders.</p>"
                "</body></html>";
            ::send(cli, html, int(std::strlen(html)), 0);
            closesocket(cli);
        }
        if (code.empty()) {
            std::lock_guard lk(g_mu);
            g_last_error = "OAuth login cancelled or timed out";
            g_login_busy = false;
            return;
        }

        const std::string form =
            "grant_type=authorization_code&code=" + url_encode(code) + "&redirect_uri=" + url_encode(kRedirectUri) +
            "&client_id=" + url_encode(client_id) + "&code_verifier=" + url_encode(verifier);
        DWORD st = 0;
        std::string body;
        const std::string hdrs =
            "Content-Type: application/x-www-form-urlencoded\r\nX-Nextendo-Client-Id: " + client_id + "\r\n";
        if (winhttp_request("POST", "/api/oauth/token", hdrs, form, st, body) && st == 200) {
            std::lock_guard lk(g_mu);
            g_access_token = json_get_string(body, "access_token");
            g_refresh_token = json_get_string(body, "refresh_token");
            fetch_nex_token_locked();
        } else {
            std::lock_guard lk(g_mu);
            g_last_error = "OAuth token exchange failed (HTTP " + std::to_string(st) + ")";
        }
        g_login_busy = false;
    }).detach();
    return true;
#else
    return false;
#endif
}

std::string get_baas_id_token(const std::string& game_version) {
#if defined(_WIN32)
    std::lock_guard lk(g_mu);
    const int64_t now = std::time(nullptr);
    if (g_nex_token.empty() || now + 60 >= g_nex_expires_at) {
        if (!g_access_token.empty()) fetch_nex_token_locked();
    }
    if (g_nex_token.empty() || g_pid == 0) return {};
    if (!g_cached_jwt.empty() && now + 300 < g_cached_jwt_exp) return g_cached_jwt;

    const int64_t exp = now + 3 * 3600;
    const std::string jti = random_hex(16);
    const std::string di = random_hex(16);
    std::ostringstream payload;
    payload << "{"
            << "\"iss\":\"e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com\","
            << "\"aud\":\"ed9e2f05d286f7b8\","
            << "\"sub\":\"" << g_pid << "\","
            << "\"iat\":" << now << ","
            << "\"exp\":" << exp << ","
            << "\"jti\":\"" << jti << "\","
            << "\"di\":\"" << di << "\","
            << "\"bs:did\":\"" << di << "\","
            << "\"sn\":\"XAW10000000000\","
            << "\"hm\":true,"
            << "\"tv\":\"" << game_version << "\","
            << "\"nnex\":\"" << g_nex_token << "\""
            << "}";
    const std::string hdr = "{\"alg\":\"RS256\",\"typ\":\"id_token\"}";
    const std::string signing_input = base64url_str(hdr) + "." + base64url_str(payload.str());
    const std::string sig = sign_rs256_jwt(signing_input);
    if (sig.empty()) return {};
    g_cached_jwt = signing_input + "." + sig;
    g_cached_jwt_exp = exp;
    return g_cached_jwt;
#else
    (void)game_version;
    return {};
#endif
}

uint64_t current_pid() {
    std::lock_guard lk(g_mu);
    return g_pid;
}

void start_session_heartbeat() {
#if defined(_WIN32)
    std::lock_guard lk(g_mu);
    if (!g_enabled || g_heartbeat_thread.joinable()) return;
    g_heartbeat_stop = false;
    g_heartbeat_thread = std::thread([]() {
        while (!g_heartbeat_stop.load()) {
            std::string tok, cid;
            {
                std::lock_guard lock(g_mu);
                tok = g_nex_token;
                cid = g_client_id;
            }
            if (!tok.empty()) {
                DWORD st = 0;
                std::string body;
                const std::string hdrs = "Authorization: Bearer " + tok + "\r\nX-Nextendo-Client-Id: " + cid +
                                         "\r\nContent-Type: application/json\r\n";
                winhttp_request("POST", "/api/nex-session", hdrs, "{}", st, body);
            }
            for (int i = 0; i < 60 && !g_heartbeat_stop.load(); ++i)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
#endif
}

std::vector<FriendInfo> get_friends() {
    static std::mutex cache_mu;
    static std::vector<FriendInfo> cached;
    static int64_t cached_at = 0;
    std::lock_guard clk(cache_mu);
    const int64_t now = std::time(nullptr);
    if (now - cached_at < 10 && !cached.empty()) return cached;

    std::vector<FriendInfo> out;
    std::string root, tok, cid;
    {
        std::lock_guard lk(g_mu);
        root = g_data_root;
        tok = g_access_token;
        cid = g_client_id;
    }
    if (!root.empty()) {
        std::ifstream ff(std::filesystem::u8path(root) / "friends.txt");
        std::string line;
        while (std::getline(ff, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            size_t sp = line.find_first_of(" \t,");
            std::string pid_s = (sp == std::string::npos) ? line : line.substr(0, sp);
            std::string nick = (sp == std::string::npos) ? "Friend" : line.substr(sp + 1);
            while (!nick.empty() && (nick.front() == ' ' || nick.front() == '\t')) nick.erase(nick.begin());
            uint64_t pid = std::strtoull(pid_s.c_str(), nullptr, 10);
            if (pid != 0) out.push_back({pid, nick.empty() ? "Friend" : nick, 2, true});
        }
    }
#if defined(_WIN32)
    if (!tok.empty() && !cid.empty()) {
        DWORD st = 0;
        std::string body;
        auto fetch_friends = [&](const std::string& t) {
            const std::string hdrs = "Authorization: Bearer " + t + "\r\nX-Nextendo-Client-Id: " + cid + "\r\n";
            return winhttp_request("GET", "/api/friends", hdrs, "", st, body);
        };
        bool ok = fetch_friends(tok);
        if (ok && st == 401) {
            std::lock_guard lk(g_mu);
            if (refresh_oauth_token_locked()) {
                tok = g_access_token;
                ok = fetch_friends(tok);
            }
        }
        static bool logged_once = false;
        if (!logged_once) {
            logged_once = true;
            log("nextendo: GET /api/friends ok=%d http=%lu %.300s", int(ok), (unsigned long)st, body.c_str());
        }
        if (ok && st == 200) {
            const size_t arr_pos = body.find("\"friends\"");
            size_t pos = (arr_pos != std::string::npos) ? body.find('[', arr_pos) : 0;
            if (pos == std::string::npos) pos = 0;
            while ((pos = body.find('{', pos)) != std::string::npos) {
                int depth = 0;
                bool in_str = false;
                bool esc = false;
                size_t end = pos;
                for (; end < body.size(); ++end) {
                    const char ch = body[end];
                    if (esc) {
                        esc = false;
                        continue;
                    }
                    if (in_str) {
                        if (ch == '\\') esc = true;
                        else if (ch == '"') in_str = false;
                        continue;
                    }
                    if (ch == '"') in_str = true;
                    else if (ch == '{') ++depth;
                    else if (ch == '}' && --depth == 0) break;
                }
                if (end >= body.size()) break;
                std::string obj = body.substr(pos, end - pos + 1);
                const std::string nsa_s = json_get_string(obj, "nsa");
                uint64_t aid = !nsa_s.empty() ? std::strtoull(nsa_s.c_str(), nullptr, 16) : 0;
                if (aid == 0) {
                    int64_t pid = json_get_int(obj, "pid");
                    if (pid <= 0) pid = json_get_int(obj, "baasUserId");
                    if (pid <= 0) pid = std::strtoll(json_get_string(obj, "baasUserId").c_str(), nullptr, 10);
                    if (pid > 0) aid = uint64_t(pid);
                }
                std::string uname = json_get_string(obj, "name");
                if (uname.empty()) uname = json_get_string(obj, "username");
                if (uname.empty()) uname = json_get_string(obj, "nickname");
                if (aid != 0 && std::none_of(out.begin(), out.end(), [&](const FriendInfo& f) { return f.pid == aid; })) {
                    out.push_back({aid, uname.empty() ? "Friend" : uname, 2, true});
                }
                pos = end + 1;
            }
        }
    }
#endif
    cached = out;
    cached_at = now;
    return out;
}

}  // namespace nx::nextendo
