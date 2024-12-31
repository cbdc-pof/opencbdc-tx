// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "uhs/sentinel/client.hpp"
#include "uhs/sentinel/format.hpp"
#include "uhs/transaction/messages.hpp"
#include "uhs/transaction/swap_wallet.hpp"
#include "uhs/transaction/wallet.hpp"
#include "uhs/twophase/coordinator/client.hpp"
#include "uhs/twophase/locking_shard/status_client.hpp"
#include "util/common/config.hpp"
#include "util/common/logging.hpp"
#include "util/network/connection_manager.hpp"
#include "util/serialization/format.hpp"

#include <csignal>
#include <iostream>

/* handles Transactions */
struct transaction_handler {

    transaction_handler(cbdc::sentinel::rpc::client& sentinel_client,
                        std::shared_ptr<cbdc::logging::log> logger)
        : m_sentinel_client(sentinel_client),
          m_logger(std::move(logger)) {}

    std::future<bool> execute_txn(cbdc::transaction::swap_wallet& wallet,
                                  const cbdc::transaction::full_tx& txn) {
        // Create a promise to manage the asynchronous result
        auto promise = std::make_shared<std::promise<bool>>();
        auto future = promise->get_future();

        // Define the callback
        auto res_cb
            = [promise, txn = txn, &wallet, this](
                  cbdc::sentinel::rpc::client::execute_result_type res) {
                  auto tx_id = cbdc::transaction::tx_id(txn);
                  if (!res.has_value()) {
                      m_logger->warn("Failure response from sentinel for ",
                                     cbdc::to_string(tx_id));
                      wallet.confirm_inputs(txn.m_inputs);
                      promise->set_value(false); // Notify failure
                      return;
                  }

                  auto& sent_resp = res.value();
                  if (sent_resp.m_tx_status == cbdc::sentinel::tx_status::confirmed) {
                      wallet.confirm_transaction(txn);
                      m_logger->info("Transaction ",
                                     cbdc::to_string(tx_id),
                                     " confirmed successfully.");
                      promise->set_value(true); // Notify success
                  } else {
                      m_logger->warn(cbdc::to_string(tx_id),
                                     " had error in execution");
                      wallet.confirm_inputs(txn.m_inputs);
                      promise->set_value(false); // Notify failure
                  }
              };

        // Execute the transaction and pass the callback
        if (!m_sentinel_client.execute_transaction(txn, std::move(res_cb))) {
            m_logger->error("Failure sending transaction to sentinel for ",
                            cbdc::to_string(cbdc::transaction::tx_id(txn)));
            wallet.confirm_inputs(txn.m_inputs);
            promise->set_value(false); // Notify failure
        }

        return future; // Return the future to the caller
    }

  private:
    cbdc::sentinel::rpc::client& m_sentinel_client;
    std::shared_ptr<cbdc::logging::log> m_logger;
};


/* Minting the Money */
static void mint_money(cbdc::transaction::swap_wallet& wallet,
                       int count,
                       int amount,
                       cbdc::config::options& cfg,
                       cbdc::coordinator::rpc::client& coordinator_client,
                       std::shared_ptr<cbdc::logging::log> logger) {
    auto mint_tx = wallet.mint_new_coins(count, amount);
    auto compact_mint_tx = cbdc::transaction::compact_tx(mint_tx);
    auto secp = std::unique_ptr<secp256k1_context,
                                decltype(&secp256k1_context_destroy)>{
        secp256k1_context_create(SECP256K1_CONTEXT_SIGN),
        &secp256k1_context_destroy};

    for(size_t i = 0; i < cfg.m_attestation_threshold; i++) {
        auto att
            = compact_mint_tx.sign(secp.get(), cfg.m_sentinel_private_keys[i]);
        compact_mint_tx.m_attestations.insert(att);
    }

    auto mint_successful = std::promise<bool>();
    auto mint_successful_fut = mint_successful.get_future();
    auto send_successful = coordinator_client.execute_transaction(
        compact_mint_tx,
        [&](std::optional<bool> resp) {
            if(!resp.has_value()) {
                mint_successful.set_value(false);
                return;
            }
            mint_successful.set_value(resp.value());
        });

    if(!send_successful) {
        logger->error("Failed to send mint TX to coordinator");
        return;
    }

    logger->info("Waiting for mint confirmation");
    auto mint_status = mint_successful_fut.wait_for(std::chrono::seconds(10));
    if(mint_status != std::future_status::ready
       || !mint_successful_fut.get()) {
        logger->error("Mint TX failed or timed out.");
        return;
    }

    wallet.confirm_transaction(mint_tx);
    logger->info("Mint confirmed");
}


/* Main function */
int main(int argc, char** argv) {
    auto args = cbdc::config::get_args(argc, argv);
    if(args.size() < 3) {
        std::cerr << "Usage: " << args[0] << " <config file> <gen ID>"
                  << std::endl;
        return -1;
    }

    auto cfg_or_err = cbdc::config::load_options(args[1]);
    if(std::holds_alternative<std::string>(cfg_or_err)) {
        std::cerr << "Error loading config file: "
                  << std::get<std::string>(cfg_or_err) << std::endl;
        return -1;
    }
    auto cfg = std::get<cbdc::config::options>(cfg_or_err);
    auto logger = std::make_shared<cbdc::logging::log>(
        cbdc::logging::log_level::trace);

    using alice_wallet_list = std::array<cbdc::transaction::swap_wallet, 2>;
    using bob_wallet_list = std::array<cbdc::transaction::swap_wallet, 2>;
    constexpr int cbdc1 = 0;
    constexpr int cbdc2 = 1;


    // Wallet Initialization
    alice_wallet_list alice_wallet{};
    bob_wallet_list bob_wallet{};
    
    // Coordinator Client
    auto coordinator_client
        = cbdc::coordinator::rpc::client(cfg.m_coordinator_endpoints[0]);
    if(!coordinator_client.init()) {
        logger->warn("Failed to connect to coordinator");
        return -1;
    }

    // Mint Money for Alice Wallet
    mint_money(alice_wallet[cbdc1], 5, 1000, cfg, coordinator_client, logger);
    mint_money(bob_wallet[cbdc2], 5, 1000, cfg, coordinator_client, logger);
    

    // Sentinel Client
    auto sentinel_client
        = cbdc::sentinel::rpc::client(cfg.m_sentinel_endpoints, logger);
    if(!sentinel_client.init()) {
        logger->warn("Failed to connect to sentinel");
        return -1;
    }

    // Shard Status Client Initialization (for additional verification
    // purposes)
    static constexpr auto lookup_timeout = std::chrono::milliseconds(5000);
    auto status_client = cbdc::locking_shard::rpc::status_client(
        cfg.m_locking_shard_readonly_endpoints,
        cfg.m_shard_ranges,
        lookup_timeout);
    if(!status_client.init()) {
        logger->warn("Failed to connect to shard read-only endpoints");
        return -1;
    }

    // Transaction Handler Initialization
    transaction_handler txn_handler(sentinel_client, logger);

 
    auto creator
        = [&](cbdc::transaction::swap_wallet& wallet,
              uint32_t amount,
              cbdc::pubkey_t payee,
              cbdc::skey_hash_t shash,
              uint64_t expiry) -> std::optional<cbdc::transaction::full_tx> {
        auto txn = wallet.create_txn_tnswap(amount, payee, shash, expiry);
        if(txn) {
            auto result = txn_handler.execute_txn(wallet, txn.value()).get();
            if(result) {
                return txn;
            }
        }
        return std::nullopt;
    };

    auto receiver
        = [&](cbdc::transaction::swap_wallet& wallet,
              const cbdc::transaction::full_tx& prev_txn,
              cbdc::skey_t skey,
              uint64_t expiry, cbdc::pubkey_t sender_key,
              cbdc::pubkey_t payee) -> std::optional<cbdc::transaction::full_tx> {
        auto ins = cbdc::transaction::swap_wallet::export_raw_inputs(prev_txn);
        auto txn = wallet.create_txn_tnswap_receive(
            ins[0],
            expiry,
            sender_key,
            payee,
            skey);
        ;
        if(txn) {
            auto result = txn_handler.execute_txn(wallet, txn.value()).get();
            if(result) {
                return txn;
            }
        }
        return std::nullopt;
    };

    logger->trace("Starting a Atomic Swap txn...");
    
    logger->trace("Alice Balance CBDC1: ", alice_wallet[cbdc1].balance(), ", CBDC2: ", alice_wallet[cbdc2].balance());
    logger->trace("Bob Balance CBDC1: ", bob_wallet[cbdc1].balance(), ", CBDC2: ", bob_wallet[cbdc2].balance());
    

    const auto amount = 1000;
    logger->trace("STEP1 -- Initializing data, Amount for Swap: ", amount);

    const auto bob_pubk = bob_wallet[cbdc1].generate_key();
    const auto alice_pubk = alice_wallet[cbdc2].generate_key();

    const auto [sk, sk_hash] = alice_wallet[cbdc1].generate_skey();
    logger->trace("Alice generated secret key (skey {", cbdc::to_string(sk) ,"}) and\n its hash (", cbdc::to_string(sk_hash) ,").");

    // Set the expiry time for the transaction
    const uint64_t expiry_time = alice_wallet[cbdc1].set_expiry(10);

    logger->trace("**************CBDC1: Alice [Initiator] sends to Bob [Joiner]");
    // Create the initial transaction for Alice to send to Bob
    
    auto intiator_txn = creator(alice_wallet[cbdc1], amount, bob_pubk, sk_hash, expiry_time);
    auto intiator_sender_key = alice_wallet[cbdc1].get_pubkey_swap_session(sk_hash);

    if (!intiator_txn.has_value()) {
        logger->error("Inititor txn Failed");
        return -1;
    } else {
        logger->trace("CBDC1: Alice txn successful");
    }

    logger->trace("++++++++++++++CBDC2: Bob [Joiner] sends to Alice [Initiator]");
    auto joiner_txn = creator(bob_wallet[cbdc2], amount, alice_pubk, sk_hash, expiry_time);
    auto joiner_sender_key = bob_wallet[cbdc2].get_pubkey_swap_session(sk_hash);

    if (!joiner_txn.has_value()) {
        logger->error("Joiner Inititor txn failed");
        return -1;
    } else {
        logger->trace("+++++++++++++CBDC2: Bob[Joiner] txn successfull");
    }

    logger->trace("+++++++++++++CBDC2: Alice[Initiator] spending txn");
    auto intiator_spend_txn = receiver(alice_wallet[cbdc2], joiner_txn.value(), sk, expiry_time, joiner_sender_key.value(), alice_pubk);
    if (!intiator_spend_txn.has_value()) {
        logger->error("Spending Inititor txn failed");
        return -1;
    } else {
        logger->trace("+++++++++++++CBDC2: Alice[Initiator] spent txn successfully");
    }

    logger->trace("**************CBDC1: Bob [Joiner] spending txn");
    auto joiner_spend_txn = receiver(bob_wallet[cbdc1], intiator_txn.value(), sk, expiry_time, intiator_sender_key.value(), bob_pubk);
    if (!joiner_spend_txn.has_value()) {
        logger->error("Spending Joiner txn failed");
        return -1;
    } else {
        logger->trace("**************CBDC1: Bob [Joiner] spent txn successfully");
    }

    logger->trace("Alice Balance CBDC1: ", alice_wallet[cbdc1].balance(), ", CBDC2: ", alice_wallet[cbdc2].balance());
    logger->trace("Bob Balance CBDC1: ", bob_wallet[cbdc1].balance(), ", CBDC2: ", bob_wallet[cbdc2].balance());

    // Signal Handling
    static std::atomic_bool running{true};
    std::signal(SIGINT, [](int /* sig */) {
        running = false;
    });

    while(running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}

