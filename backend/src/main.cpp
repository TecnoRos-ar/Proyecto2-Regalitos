#include <crow.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "config.hpp"
#include "db/pool.hpp"
#include "auth/crypto.hpp"
#include "auth/user_repo.hpp"

// Forward decls de las rutas
void register_material_routes(crow::SimpleApp&, ConnectionPool&);
void register_product_routes(crow::SimpleApp&, ConnectionPool&);
void register_production_routes(crow::SimpleApp&, ConnectionPool&);
void register_auth_routes(crow::SimpleApp&, ConnectionPool&);
void register_admin_routes(crow::SimpleApp&, ConnectionPool&);
void register_state_routes(crow::SimpleApp&, ConnectionPool&);
void register_password_routes(crow::SimpleApp&, ConnectionPool&);

// ---------- helpers ----------
static crow::response serve_file(const std::string& rel_path,
                                 const std::string& content_type) {
    auto base = env("PUBLIC_DIR", "public");
    std::ifstream f(base + "/" + rel_path);
    if (!f.is_open())
        return crow::response{404, "Archivo no encontrado: " + rel_path};
    std::stringstream ss;
    ss << f.rdbuf();
    auto res = crow::response{200, ss.str()};
    res.set_header("Content-Type", content_type);
    return res;
}

static void ensure_bootstrap_admin(ConnectionPool& pool) {
    auto conn = pool.acquire();
    pqxx::work tx{*conn};
    auto r = tx.exec("SELECT count(*) FROM users");
    if (r[0][0].as<int>() > 0) { tx.commit(); return; }

    auto email = env("ADMIN_EMAIL", "admin@regalitos.local");
    auto pass  = env("ADMIN_PASSWORD", "cambiar123");
    auto hash  = auth::crypto::hash_password(pass);
    auth::create_user(tx, email, hash, "Admin", "admin");
    tx.commit();

    std::cout << "\n=============================================\n"
              << "[BOOTSTRAP] Usuario admin creado:\n"
              << "  Email:    " << email << "\n"
              << "  Password: (la definida en ADMIN_PASSWORD)\n"
              << "  Cambiala apenas puedas.\n"
              << "=============================================\n\n";
}

// ---------- main ----------
int main() {
    try {
        auth::crypto::init();

        auto conn_str  = env("DATABASE_URL",
            "postgresql://regalitos:regalitos@localhost:5532/regalitos");
        auto pool_size = std::stoul(env("DB_POOL_SIZE", "10"));
        ConnectionPool pool{conn_str, pool_size};

        ensure_bootstrap_admin(pool);

        crow::SimpleApp app;

        // Datos con tipo o valor inválido: 400 en lugar de 500.
        app.exception_handler([](crow::response& res) {
            try {
                throw;
            } catch (const nlohmann::json::exception&) {
                res = crow::response{400, R"({"error":"tipo de dato inválido"})"};
            } catch (const pqxx::check_violation&) {
                res = crow::response{400, R"({"error":"valor fuera de rango"})"};
            } catch (const pqxx::data_exception&) {
                res = crow::response{400, R"({"error":"valor fuera de rango"})"};
            } catch (const std::exception& e) {
                CROW_LOG_ERROR << "Excepción sin atrapar: " << e.what();
                res = crow::response{500, R"({"error":"error interno"})"};
            }
        });

        // ---- Health ----
        CROW_ROUTE(app, "/health")([&pool] {
            auto conn = pool.acquire();
            pqxx::work tx{*conn};
            tx.exec("SELECT 1");
            tx.commit();
            return crow::response{200, R"({"status":"ok"})"};
        });

        // ---- Páginas ----
        CROW_ROUTE(app, "/")      ([] { return serve_file("index.html", "text/html; charset=utf-8"); });
        CROW_ROUTE(app, "/login") ([] { return serve_file("login.html", "text/html; charset=utf-8"); });
        CROW_ROUTE(app, "/admin") ([] { return serve_file("admin.html", "text/html; charset=utf-8"); });
        CROW_ROUTE(app, "/forgot")([] { return serve_file("forgot.html", "text/html; charset=utf-8"); });
        CROW_ROUTE(app, "/reset") ([] { return serve_file("reset.html", "text/html; charset=utf-8"); });

        // ---- APIs ----
        register_auth_routes      (app, pool);
        register_admin_routes     (app, pool);
        register_material_routes  (app, pool);
        register_product_routes   (app, pool);
        register_production_routes(app, pool);
        register_state_routes     (app, pool);
        register_password_routes  (app, pool);

        if (env("JWT_SECRET", "").size() < 32)
            CROW_LOG_WARNING << "JWT_SECRET ausente o corto (<32 chars): inseguro para producción";

        auto port = std::stoi(env("PORT", "8080"));
        CROW_LOG_INFO << "regalitos-backend escuchando en :" << port;
        app.port(port).multithreaded().run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << "\n\n"
                  << "Checklist:\n"
                  << "  1) docker compose up -d          (DB arriba)\n"
                  << "  2) docker compose ps             (dice healthy)\n"
                  << "  3) source .env                   (DATABASE_URL, JWT_SECRET)\n"
                  << "  4) ./build/regalitos\n\n";
        return 1;
    }
}
