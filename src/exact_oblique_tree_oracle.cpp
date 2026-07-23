#include "neoeng/core/exact_oblique_tree_oracle.hpp"

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/rational_adaptor.hpp>

#include <algorithm>
#include <limits>
#include <numeric>

namespace neoeng::core {
namespace {
using Rational = boost::multiprecision::cpp_rational;
using Integer = boost::multiprecision::cpp_int;

void mix_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}

bool is_tree(std::size_t bodies, std::span<const NormalContact> contacts) {
    if (bodies == 0U || contacts.size() + 1U != bodies) return false;
    std::vector<std::size_t> parent(bodies);
    std::iota(parent.begin(), parent.end(), 0U);
    const auto root = [&parent](std::size_t body) {
        while (parent[body] != body) body = parent[body];
        return body;
    };
    for (const NormalContact& contact : contacts) {
        std::size_t first = root(contact.first);
        std::size_t second = root(contact.second);
        if (first == second) return false;
        if (second < first) std::swap(first, second);
        parent[second] = first;
    }
    const std::size_t first_root = root(0U);
    for (std::size_t body = 1U; body < bodies; ++body) if (root(body) != first_root) return false;
    return true;
}

bool gaussian_solve(std::vector<Rational> matrix, std::vector<Rational> rhs,
                    std::size_t size, std::vector<Rational>& solution) {
    solution.assign(size, Rational{0});
    for (std::size_t column = 0U; column < size; ++column) {
        std::size_t pivot = column;
        while (pivot < size && matrix[pivot * size + column] == 0) ++pivot;
        if (pivot == size) return false;
        if (pivot != column) {
            for (std::size_t item = column; item < size; ++item) {
                std::swap(matrix[column * size + item], matrix[pivot * size + item]);
            }
            std::swap(rhs[column], rhs[pivot]);
        }
        const Rational diagonal = matrix[column * size + column];
        for (std::size_t item = column; item < size; ++item) matrix[column * size + item] /= diagonal;
        rhs[column] /= diagonal;
        for (std::size_t row = 0U; row < size; ++row) {
            if (row == column) continue;
            const Rational factor = matrix[row * size + column];
            if (factor == 0) continue;
            for (std::size_t item = column; item < size; ++item) {
                matrix[row * size + item] -= factor * matrix[column * size + item];
            }
            rhs[row] -= factor * rhs[column];
        }
    }
    solution = std::move(rhs);
    return true;
}

Rational incidence_dot(const NormalContact& first, const NormalContact& second,
                       std::size_t body, std::uint32_t mass) {
    Integer ax = 0, ay = 0, bx = 0, by = 0;
    if (first.first == body) { ax = first.normal.x; ay = first.normal.y; }
    else if (first.second == body) { ax = -Integer(first.normal.x); ay = -Integer(first.normal.y); }
    if (second.first == body) { bx = second.normal.x; by = second.normal.y; }
    else if (second.second == body) { bx = -Integer(second.normal.x); by = -Integer(second.normal.y); }
    return Rational(ax * bx + ay * by, mass);
}

Rational constraint_value(const NormalContact& contact,
                          const std::vector<Rational>& velocity_x,
                          const std::vector<Rational>& velocity_y) {
    return Rational(contact.normal.x)
            * (velocity_x[contact.first] - velocity_x[contact.second])
        + Rational(contact.normal.y)
            * (velocity_y[contact.first] - velocity_y[contact.second]);
}

Fixed::rep round_nearest_even(const Rational& value) {
    Integer num = numerator(value);
    Integer den = denominator(value);
    const bool negative = num < 0;
    if (negative) num = -num;
    Integer quotient = num / den;
    const Integer remainder = num % den;
    const Integer twice = remainder * 2;
    if (twice > den || (twice == den && (quotient & 1) != 0)) ++quotient;
    if (negative) quotient = -quotient;
    const Integer minimum = std::numeric_limits<Fixed::rep>::min();
    const Integer maximum = std::numeric_limits<Fixed::rep>::max();
    if (quotient < minimum) return std::numeric_limits<Fixed::rep>::min();
    if (quotient > maximum) return std::numeric_limits<Fixed::rep>::max();
    return quotient.convert_to<Fixed::rep>();
}

std::string integer_string(const Integer& value) { return value.convert_to<std::string>(); }


struct QuantizedRepair final {
    bool certified{};
    Rational error{};
    std::vector<Fixed::rep> x{};
    std::vector<Fixed::rep> y{};
};

bool integer_contact_feasible(
    const NormalContact& edge,
    Fixed::rep first_x,
    Fixed::rep first_y,
    Fixed::rep second_x,
    Fixed::rep second_y) {
    const Integer value = Integer(edge.normal.x) * (Integer(first_x) - second_x)
        + Integer(edge.normal.y) * (Integer(first_y) - second_y);
    return value <= 0;
}

QuantizedRepair repair_quantized_tree_neighbourhood(
    const std::vector<Rational>& exact_x,
    const std::vector<Rational>& exact_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    std::span<const Fixed::rep> rounded_x,
    std::span<const Fixed::rep> rounded_y,
    std::size_t radius) {
    QuantizedRepair result{};
    const std::size_t bodies = exact_x.size();
    if (bodies == 0U || radius > 3U) return result;
    const std::size_t side = radius * 2U + 1U;
    const std::size_t states = side * side;
    std::vector<Fixed::rep> candidate_x(bodies * states);
    std::vector<Fixed::rep> candidate_y(bodies * states);
    std::vector<std::uint8_t> candidate_valid(bodies * states, 1U);
    for (std::size_t body = 0U; body < bodies; ++body) {
        std::size_t state = 0U;
        for (std::int64_t dy = -static_cast<std::int64_t>(radius);
             dy <= static_cast<std::int64_t>(radius); ++dy) {
            for (std::int64_t dx = -static_cast<std::int64_t>(radius);
                 dx <= static_cast<std::int64_t>(radius); ++dx, ++state) {
                const Integer x = Integer(rounded_x[body]) + dx;
                const Integer y = Integer(rounded_y[body]) + dy;
                if (x < std::numeric_limits<Fixed::rep>::min()
                    || x > std::numeric_limits<Fixed::rep>::max()
                    || y < std::numeric_limits<Fixed::rep>::min()
                    || y > std::numeric_limits<Fixed::rep>::max()) {
                    candidate_valid[body * states + state] = 0U;
                    continue;
                }
                candidate_x[body * states + state] = x.convert_to<Fixed::rep>();
                candidate_y[body * states + state] = y.convert_to<Fixed::rep>();
            }
        }
    }

    struct Adjacent final { std::size_t other{}; std::size_t edge{}; };
    std::vector<std::vector<Adjacent>> adjacency(bodies);
    for (std::size_t edge = 0U; edge < contacts.size(); ++edge) {
        adjacency[contacts[edge].first].push_back({contacts[edge].second, edge});
        adjacency[contacts[edge].second].push_back({contacts[edge].first, edge});
    }
    for (auto& items : adjacency) {
        std::sort(items.begin(), items.end(), [](const Adjacent& a, const Adjacent& b) {
            if (a.other != b.other) return a.other < b.other;
            return a.edge < b.edge;
        });
    }
    std::vector<std::size_t> parent(bodies, bodies), parent_edge(bodies, contacts.size()), order;
    order.reserve(bodies);
    parent[0] = 0U;
    order.push_back(0U);
    for (std::size_t cursor = 0U; cursor < order.size(); ++cursor) {
        const std::size_t body = order[cursor];
        for (const Adjacent item : adjacency[body]) {
            if (parent[item.other] != bodies) continue;
            parent[item.other] = body;
            parent_edge[item.other] = item.edge;
            order.push_back(item.other);
        }
    }
    if (order.size() != bodies) return result;

    std::vector<Rational> dp(bodies * states, Rational{0});
    std::vector<std::uint8_t> feasible(bodies * states, 0U);
    std::vector<std::uint32_t> choice(bodies * states, 0U);
    for (auto iterator = order.rbegin(); iterator != order.rend(); ++iterator) {
        const std::size_t body = *iterator;
        for (std::size_t state = 0U; state < states; ++state) {
            if (candidate_valid[body * states + state] == 0U) continue;
            const Rational dx = Rational(candidate_x[body * states + state]) - exact_x[body];
            const Rational dy = Rational(candidate_y[body * states + state]) - exact_y[body];
            Rational cost = Rational(masses[body]) * (dx * dx + dy * dy);
            bool valid = true;
            for (const Adjacent item : adjacency[body]) {
                if (parent[item.other] != body) continue;
                bool child_found = false;
                Rational child_best{};
                std::size_t child_state_best = 0U;
                const NormalContact& edge = contacts[item.edge];
                for (std::size_t child_state = 0U; child_state < states; ++child_state) {
                    if (feasible[item.other * states + child_state] == 0U) continue;
                    Fixed::rep first_x{}, first_y{}, second_x{}, second_y{};
                    if (edge.first == body) {
                        first_x = candidate_x[body * states + state];
                        first_y = candidate_y[body * states + state];
                        second_x = candidate_x[item.other * states + child_state];
                        second_y = candidate_y[item.other * states + child_state];
                    } else {
                        first_x = candidate_x[item.other * states + child_state];
                        first_y = candidate_y[item.other * states + child_state];
                        second_x = candidate_x[body * states + state];
                        second_y = candidate_y[body * states + state];
                    }
                    if (!integer_contact_feasible(edge, first_x, first_y, second_x, second_y)) continue;
                    const Rational& child_cost = dp[item.other * states + child_state];
                    if (!child_found || child_cost < child_best
                        || (child_cost == child_best && child_state < child_state_best)) {
                        child_found = true;
                        child_best = child_cost;
                        child_state_best = child_state;
                    }
                }
                if (!child_found) { valid = false; break; }
                cost += child_best;
                choice[item.other * states + state] = static_cast<std::uint32_t>(child_state_best);
            }
            if (valid) {
                feasible[body * states + state] = 1U;
                dp[body * states + state] = std::move(cost);
            }
        }
    }
    bool root_found = false;
    Rational root_best{};
    std::size_t root_state = 0U;
    for (std::size_t state = 0U; state < states; ++state) {
        if (feasible[state] == 0U) continue;
        if (!root_found || dp[state] < root_best || (dp[state] == root_best && state < root_state)) {
            root_found = true;
            root_best = dp[state];
            root_state = state;
        }
    }
    if (!root_found) return result;

    result.x.resize(bodies);
    result.y.resize(bodies);
    std::vector<std::size_t> selected(bodies);
    selected[0] = root_state;
    for (const std::size_t body : order) {
        const std::size_t state = selected[body];
        result.x[body] = candidate_x[body * states + state];
        result.y[body] = candidate_y[body * states + state];
        for (const Adjacent item : adjacency[body]) {
            if (parent[item.other] == body) {
                selected[item.other] = choice[item.other * states + state];
            }
        }
    }
    result.error = std::move(root_best);
    result.certified = true;
    return result;
}
} // namespace

QuantizedTreeRepairResult repair_exact_oblique_tree_neighbourhood(
    const ExactObliqueTreeOracleResult& continuous,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    std::size_t radius) {
    QuantizedTreeRepairResult result{};
    const std::size_t bodies = continuous.rounded_velocity_x.size();
    if (!continuous.certified_continuous || continuous.rounded_velocity_y.size() != bodies
        || continuous.exact_velocity_x_numerator.size() != bodies
        || continuous.exact_velocity_x_denominator.size() != bodies
        || continuous.exact_velocity_y_numerator.size() != bodies
        || continuous.exact_velocity_y_denominator.size() != bodies
        || masses.size() != bodies || radius > 3U) {
        return result;
    }
    std::vector<Rational> exact_x(bodies), exact_y(bodies);
    for (std::size_t body = 0U; body < bodies; ++body) {
        exact_x[body] = Rational(Integer(continuous.exact_velocity_x_numerator[body]),
            Integer(continuous.exact_velocity_x_denominator[body]));
        exact_y[body] = Rational(Integer(continuous.exact_velocity_y_numerator[body]),
            Integer(continuous.exact_velocity_y_denominator[body]));
    }
    result.valid_input = true;
    result.radius = radius;
    const QuantizedRepair repair = repair_quantized_tree_neighbourhood(
        exact_x, exact_y, masses, contacts, continuous.rounded_velocity_x,
        continuous.rounded_velocity_y, radius);
    result.certified_neighbourhood = repair.certified;
    if (!repair.certified) return result;
    result.error_numerator = integer_string(numerator(repair.error));
    result.error_denominator = integer_string(denominator(repair.error));
    result.velocity_x = repair.x;
    result.velocity_y = repair.y;
    for (const NormalContact& edge : contacts) {
        const Integer value = Integer(edge.normal.x)
                * (Integer(result.velocity_x[edge.first]) - result.velocity_x[edge.second])
            + Integer(edge.normal.y)
                * (Integer(result.velocity_y[edge.first]) - result.velocity_y[edge.second]);
        if (value > 0) {
            const Integer maximum = std::numeric_limits<std::uint64_t>::max();
            const std::uint64_t violation = value > maximum
                ? std::numeric_limits<std::uint64_t>::max()
                : value.convert_to<std::uint64_t>();
            result.primal_violation_raw = std::max(result.primal_violation_raw, violation);
        }
    }
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    mix_u64(hash, radius);
    for (Fixed::rep value : result.velocity_x) mix_u64(hash, static_cast<std::uint64_t>(value));
    for (Fixed::rep value : result.velocity_y) mix_u64(hash, static_cast<std::uint64_t>(value));
    result.hash = hash;
    return result;
}

ExactObliqueTreeOracleResult solve_exact_oblique_tree_active_sets(
    std::span<const Fixed::rep> input_x,
    std::span<const Fixed::rep> input_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    ExactObliqueTreeOracleConfig config) {
    ExactObliqueTreeOracleResult result{};
    const std::size_t bodies = input_x.size();
    const std::size_t edges = contacts.size();
    if (bodies == 0U || input_y.size() != bodies || masses.size() != bodies
        || bodies > config.maximum_bodies || edges > config.maximum_contacts
        || edges >= 63U) return result;
    for (std::size_t body = 0U; body < bodies; ++body) if (masses[body] == 0U) return result;
    for (const NormalContact& contact : contacts) {
        if (contact.first >= bodies || contact.second >= bodies || contact.first == contact.second
            || (contact.normal.x == 0 && contact.normal.y == 0)) return result;
    }
    result.valid_input = true;
    result.tree_valid = is_tree(bodies, contacts);
    if (!result.tree_valid) return result;

    const std::uint64_t masks = std::uint64_t{1} << edges;
    bool found = false;
    Rational best_objective{};
    std::vector<Rational> best_x, best_y;
    std::uint64_t best_mask{};

    for (std::uint64_t mask = 0U; mask < masks; ++mask) {
        ++result.active_sets_tested;
        std::vector<std::size_t> active;
        active.reserve(edges);
        for (std::size_t edge = 0U; edge < edges; ++edge) {
            if ((mask & (std::uint64_t{1} << edge)) != 0U) active.push_back(edge);
        }
        std::vector<Rational> lambda(active.size(), Rational{0});
        if (!active.empty()) {
            std::vector<Rational> matrix(active.size() * active.size(), Rational{0});
            std::vector<Rational> rhs(active.size(), Rational{0});
            for (std::size_t row = 0U; row < active.size(); ++row) {
                const NormalContact& edge = contacts[active[row]];
                rhs[row] = Rational(edge.normal.x)
                        * Rational(input_x[edge.first] - input_x[edge.second])
                    + Rational(edge.normal.y)
                        * Rational(input_y[edge.first] - input_y[edge.second]);
                for (std::size_t column = 0U; column < active.size(); ++column) {
                    Rational value{0};
                    const NormalContact& other = contacts[active[column]];
                    value += incidence_dot(edge, other, edge.first, masses[edge.first]);
                    value += incidence_dot(edge, other, edge.second, masses[edge.second]);
                    matrix[row * active.size() + column] = value;
                }
            }
            if (!gaussian_solve(std::move(matrix), std::move(rhs), active.size(), lambda)) continue;
            if (std::any_of(lambda.begin(), lambda.end(), [](const Rational& value) { return value < 0; })) continue;
        }

        std::vector<Rational> velocity_x(bodies), velocity_y(bodies);
        for (std::size_t body = 0U; body < bodies; ++body) {
            velocity_x[body] = input_x[body];
            velocity_y[body] = input_y[body];
        }
        for (std::size_t item = 0U; item < active.size(); ++item) {
            const NormalContact& edge = contacts[active[item]];
            const Rational first_scale = lambda[item] / masses[edge.first];
            const Rational second_scale = lambda[item] / masses[edge.second];
            velocity_x[edge.first] -= first_scale * edge.normal.x;
            velocity_y[edge.first] -= first_scale * edge.normal.y;
            velocity_x[edge.second] += second_scale * edge.normal.x;
            velocity_y[edge.second] += second_scale * edge.normal.y;
        }
        bool feasible = true;
        for (const NormalContact& edge : contacts) {
            if (constraint_value(edge, velocity_x, velocity_y) > 0) { feasible = false; break; }
        }
        if (!feasible) continue;

        Rational objective{0};
        for (std::size_t body = 0U; body < bodies; ++body) {
            const Rational dx = velocity_x[body] - input_x[body];
            const Rational dy = velocity_y[body] - input_y[body];
            objective += Rational(masses[body]) * (dx * dx + dy * dy);
        }
        if (!found || objective < best_objective || (objective == best_objective && mask < best_mask)) {
            found = true;
            best_objective = objective;
            best_x = std::move(velocity_x);
            best_y = std::move(velocity_y);
            best_mask = mask;
        }
    }
    if (!found) return result;

    result.certified_continuous = true;
    result.active_mask = best_mask;
    result.objective_numerator = integer_string(numerator(best_objective));
    result.objective_denominator = integer_string(denominator(best_objective));
    result.exact_velocity_x_numerator.resize(bodies);
    result.exact_velocity_x_denominator.resize(bodies);
    result.exact_velocity_y_numerator.resize(bodies);
    result.exact_velocity_y_denominator.resize(bodies);
    result.rounded_velocity_x.resize(bodies);
    result.rounded_velocity_y.resize(bodies);
    for (std::size_t body = 0U; body < bodies; ++body) {
        result.exact_velocity_x_numerator[body] = integer_string(numerator(best_x[body]));
        result.exact_velocity_x_denominator[body] = integer_string(denominator(best_x[body]));
        result.exact_velocity_y_numerator[body] = integer_string(numerator(best_y[body]));
        result.exact_velocity_y_denominator[body] = integer_string(denominator(best_y[body]));
        result.rounded_velocity_x[body] = round_nearest_even(best_x[body]);
        result.rounded_velocity_y[body] = round_nearest_even(best_y[body]);
    }
    for (const NormalContact& edge : contacts) {
        const Integer value = Integer(edge.normal.x)
                * (Integer(result.rounded_velocity_x[edge.first])
                    - result.rounded_velocity_x[edge.second])
            + Integer(edge.normal.y)
                * (Integer(result.rounded_velocity_y[edge.first])
                    - result.rounded_velocity_y[edge.second]);
        if (value > 0) {
            const Integer maximum = std::numeric_limits<std::uint64_t>::max();
            const std::uint64_t violation = value > maximum
                ? std::numeric_limits<std::uint64_t>::max()
                : value.convert_to<std::uint64_t>();
            result.rounded_primal_violation_raw = std::max(result.rounded_primal_violation_raw, violation);
        }
    }
    if (config.perform_quantized_repair) {
        const QuantizedRepair repair = repair_quantized_tree_neighbourhood(
            best_x, best_y, masses, contacts, result.rounded_velocity_x,
            result.rounded_velocity_y, config.quantized_repair_radius);
        result.repair_radius = config.quantized_repair_radius;
        result.repair_certified_neighbourhood = repair.certified;
        result.repaired_quantized = repair.certified;
        if (repair.certified) {
            result.repair_error_numerator = integer_string(numerator(repair.error));
            result.repair_error_denominator = integer_string(denominator(repair.error));
            result.repaired_velocity_x = repair.x;
            result.repaired_velocity_y = repair.y;
            for (const NormalContact& edge : contacts) {
                const Integer value = Integer(edge.normal.x)
                        * (Integer(result.repaired_velocity_x[edge.first])
                            - result.repaired_velocity_x[edge.second])
                    + Integer(edge.normal.y)
                        * (Integer(result.repaired_velocity_y[edge.first])
                            - result.repaired_velocity_y[edge.second]);
                if (value > 0) {
                    const Integer maximum = std::numeric_limits<std::uint64_t>::max();
                    const std::uint64_t violation = value > maximum
                        ? std::numeric_limits<std::uint64_t>::max()
                        : value.convert_to<std::uint64_t>();
                    result.repaired_primal_violation_raw = std::max(
                        result.repaired_primal_violation_raw, violation);
                }
            }
        }
    }
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    mix_u64(hash, best_mask);
    for (char item : result.objective_numerator) mix_u64(hash, static_cast<unsigned char>(item));
    for (char item : result.objective_denominator) mix_u64(hash, static_cast<unsigned char>(item));
    for (Fixed::rep value : result.rounded_velocity_x) mix_u64(hash, static_cast<std::uint64_t>(value));
    for (Fixed::rep value : result.rounded_velocity_y) mix_u64(hash, static_cast<std::uint64_t>(value));
    mix_u64(hash, result.repair_certified_neighbourhood ? 1U : 0U);
    for (Fixed::rep value : result.repaired_velocity_x) mix_u64(hash, static_cast<std::uint64_t>(value));
    for (Fixed::rep value : result.repaired_velocity_y) mix_u64(hash, static_cast<std::uint64_t>(value));
    result.hash = hash;
    return result;
}

} // namespace neoeng::core
