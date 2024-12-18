// client_transaction_handler.cpp
// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "client_transaction_handler.hpp"

#include <utility>
namespace cbdc {
    client_transaction_handler::client_transaction_handler(
        cbdc::sentinel::rpc::client  &sentinel_client,
        std::shared_ptr<cbdc::logging::log> logger)
        : m_sentinel_client(sentinel_client),
          m_logger(std::move(logger)) {}

    std::future<bool> client_transaction_handler::execute_txn(
        cbdc::transaction::swap_wallet& wallet,
        const cbdc::transaction::full_tx& txn) {
        auto promise = std::make_shared<std::promise<bool>>();
        auto future = promise->get_future();

        auto res_cb
            = [promise, &wallet, txn, this](
                  cbdc::sentinel::rpc::client::execute_result_type res) {
                  auto tx_id = cbdc::transaction::tx_id(txn);
                  if(!res.has_value()) {
                      m_logger->warn("Failure response from sentinel for ",
                                     cbdc::to_string(tx_id));
                      wallet.confirm_inputs(txn.m_inputs);
                      promise->set_value(false);
                      return;
                  }

                  const auto& sent_resp = res.value();
                  if(sent_resp.m_tx_status
                     == cbdc::sentinel::tx_status::confirmed) {
                      wallet.confirm_transaction(txn);
                      m_logger->info("Transaction ",
                                     cbdc::to_string(tx_id),
                                     " confirmed successfully.");
                      promise->set_value(true);
                  } else {
                      m_logger->warn(cbdc::to_string(tx_id),
                                     " had error in execution");
                      wallet.confirm_inputs(txn.m_inputs);
                      promise->set_value(false);
                  }
              };

        if(!m_sentinel_client.execute_transaction(txn, std::move(res_cb))) {
            m_logger->error("Failure sending transaction to sentinel for ",
                            cbdc::to_string(cbdc::transaction::tx_id(txn)));
            wallet.confirm_inputs(txn.m_inputs);
            promise->set_value(false);
        }

        return future;
    }

    std::optional<cbdc::transaction::full_tx>
    client_transaction_handler::create_transaction(
        cbdc::transaction::swap_wallet& wallet,
        uint32_t amount,
        cbdc::pubkey_t payee,
        cbdc::skey_hash_t shash,
        uint64_t expiry) {
        auto txn = wallet.create_txn_tnswap(amount, payee, shash, expiry);
        if(txn) {
            auto result = execute_txn(wallet, txn.value()).get();
            if(result) {
                return txn;
            }
        }
        return std::nullopt;
    }

    std::optional<cbdc::transaction::full_tx>
    client_transaction_handler::receive_transaction(
        cbdc::transaction::swap_wallet& wallet,
        const cbdc::transaction::input& prev_txn,
        cbdc::skey_t skey,
        uint64_t expiry,
        cbdc::pubkey_t sender_key,
        cbdc::pubkey_t payee) {
        auto txn = wallet.create_txn_tnswap_receive({prev_txn},
                                                    expiry,
                                                    sender_key,
                                                    payee,
                                                    skey);
        if(txn) {
            auto result = execute_txn(wallet, txn.value()).get();
            if(result) {
                return txn;
            }
        }
        return std::nullopt;
    }
} // namespace cbdc
