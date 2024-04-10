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
#include "uhs/twophase/sentinel_2pc/tx_history_archive/tx_history.hpp"

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
bool visualize = false;
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
    std::vector<int> statuses;

    leveldb::Status res;

    // Add transaction to THA 
    void process_tx(cbdc::sentinel_2pc::tx_history_archiver& tha) {

        wallet1.confirm_transaction(m_valid_tx);
        wallet2.confirm_transaction(m_valid_tx);
        full_block.push_back(m_valid_tx);
        block.push_back(cbdc::transaction::compact_tx(m_valid_tx));

        if(!tha.add_transaction(m_valid_tx)) cout << "Failure on attempt to add transaction"; 
        else { 
            ++total_tha_calls;
        }
    }

    // default constructor
    // this is intended to mimic what benchmark fixtures
    // do, while permitting benchmark modificaitons
    db_container() {
        opt.create_if_missing = true;
        TestTimer tt("db_container"); 
        auto mint_tx1 = wallet1.mint_new_coins(2, 100);
        auto mint_tx2 = wallet2.mint_new_coins(1, 100);
        shared_ptr<cbdc::logging::log> logger = std::make_shared <cbdc::logging::log>(cbdc::logging::log_level::warn);
        cbdc::config::options opts;
        opts.tha_type = std::string("leveldb");
        opts.tha_parameter = std::string("./tha_db");

        cbdc::sentinel_2pc::tx_history_archiver tha(1, opts);

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
            process_tx(tha);

            m_valid_tx
                = wallet2.send_to(50, wallet1.generate_key(), true).value();
            process_tx(tha);

            m_valid_tx
                = wallet2.send_to(50, wallet1.generate_key(), true).value();
            process_tx(tha);
        }
    }

    void tear_down() {
        std::filesystem::remove_all(g_shard_test_dir);
    }
};

// Test THA 
static void test_tx_history_archive(benchmark::State& state) {

    auto db = db_container();
    shared_ptr<cbdc::logging::log> logger = std::make_shared <cbdc::logging::log>(cbdc::logging::log_level::warn);
    cbdc::config::options opts;
    opts.tha_type = string("leveldb");
    opts.tha_parameter = std::string("./tha_db");
    cbdc::sentinel_2pc::tx_history_archiver tha(1, opts);

    TestTimer tt("test_tx_history_archive");
    
    for(auto _: state ) {
        for(auto tx: db.full_block) {
            if(!tha.add_transaction(tx)) cout << "Failure on attempt to ";
            auto status = (cbdc::sentinel_2pc::tx_state)(rand() % 7);
            if(visualize) cout << "Add transaction: " << cbdc::sentinel_2pc::tx_history_archiver::tx_to_str_pres(tx, status, 0) << endl;
            cbdc::hash_t  txId = tx_id(tx);
            if(!tha.set_status(txId, status)) cout << "Failure on attempt to ";
            cout << "Set status: " << (int)status << " to TX " << cbdc::to_string(txId) << endl;
        }

        for(auto tx: db.full_block) {
            cbdc::hash_t txId = cbdc::transaction::tx_id(tx);
            cbdc::sentinel_2pc::tx_state last_status;
            cbdc::transaction::full_tx readTx;
            uint64_t timestamp;

            if(tha.get_transaction_by_hash(txId, last_status, readTx, timestamp)) {
                cout << "Successfully read TX: " << cbdc::to_string(txId) << " with status " << (int)last_status << endl;
                if(visualize) cout << cbdc::sentinel_2pc::tx_history_archiver::tx_to_str_pres(tx, last_status, timestamp) << endl;
            }
        }
    }
    db.tear_down();
}

BENCHMARK(test_tx_history_archive);

auto main([[maybe_unused]]int argc, [[maybe_unused]]char** argv) -> int {
    string arg1 = (argc > 1) ? argv[1] : "";
    std::transform(arg1.begin(), arg1.end(), arg1.begin(),[](unsigned char c) { return std::tolower(c); });
    visualize = (arg1 == "v") ? true : false;

    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();

    return 0;
}
