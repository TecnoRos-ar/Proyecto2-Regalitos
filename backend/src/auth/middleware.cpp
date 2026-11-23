// src/auth/middleware.cpp
#include "auth/middleware.hpp"
#include "auth/crypto.hpp"
#include "auth/user_repo.hpp"
#include "config.hpp"
#include <cstdlib>

namespace auth {

static std::string jwt_secret() {
    return env("JWT_SECRET", "dev-only-secret-cambiar-en-prod");
}

static std::string get_cookie(const crow::request& req, const std::string& name) {
    auto h = req.get_header_value("Cookie");
    std::string key = name + "=";
    auto pos = h.find(key);
    if (pos == std::string::npos) return {};
    pos += key.size();
    auto end = h.find(';', pos);
    return h.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

std::optional<AuthCtx> get_user(const crow::request& req, ConnectionPool& pool) {
    auto token = get_cookie(req, "session");
    if (token.empty()) return std::nullopt;
    auto payload = crypto::jwt_verify(token, jwt_secret());
    if (!payload || payload->value("purpose", "") != "session") return std::nullopt;
    auto conn = pool.acquire();
    pqxx::work tx{*conn};
    auto u = auth::find_by_id(tx, payload->value("sub", ""));
    tx.commit();
    if (!u || !u->active) return std::nullopt;
    return AuthCtx{ u->id, u->email, u->role };
}

crow::response unauthorized() {
    return crow::response{401, R"({"error":"no autorizado"})"};
}
crow::response forbidden() {
    return crow::response{403, R"({"error":"prohibido"})"};
}

} // namespace auth
