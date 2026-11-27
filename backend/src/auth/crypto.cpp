// src/auth/crypto.cpp
#include "auth/crypto.hpp"
#include <sodium.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace auth::crypto {

void init() {
    if (sodium_init() < 0) throw std::runtime_error("sodium_init failed");
}

// ---------- password ----------
std::string hash_password(const std::string& password) {
    char out[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(
            out, password.c_str(), password.size(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
        throw std::runtime_error("hash_password failed");
    return std::string(out);
}

bool verify_password(const std::string& password, const std::string& hash) {
    return crypto_pwhash_str_verify(
        hash.c_str(), password.c_str(), password.size()) == 0;
}

// ---------- base64url ----------
static std::string b64enc(const std::string& in) {
    std::string out;
    out.resize(4 * ((in.size() + 2) / 3) + 1);
    int n = EVP_EncodeBlock(
        reinterpret_cast<unsigned char*>(out.data()),
        reinterpret_cast<const unsigned char*>(in.data()), (int)in.size());
    out.resize(n);
    for (auto& c : out) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!out.empty() && out.back() == '=') out.pop_back();
    return out;
}
static std::string b64dec(const std::string& in) {
    std::string s = in;
    for (auto& c : s) { if (c == '-') c = '+'; else if (c == '_') c = '/'; }
    while (s.size() % 4) s.push_back('=');
    std::string out;
    out.resize(3 * (s.size() / 4) + 4);
    int n = EVP_DecodeBlock(
        reinterpret_cast<unsigned char*>(out.data()),
        reinterpret_cast<const unsigned char*>(s.data()), (int)s.size());
    if (n < 0) return {};
    int pad = 0;
    for (int i = (int)s.size() - 1; i >= 0 && s[i] == '='; --i) ++pad;
    out.resize(n - pad);
    return out;
}

// ---------- HMAC ----------
static std::string hmac_sha1(const std::string& key, const std::string& data) {
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha1(), key.data(), (int)key.size(),
         reinterpret_cast<const unsigned char*>(data.data()),
         data.size(), out, &len);
    return std::string(reinterpret_cast<char*>(out), len);
}
static std::string hmac_sha256(const std::string& key, const std::string& data) {
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.data(), (int)key.size(),
         reinterpret_cast<const unsigned char*>(data.data()),
         data.size(), out, &len);
    return std::string(reinterpret_cast<char*>(out), len);
}

// ---------- TOTP ----------
static int b32val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}
static std::string b32dec(const std::string& in) {
    std::string out;
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=' || c == ' ') continue;
        int v = b32val(c);
        if (v < 0) return {};
        buf = (buf << 5) | v;
        bits += 5;
        if (bits >= 8) { bits -= 8; out.push_back((char)((buf >> bits) & 0xff)); }
    }
    return out;
}
static const char* B32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
std::string base32_random_secret(size_t bytes) {
    std::vector<unsigned char> rnd(bytes);
    RAND_bytes(rnd.data(), (int)bytes);
    std::string out;
    int buf = 0, bits = 0;
    for (auto b : rnd) {
        buf = (buf << 8) | b;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out.push_back(B32[(buf >> bits) & 0x1f]);
        }
    }
    if (bits > 0) out.push_back(B32[(buf << (5 - bits)) & 0x1f]);
    return out;
}
static std::string totp_at(const std::string& secret_b32, time_t t) {
    auto key = b32dec(secret_b32);
    if (key.empty()) return {};
    uint64_t counter = (uint64_t)t / 30;
    std::string msg(8, 0);
    for (int i = 7; i >= 0; --i) { msg[i] = counter & 0xff; counter >>= 8; }
    auto mac = hmac_sha1(key, msg);
    int off = mac[mac.size() - 1] & 0x0f;
    uint32_t code =
        (((uint8_t)mac[off]     & 0x7f) << 24) |
        (((uint8_t)mac[off + 1] & 0xff) << 16) |
        (((uint8_t)mac[off + 2] & 0xff) << 8)  |
        ((uint8_t)mac[off + 3]  & 0xff);
    code %= 1000000;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%06u", code);
    return std::string(buf);
}
std::string totp_now(const std::string& secret_b32, time_t t) {
    if (t == 0) t = std::time(nullptr);
    return totp_at(secret_b32, t);
}
bool totp_verify(const std::string& secret_b32, const std::string& code, int window) {
    if (code.size() != 6) return false;
    time_t now = std::time(nullptr);
    for (int d = -window; d <= window; ++d) {
        if (totp_at(secret_b32, now + d * 30) == code) return true;
    }
    return false;
}

std::optional<int64_t> totp_match_step(const std::string& secret_b32,
                                       const std::string& code, int window) {
    if (code.size() != 6) return std::nullopt;
    int64_t now_step = (int64_t)std::time(nullptr) / 30;
    for (int d = -window; d <= window; ++d) {
        auto expected = totp_at(secret_b32, (time_t)((now_step + d) * 30));
        if (!expected.empty() &&
            sodium_memcmp(expected.data(), code.data(), 6) == 0)
            return now_step + d;
    }
    return std::nullopt;
}

// ---------- JWT HS256 ----------
std::string jwt_sign(const nlohmann::json& payload, const std::string& secret) {
    nlohmann::json header = {{"alg","HS256"},{"typ","JWT"}};
    auto h = b64enc(header.dump());
    auto p = b64enc(payload.dump());
    auto input = h + "." + p;
    auto sig = b64enc(hmac_sha256(secret, input));
    return input + "." + sig;
}
std::optional<nlohmann::json> jwt_verify(const std::string& token,
                                         const std::string& secret) {
    auto dot1 = token.find('.');
    if (dot1 == std::string::npos) return std::nullopt;
    auto dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return std::nullopt;
    auto input = token.substr(0, dot2);
    auto sig_in = token.substr(dot2 + 1);
    auto expected = b64enc(hmac_sha256(secret, input));
    if (expected.size() != sig_in.size() ||
        sodium_memcmp(expected.data(), sig_in.data(), expected.size()) != 0)
        return std::nullopt;
    auto payload = b64dec(token.substr(dot1 + 1, dot2 - dot1 - 1));
    try {
        auto j = nlohmann::json::parse(payload);
        if (j.contains("exp") && (time_t)j["exp"].get<int64_t>() < std::time(nullptr))
            return std::nullopt;
        return j;
    } catch (...) { return std::nullopt; }
}

} // namespace auth::crypto
