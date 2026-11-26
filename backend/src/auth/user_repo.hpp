// src/auth/user_repo.hpp
#pragma once
#include <pqxx/pqxx>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace auth {

struct User {
    std::string id, email, name, role, password_hash;
    bool active = true, totp_enabled = false;
    std::optional<std::string> totp_secret;
    int failed_attempts = 0;
    int session_version = 0;
    bool pending = false;
    std::optional<std::string> locked_until;
    std::string password_changed_at;
};

std::optional<User> find_by_email(pqxx::work& tx, const std::string& email);
std::optional<User> find_by_id(pqxx::work& tx, const std::string& id);
User create_user(pqxx::work& tx, const std::string& email,
                 const std::string& password_hash, const std::string& name,
                 const std::string& role);
void log_audit(pqxx::work& tx, const std::string& user_id,
               const std::string& action, nlohmann::json meta = {});

} // namespace auth
