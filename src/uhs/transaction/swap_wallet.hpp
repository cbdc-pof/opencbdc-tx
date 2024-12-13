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
    /// \brief Cryptographic wallet for digital currency assets and secrets.
    ///
    /// Stores unspent transaction outputs (UTXOs), and public/private key
    /// pairs for Pay-to-Public-Key transaction attestations.
    class swap_wallet : public wallet {
      public:
        swap_wallet() {}

        /// Creates a initial transaction for tnswap
        auto create_txn_tnswap(const uint32_t amount,
                               const pubkey_t& payee,
                               const skey_hash_t s_hash,
                               uint64_t time_expiry) -> std::optional<full_tx>;

        /// Creates a spend transaction for tnswap
        auto
        create_txn_tnswap_receive(const full_tx& input_tx,
                                  const uint64_t expiry_time,
                                  const pubkey_t& sender_payee,
                                  const pubkey_t& receiver_key,
                                  const skey_t& sk) -> std::optional<full_tx>;
        /// Creates a refund transaction for tnswap
        auto create_txn_tnswap_refund(const full_tx& input_tx,
                                      const uint64_t expiry_time,
                                      const pubkey_t& sender_payee,
                                      const pubkey_t& receiver_key,
                                      const skey_hash_t& sk_hash)
            -> std::optional<full_tx>;

        /// Generates a new secret key at which this wallet can receive
        /// payments via \ref send_to.
        /// \return a pair of skey and shash.
        auto generate_skey() -> std::pair<skey_t, skey_hash_t>;
        auto set_expiry(uint32_t minutes) -> uint64_t;
        auto
        get_pubkey_swap_session(skey_hash_t s_hash) -> std::optional<pubkey_t>;

        // TODO remove
        auto getskey(skey_hash_t hash) -> std::optional<skey_t>;
        auto current_shash() -> skey_hash_t {
            return m_current_skey_hash;
        };

      private:
        /// Signs each of the transaction's inputs using Schnorr signatures.
        /// \param tx the transaction whose inputs to sign.
        void sign_tnswap(full_tx& tx) const;
        void sign_tnswap(transaction::full_tx& tx,
                         const pubkey_t& pubkey,
                         size_t witness_prog_len) const;
        void sign_tnswap_receive(transaction::full_tx& tx,
                                 const pubkey_t& pubkey) const;
        void sign_tnswap_refund(transaction::full_tx& tx,
                                const pubkey_t& pubkey) const;

        auto confirm(const transaction::full_tx& tx)
            -> std::vector<transaction::input>;

        mutable std::shared_mutex m_sk_mut;
        std::
            unordered_map<skey_hash_t, skey_t, hashing::const_sip_hash<skey_t>>
                m_skeys;

        std::unordered_map<skey_hash_t,
                           pubkey_t,
                           hashing::const_sip_hash<skey_hash_t>>
            m_sessions;
        static constexpr uint32_t DEFAULT_EXPIRY_MINS = 10;
        skey_hash_t m_current_skey_hash{};
    };
}

#endif // OPENCBDC_TX_SRC_TRANSACTION_SWAP_WALLET_H_
