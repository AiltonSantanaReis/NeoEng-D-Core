#include "neoeng/core/arbitrary_normal_projection.hpp"
#include "neoeng/core/exact_oblique_tree_oracle.hpp"
#include "neoeng/core/oblique_star_projection.hpp"
#include "neoeng/core/paged_segmented_pair_history.hpp"
#include "neoeng/core/segmented_authoritative_paged_temporal_physics.hpp"

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/rational_adaptor.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <span>
#include <vector>

using namespace neoeng::core;
namespace {
constexpr std::int32_t kOne = 1 << 30;
constexpr std::array<NormalQ30, 8> kNormals{{
    {kOne,0},{0,kOne},{759250125,759250125},{-759250125,759250125},
    {644245094,858993459},{-644245094,858993459},{960383883,480191942},{-480191942,960383883}
}};
void mix(std::uint64_t& hash, std::uint64_t value) noexcept {
    for(unsigned byte=0U;byte<8U;++byte){hash^=(value>>(byte*8U))&0xFFU;hash*=0x100000001B3ULL;}
}


bool interleaved_merge_case(std::uint64_t& aggregate) {
    constexpr std::size_t bodies = 8U;
    const std::array<NormalContact, 3> contacts{{
        {0U, 5U, {kOne, 0}}, {5U, 7U, {kOne, 0}}, {1U, 2U, {kOne, 0}}
    }};
    const std::array<BroadphasePair, 3> expected{{{0U, 7U}, {1U, 2U}, {5U, 7U}}};
    PagedSegmentedPairHistory history({
        .bodies = bodies, .maximum_contacts = contacts.size(), .maximum_pairs = expected.size(),
        .maximum_pairs_per_segment = 2U, .history_capacity = 4U, .table_page_elements = 4U,
        .segment_generations = 12U, .spill_generations = 4U, .table_generations = 6U,
        .body_key_page_generations = 8U, .segment_map_page_generations = 8U,
    });
    history.initialize(0U, contacts, expected);
    std::array<BroadphasePair, 3> output{};
    const std::size_t count = history.restore_pairs(0U, output);
    if (count != expected.size() || output != expected || history.stats().merged_restores != 1U) return false;
    mix(aggregate, history.hash(0U));
    return true;
}

bool restore_case(std::mt19937_64& rng, std::uint64_t& aggregate) {
    constexpr std::size_t bodies=48U, max_pairs=96U, capacity=8U;
    const std::size_t pages=(bodies+7U)/8U;
    PagedSegmentedPairHistory history({.bodies=bodies,.maximum_contacts=40U,.maximum_pairs=max_pairs,
        .maximum_pairs_per_segment=48U,.history_capacity=capacity,.table_page_elements=8U,
        .segment_generations=192U,.spill_generations=16U,.table_generations=10U,
        .body_key_page_generations=pages*capacity+8U,.segment_map_page_generations=pages*capacity+24U});
    auto topology=[&](){
        std::vector<NormalContact> edges; std::vector<std::size_t> parent(bodies); std::iota(parent.begin(),parent.end(),0U);
        const auto root=[&](std::size_t b){while(parent[b]!=b)b=parent[b];return b;};
        const std::size_t wanted=6U+static_cast<std::size_t>(rng()%30U);
        while(edges.size()<wanted){std::size_t a=rng()%bodies,b=rng()%bodies;if(a==b)continue;auto ra=root(a),rb=root(b);if(ra==rb)continue;if(rb<ra)std::swap(ra,rb);parent[rb]=ra;edges.push_back({a,b,kNormals[rng()%kNormals.size()]});}
        std::shuffle(edges.begin(),edges.end(),rng);return edges;
    };
    auto pairs=[&](){
        std::set<BroadphasePair> unique; const std::size_t wanted=12U+static_cast<std::size_t>(rng()%60U);
        while(unique.size()<wanted){std::size_t a=rng()%bodies,b=rng()%bodies;if(a==b)continue;if(b<a)std::swap(a,b);unique.insert({a,b});}
        return std::vector<BroadphasePair>(unique.begin(),unique.end());
    };
    auto contacts=topology(); auto expected=pairs(); history.initialize(0U,contacts,expected);
    std::vector<BroadphasePair> output(max_pairs);
    for(std::uint64_t frame=1U;frame<=40U;++frame){
        contacts=topology(); expected=pairs(); std::array<std::size_t,2> dirty{rng()%bodies,rng()%bodies};
        history.capture(frame,contacts,expected,dirty,true,true,false);
        output.resize(max_pairs); output.resize(history.restore_pairs(frame,output));
        if(output!=expected)return false;
        mix(aggregate,history.hash(frame));
    }
    const auto stats=history.stats();
    if(stats.direct_ordered_restores+stats.merged_restores==0U)return false;
    mix(aggregate,stats.direct_ordered_restores); mix(aggregate,stats.merged_restores); mix(aggregate,stats.merge_heap_pushes);
    return true;
}

bool star_case(std::mt19937_64& rng, std::uint64_t& aggregate) {
    const std::size_t bodies=2U+static_cast<std::size_t>(rng()%7U);
    const std::size_t center=static_cast<std::size_t>(rng()%bodies);
    std::vector<Fixed::rep>x(bodies),y(bodies); std::vector<std::uint32_t> masses(bodies);
    for(std::size_t body=0U;body<bodies;++body){
        x[body]=static_cast<Fixed::rep>(static_cast<std::int64_t>(rng()%31U)-15);
        y[body]=static_cast<Fixed::rep>(static_cast<std::int64_t>(rng()%31U)-15);
        masses[body]=1U+static_cast<std::uint32_t>(rng()%9U);
    }
    std::vector<NormalContact> contacts; contacts.reserve(bodies-1U);
    for(std::size_t body=0U;body<bodies;++body){if(body==center)continue;
        const auto normal=kNormals[rng()%kNormals.size()];
        if((rng()&1U)==0U)contacts.push_back({center,body,normal}); else contacts.push_back({body,center,normal});
    }
    std::shuffle(contacts.begin(),contacts.end(),rng);
    const auto candidate=solve_oblique_star_leaf_elimination(x,y,masses,contacts,{.maximum_iterations=64U});
    const auto oracle=solve_exact_oblique_tree_active_sets(x,y,masses,contacts,
        {.maximum_bodies=8U,.maximum_contacts=7U,.quantized_repair_radius=0U,.perform_quantized_repair=false});
    if(!candidate.certified_continuous||!oracle.certified_continuous)return false;
    if(candidate.objective_numerator!=oracle.objective_numerator||candidate.objective_denominator!=oracle.objective_denominator)return false;
    if(candidate.exact_velocity_x_numerator!=oracle.exact_velocity_x_numerator
        ||candidate.exact_velocity_x_denominator!=oracle.exact_velocity_x_denominator
        ||candidate.exact_velocity_y_numerator!=oracle.exact_velocity_y_numerator
        ||candidate.exact_velocity_y_denominator!=oracle.exact_velocity_y_denominator)return false;
    auto reordered=contacts;std::shuffle(reordered.begin(),reordered.end(),rng);
    const auto again=solve_oblique_star_leaf_elimination(x,y,masses,reordered,{.maximum_iterations=64U});
    if(!again.certified_continuous||again.objective_numerator!=candidate.objective_numerator
        ||again.objective_denominator!=candidate.objective_denominator||again.rounded_velocity_x!=candidate.rounded_velocity_x
        ||again.rounded_velocity_y!=candidate.rounded_velocity_y)return false;
    mix(aggregate,candidate.hash);mix(aggregate,candidate.iterations);mix(aggregate,candidate.active_leaves);
    return true;
}


bool candidate_lower_bound_case(std::mt19937_64& rng, std::uint64_t& aggregate) {
    const std::size_t bodies = 3U + static_cast<std::size_t>(rng() % 6U);
    std::vector<Fixed::rep> input_x(bodies), input_y(bodies), vx(bodies), vy(bodies);
    std::vector<std::uint32_t> masses(bodies);
    std::vector<NormalContact> contacts; contacts.reserve(bodies - 1U);
    for (std::size_t body = 0U; body < bodies; ++body) {
        input_x[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 31U) - 15);
        input_y[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 31U) - 15);
        masses[body] = 1U + static_cast<std::uint32_t>(rng() % 7U);
        if (body != 0U) {
            const std::size_t parent = static_cast<std::size_t>(rng() % body);
            const NormalQ30 normal = kNormals[rng() % kNormals.size()];
            contacts.push_back((rng() & 1U) == 0U ? NormalContact{parent, body, normal}
                                                   : NormalContact{body, parent, normal});
        }
    }
    std::shuffle(contacts.begin(), contacts.end(), rng);
    const auto oracle = solve_exact_oblique_tree_active_sets(input_x, input_y, masses, contacts,
        {.maximum_bodies = 8U, .maximum_contacts = 7U, .quantized_repair_radius = 0U,
         .perform_quantized_repair = false});
    if (!oracle.certified_continuous) return false;
    vx = input_x; vy = input_y;
    ArbitraryNormalScratch scratch(8U, 7U);
    (void)project_arbitrary_normals_inplace(vx, vy, masses, contacts,
        {.maximum_iterations = 64U, .feasibility_tolerance_raw = 0U,
         .unit_norm_tolerance_q60 = std::numeric_limits<std::uint64_t>::max()}, scratch);
    bool feasible = true;
    for (const NormalContact& edge : contacts) {
        const WideInteger residual = WideInteger(edge.normal.x) * (vx[edge.first] - vx[edge.second])
            + WideInteger(edge.normal.y) * (vy[edge.first] - vy[edge.second]);
        if (residual > 0) { feasible = false; break; }
    }
    if (feasible) {
        using Rational = boost::multiprecision::cpp_rational;
        using Integer = boost::multiprecision::cpp_int;
        Integer candidate = 0;
        for (std::size_t body = 0U; body < bodies; ++body) {
            const Integer dx = Integer(vx[body]) - input_x[body];
            const Integer dy = Integer(vy[body]) - input_y[body];
            candidate += Integer(masses[body]) * (dx * dx + dy * dy);
        }
        const Rational optimum = Rational(Integer(oracle.objective_numerator)) / Integer(oracle.objective_denominator);
        if (Rational(candidate) < optimum) return false;
    }
    mix(aggregate, static_cast<std::uint64_t>(feasible));
    mix(aggregate, static_cast<std::uint64_t>(vx.front()));
    mix(aggregate, oracle.hash);
    return true;
}

} // namespace

int main(int argc,char**argv){
    const std::size_t iterations=argc>1?static_cast<std::size_t>(std::strtoull(argv[1],nullptr,10)):5'000U;
    std::mt19937_64 rng(0x4E454F454E475632ULL);std::uint64_t aggregate=0xCBF29CE484222325ULL;
    if (!interleaved_merge_case(aggregate)) { std::cerr << "v0.25 interleaved merge failed\n"; return EXIT_FAILURE; }
    for(std::size_t i=0U;i<iterations;++i){
        if(!restore_case(rng,aggregate)){std::cerr<<"v0.25 canonical restore failed at "<<i<<'\n';return EXIT_FAILURE;}
        if(!star_case(rng,aggregate)){std::cerr<<"v0.25 star elimination failed at "<<i<<'\n';return EXIT_FAILURE;}
        if(!candidate_lower_bound_case(rng,aggregate)){std::cerr<<"v0.25 candidate lower-bound failure at "<<i<<'\n';return EXIT_FAILURE;}
    }
    std::cout<<"v0.25 fuzz iterations="<<iterations<<" aggregate=0x"<<std::hex<<std::uppercase<<aggregate<<std::dec<<'\n';
    return EXIT_SUCCESS;
}
