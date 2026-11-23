// src/auth/crypto.hpp
#pragma once
#include <string>
#include <ctime>
#include <nlohmann/json.hpp>

namespace auth::crypto {

void init();  // llamar una vez al arrancar (sodium_init)

std::string hash_password(const std::string& password);
bool        verify_password(const std::string& password, const std::string& hash);

std::string base32_random_secret(size_t bytes = 20);
std::string totp_now(const std::string& secret_b32, time_t t = 0);
bool        totp_verify(const std::string& secret_b32,
                        const std::string& code,
                        int window = 1);

std::string jwt_sign(const nlohmann::json& payload, const std::string& secret);
std::optional<nlohmann::json> jwt_verify(const std::string& token,
                                         const std::string& secret);

} // namespace auth::crypto
