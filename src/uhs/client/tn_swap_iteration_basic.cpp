// main.cpp
// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "client_transaction_handler.hpp"
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
#include <memory>
#include <optional>
#include <thread>

static std::atomic_bool running{true};

/// \brief Handles CTRL+C signal to stop the program gracefully.
void signal_handler(int /*sig*/) {
    running = false;
}

/* Minting the Money */
static void mint_money(cbdc::transaction::swap_wallet& wallet,
                       size_t count,
                       uint32_t amount,
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

/// \brief Executes a Tier Nolan Swap transaction between two wallets.
///
/// Demonstrates the flow of creating, sending, and receiving transactions
/// between two wallets.
/// \param cfg Configuration options.
/// \param logger Shared pointer to the logging system.
/// \return 0 if successful, non-zero otherwise.
static int
run_tier_nolan_swap(cbdc::config::options& cfg,
                    std::shared_ptr<cbdc::logging::log> logger,
                    cbdc::sentinel::rpc::client& sentinel_client,
                    cbdc::coordinator::rpc::client& coordinator_client) {
    using wallet_t = cbdc::transaction::swap_wallet;

    // Initialize wallets for Alice and Bob
    wallet_t alice_wallet;
    wallet_t bob_wallet;

    // Initialize Transaction Handler
    cbdc::client_transaction_handler txn_handler(sentinel_client, logger);

    // Mint coins for Alice
    constexpr uint32_t mint_amount = 1000;
    constexpr size_t mint_count = 5;
    logger->info("Minting coins for Alice...");
    // auto mint_tx = alice_wallet.mint_new_coins(mint_count, mint_amount);
    // alice_wallet.confirm_transaction(mint_tx);
    mint_money(alice_wallet,
               mint_count,
               mint_amount,
               cfg,
               coordinator_client,
               logger);
    logger->info("Alice's initial balance: ", alice_wallet.balance());
    logger->info("Bob's initial balance: ", bob_wallet.balance());

    // Create a Tier Nolan Swap transaction from Alice to Bob
    const auto bob_pubkey = bob_wallet.generate_key();
    const auto [secret_key, secret_key_hash] = alice_wallet.generate_skey();
    logger->info("secret: ",
                 cbdc::to_string(secret_key),
                 "\n secret_key_hash: ",
                 cbdc::to_string(secret_key_hash));
    const uint64_t expiry_time
        = alice_wallet.set_expiry(10); // 10 minutes expiry

    logger->info("Creating Tier Nolan Swap transaction...");
    auto initial_txn = txn_handler.create_transaction(alice_wallet,
                                                      mint_amount,
                                                      bob_pubkey,
                                                      secret_key_hash,
                                                      expiry_time);

    if(!initial_txn) {
        logger->error("Failed to create initial Tier Nolan Swap transaction");
        return -1;
    }

    logger->info("Alice successfully created and sent the transaction.");

    static std::atomic_bool pause{false};

    // HACK to wait for CBDC2 completes
    while(running && !pause) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        char input;
        std::cin >> input; // wait for user input
        if(input == 'q') {
            pause = true;
        }
    }

    auto ins = cbdc::transaction::swap_wallet::export_raw_inputs(
        initial_txn.value());
    // Bob receives the transaction
    logger->info("Bob attempting to spend the transaction...");
    auto received_txn = txn_handler.receive_transaction(
        bob_wallet,
        ins[0],
        secret_key,
        expiry_time,
        alice_wallet.get_pubkey_swap_session(secret_key_hash).value(),
        bob_pubkey);

    if(!received_txn) {
        logger->error("Bob failed to receive the transaction");
        return -1;
    }

    logger->info("Bob successfully received and confirmed the transaction.");

    logger->info("Final Balances:");
    logger->info("Alice: ", alice_wallet.balance());
    logger->info("Bob: ", bob_wallet.balance());

    // Signal Handling
    std::signal(SIGINT, [](int /* sig */) {
        running = false;
    });

    while(running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}

// \brief Executes a Tier Nolan Swap transaction between two wallets.
///
/// Demonstrates the flow of creating, sending, and receiving transactions
/// between two wallets.
/// \param cfg Configuration options.
/// \param logger Shared pointer to the logging system.
/// \return 0 if successful, non-zero otherwise.
static int run_tier_nolan_swap_refund(
    cbdc::config::options& cfg,
    std::shared_ptr<cbdc::logging::log> logger,
    cbdc::sentinel::rpc::client& sentinel_client,
    cbdc::coordinator::rpc::client& coordinator_client) {
    using wallet_t = cbdc::transaction::swap_wallet;

    // Initialize wallets for Alice and Bob
    wallet_t alice_wallet;
    wallet_t bob_wallet;

    // Initialize Transaction Handler
    cbdc::client_transaction_handler txn_handler(sentinel_client, logger);

    // Mint coins for Alice
    constexpr uint32_t mint_amount = 1000;
    constexpr size_t mint_count = 5;
    logger->info("Minting coins for Alice...");
    // auto mint_tx = alice_wallet.mint_new_coins(mint_count, mint_amount);
    // alice_wallet.confirm_transaction(mint_tx);
    mint_money(alice_wallet,
               mint_count,
               mint_amount,
               cfg,
               coordinator_client,
               logger);
    logger->info("Alice's initial balance: ", alice_wallet.balance());
    logger->info("Bob's initial balance: ", bob_wallet.balance());

    // Create a Tier Nolan Swap transaction from Alice to Bob
    const auto bob_pubkey = bob_wallet.generate_key();
    const auto [secret_key, secret_key_hash] = alice_wallet.generate_skey();
    logger->info("secret: ",
                 cbdc::to_string(secret_key),
                 "\n secret_key_hash: ",
                 cbdc::to_string(secret_key_hash));
    const uint64_t expiry_time
        = alice_wallet.set_expiry(0); // 10 minutes expiry

    logger->info("Creating Tier Nolan Swap transaction...");
    auto initial_txn = txn_handler.create_transaction(alice_wallet,
                                                      mint_amount,
                                                      bob_pubkey,
                                                      secret_key_hash,
                                                      expiry_time);

    if(!initial_txn) {
        logger->error("Failed to create initial Tier Nolan Swap transaction");
        return -1;
    }

    logger->info("Alice successfully created and sent the transaction.");

    static std::atomic_bool pause{false};

    // HACK to wait for CBDC2 completes
    while(running && !pause) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        char input;
        std::cin >> input; // wait for user input
        if(input == 'q') {
            pause = true;
        }
    }

    auto ins = cbdc::transaction::swap_wallet::export_raw_inputs(
        initial_txn.value());
    // Bob receives the transaction
    logger->info("Alice attempting to refund the transaction...");
    auto received_txn = txn_handler.refund_transaction(
        alice_wallet,
        ins[0],
        expiry_time,
        alice_wallet.get_pubkey_swap_session(secret_key_hash).value(),
        bob_pubkey,
        secret_key_hash);

    if(!received_txn) {
        logger->error("Bob failed to receive the transaction");
        return -1;
    }

    logger->info("Alice successfully refunded and confirmed the transaction.");

    logger->info("Final Balances:");
    logger->info("Alice: ", alice_wallet.balance());
    logger->info("Bob: ", bob_wallet.balance());

    // Signal Handling
    std::signal(SIGINT, [](int /* sig */) {
        running = false;
    });

    while(running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}

int main(int argc, char** argv) {
    // Parse command-line arguments for configuration file
    if(argc < 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <config file> <0-receive|1-refund> " << std::endl;
        return -1;
    }

    auto cfg_or_err = cbdc::config::load_options(argv[1]);
    if(std::holds_alternative<std::string>(cfg_or_err)) {
        std::cerr << "Error loading config file: "
                  << std::get<std::string>(cfg_or_err) << std::endl;
        return -1;
    }
    auto cfg = std::get<cbdc::config::options>(cfg_or_err);

    // Initialize Logger
    auto logger = std::make_shared<cbdc::logging::log>(
        cbdc::logging::log_level::trace);

    // Register signal handler for graceful shutdown
    std::signal(SIGINT, signal_handler);

    // Coordinator Client
    auto coordinator_client
        = cbdc::coordinator::rpc::client(cfg.m_coordinator_endpoints[0]);
    if(!coordinator_client.init()) {
        logger->warn("Failed to connect to coordinator");
        return -1;
    }

    // Initialize Sentinel Client
    cbdc::sentinel::rpc::client sentinel_client(cfg.m_sentinel_endpoints,
                                                logger);
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

    // Run the Tier Nolan Swap transaction demo
    if(std::stoi(argv[2]) > 0) {
        return run_tier_nolan_swap_refund(cfg,
                                          logger,
                                          sentinel_client,
                                          coordinator_client);
    } else {
        return run_tier_nolan_swap(cfg,
                                   logger,
                                   sentinel_client,
                                   coordinator_client);
    }
}
