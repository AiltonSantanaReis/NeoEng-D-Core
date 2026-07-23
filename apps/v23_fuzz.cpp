#include "neoeng/core/exact_oblique_tree_oracle.hpp"
#include "neoeng/core/paged_segmented_pair_history.hpp"
#include "neoeng/core/segmented_authoritative_paged_temporal_physics.hpp"
#include "neoeng/core/segmented_dynamic_pair_history.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <span>
#include <vector>

using namespace neoeng::core;
namespace {
constexpr std::int32_t kOne = 1 << 30;
constexpr std::array<NormalQ30, 6> kNormals{{
    {kOne,0},{0,kOne},{759250125,759250125},{-759250125,759250125},
    {644245094,858993459},{-644245094,858993459}
}};
void mix(std::uint64_t& hash,std::uint64_t value) noexcept {
    for(unsigned byte=0U;byte<8U;++byte){hash^=(value>>(byte*8U))&0xFFU;hash*=0x100000001B3ULL;}
}

std::vector<std::size_t> model_keys(std::size_t bodies,std::span<const NormalContact> contacts){
    std::vector<std::size_t> parent(bodies);std::iota(parent.begin(),parent.end(),0U);
    const auto root=[&](std::size_t body){while(parent[body]!=body)body=parent[body];return body;};
    for(const auto& c:contacts){std::size_t a=root(c.first),b=root(c.second);if(a==b)continue;if(b<a)std::swap(a,b);parent[b]=a;}
    std::vector<std::size_t> out(bodies);for(std::size_t body=0;body<bodies;++body)out[body]=root(body);return out;
}

bool history_case(std::mt19937_64& rng,std::uint64_t& aggregate){
    constexpr std::size_t bodies=32U,capacity=8U,max_pairs=48U;
    const std::size_t pages=(bodies+7U)/8U;
    PagedSegmentedPairHistory paged({.bodies=bodies,.maximum_contacts=24U,.maximum_pairs=max_pairs,
        .maximum_pairs_per_segment=48U,.history_capacity=capacity,.table_page_elements=8U,
        .segment_generations=96U,.spill_generations=12U,.table_generations=10U,
        .body_key_page_generations=pages*capacity+8U,.segment_map_page_generations=pages*capacity+16U});
    SegmentedDynamicPairHistory dense({.bodies=bodies,.maximum_contacts=24U,.maximum_pairs=max_pairs,
        .maximum_pairs_per_segment=48U,.history_capacity=capacity,.segment_generations=96U,
        .spill_generations=12U,.table_generations=10U});
    auto make_topology=[&](){
        std::vector<NormalContact> contacts;std::vector<std::size_t> parent(bodies);std::iota(parent.begin(),parent.end(),0U);
        const auto root=[&](std::size_t body){while(parent[body]!=body)body=parent[body];return body;};
        const std::size_t desired=4U+static_cast<std::size_t>(rng()%18U);
        while(contacts.size()<desired){std::size_t a=rng()%bodies,b=rng()%bodies;if(a==b)continue;std::size_t ra=root(a),rb=root(b);if(ra==rb)continue;if(rb<ra)std::swap(ra,rb);parent[rb]=ra;contacts.push_back({a,b,kNormals[rng()%kNormals.size()]});}
        std::shuffle(contacts.begin(),contacts.end(),rng);return contacts;
    };
    auto make_pairs=[&](){
        std::set<BroadphasePair> unique;const std::size_t count=8U+static_cast<std::size_t>(rng()%24U);
        while(unique.size()<count){std::size_t a=rng()%bodies,b=rng()%bodies;if(a==b)continue;if(b<a)std::swap(a,b);unique.insert({a,b});}
        return std::vector<BroadphasePair>(unique.begin(),unique.end());
    };
    auto contacts=make_topology();auto pairs=make_pairs();
    paged.initialize(0U,contacts,pairs);dense.initialize(0U,contacts,pairs);
    for(std::uint64_t frame=1U;frame<=48U;++frame){
        contacts=make_topology();pairs=make_pairs();std::array<std::size_t,2> dirty{rng()%bodies,rng()%bodies};
        paged.capture(frame,contacts,pairs,dirty,true,true,false);
        dense.capture(frame,contacts,pairs,dirty,true,true,false);
        std::vector<BroadphasePair>a(max_pairs),b(max_pairs);std::vector<std::size_t>keys(bodies),expected=model_keys(bodies,contacts);
        a.resize(paged.restore_pairs(frame,a));b.resize(dense.restore_pairs(frame,b));
        const std::size_t restored_islands = paged.restore_body_island_keys(frame,keys);
        if(restored_islands==0U||a!=b||keys!=expected||paged.pair_count(frame)!=pairs.size())return false;
        if(paged.hash(frame)!=dense.hash(frame))return false;
        mix(aggregate,paged.hash(frame));
    }
    const std::uint64_t retained=44U;paged.truncate_after(retained);dense.truncate_after(retained);
    if(paged.contains(48U)||!paged.contains(retained)||dense.contains(48U)||!dense.contains(retained))return false;
    mix(aggregate,paged.hash(retained));mix(aggregate,paged.stats().segment_pages_written);
    return true;
}

bool repair_case(std::mt19937_64& rng,std::uint64_t& aggregate){
    const std::size_t bodies=2U+static_cast<std::size_t>(rng()%6U);
    std::vector<Fixed::rep>x(bodies),y(bodies);std::vector<std::uint32_t>masses(bodies);std::vector<NormalContact>contacts;contacts.reserve(bodies-1U);
    for(std::size_t body=0U;body<bodies;++body){
        x[body]=static_cast<Fixed::rep>(static_cast<std::int64_t>(rng()%11U)-5);
        y[body]=static_cast<Fixed::rep>(static_cast<std::int64_t>(rng()%11U)-5);
        masses[body]=1U+static_cast<std::uint32_t>(rng()%7U);
        if(body)contacts.push_back({static_cast<std::size_t>(rng()%body),body,kNormals[rng()%kNormals.size()]});
    }
    const auto continuous=solve_exact_oblique_tree_active_sets(x,y,masses,contacts,
        {.maximum_bodies=7U,.maximum_contacts=6U,.quantized_repair_radius=0U,.perform_quantized_repair=false});
    if(!continuous.certified_continuous)return false;
    QuantizedTreeRepairResult repair{};std::size_t radius=0U;
    for(;radius<=2U;++radius){repair=repair_exact_oblique_tree_neighbourhood(continuous,masses,contacts,radius);if(repair.certified_neighbourhood)break;}
    if(!repair.certified_neighbourhood||repair.primal_violation_raw!=0U)return false;
    auto reordered=contacts;std::shuffle(reordered.begin(),reordered.end(),rng);
    const auto continuous_reordered=solve_exact_oblique_tree_active_sets(x,y,masses,reordered,
        {.maximum_bodies=7U,.maximum_contacts=6U,.quantized_repair_radius=0U,.perform_quantized_repair=false});
    if(!continuous_reordered.certified_continuous
        ||continuous_reordered.objective_numerator!=continuous.objective_numerator
        ||continuous_reordered.objective_denominator!=continuous.objective_denominator)return false;
    QuantizedTreeRepairResult repair_reordered{};std::size_t radius_reordered=0U;
    for(;radius_reordered<=2U;++radius_reordered){repair_reordered=repair_exact_oblique_tree_neighbourhood(continuous_reordered,masses,reordered,radius_reordered);if(repair_reordered.certified_neighbourhood)break;}
    if(radius_reordered!=radius||repair_reordered.velocity_x!=repair.velocity_x||repair_reordered.velocity_y!=repair.velocity_y)return false;
    mix(aggregate,continuous.hash);mix(aggregate,repair.hash);mix(aggregate,radius);return true;
}

bool integrated_case(std::uint64_t& aggregate){
    constexpr std::size_t bodies=16U,pairs_count=8U;
    std::array<Fixed::rep,bodies>px{},py{},vx{},vy{};std::array<std::uint32_t,bodies>masses{};std::array<NormalContact,pairs_count>contacts{};
    for(std::size_t body=0U;body<bodies;++body){px[body]=static_cast<Fixed::rep>((body/2U)*4U);masses[body]=1U+static_cast<std::uint32_t>(body%5U);}
    for(std::size_t pair=0U;pair<pairs_count;++pair){contacts[pair]={pair*2U,pair*2U+1U,{kOne,0}};px[pair*2U+1U]=px[pair*2U]+1;vx[pair*2U]=1;vx[pair*2U+1U]=-1;}
    const AtomicTemporalPhysicsConfig physics{.bodies=bodies,.contacts=pairs_count,.maximum_candidate_pairs=128U,
        .history_capacity=8U,.horizon_frames=4U,.maximum_velocity_mutations=1U,.maximum_mass_mutations=1U,
        .maximum_contact_mutations=1U,.half_extent=Fixed::from_ratio(1,2),
        .projection={.maximum_iterations=16U,.feasibility_tolerance_raw=8U}};
    const SegmentedAuthoritativeTemporalConfig config{.physics={.physics=physics,.history={
        .bodies=bodies,.contacts=pairs_count,.maximum_candidate_pairs=128U,.history_capacity=8U,
        .page_elements=4U,.maximum_position_dirty_pages_per_frame=4U,
        .maximum_velocity_dirty_pages_per_frame=4U,.maximum_mass_dirty_pages_per_frame=2U,
        .maximum_contact_dirty_pages_per_frame=2U,.full_position_generations=3U,
        .full_velocity_generations=3U,.full_contact_generations=3U,.maximum_cache_generations=4U}},
        .pair_history={.bodies=bodies,.maximum_contacts=pairs_count,.maximum_pairs=128U,
        .maximum_pairs_per_segment=8U,.history_capacity=8U,.table_page_elements=4U,
        .segment_generations=64U,.spill_generations=4U,.table_generations=10U,
        .body_key_page_generations=12U,.segment_map_page_generations=20U}};
    const VelocityMutation original{3U,7,-2},corrected{3U,19,-5};
    SegmentedAuthoritativePagedTemporalPhysicsEngine clean(config),rollback(config);
    clean.initialize(px,py,vx,vy,masses,contacts);rollback.initialize(px,py,vx,vy,masses,contacts);
    rollback.set_input(2U,{.velocity=std::span<const VelocityMutation>(&original,1U)});
    clean.set_input(2U,{.velocity=std::span<const VelocityMutation>(&corrected,1U)});
    clean.simulate_to(4U);rollback.simulate_to(4U);
    rollback.correct_and_resimulate(2U,{.velocity=std::span<const VelocityMutation>(&corrected,1U)},4U);
    if(!rollback.physically_equivalent_to(clean)||rollback.authoritative_pair_hash()!=clean.authoritative_pair_hash())return false;
    mix(aggregate,rollback.physical_hash());mix(aggregate,rollback.authoritative_pair_hash());return true;
}
} // namespace

int main(int argc,char** argv){
    const std::size_t iterations=argc>1?static_cast<std::size_t>(std::strtoull(argv[1],nullptr,10)):5'000U;
    std::mt19937_64 rng(0x4E454F454E475633ULL);std::uint64_t aggregate=0xCBF29CE484222325ULL;
    if(!integrated_case(aggregate)){std::cerr<<"v0.23 integrated rollback failed\n";return EXIT_FAILURE;}
    for(std::size_t i=0U;i<iterations;++i){
        if(!history_case(rng,aggregate)){std::cerr<<"v0.23 history fuzz failed at "<<i<<'\n';return EXIT_FAILURE;}
        if(!repair_case(rng,aggregate)){std::cerr<<"v0.23 repair fuzz failed at "<<i<<'\n';return EXIT_FAILURE;}
    }
    std::cout<<"v0.23 fuzz iterations="<<iterations<<" aggregate=0x"<<std::hex<<std::uppercase<<aggregate<<std::dec<<'\n';
    return EXIT_SUCCESS;
}
