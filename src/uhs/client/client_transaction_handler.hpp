// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLIENT_TRANSACTION_HANDLER_HPP
#define CLIENT_TRANSACTION_HANDLER_HPP

#include "uhs/sentinel/client.hpp"
#include "uhs/sentinel/format.hpp"
#include "uhs/transaction/messages.hpp"
#include "uhs/transaction/swap_wallet.hpp"
#include "util/common/logging.hpp"

#include <future>
#include <optional>

namespace cbdc {

    /// \brief Handles transaction processing and execution for wallets.
    ///
    /// Provides functionality to execute transactions, including specialized
    /// member functions for creating and receiving transactions in Toer Nolan
    /// Swaps.
    class client_transaction_handler {
      public:
        /// \brief Constructor.
        ///
        /// Initializes the handler with the provided sentinel client and
        /// logger.
        /// \param sentinel_client Reference to the sentinel RPC client.
        /// \param logger Shared pointer to the logging system.
        client_transaction_handler(
            cbdc::sentinel::rpc::client &sentinel_client,
            std::shared_ptr<cbdc::logging::log> logger);

        /// \brief Creates a transaction for Toer Nolan Swap.
        ///
        /// Generates a transaction for locking funds and submits it for
        /// execution. \param wallet The wallet used to create the transaction.
        /// \param amount The amount to lock in the transaction.
        /// \param payee The recipient's public key.
        /// \param shash The secret key hash for the transaction.
        /// \param expiry The expiration time for the transaction.
        /// \return The created transaction if successful, otherwise nullopt.
        std::optional<cbdc::transaction::full_tx>
        create_transaction(cbdc::transaction::swap_wallet& wallet,
                           uint32_t amount,
                           cbdc::pubkey_t payee,
                           cbdc::skey_hash_t shash,
                           uint64_t expiry);

        /// \brief Receives a transaction for Toer Nolan Swap.
        ///
        /// Generates a transaction for spending a locked transaction and
        /// submits it for execution.
        /// \param wallet The wallet used to receive the transaction.
        /// \param prev_txn The previous transaction to spend.
        /// \param skey The secret key for unlocking the transaction.
        /// \param expiry The expiration time for the transaction.
        /// \param sender_key The sender's public key.
        /// \param payee The recipient's public key.
        /// \return The created transaction if successful, otherwise nullopt.
        std::optional<cbdc::transaction::full_tx>
        receive_transaction(cbdc::transaction::swap_wallet& wallet,
                            const cbdc::transaction::input& prev_txn,
                            cbdc::skey_t skey,
                            uint64_t expiry,
                            cbdc::pubkey_t sender_key,
                            cbdc::pubkey_t payee);


      private:
        /// \brief Executes a transaction asynchronously.
        ///
        /// Sends the transaction to the sentinel client for execution and
        /// manages the response.
        /// \param wallet The wallet associated with the transaction.
        /// \param txn The transaction to execute.
        /// \return A future indicating success or failure of the transaction.
        std::future<bool> execute_txn(cbdc::transaction::swap_wallet& wallet,
                                      const cbdc::transaction::full_tx& txn);

        cbdc::sentinel::rpc::client
            &m_sentinel_client; ///< Sentinel RPC client reference.
        std::shared_ptr<cbdc::logging::log>
            m_logger; ///< Shared logger instance.
    };
} // namespace cbdc
#endif // CLIENT_TRANSACTION_HANDLER_HPP
