#include "db/pool.hpp"

ConnectionPool::ConnectionPool(const std::string& s, std::size_t size) : max_(size) {
    for (std::size_t i = 0; i < max_; ++i)
        idle_.push(std::make_unique<pqxx::connection>(s));
}

PooledConn ConnectionPool::acquire() {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait(lk, [&]{ return !idle_.empty(); });
    auto c = std::move(idle_.front());
    idle_.pop();
    return PooledConn(*this, std::move(c));
}

void ConnectionPool::release(std::unique_ptr<pqxx::connection> c) {
    { std::lock_guard<std::mutex> lk(mtx_); idle_.push(std::move(c)); }
    cv_.notify_one();
}

PooledConn::PooledConn(ConnectionPool& p, std::unique_ptr<pqxx::connection> c)
    : pool_(&p), c_(std::move(c)) {}
PooledConn::~PooledConn() { if (c_) pool_->release(std::move(c_)); }
PooledConn::PooledConn(PooledConn&& o) noexcept
    : pool_(o.pool_), c_(std::move(o.c_)) { o.pool_ = nullptr; }
