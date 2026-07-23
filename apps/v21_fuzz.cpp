#include "neoeng/core/authoritative_paged_temporal_physics.hpp"
#include "neoeng/core/dynamic_island_pair_history.hpp"
#include "neoeng/core/exact_oblique_tree_oracle.hpp"
#include "neoeng/core/oblique_tree_grid_dp.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace neoeng::core;
constexpr std::int32_t kOne=1<<30;
constexpr std::array<NormalQ30,4> kNormals{{
    {kOne,0},{0,kOne},{759'250'125,759'250'125},{644'245'094,858'993'459}
}};
void mix(std::uint64_t& hash,std::uint64_t value) noexcept {
    for(unsigned byte=0;byte<8;++byte){hash^=(value>>(byte*8U))&0xFFU;hash*=0x100000001B3ULL;}
}

std::vector<std::size_t> model_islands(std::size_t bodies,std::span<const NormalContact> contacts) {
    std::vector<std::size_t> parent(bodies); std::iota(parent.begin(),parent.end(),0U);
    const auto root=[&](std::size_t body){while(parent[body]!=body)body=parent[body];return body;};
    for(const auto& c:contacts){std::size_t a=root(c.first),b=root(c.second);if(a==b)continue;if(b<a)std::swap(a,b);parent[b]=a;}
    std::vector<std::size_t> root_id(bodies,~std::size_t{0}),out(bodies);std::size_t count=0;
    for(std::size_t body=0;body<bodies;++body){const std::size_t r=root(body);if(root_id[r]==~std::size_t{0})root_id[r]=count++;out[body]=root_id[r];}
    return out;
}

bool dynamic_history_case(std::mt19937_64& rng,std::uint64_t& aggregate) {
    constexpr std::size_t bodies=16U, capacity=8U, max_pairs=24U;
    DynamicIslandPairHistory history({.bodies=bodies,.maximum_contacts=15U,.maximum_pairs=max_pairs,
        .history_capacity=capacity,.pair_generations=capacity+2U,.topology_generations=capacity+2U});
    std::array<std::vector<BroadphasePair>,capacity> pair_model;
    std::array<std::vector<std::size_t>,capacity> island_model;
    std::array<std::size_t,capacity> spill_model{};

    auto make_topology=[&](){
        std::vector<NormalContact> contacts; std::vector<std::size_t> parent(bodies);std::iota(parent.begin(),parent.end(),0U);
        const auto root=[&](std::size_t body){while(parent[body]!=body)body=parent[body];return body;};
        const std::size_t desired=4U+static_cast<std::size_t>(rng()%9U);
        while(contacts.size()<desired){std::size_t a=rng()%bodies,b=rng()%bodies;if(a==b)continue;std::size_t ra=root(a),rb=root(b);if(ra==rb)continue;if(rb<ra)std::swap(ra,rb);parent[rb]=ra;contacts.push_back({a,b,kNormals[rng()%kNormals.size()]});}
        std::shuffle(contacts.begin(),contacts.end(),rng);return contacts;
    };
    auto make_pairs=[&](){
        std::set<BroadphasePair> unique; const std::size_t count=8U+static_cast<std::size_t>(rng()%12U);
        while(unique.size()<count){std::size_t a=rng()%bodies,b=rng()%bodies;if(a==b)continue;if(b<a)std::swap(a,b);unique.insert({a,b});}
        return std::vector<BroadphasePair>(unique.begin(),unique.end());
    };

    auto contacts=make_topology(); auto pairs=make_pairs();
    history.initialize(0U,contacts,pairs);
    pair_model[0]=pairs; island_model[0]=model_islands(bodies,contacts);
    spill_model[0]=static_cast<std::size_t>(std::count_if(pairs.begin(),pairs.end(),[&](const BroadphasePair& p){return island_model[0][p.first]!=island_model[0][p.second];}));
    for(std::uint64_t frame=1U;frame<=80U;++frame){
        contacts=make_topology(); pairs=make_pairs(); const std::size_t dirty=rng()%bodies;
        history.capture(frame,contacts,pairs,std::span<const std::size_t>(&dirty,1U),true,true);
        const std::size_t slot=frame%capacity; pair_model[slot]=pairs; island_model[slot]=model_islands(bodies,contacts);
        spill_model[slot]=static_cast<std::size_t>(std::count_if(pairs.begin(),pairs.end(),[&](const BroadphasePair& p){return island_model[slot][p.first]!=island_model[slot][p.second];}));
        std::vector<BroadphasePair> restored(max_pairs),spill(max_pairs); std::vector<std::size_t> islands(bodies);
        restored.resize(history.restore_pairs(frame,restored)); spill.resize(history.restore_spill_pairs(frame,spill));
        const std::size_t restored_island_count=history.restore_body_islands(frame,islands);
        if(restored_island_count==0U)return false;
        if(restored!=pair_model[slot]||islands!=island_model[slot]||spill.size()!=spill_model[slot])return false;
        for(const auto& p:spill)if(islands[p.first]==islands[p.second])return false;
    }
    const std::uint64_t retained=76U;
    history.truncate_after(retained);
    if(history.contains(80U)||!history.contains(retained))return false;
    std::vector<BroadphasePair> overflow(max_pairs+1U);
    for(std::size_t i=0;i<overflow.size();++i)overflow[i]={i%bodies,(i+1U)%bodies};
    bool failed=false;try{history.capture(retained+1U,contacts,overflow,{},true,true);}catch(const std::length_error&){failed=true;}
    if(!failed||!history.contains(retained)||history.contains(retained+1U))return false;
    mix(aggregate,history.hash(retained));mix(aggregate,history.stats().topology_changes);return true;
}

std::uint64_t exact_two_body_grid_objective(
    std::span<const Fixed::rep> x, std::span<const Fixed::rep> y,
    std::span<const std::uint32_t> masses, const NormalContact& contact) {
    std::uint64_t best=~std::uint64_t{0};
    for(Fixed::rep ax=-5;ax<=5;++ax)for(Fixed::rep ay=-5;ay<=5;++ay)
    for(Fixed::rep bx=-5;bx<=5;++bx)for(Fixed::rep by=-5;by<=5;++by){
        const WideInteger constraint=static_cast<WideInteger>(contact.normal.x)*(ax-bx)
            +static_cast<WideInteger>(contact.normal.y)*(ay-by);
        if(constraint>0)continue;
        const WideInteger dax=ax-x[0],day=ay-y[0],dbx=bx-x[1],dby=by-y[1];
        const WideInteger cost=static_cast<WideInteger>(masses[0])*(dax*dax+day*day)
            +static_cast<WideInteger>(masses[1])*(dbx*dbx+dby*dby);
        if(cost<best)best=static_cast<std::uint64_t>(cost);
    }
    return best;
}

bool authoritative_topology_rollback_case(std::uint64_t& aggregate) {
    constexpr std::size_t bodies=4U, contacts_count=2U;
    std::array<Fixed::rep,bodies> px{0,4,8,12},py{},vx{},vy{};
    std::array<std::uint32_t,bodies> masses{1,2,3,4};
    std::array<NormalContact,contacts_count> contacts{{
        {0U,1U,{kOne,0}},{2U,3U,{kOne,0}}
    }};
    const AtomicTemporalPhysicsConfig physics{.bodies=bodies,.contacts=contacts_count,
        .maximum_candidate_pairs=8U,.history_capacity=8U,.horizon_frames=4U,
        .maximum_velocity_mutations=1U,.maximum_mass_mutations=1U,.maximum_contact_mutations=1U,
        .half_extent=Fixed::from_ratio(1,2),.projection={.maximum_iterations=16U,.feasibility_tolerance_raw=8U}};
    const AuthoritativePagedTemporalConfig config{.physics={.physics=physics,.history={
        .bodies=bodies,.contacts=contacts_count,.maximum_candidate_pairs=8U,.history_capacity=8U,
        .page_elements=4U,.maximum_position_dirty_pages_per_frame=2U,
        .maximum_velocity_dirty_pages_per_frame=2U,.maximum_mass_dirty_pages_per_frame=2U,
        .maximum_contact_dirty_pages_per_frame=2U,.full_position_generations=3U,
        .full_velocity_generations=3U,.full_contact_generations=3U,.maximum_cache_generations=4U}},
        .pair_history={.bodies=bodies,.maximum_contacts=contacts_count,.maximum_pairs=8U,
        .history_capacity=8U,.pair_generations=4U,.topology_generations=4U}};
    const ContactMutation mutation{1U,{1U,2U,{kOne,0}}};
    AuthoritativePagedTemporalPhysicsEngine clean(config),rollback(config);
    clean.initialize(px,py,vx,vy,masses,contacts);
    rollback.initialize(px,py,vx,vy,masses,contacts);
    clean.set_input(2U,{.contact=std::span<const ContactMutation>(&mutation,1U)});
    clean.simulate_to(4U);
    rollback.simulate_to(4U);
    rollback.correct_and_resimulate(2U,{.contact=std::span<const ContactMutation>(&mutation,1U)},4U);
    if(!rollback.physically_equivalent_to(clean)
        ||rollback.authoritative_pair_hash()!=clean.authoritative_pair_hash())return false;
    mix(aggregate,rollback.physical_hash());
    mix(aggregate,rollback.authoritative_pair_hash());
    return true;
}

bool exact_oracle_case(std::mt19937_64& rng,std::uint64_t& aggregate) {
    const std::size_t bodies=2U+static_cast<std::size_t>(rng()%6U);
    std::vector<Fixed::rep>x(bodies),y(bodies);std::vector<std::uint32_t>masses(bodies);std::vector<NormalContact>contacts;contacts.reserve(bodies-1U);
    for(std::size_t body=0;body<bodies;++body){x[body]=static_cast<Fixed::rep>(static_cast<std::int64_t>(rng()%7U)-3);y[body]=static_cast<Fixed::rep>(static_cast<std::int64_t>(rng()%7U)-3);masses[body]=1U+static_cast<std::uint32_t>(rng()%5U);if(body)contacts.push_back({static_cast<std::size_t>(rng()%body),body,kNormals[rng()%kNormals.size()]});}
    const auto exact=solve_exact_oblique_tree_active_sets(x,y,masses,contacts,{.maximum_bodies=7U,.maximum_contacts=6U});
    if(!exact.certified_continuous){std::cerr<<"exact not certified bodies="<<bodies<<"\n";return false;}
    auto reordered_contacts=contacts;std::shuffle(reordered_contacts.begin(),reordered_contacts.end(),rng);
    const auto reordered=solve_exact_oblique_tree_active_sets(x,y,masses,reordered_contacts,{.maximum_bodies=7U,.maximum_contacts=6U});
    if(!reordered.certified_continuous||reordered.objective_numerator!=exact.objective_numerator
        ||reordered.objective_denominator!=exact.objective_denominator
        ||reordered.rounded_velocity_x!=exact.rounded_velocity_x||reordered.rounded_velocity_y!=exact.rounded_velocity_y){std::cerr<<"reorder mismatch bodies="<<bodies<<" obj="<<exact.objective_numerator<<"/"<<exact.objective_denominator<<" vs "<<reordered.objective_numerator<<"/"<<reordered.objective_denominator<<"\n";return false;}
    if(bodies==2U){
        const std::uint64_t grid=exact_two_body_grid_objective(x,y,masses,contacts.front());
        const long double continuous=std::stold(exact.objective_numerator)/std::stold(exact.objective_denominator);
        if(continuous>static_cast<long double>(grid)){std::cerr<<"exact-grid order fail continuous="
            <<static_cast<double>(continuous)<<" grid="<<grid<<"\n";return false;}
        mix(aggregate,grid);
    }
    mix(aggregate,exact.hash);return true;
}
} // namespace

int main(int argc,char** argv){
    const std::size_t iterations=argc>1?static_cast<std::size_t>(std::strtoull(argv[1],nullptr,10)):5'000U;
    std::mt19937_64 rng(0x4E454F454E475632ULL);std::uint64_t aggregate=0xCBF29CE484222325ULL;
    if(!authoritative_topology_rollback_case(aggregate)){std::cerr<<"v0.21 authoritative topology rollback failed\n";return EXIT_FAILURE;}
    for(std::size_t i=0;i<iterations;++i){
        if(!dynamic_history_case(rng,aggregate)){std::cerr<<"v0.21 dynamic history fuzz failed at "<<i<<'\n';return EXIT_FAILURE;}
        if(!exact_oracle_case(rng,aggregate)){std::cerr<<"v0.21 exact oracle fuzz failed at "<<i<<'\n';return EXIT_FAILURE;}
    }
    std::cout<<"v0.21 fuzz iterations="<<iterations<<" aggregate=0x"<<std::hex<<std::uppercase<<aggregate<<std::dec<<'\n';return EXIT_SUCCESS;
}
