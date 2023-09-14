// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "uhs/transaction/transaction.hpp"
#include "uhs/twophase/locking_shard/locking_shard.hpp"
#include "util/common/hash.hpp"
#include "util/common/hashmap.hpp"
#include "util/common/keys.hpp"
#include "util/common/config.hpp"
#include "util/common/random_source.hpp"
#include "util/common/snapshot_map.hpp"

#include <secp256k1_bppp.h>
#include <benchmark/benchmark.h>
#include <gtest/gtest.h>
#include <unordered_map>
#include <random>
#include <unordered_set>

#define SWEEP_MAX 10000
#define EPOCH     1000

using namespace cbdc;
using secp256k1_context_destroy_type = void (*)(secp256k1_context*);
using uhs_element = locking_shard::locking_shard::uhs_element;

struct GensDeleter {
    explicit GensDeleter(secp256k1_context* ctx) : m_ctx(ctx) {}

    void operator()(secp256k1_bppp_generators* gens) const {
        secp256k1_bppp_generators_destroy(m_ctx, gens);
    }

    secp256k1_context* m_ctx;
};

static std::default_random_engine m_shuffle;

static const inline auto rnd
    = std::make_unique<random_source>(config::random_source);

static std::unique_ptr<secp256k1_context, secp256k1_context_destroy_type>
    secp{secp256k1_context_create(SECP256K1_CONTEXT_NONE),
         &secp256k1_context_destroy};

/// should be set to exactly `floor(log_base(value)) + 1`
///
/// We use n_bits = 64, base = 16, so this should always be 24.
static const inline auto generator_count = 16 + 8;

static std::unique_ptr<secp256k1_bppp_generators, GensDeleter>
    generators{
        secp256k1_bppp_generators_create(secp.get(),
                                         generator_count),
        GensDeleter(secp.get())};

static auto gen_map(uint64_t map_size, bool deleted = false) -> snapshot_map<hash_t, uhs_element> {
    std::uniform_int_distribution<uint64_t> dist(EPOCH - 100, EPOCH + 100);
    auto uhs = snapshot_map<hash_t, uhs_element>();

    auto comm = commit(secp.get(), 10, hash_t{}).value();
    auto rng
        = transaction::prove(secp.get(),
                             generators.get(),
                             *rnd,
                             {hash_t{}, 10},
                             &comm);
    auto commitment = serialize_commitment(secp.get(), comm);

    for(uint64_t i = 1; i <= map_size; i++) {
        transaction::compact_output out{commitment, rng, rnd->random_hash()};
        auto del = deleted ? std::optional<uint64_t>{dist(m_shuffle)} : std::nullopt;
        uhs_element el0{out, 0, del};
        auto key = transaction::calculate_uhs_id(out);
        uhs.emplace(key, el0);
    }
    return uhs;
}

static auto audit(snapshot_map<hash_t, uhs_element>& uhs,
                  snapshot_map<hash_t, uhs_element>& locked,
                  snapshot_map<hash_t, uhs_element>& spent)
    -> std::optional<commitment_t> {

    {
        uhs.snapshot();
        locked.snapshot();
        spent.snapshot();
    }

    uint64_t epoch = EPOCH;

    std::vector<commitment_t> comms{};
    auto summarize
        = [epoch, &comms](const snapshot_map<hash_t, uhs_element>& m)
        -> bool {
        for(const auto& [id, elem] : m) {
            if(elem.m_creation_epoch <= epoch
               && (!elem.m_deletion_epoch.has_value()
                   || (elem.m_deletion_epoch.value() > epoch))) {
                auto uhs_id = transaction::calculate_uhs_id(elem.m_out);
                if(uhs_id != id) {
                    return false;
                }

                auto rng = transaction::validation::check_range(
                    elem.m_out.m_auxiliary, elem.m_out.m_range);
                if(rng.has_value()) {
                    return false;
                }
                comms.push_back(elem.m_out.m_auxiliary);
            }
        }

        return true;
    };

    auto available_sum = summarize(uhs);
    if(!available_sum) {
        return std::nullopt;
    }

    auto locked_sum = summarize(locked);
    if(!locked_sum) {
        return std::nullopt;
    }

    auto spent_sum = summarize(spent);
    if(!spent_sum) {
        return std::nullopt;
    }

    {
        uhs.release_snapshot();
        locked.release_snapshot();
        spent.release_snapshot();
    }

    return sum_commitments(secp.get(), comms);
}

static void audit_routine(benchmark::State& state) {
    auto key_count = state.range(0);

    auto seed = std::chrono::high_resolution_clock::now()
                    .time_since_epoch()
                    .count();
    seed %= std::numeric_limits<uint32_t>::max();
    m_shuffle.seed(static_cast<uint32_t>(seed));

    uint32_t locked_sz{};
    uint32_t spent_sz{};
    {
        std::uniform_int_distribution<uint32_t> locked(0, key_count);
        locked_sz = locked(m_shuffle);
        std::uniform_int_distribution<uint32_t> spent(0, key_count - locked_sz);
        spent_sz = spent(m_shuffle);
    }

    snapshot_map<hash_t, uhs_element> uhs = gen_map(key_count);
    snapshot_map<hash_t, uhs_element> locked = gen_map(locked_sz);
    snapshot_map<hash_t, uhs_element> spent = gen_map(spent_sz, true);
    for(auto _ : state) {
        auto res = audit(uhs, locked, spent);
        ASSERT_NE(res, std::nullopt);
    }
}

BENCHMARK(audit_routine)
    ->RangeMultiplier(10)
    ->Range(10, SWEEP_MAX)
    ->Complexity(benchmark::oAuto);

BENCHMARK_MAIN();