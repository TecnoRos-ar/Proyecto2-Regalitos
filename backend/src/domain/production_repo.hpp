#pragma once
#include <pqxx/pqxx>
#include <string>

namespace production_repo {

struct ProduceResult {
    int produced = 0;
    std::string error;
};

ProduceResult produce(pqxx::work& tx,
                      const std::string& product_id,
                      int quantity,
                      const std::string& user_id);

} // namespace production_repo
