// src/auth/user_repo.cpp
#include "db/pool.hpp"
#include "auth/user_repo.hpp"
namespace auth {

static User row_to_user(const pqxx::row_ref& r) {
    User u;
    u.id                  = r["id"].as<std::string>();
    u.email               = r["email"].as<std::string>();
    u.name                = r["name"].is_null() ? "" : r["name"].as<std::string>();
    u.role                = r["role"].as<std::string>();
    u.password_hash       = r["password_hash"].as<std::string>();
    u.active              = r["active"].as<bool>();
    u.totp_enabled        = r["totp_enabled"].as<bool>();
    u.failed_attempts     = r["failed_attempts"].as<int>();
    u.session_version     = r["session_version"].as<int>();
    u.password_changed_at = r["password_changed_at"].as<std::string>();
    if (!r["totp_secret"].is_null()) u.totp_secret = r["totp_secret"].as<std::string>();
    if (!r["locked_until"].is_null()) u.locked_until = r["locked_until"].as<std::string>();
    return u;
}

std::optional<User> find_by_email(pqxx::work& tx, const std::string& email) {
    auto r = db::exec(tx, "SELECT * FROM users WHERE email = $1 FOR UPDATE", email);
    if (r.empty()) return std::nullopt;
    return row_to_user(r[0]);
}
std::optional<User> find_by_id(pqxx::work& tx, const std::string& id) {
    auto r = db::exec(tx, "SELECT * FROM users WHERE id = $1", id);
    if (r.empty()) return std::nullopt;
    return row_to_user(r[0]);
}
User create_user(pqxx::work& tx, const std::string& email,
                 const std::string& hash, const std::string& name,
                 const std::string& role) {
    auto r = db::exec(tx,
        "INSERT INTO users (email, password_hash, name, role) "
        "VALUES ($1,$2,$3,$4) RETURNING *",
        email, hash, name, role);
    return row_to_user(r[0]);
}
void log_audit(pqxx::work& tx, const std::string& user_id,
               const std::string& action, nlohmann::json meta) {
    db::exec(tx,
        "INSERT INTO audit_log (user_id, action, metadata) "
        "VALUES ($1,$2,$3)",
        user_id, action, meta.dump());
}

} // namespace auth
