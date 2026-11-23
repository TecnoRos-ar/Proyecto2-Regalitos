#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain {

struct MaterialInput {
    std::string id;
    std::string name;
    std::string unit;
    double stock = 0;
    double last_purchase_price = 0;
    double last_purchase_qty = 1;
};

struct RecipeItem {
    std::string material_id;
    double quantity = 0;
};

struct ProductInput {
    std::string id;
    std::string name;
    std::vector<RecipeItem> recipe;
    double labor_cost = 0;
    double other_cost = 0;
    std::string profit_type = "pct";
    double profit_value = 0;
};

struct CostBreakdown {
    double materials = 0;
    double extra = 0;
    double cost = 0;
    double price = 0;
    double gain = 0;
    double margin_pct = 0;
    std::optional<int> capacity;
    std::string limiting_material;
};

double unit_cost(const MaterialInput& m);
std::unordered_map<std::string, double> aggregate_recipe(
    const std::vector<RecipeItem>& recipe, int times);
CostBreakdown calculate(
    const ProductInput& p,
    const std::unordered_map<std::string, MaterialInput>& materials,
    int rounding = 100);

} // namespace domain
