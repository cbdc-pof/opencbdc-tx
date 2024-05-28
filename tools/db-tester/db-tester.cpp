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
int errors_number = 0;
const int sentinel_id = 0;
cbdc::config::options opts;

class TestTimer
{
    public:
    TestTimer(const string& method) : m_active(true), m_method(method) {
        m_start = high_resolution_clock::now();
        errors_number = total_tha_calls = 0;
    }

    void resetTimer() {
                m_start = high_resolution_clock::now();
    }

    void summarize() {
        auto duration = duration_cast<milliseconds>(high_resolution_clock::now() - m_start);
        cout << "Method " << m_method << " took " << duration.count() << " milliseconds to execute.";
        total_db_container_msec += duration.count();
        cout << " Total: " << total_db_container_msec << "msecs. # of THA calls: " << total_tha_calls << " # of errors: " << errors_number << endl;
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
    void process_tx() {

        wallet1.confirm_transaction(m_valid_tx);
        wallet2.confirm_transaction(m_valid_tx);
        full_block.push_back(m_valid_tx);
        block.push_back(cbdc::transaction::compact_tx(m_valid_tx));
    }

    // default constructor
    // this is intended to mimic what benchmark fixtures
    // do, while permitting benchmark modificaitons
    db_container() {
        opt.create_if_missing = true;
        auto mint_tx1 = wallet1.mint_new_coins(2, 100);
        auto mint_tx2 = wallet2.mint_new_coins(1, 100);

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
            process_tx();

            m_valid_tx
                = wallet2.send_to(50, wallet1.generate_key(), true).value();
            process_tx();

            m_valid_tx
                = wallet2.send_to(50, wallet1.generate_key(), true).value();
            process_tx();
        }
   }

    void tear_down() {
        std::filesystem::remove_all(g_shard_test_dir);
    }
};

// Test THA 
static void test_tx_history_archive(benchmark::State& state) {
    opts.m_sentinel_loglevels.push_back(visualize ? cbdc::logging::log_level::trace : cbdc::logging::log_level::warn);
    auto db = db_container();
    opts.m_sentinel_loglevels[sentinel_id] = cbdc::logging::log_level::warn;
///    opts.tha_type = string("leveldb");
    opts.m_tha_type = string("Keyspaces");
///    opts.tha_parameter = std::string("./tha_db");
    opts.m_tha_parameter = "localhost";
    opts.m_tha_port = 9042;
    opts.m_tha_user = "cassandra";
    opts.m_tha_password = "cassandra";
    opts.m_tha_ssl_version = "none";
    
    cbdc::sentinel_2pc::tx_history_archiver tha(sentinel_id, opts);
    cbdc::sentinel_2pc::tx_state statuses[100000];

    TestTimer tt("test_tx_history_archive");
    
    for(auto _: state ) {
        int i = 0;
        for(auto tx: db.full_block) {
            auto status = (cbdc::sentinel_2pc::tx_state)(rand() % 7);

            if(!tha.add_transaction(tx)) {
                cout << "Failure on attempt to add transaction: " << cbdc::sentinel_2pc::tx_history_archiver::tx_to_str_pres(tx, status, 0) << endl;
                ++errors_number;
            }
            else if(visualize) cout << "Add transaction #" << i << ": " << cbdc::sentinel_2pc::tx_history_archiver::tx_to_str_pres(tx, status, 0) << endl;

            cbdc::hash_t  txId = tx_id(tx);
            if(!tha.set_status(txId, status)) {
                cout << "Failure on attempt to set status: " << (int)status << " to TX " << cbdc::to_string(txId) << endl;
                ++errors_number;
            }
            statuses[i++] = status;
            total_tha_calls += 2;
        }

        i = 0;
        for(auto tx: db.full_block) {
            cbdc::hash_t txId = cbdc::transaction::tx_id(tx);
            cbdc::sentinel_2pc::tx_state last_status;
            cbdc::transaction::full_tx readTx;
            uint64_t timestamp;

            // Read newly added records and their statuses
            if(tha.get_transaction_by_hash(txId, last_status, readTx, timestamp)) {
                if(visualize) cout << "Successfully read TX #" << i << ": " << cbdc::sentinel_2pc::tx_history_archiver::tx_to_str_pres(tx, last_status, timestamp) << endl;
            }
            else {
                cout << "Cannot read TX: "<< endl;
                ++errors_number;
            }

            // Statuses should match
            if(last_status != statuses[i]) {
                cout << "Wrong status (" << (int)last_status << ") while expected " << (int)statuses[i] << " for TX# " << i << " TxId=" << tx_history_archiver::mem_to_hex_str(txId.data()) << endl;                
                ++errors_number;
            }

            // Delete the record and its' statuses
            int deletedRec = tha.delete_transaction_by_hash(txId);
            if(deletedRec > 0) {
                if(visualize) cout << "Successfully deleted " << deletedRec << " records" << endl;
            }
            else {
                cout << "Cannot delete TX "<< endl;
                ++errors_number;
            }

            total_tha_calls += 2;

            // Negative scenarios:. 
            // Try to read deleted record. Should fail.
            if(tha.get_transaction_by_hash(txId, last_status, readTx, timestamp) == 0) {
                if(visualize) cout << "As expected: cannot read deleted TX" << endl;
            }
            else {
                cout << "Error: can read deleted TX"<< endl;
                ++errors_number;
            }

            // Delete an absent record and its' statuses. Should fail (BUT NOT FOR KEYSPACES/KASSADRA)
            if(opts.m_tha_type != string("Keyspaces")) {
                deletedRec = tha.delete_transaction_by_hash(txId);
                if(deletedRec == 0) {
                    if(visualize) cout << "As expected: cannot delete an absent record " << endl;
                }
                else {
                    cout << "Error: can delete an absent TX " << endl;
                    ++errors_number;
                }
            }
            ++i;
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
//    benchmark::Shutdown();

    return 0;
}
