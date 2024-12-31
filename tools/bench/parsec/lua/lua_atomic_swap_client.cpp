// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "crypto/sha256.h"
#include "parsec/agent/client.hpp"
#include "parsec/broker/impl.hpp"
#include "parsec/directory/impl.hpp"
#include "parsec/runtime_locking_shard/client.hpp"
#include "parsec/ticket_machine/client.hpp"
#include "parsec/util.hpp"
#include "util/common/random_source.hpp"
#include "wallet.hpp"

#include <lua.hpp>
#include <random>
#include <thread>

// Function to generate hash of contract key
auto to_hash(std::string key) -> cbdc::hash_t {
    cbdc::hash_t ret;
    CSHA256 sha;
    auto data_vec = std::vector<unsigned char>(key.length());
    std::memcpy(data_vec.data(), key.data(), key.length());
    sha.Write(data_vec.data(), key.length());
    sha.Finalize(ret.data());

    return ret;
}

// Function to initialize shards
auto init_shards(const cbdc::parsec::config& cfg,
                 std::shared_ptr<cbdc::logging::log> log)
    -> std::vector<
        std::shared_ptr<cbdc::parsec::runtime_locking_shard::interface>> {
    log->info("Connecting to shards");
    auto shards = std::vector<
        std::shared_ptr<cbdc::parsec::runtime_locking_shard::interface>>();
    for(const auto& shard_ep : cfg.m_shard_endpoints) {
        auto client = std::make_shared<
            cbdc::parsec::runtime_locking_shard::rpc::client>(
            std::vector<cbdc::network::endpoint_t>{shard_ep});
        if(!client->init()) {
            log->fatal("Error connecting to shard");
            //throw std::runtime_error("Shard connection failed");
        }
        shards.emplace_back(client);
    }
    log->info("Connected to shards");
    return shards;
}

// Function to initialize ticket machine
auto init_ticket_machine(const cbdc::parsec::config& cfg,
                         std::shared_ptr<cbdc::logging::log> log)
    -> std::shared_ptr<cbdc::parsec::ticket_machine::rpc::client> {
    log->info("Connecting to ticket machine");
    auto ticketer
        = std::make_shared<cbdc::parsec::ticket_machine::rpc::client>(
            std::vector<cbdc::network::endpoint_t>{
                cfg.m_ticket_machine_endpoints});
    if(!ticketer->init()) {
        log->fatal("Error connecting to ticket machine");
    }
    log->info("Connected to ticket machine");
    return ticketer;
}

// Function to load Lua contract
auto load_lua_contract(const std::string& contract_file,
                       std::shared_ptr<cbdc::logging::log> log) -> lua_State* {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    if(luaL_dofile(L, contract_file.c_str()) != LUA_OK) {
        log->fatal("Failed to load contract file:", contract_file);
        lua_close(L);
    }
    return L;
}

// Function to insert contract
void insert_contract(std::shared_ptr<cbdc::parsec::broker::interface> broker,
                     const cbdc::buffer& key,
                     const cbdc::buffer& contract,
                     std::atomic<size_t>& init_count,
                     std::atomic_bool& init_error,
                     std::shared_ptr<cbdc::logging::log> log) {
    log->info("Inserting contract with key:", key.to_hex());
    auto ret = cbdc::parsec::put_row(broker, key, contract, [&](bool res) {
        if(!res) {
            init_error = true;
            log->fatal("Failed to insert contract:", key.to_hex());
        } else {
            log->info("Inserted contract:", key.to_hex());
            init_count++;
            log->info("Init Count:", init_count);
        }
    });
    if(!ret) {
        log->error("Failed to initiate contract insertion:", key.to_hex());
        init_error = true;
    }
}

// Function to wait for initialization
void wait_for_init(size_t expected_count,
                   const std::atomic<size_t>& init_count,
                   const std::atomic_bool& init_error,
                   std::shared_ptr<cbdc::logging::log> log) {
    constexpr uint64_t timeout = 300;
    constexpr auto wait_time = std::chrono::seconds(2);
    for(size_t count = 0;
        init_count < expected_count && !init_error && count < timeout;
        count++) {
        log->info("Waiting for initialization, current count:",
                  init_count.load(),
                  ", expected:",
                  expected_count);
        std::this_thread::sleep_for(wait_time);
    }
    if(init_count < expected_count || init_error) {
        log->fatal("Initialization failed");
    }
}

auto main(int argc, char** argv) -> int {
    auto log
        = std::make_shared<cbdc::logging::log>(cbdc::logging::log_level::info);
    auto sha2_impl = SHA256AutoDetect();
    log->info("Using SHA2 implementation:", sha2_impl);

    // Check command line arguments
    if(argc < 2) {
        log->error("Not enough arguments");
        return 1;
    }

    // Parse configuration
    auto cfg = cbdc::parsec::read_config(argc - 3, argv);
    if(!cfg.has_value()) {
        log->error("Error parsing options");
        return 1;
    }
    log->set_loglevel(cfg->m_loglevel);

    auto args = cbdc::config::get_args(argc, argv);
    auto n_wallets = std::stoull(args.back());
    if(n_wallets < 2) {
        log->error("Must be at least two threads");
        return 1;
    }
    log->info("Number of wallets:", n_wallets);

    // Initialize shards and ticket machine
    auto shards = init_shards(*cfg, log);
    auto ticketer = init_ticket_machine(*cfg, log);

    auto directory
        = std::make_shared<cbdc::parsec::directory::impl>(shards.size());
    auto broker = std::make_shared<cbdc::parsec::broker::impl>(
        std::numeric_limits<size_t>::max(),
        shards,
        ticketer,
        directory,
        log);

    // Load Lua contract
    auto contract_file = args[args.size() - 2];
    log->info("Loading contract file:", contract_file);
    auto L = load_lua_contract(contract_file, log);

    // Generate contract bytecode
    lua_getglobal(L, "gen_bytecode_aswap");
    if(lua_pcall(L, 0, 1, 0) != LUA_OK) {
        log->error("Contract bytecode generation failed:",
                   lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }

    auto pay_keys = std::vector<cbdc::buffer>();
    auto init_count = std::atomic<size_t>(0);
    auto init_error = std::atomic_bool{false};
    constexpr auto wait_time = std::chrono::seconds(2);

    // Insert creation contract
    auto creation_contract_key = cbdc::buffer();
    auto key_hash = to_hash("swap_creation");
    creation_contract_key.append(key_hash.data(), key_hash.size());
    {
        lua_pushinteger(L, 0);  // Index 0 of byte code table
        lua_gettable(L, -2);
        if(lua_isstring(L, -1)) {
            auto pay_contract
                = cbdc::buffer::from_hex(lua_tostring(L, -1)).value();
            // log->info("Creation contract bytecode:", pay_contract.to_hex());

            pay_keys.push_back(creation_contract_key);
            insert_contract(broker,
                            creation_contract_key,
                            pay_contract,
                            init_count,
                            init_error,
                            log);
        } else {
            log->error("Failed to retrieve creation bytecode");
        }
        lua_pop(L, 1);
    }

    wait_for_init(1, init_count, init_error, log);

    // Insert execution contract
    auto execution_contract_key = cbdc::buffer();
    key_hash = to_hash("swap_execution");
    execution_contract_key.append(key_hash.data(), key_hash.size());
    {
        lua_pushinteger(L, 1);  // Index 1 of byte code table
        lua_gettable(L, -2);
        if(lua_isstring(L, -1)) {
            auto pay_contract
                = cbdc::buffer::from_hex(lua_tostring(L, -1)).value();
            auto key = std::string("swap_execution");

            pay_keys.push_back(execution_contract_key);
            insert_contract(broker,
                            execution_contract_key,
                            pay_contract,
                            init_count,
                            init_error,
                            log);
        } else {
            log->error("Failed to retrieve execution bytecode");
        }
        lua_pop(L, 1);
    }

    // Insert get secret contract
    auto get_secret_contract_key = cbdc::buffer();
    key_hash = to_hash("get_secret_key");
    get_secret_contract_key.append(key_hash.data(), key_hash.size());
    {
        lua_pushinteger(L, 2);  // Index 2 of byte code table
        lua_gettable(L, -2);
        if(lua_isstring(L, -1)) {
            auto pay_contract
                = cbdc::buffer::from_hex(lua_tostring(L, -1)).value();
            pay_keys.push_back(get_secret_contract_key);
            insert_contract(broker,
                            get_secret_contract_key,
                            pay_contract,
                            init_count,
                            init_error,
                            log);
        } else {
            log->error("Failed to retrieve get secret bytecode");
        }
    }

    wait_for_init(3, init_count, init_error, log);

    lua_close(L);

    // Initialize agents and wallets
    log->info("Connecting to agents");
    auto agents
        = std::vector<std::shared_ptr<cbdc::parsec::agent::rpc::client>>();
    for(auto& a : cfg->m_agent_endpoints) {
        auto agent = std::make_shared<cbdc::parsec::agent::rpc::client>(
            std::vector<cbdc::network::endpoint_t>{a});
        if(!agent->init()) {
            log->error("Error connecting to agent");
            return 1;
        }
        agents.emplace_back(agent);
    }

    auto wallets = std::vector<cbdc::parsec::account_wallet>();
    for(size_t i = 0; i < n_wallets; i++) {
        auto agent_idx = i % agents.size();
        wallets.emplace_back(log, broker, agents[agent_idx], pay_keys[i]);
    }

    // Initialize wallet balances
    constexpr auto init_balance = 10000;
    init_count = 0;
    init_error = false;
    for(size_t i = 0; i < n_wallets; i++) {
        for(size_t j = 0; j < static_cast<size_t>(
                              cbdc::parsec::account_wallet::CBDC_TAG::count);
            j++) {
            auto res = wallets[i].init(
                static_cast<cbdc::parsec::account_wallet::CBDC_TAG>(j),
                init_balance,
                [&](bool retu) {
                    if(!retu) {
                        init_error = true;
                    } else {
                        init_count++;
                    }
                });
            if(!res) {
                init_error = true;
                break;
            }
        }
    }

    wait_for_init(n_wallets
                      * static_cast<size_t>(
                          cbdc::parsec::account_wallet::CBDC_TAG::count),
                  init_count,
                  init_error,
                  log);
    log->info("Added new accounts with balances:", init_balance);

    /* ################################# STEP 0: Initialization * ################################# */
    log->info("Initiating the Atomic Swap Protocol");
    log->info("Roles:");
    log->info("- Wallet 0 (Alice): Initiator with CBDC1");
    log->info("- Wallet 1 (Bob): Receiver with CBDC2");

    size_t from = 0, to = 1;
    auto [s_hash, sk] = wallets[from].generate_sk_and_returned_hash();
    log->info("Generated s_hash:", cbdc::to_string(s_hash), " and secret key:", cbdc::to_string(sk));

    // Setting Time Expiry ( 1 hour )
    auto duration
        = std::chrono::high_resolution_clock::now().time_since_epoch();
    auto time_exp
        = std::chrono::duration_cast<std::chrono::milliseconds>(duration)
              .count()
        + 1000 * 60 * 60 /* 1 hour */;
    auto in_flight = std::atomic<size_t>(0);
    uint64_t amount = 100; // assumes swap ratio as 1:1

    /* ################################# STEP 1: Creation of Contracts * ################################# */
    log->info("STEP 1: Creation of Contracts");

    /* --------- Initiator (Alice) Creates Swap Transaction --------- */
    log->info("Initiator (Alice, Wallet :", from, ") creating swap transaction in CBDC1");
    in_flight++;
    wallets[from].create_swap_transaction(
        creation_contract_key,
        cbdc::parsec::account_wallet::CBDC_TAG::cbdc1,
        wallets[to].get_pubkey(),
        amount,
        time_exp,
        s_hash,
        [&, from, to](bool retu) {
            if(!retu) {
                log->error("Creation contract error for initiator (Alice)");
            }
            log->info("Initiator (Alice, Wallet",
                       from,
                       ") created swap contract for Wallet:",
                       to, " in CBDC1");
            in_flight--;
        });

    while(in_flight > 0) {
        std::this_thread::sleep_for(wait_time);
    }

    /* --------- Receiver (Bob) Creates Swap Transaction --------- */
    log->info("Receiver (Bob, Wallet", to, ") creating swap transaction in CBDC2");
    in_flight++;
    wallets[to].create_swap_transaction(
        creation_contract_key,
        cbdc::parsec::account_wallet::CBDC_TAG::cbdc2,
        wallets[from].get_pubkey(),
        amount,
        time_exp,
        s_hash,
        [&, from, to](bool retu) {
            if(!retu) {
                log->error("Creation contract error for receiver (Bob)");
            }
            log->info("Receiver (Bob, Wallet",
                       to,
                       ") created swap contract for Wallet",
                       from, " in CBDC2");
            in_flight--;
        });

    while(in_flight > 0) {
        std::this_thread::sleep_for(wait_time);
    }

    for (auto i = 0; i < 10; i++) {
      std::cout<<".";
      std::this_thread::sleep_for(wait_time);
    }
    std::cout<<std::endl;
    /* ################################# STEP 2: Execution of Contracts * ################################# */
    log->info("STEP 2: Execution of Contracts");

    /* --------- Initiator (Alice) Executes Swap --------- */
    log->info("Initiator (Alice, Wallet", from, ") executing the swap in CBDC2");
    in_flight++;
    wallets[from].execute_swap_contract(
        execution_contract_key,
        cbdc::parsec::account_wallet::CBDC_TAG::cbdc2,
        sk,
        [&, from, to](bool retu) {
            if(!retu) {
                log->error("Execution contract error for initiator (Alice)");
            }
            log->info("Initiator (Alice, Wallet", from, ") executed swap, receiving",
                       amount,
                       "from Wallet",
                       to, "in CBDC2");
            in_flight--;
        });

    while(in_flight > 0) {
        std::this_thread::sleep_for(wait_time);
    }

    for (auto i = 0; i < 10; i++) {
      std::cout<<".";
      std::this_thread::sleep_for(wait_time);
    }
    std::cout<<std::endl;

    /* --------- Receiver (Bob) Completes the Swap --------- */
    log->info("Receiver (Bob, Wallet", to, ") completing the swap");
    in_flight++;
    log->info("Receiver (Bob, Wallet", to, ") retrieving secret key from CBDC2");
    wallets[to].get_secret_key(
        get_secret_contract_key,
        cbdc::parsec::account_wallet::CBDC_TAG::cbdc2,
        s_hash,
        [&](std::string _sk) {
            log->info("Secret key retrieval result for Wallet:", to);
            if(!_sk.empty()) {
                log->info("Secret key retrieved successfully for Wallet:", to, " from CBDC2");
                in_flight++;
                log->info("Receiver (Bob, Wallet",
                           to,
                           ") executing swap contract from CBDC1");
                wallets[to].execute_swap_contract(
                    execution_contract_key,
                    cbdc::parsec::account_wallet::CBDC_TAG::cbdc1,
                    cbdc::hash_from_hex(_sk),
                    [&, from, to](bool retu) {
                        if(!retu) {
                            log->error("Execution contract error for receiver "
                                       "(Bob, Wallet",
                                       to,
                                       ")");
                        }
                        log->info("Receiver (Bob, Wallet",
                                   to,
                                   ") executed swap, receiving",
                                   amount,
                                   "from Wallet in CBDC1",
                                   from);
                        in_flight--;
                    });
            } else {
                log->error("Failed to retrieve secret key for Wallet", to);
            }
            in_flight--;
        });

    // Wait for all transactions to be processed
    while(in_flight > 0) {
        std::this_thread::sleep_for(wait_time);
    }

    return 0;
}
