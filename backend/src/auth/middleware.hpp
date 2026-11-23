// src/auth/middleware.hpp
#pragma once
#include <crow.h>
#include <optional>
#include "db/pool.hpp"

namespace auth {

struct AuthCtx { std::string id, email, role; };

std::optional<AuthCtx> get_user(const crow::request& req,
                                ConnectionPool& pool);

// Helpers de respuesta
crow::response unauthorized();
crow::response forbidden();

} // namespace auth
