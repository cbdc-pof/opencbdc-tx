// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENCBDC_TX_SRC_TRANSACTION_SWAP_WALLET_H_
#define OPENCBDC_TX_SRC_TRANSACTION_SWAP_WALLET_H_

#include "uhs/transaction/transaction.hpp"
#include "uhs/transaction/validation.hpp"
#include "uhs/transaction/wallet.hpp"
#include "util/common/config.hpp"
#include "util/common/hashmap.hpp"
#include "util/common/random_source.hpp"

#include <atomic>
#include <fstream>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <secp256k1.h>
#include <set>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace cbdc::transaction {
    /// \brief Specialized cryptographic wallet for Toer Nolan Swap (tnswap) transactions.
    ///
    /// Provides functionality for creating, signing, and managing transactions
    /// specific to Tier Nolan Swap operations. Extends the base wallet
    /// functionality by adding support for tnswap-specific transaction flows.
    class swap_wallet : public wallet {
      public:
        swap_wallet() {}

        /// \brief Creates an initial transaction for Toer Nolan Swap (tnswap).
        ///
        /// Generates a transaction that initiates a tnswap by locking a
        /// specified amount with a hash-based lock for the specified payee.
        /// \param amount The amount to lock in the transaction.
        /// \param payee The public key of the recipient.
        /// \param s_hash The secret key hash to use for locking.
        /// \param time_expiry The expiration time for the transaction lock.
        /// \return An optional transaction. Contains a transaction if successful.
        auto create_txn_tnswap(const uint32_t amount,
                               const pubkey_t& payee,
                               const skey_hash_t s_hash,
                               uint64_t time_expiry) -> std::optional<full_tx>;

        /// \brief Creates a receive transaction for Toer Nolan Swap (tnswap).
        ///
        /// Generates a transaction that spends a previously locked tnswap
        /// input by unlocking it with the sender's key and transferring the
        /// output to the receiver's key.
        /// \param input_tx The input transaction to be spent.
        /// \param expiry_time The expiration time for the transaction.
        /// \param sender_payee The sender's public key.
        /// \param receiver_key The receiver's public key.
        /// \param sk The secret key to unlock the input.
        /// \return An optional transaction. Contains a transaction if successful.
        auto
        create_txn_tnswap_receive(const transaction::input& input_tx,
                                  const uint64_t expiry_time,
                                  const pubkey_t& sender_payee,
                                  const pubkey_t& receiver_key,
                                  const skey_t& sk) -> std::optional<full_tx>;

        /// \brief Creates a refund transaction for Toer Nolan Swap (tnswap).
        ///
        /// Generates a refund transaction to recover funds from an expired
        /// tnswap lock using the original sender's key-hash.
        /// \param input_tx The input transaction to be refunded.
        /// \param expiry_time The expiration time for the transaction.
        /// \param sender_payee The sender's public key.
        /// \param receiver_key The receiver's public key.
        /// \param sk_hash The secret key hash used in the original lock.
        /// \return An optional transaction. Contains a transaction if successful.
        auto create_txn_tnswap_refund(const transaction::input& input_tx,
                                      const uint64_t expiry_time,
                                      const pubkey_t& sender_payee,
                                      const pubkey_t& receiver_key,
                                      const skey_hash_t& sk_hash)
            -> std::optional<full_tx>;

        /// \brief Generates a new secret key and its corresponding hash.
        ///
        /// Produces a new key pair that can be used to receive payments or
        /// perform tnswap operations.
        /// \return A pair containing the secret key and its hash.
        auto generate_skey() -> std::pair<skey_t, skey_hash_t>;

        /// \brief Sets the default expiration time for tnswap transactions.
        ///
        /// Configures the timeout duration for new transactions.
        /// \param minutes The expiration time in minutes.
        /// \return The computed expiration time as a Unix timestamp.
        auto set_expiry(uint32_t minutes) -> uint64_t;

        /// \brief Retrieves the public key associated with a specific tnswap session.
        ///
        /// Finds the public key linked to the provided secret key hash.
        /// \param s_hash The secret key hash to search for.
        /// \return The public key, if found.
        auto
        get_pubkey_swap_session(skey_hash_t s_hash) -> std::optional<pubkey_t>;

        /// \brief Retrieves the secret key associated with a specific hash.
        ///
        /// Searches for and returns the secret key linked to the provided
        /// hash. \param hash The hash of the secret key to search for.
        /// \return The secret key, if found.
        auto getskey(skey_hash_t hash) -> std::optional<skey_t>;

        /// \brief Retrieves the current session's secret key hash.
        /// \return The hash of the current secret key.
        auto current_shash() -> skey_hash_t {
            return m_current_skey_hash;
        };

        /// \brief Confirms a transaction by importing its outputs as inputs.
        ///
        /// Adds the outputs of the given transaction to the wallet's list of
        /// spendable inputs.
        /// \param tx The transaction whose outputs are to be imported.
        /// \return A vector of inputs created from the transaction's outputs.
        static auto export_raw_inputs(const transaction::full_tx& tx)
            -> std::vector<transaction::input>;

      private:
        /// \brief Signs a tnswap transaction's inputs using Schnorr signatures.
        /// \param tx The transaction whose inputs are to be signed.
        void sign_tnswap(full_tx& tx) const;

        /// \brief Signs a tnswap transaction with a specific public key.
        /// \param tx The transaction to sign.
        /// \param pubkey The public key used for signing.
        /// \param witness_prog_len The length of the witness program.
        void sign_tnswap(transaction::full_tx& tx,
                         const pubkey_t& pubkey,
                         size_t witness_prog_len) const;

        /// \brief Signs a tnswap receive transaction.
        /// \param tx The transaction to sign.
        /// \param pubkey The public key used for signing.
        void sign_tnswap_receive(transaction::full_tx& tx,
                                 const pubkey_t& pubkey) const;

        /// \brief Signs a tnswap refund transaction.
        /// \param tx The transaction to sign.
        /// \param pubkey The public key used for signing.
        void sign_tnswap_refund(transaction::full_tx& tx,
                                const pubkey_t& pubkey) const;

        /// Mutex for thread-safe access to secret keys.
        mutable std::shared_mutex m_sk_mut;

        /// Stores secret keys and their corresponding hashes.
        std::
            unordered_map<skey_hash_t, skey_t, hashing::const_sip_hash<skey_t>>
                m_skeys;

        /// Tracks public keys for active swap sessions by secret key hash.
        std::unordered_map<skey_hash_t,
                           pubkey_t,
                           hashing::const_sip_hash<skey_hash_t>>
            m_sessions;

        /// Default expiration time for tnswap transactions in minutes.
        static constexpr uint32_t DEFAULT_EXPIRY_MINS = 10;

        /// Stores the current session's secret key hash.
        skey_hash_t m_current_skey_hash{};
    };
}

#endif // OPENCBDC_TX_SRC_TRANSACTION_SWAP_WALLET_H_
