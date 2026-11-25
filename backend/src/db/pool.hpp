#pragma once
#include <pqxx/pqxx>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

class ConnectionPool;

class PooledConn {
public:
    PooledConn(ConnectionPool& pool, std::unique_ptr<pqxx::connection> c);
    ~PooledConn();
    PooledConn(const PooledConn&) = delete;
    PooledConn& operator=(const PooledConn&) = delete;
    PooledConn(PooledConn&&) noexcept;

    pqxx::connection& operator*()  { return *c_; }
    pqxx::connection* operator->() { return c_.get(); }

private:
    ConnectionPool* pool_ = nullptr;
    std::unique_ptr<pqxx::connection> c_;
};

class ConnectionPool {
public:
    ConnectionPool(const std::string& conn_str, std::size_t size);
    ~ConnectionPool() = default;

    PooledConn acquire();
    void release(std::unique_ptr<pqxx::connection> c);

private:
    std::size_t max_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<std::unique_ptr<pqxx::connection>> idle_;
};

#include <regex>
#include <utility>

namespace db {
// Reemplazo de tx.exec_params (deprecado en libpqxx 8).
template <class... A>
pqxx::result exec(pqxx::transaction_base& tx, std::string_view q, A&&... a) {
    return tx.exec(q, pqxx::params{std::forward<A>(a)...});
}
inline bool is_uuid(const std::string& s) {
    static const std::regex re(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    return std::regex_match(s, re);
}
inline bool is_blank(const std::string& s) {
    return s.find_first_not_of(" \t\r\n") == std::string::npos;
}
} // namespace db
