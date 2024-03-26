// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "uhs/transaction/transaction.hpp"
#include "uhs/transaction/validation.hpp"
#include "uhs/transaction/wallet.hpp"
#include "util/common/config.hpp"
#include "util/serialization/buffer_serializer.hpp"
#include "util/serialization/format.hpp"
#include "util/serialization/ostream_serializer.hpp"
#include <benchmark/benchmark.h>
#include "uhs/twophase/sentinel_2pc/tx_history.hpp"

#include <filesystem>
#include <gtest/gtest.h>
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <vector>
#include <chrono>
#include <string>
#include <string>

using namespace std;
using namespace chrono;
using namespace cbdc::sentinel_2pc;

static constexpr auto g_shard_test_dir = "test_shard_db";
bool call_tha = false;
uint64_t total_db_container_msec = 0;
int total_tha_calls = 0;

class TestTimer
{
    public:
    TestTimer(const string& method) : m_active(true), m_method(method) {
        m_start = high_resolution_clock::now();
    }

    void resetTimer() {
                m_start = high_resolution_clock::now();
    }

    void summarize(const string& outMsg = "") {
        auto duration = duration_cast<milliseconds>(high_resolution_clock::now() - m_start);
        cout << "Method " << m_method << " took " << duration.count() << " milliseconds to execute.";
        if(m_method == "db_container") {
            total_db_container_msec += duration.count();
            cout << " Total: " << total_db_container_msec << "msecs. # of saved TXs: " << total_tha_calls;
        }
        cout << (outMsg.empty() ? "" : " Message: ") << outMsg << endl;
        m_active = false;
    }

    ~TestTimer() {
        if(!m_active) return;
        summarize();
    }

    private:
        bool m_active;
        string m_method;
        time_point<high_resolution_clock> m_start;
};


// container for database variables
struct db_container {
    leveldb::DB* db_ptr{};
    leveldb::Options opt;
    leveldb::WriteBatch batch;
    leveldb::WriteOptions write_opt;
    leveldb::ReadOptions read_opt;

    std::unique_ptr<leveldb::DB> m_db;

    cbdc::transaction::wallet wallet1;
    cbdc::transaction::wallet wallet2;

    cbdc::transaction::full_tx m_valid_tx{};
    cbdc::transaction::compact_tx m_cp_tx;
    std::vector<cbdc::transaction::full_tx> full_block;
    std::vector<cbdc::transaction::compact_tx> block;
    std::vector<cbdc::transaction::compact_tx> block_abridged;

    leveldb::Status res;

    // default constructor
    // this is intended to mimic what benchmark fixtures
    // do, while permitting benchmark modificaitons
    db_container() {
        opt.create_if_missing = true;
TestTimer tt("db_container"); /// printf("<<< db_container\n");    /// BBW
        auto mint_tx1 = wallet1.mint_new_coins(2, 100);
        auto mint_tx2 = wallet2.mint_new_coins(1, 100);
        shared_ptr<cbdc::logging::log> logger = std::make_shared <cbdc::logging::log>(cbdc::logging::log_level::trace);
        cbdc::sentinel_2pc::tx_history_archiver tha(1, logger, "tha_test");

        wallet1.confirm_transaction(mint_tx1);
        wallet2.confirm_transaction(mint_tx2);

        res = leveldb::DB::Open(opt, g_shard_test_dir, &db_ptr);
        m_db.reset(db_ptr);

        block.push_back(cbdc::transaction::compact_tx(mint_tx1));
        block.push_back(cbdc::transaction::compact_tx(mint_tx2));

        m_valid_tx
            = wallet1.send_to(100, wallet2.generate_key(), true).value();
        block.push_back(cbdc::transaction::compact_tx(m_valid_tx));
        block_abridged.push_back(cbdc::transaction::compact_tx(m_valid_tx));

        for(int i = 0; i < 10; i++) {
            m_valid_tx
                = wallet1.send_to(100, wallet2.generate_key(), true).value();
            wallet1.confirm_transaction(m_valid_tx);
            wallet2.confirm_transaction(m_valid_tx);
            full_block.push_back(m_valid_tx);
            block.push_back(cbdc::transaction::compact_tx(m_valid_tx));

if(call_tha && !tha.add_transaction(m_valid_tx)) cout << "Failure on attempt to add transaction"; 
else if(call_tha) { auto txId = tx_id(m_valid_tx); cout << "Add Tx: " << cbdc::to_string(txId) << endl; ++total_tha_calls;}
            m_valid_tx
                = wallet2.send_to(50, wallet1.generate_key(), true).value();
            wallet1.confirm_transaction(m_valid_tx);
            wallet2.confirm_transaction(m_valid_tx);
            block.push_back(cbdc::transaction::compact_tx(m_valid_tx));
if(call_tha && !tha.add_transaction(m_valid_tx)) cout << "Failure on attempt to add transaction"; 
else if(call_tha) { auto txId = tx_id(m_valid_tx); cout << "Add Tx: " << cbdc::to_string(txId) << endl; ++total_tha_calls;}

            m_valid_tx
                = wallet2.send_to(50, wallet1.generate_key(), true).value();
            wallet1.confirm_transaction(m_valid_tx);
            wallet2.confirm_transaction(m_valid_tx);
            block.push_back(cbdc::transaction::compact_tx(m_valid_tx));
if(call_tha && !tha.add_transaction(m_valid_tx)) cout << "Failure on attempt to add transaction"; 
else if(call_tha) { auto txId = tx_id(m_valid_tx); cout << "Add Tx: " << cbdc::to_string(txId) << endl; ++total_tha_calls;}
        }
    }

    void tear_down() {
return; ///// Don't remove        BBW
printf("<<< tear_down\n");
        std::filesystem::remove_all(g_shard_test_dir);
    }
};

// test placing a new element in DB
static void uhs_leveldb_put_new(benchmark::State& state) {
printf("<<< uhs_leveldb_put_new start\n"); // BBW
int times = 0; /// BBW
    auto db = db_container();

TestTimer tt("uhs_leveldb_put_new");   /// BBW
/// db.write_opt.sync = true; ///BBW
    // simulate storage of a new set of transactions
    for(auto _ : state) {
++times;////    for(int i = 0; i < times; ++i) {   /// BBW
        db.m_valid_tx
            = db.wallet2.send_to(50, db.wallet1.generate_key(), true).value();
        db.wallet1.confirm_transaction(db.m_valid_tx);
        db.wallet2.confirm_transaction(db.m_valid_tx);
        db.m_valid_tx
            = db.wallet1.send_to(50, db.wallet2.generate_key(), true).value();
        db.wallet1.confirm_transaction(db.m_valid_tx);
        db.wallet2.confirm_transaction(db.m_valid_tx);
        
        db.m_cp_tx = cbdc::transaction::compact_tx(db.m_valid_tx);
        std::array<char, sizeof(db.m_cp_tx.m_uhs_outputs)> out_arr{};
        std::memcpy(out_arr.data(),
                    db.m_cp_tx.m_uhs_outputs.data(),
                    db.m_cp_tx.m_uhs_outputs.size());
        leveldb::Slice OutPointKey(out_arr.data(),
                                   db.m_cp_tx.m_uhs_outputs.size());

        // actual storage
        state.ResumeTiming();
        db.res = db.m_db->Put(db.write_opt, OutPointKey, leveldb::Slice());
        state.PauseTiming();
    }

    db.tear_down();
printf("<<< uhs_leveldb_put_new end. Loop executed %d times\n", times);    // BBW
}

// test deleting from database
static void uhs_leveldb_item_delete(benchmark::State& state) {
/// printf("<<< uhs_leveldb_item_delete\n"); int times = 0;   // BBW
int times = 0;  /// BBW
    auto db = db_container();
TestTimer tt("uhs_leveldb_item_delete");
    db.m_cp_tx = cbdc::transaction::compact_tx(db.m_valid_tx);
    std::array<char, sizeof(db.m_cp_tx.m_uhs_outputs)> out_arr{};
    std::memcpy(out_arr.data(),
                db.m_cp_tx.m_uhs_outputs.data(),
                db.m_cp_tx.m_uhs_outputs.size());
    leveldb::Slice OutPointKey(out_arr.data(),
                               db.m_cp_tx.m_uhs_outputs.size());

    for(auto _ : state) {
++times; ///    for(int i = 0; i < times; ++i) {   /// BBW
        state.PauseTiming();
        db.m_db->Put(db.write_opt, OutPointKey, leveldb::Slice());
        state.ResumeTiming();
        db.m_db->Delete(db.write_opt, OutPointKey);
        state.PauseTiming();
    }

    db.tear_down();
printf("<<< uhs_leveldb_item_delete end. Loop executed %d times\n", times);    // BBW
}

// actual sim of shard storing block tx info
static void uhs_leveldb_shard_sim(benchmark::State& state) {
// printf("<<< uhs_leveldb_shard_sim\n"); int times = 0;   // BBW    
int times = 0; /// BBW
    auto db = db_container();
TestTimer tt("uhs_leveldb_shard_sim");   /// BBW
    for(auto _ : state) {
++times; ///    for(int i = 0; i < times; ++i) {   /// BBW
        state.PauseTiming();
        leveldb::WriteBatch batch;
        state.ResumeTiming();
        for(const auto& tx : db.block) {
            for(const auto& out : tx.m_uhs_outputs) {
                auto id = out; /// calculate_uhs_id(out); calculate_uhs_id is not defined - BBW
                std::array<char, sizeof(id)> out_arr{};
                std::memcpy(out_arr.data(), id.data(), id.size());
                leveldb::Slice OutPointKey(out_arr.data(), id.size());
                batch.Put(OutPointKey, leveldb::Slice());
            }
            for(const auto& inp : tx.m_inputs) {
                std::array<char, sizeof(inp)> inp_arr{};
                std::memcpy(inp_arr.data(), inp.data(), inp.size());
                leveldb::Slice OutPointKey(inp_arr.data(), inp.size());
                batch.Delete(OutPointKey);
            }
        }
        db.m_db->Write(db.write_opt, &batch);
        state.PauseTiming();
    }

    db.tear_down();
printf("<<< uhs_leveldb_shard_sim end. Loop executed %d times\n", times);    // BBW    
}

// abridged sim of shard storing block tx info (Batch storage of one TX)
static void uhs_leveldb_shard_sim_brief(benchmark::State& state) {
 /// printf("<<< uhs_leveldb_shard_sim_brief\n"); int times = 0;   // BBW   
 int times = 0;    /// BBW
    auto db = db_container();

db.write_opt.sync = true; ///BBW
TestTimer tt("uhs_leveldb_shard_sim_brief");   /// BBW
    for(auto _ : state) {
++times; ///    for(int i = 0; i < times; ++i) {   /// BBW
        state.PauseTiming();
        leveldb::WriteBatch batch;
        state.ResumeTiming();
        for(const auto& tx : db.block_abridged) {
            for(const auto& out : tx.m_uhs_outputs) {
                auto id = out; /// calculate_uhs_id(out); calculate_uhs_id is not defined - BBW
                std::array<char, sizeof(id)> out_arr{};
                std::memcpy(out_arr.data(), id.data(), id.size());
                leveldb::Slice OutPointKey(out_arr.data(), id.size());
                batch.Put(OutPointKey, leveldb::Slice());
            }
            for(const auto& inp : tx.m_inputs) {
                std::array<char, sizeof(inp)> inp_arr{};
                std::memcpy(inp_arr.data(), inp.data(), inp.size());
                leveldb::Slice OutPointKey(inp_arr.data(), inp.size());
                batch.Delete(OutPointKey);
            }
        }
        db.m_db->Write(db.write_opt, &batch);
        state.PauseTiming();
    }

    db.tear_down();
printf("<<< uhs_leveldb_shard_sim_brief end. Loop executed %d times\n", times);    // BBW    
}

std::string Get(db_container& db, const std::string& key) {
    std::string result;
    db.res = db.m_db->Get(db.read_opt, key, &result);
    if (db.res.IsNotFound()) {
      result = "NOT_FOUND";
    } else if (!db.res.ok()) {
      result = db.res.ToString();
    }
    return result;
}

// Test of DB Write/Read 
static void test_db_write_read() {

    auto db = db_container();
    const int attempts = 10;
    array<array<char, 100>, attempts> outKeys{};
    char extraKeyPortion[] = {"Extra: "};

    TestTimer tt("test_db_write_read");
    printf("\n\n======================= test_db_write_read ============================\n");

    for(int i = 0; i < attempts; ++i) {
        db.m_valid_tx
            = db.wallet2.send_to(50, db.wallet1.generate_key(), true).value();
        db.wallet1.confirm_transaction(db.m_valid_tx);
        db.wallet2.confirm_transaction(db.m_valid_tx);
        db.m_valid_tx
            = db.wallet1.send_to(50, db.wallet2.generate_key(), true).value();
        db.wallet1.confirm_transaction(db.m_valid_tx);
        db.wallet2.confirm_transaction(db.m_valid_tx);

        db.m_cp_tx = cbdc::transaction::compact_tx(db.m_valid_tx);
        const int outputSize = db.m_cp_tx.m_uhs_outputs.size();
        unsigned char *outputData =  (unsigned char *)db.m_cp_tx.m_uhs_outputs.data();
        memcpy(outKeys[i].data(), outputData, outputSize);
        leveldb::Slice keySlice (outKeys[i].data(), outputSize);
        char extraKey[100];
        sprintf(extraKey,"%s%d", extraKeyPortion, *((int *)outKeys[i].data()));
        leveldb::Slice extraKeySlice (extraKey, strlen(extraKey));

        printf("### write to db #%d, key size=%ld, value: <", i, keySlice.size());  
        for(size_t j = 0; j < min(keySlice.size(), (size_t)100); ++j) printf("%d ", keySlice.data()[j]);
        printf(">\n");

        db.res = db.m_db->Put(db.write_opt, keySlice, leveldb::Slice("abcdefghijklmnopqrstuvwxyz" + i, 8));
        db.res = db.m_db->Put(db.write_opt, extraKeySlice, leveldb::Slice("abcdefghijklmnopqrstuvwxyz" + i, 8));
    }

    for(int i = 0; i < attempts; ++i) {
        auto readResult = Get(db, string(outKeys[i].data(), 1));
        printf("\n\n### read from db #%d, data size: %ld, key size: %d, key first byte: %d, data: <", i, readResult.size(), 1, (outKeys[i].data() ? outKeys[i].data()[0] : -777));  
        for(size_t j = 0; j < min(readResult.size(), (size_t)100); ++j) printf("%c ", readResult.data()[j]);
        printf(">\n");
    }
/*******************************
    printf("\n========================== Extra keys ==========================\n");
    for(int i = 0; i < attempts; ++i) {
        char extraKey[100];
        sprintf(extraKey,"%s%d", extraKeyPortion, *((int *)outKeys[i].data()));

        auto readResult = Get(db, string(extraKey, strlen(extraKey)));
        printf("\n\n### read from db #%d, data size: %ld, key: %s, data: <", i, readResult.size(), extraKey);  
        for(size_t j = 0; j < min(readResult.size(), (size_t)100); ++j) printf("%c ", readResult.data()[j]);
        printf(">\n");
    }
*******************************/
    db.tear_down();
}

// Test of DB Write/Read 
static void test_tx_history_archive() {

    auto db = db_container();
    shared_ptr<cbdc::logging::log> logger = std::make_shared <cbdc::logging::log>(cbdc::logging::log_level::trace);
    cbdc::sentinel_2pc::tx_history_archiver tha(0, logger, "tha_test");

    TestTimer tt("test_tx_history_archive");
    printf("\n\n======================= test_tx_history_archive ============================\n");
    tha.init(1);

    for(auto tx: db.full_block) {
        if(!tha.add_transaction(tx)) cout << "Failure on attempt to ";
        auto status = (cbdc::sentinel_2pc::tx_state)(rand() % 7);
        cout << "Add transaction: " << cbdc::sentinel_2pc::tx_history_archiver::tx_to_str_pres(tx, status, 0) << endl;
        tha.set_status(tx_id(tx), status);
    }

    for(auto tx: db.full_block) {
        cbdc::hash_t txId = cbdc::transaction::tx_id(tx);
        cbdc::sentinel_2pc::tx_state last_status;
        cbdc::transaction::full_tx readTx;
        uint64_t timestamp;

        if(tha.get_transaction_by_hash(txId, last_status, readTx, timestamp)) {
            cout << "Read TX: ";
            cout << cbdc::sentinel_2pc::tx_history_archiver::tx_to_str_pres(tx, last_status, timestamp) << endl;
        }
    }

    db.tear_down();
}

BENCHMARK(uhs_leveldb_put_new);
BENCHMARK(uhs_leveldb_item_delete);
BENCHMARK(uhs_leveldb_shard_sim);
BENCHMARK(uhs_leveldb_shard_sim_brief);

// Augment the main() program to invoke benchmarks if specified
// via the --benchmark_filter command line flag.  E.g.,
//       my_unittest --benchmark_filter=all
//       my_unittest --benchmark_filter=BM_StringCreation
//       my_unittest --benchmark_filter=String
//       my_unittest --benchmark_filter='Copy|Creation'
auto main([[maybe_unused]]int argc, [[maybe_unused]]char** argv) -> int {
    string arg1 = (argc > 1) ? argv[1] : "";
    std::transform(arg1.begin(), arg1.end(), arg1.begin(),[](unsigned char c) { return std::toupper(c); });
    call_tha = (arg1 == "THA") ? true : false;
    cout << "Run db_container with" << (call_tha ? " " : "out ")<< "THA " << endl; 

  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  if(argc > 4) test_tx_history_archive();
  if(argc > 5) test_db_write_read();
  return 0;
}

/*
BENCHMARK(uhs_leveldb_put_new)->Threads(1);
BENCHMARK(uhs_leveldb_item_delete)->Threads(1);
BENCHMARK(uhs_leveldb_shard_sim)->Threads(1);
BENCHMARK(uhs_leveldb_shard_sim_brief)->Threads(1);
*/