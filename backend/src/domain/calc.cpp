#include "domain/calc.hpp"
#include <cmath>
#include <limits>

namespace domain {

double unit_cost(const MaterialInput& m) {
    if (m.last_purchase_qty <= 0) return 0;
    return m.last_purchase_price / m.last_purchase_qty;
}

std::unordered_map<std::string, double> aggregate_recipe(
    const std::vector<RecipeItem>& recipe, int times) {
    std::unordered_map<std::string, double> need;
    for (const auto& r : recipe) {
        if (r.material_id.empty() || r.quantity <= 0) continue;
        need[r.material_id] += r.quantity * times;
    }
    return need;
}

CostBreakdown calculate(
    const ProductInput& p,
    const std::unordered_map<std::string, MaterialInput>& materials,
    int rounding) {

    CostBreakdown r;
    double cap = std::numeric_limits<double>::infinity();

    for (const auto& item : p.recipe) {
        auto it = materials.find(item.material_id);
        if (it == materials.end()) continue;
        const auto& m = it->second;
        r.materials += item.quantity * unit_cost(m);
        if (item.quantity > 0) {
            double c = std::floor(m.stock / item.quantity + 1e-9);
            if (c < cap) { cap = c; r.limiting_material = m.name; }
        }
    }

    r.extra = p.labor_cost + p.other_cost;
    r.cost  = r.materials + r.extra;

    double price = (p.profit_type == "fixed")
        ? r.cost + p.profit_value
        : r.cost * (1.0 + p.profit_value / 100.0);

    if (rounding > 0)
        price = std::ceil(price / rounding - 1e-9) * rounding;

    r.price      = price;
    r.gain       = price - r.cost;
    r.margin_pct = (price > 0) ? (r.gain / price * 100.0) : 0;
    if (std::isfinite(cap)) r.capacity = static_cast<int>(cap);
    return r;
}

} // namespace domain
