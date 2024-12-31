// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENCBDC_TX_SRC_TRANSACTION_TNSWAP_VALIDATION_H_
#define OPENCBDC_TX_SRC_TRANSACTION_TNSWAP_VALIDATION_H_

#include "util/serialization/format.hpp"
#include "validation.hpp"

#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <secp256k1.h>
#include <secp256k1_schnorrsig.h>
#include <set>
#include <utility>
#include <variant>

namespace cbdc::transaction::validation {
    /// Specifies the witness program for TierNolan swap (Swap Initiator Only)
    struct tnswap_witness_program {
        pubkey_t m_payee_pk;        ///< Public key of the swap counterparty
        hash_t m_h_sk;              ///< Hash-lock: secret hash preimage
        pubkey_t m_payer_refund_pk; ///< Public key of the refund payer
        uint64_t time_expiry;       ///< Expiration time of the swap
    };

    /// Witness data for spending/receiving TierNolan swap payments
    struct tnswap_receive_witness_data {
        uint64_t m_expiry_time;      ///< Expiration time of the witness
        hash_t m_sender_pubkey_hash; ///< Hash of the sender's public key
        pubkey_t m_receiver_pubkey;  ///< Public key of the receiver
        skey_t m_sk;                 ///< Swap initiator's secret preimage
    };

    /// Witness data for refunding TierNolan swaps
    struct tnswap_refund_witness_data {
        uint64_t m_expiry_time;        ///< Expiration time of the refund
        pubkey_t m_sender_pubkey;      ///< Public key of the sender
        hash_t m_receiver_pubkey_hash; ///< Hash of the receiver's public key
        skey_hash_t m_sk_hash; ///< Hash of the initiator's secret preimage
    };

    /// Length constants for each element
    // Length constants for each struct
    static constexpr auto tnswap_receive_witness_data_len
        = sizeof(hash_t)       // m_expiry_time
        + sizeof(hash_t)       // m_sender_pubkey_hash
        + sizeof(pubkey_t)     // m_receiver_pubkey
        + sizeof(skey_t)       // m_sk
        + sizeof(signature_t); // m_sig
    static constexpr auto tnswap_receive_witness_prog_len
        = sizeof(witness_program_type) + tnswap_receive_witness_data_len;

    static constexpr auto tnswap_receive_witness_len
        = tnswap_receive_witness_prog_len + sig_len;

    static constexpr auto tnswap_refund_witness_data_len
        = sizeof(uint64_t)     // m_expiry_time
        + sizeof(pubkey_t)     // m_sender_pubkey
        + sizeof(hash_t)       // m_receiver_pubkey_hash
        + sizeof(skey_hash_t)  // m_sk
        + sizeof(signature_t); // m_sig

    static constexpr auto tnswap_refund_witness_prog_len
        = sizeof(witness_program_type) + tnswap_refund_witness_data_len;

    static constexpr auto tnswap_refund_witness_len
        = tnswap_refund_witness_prog_len + sig_len;

    /// \brief Unpack the receive witness data from a transaction
    ///
    /// \param tx transaction containing the witness
    /// \param idx index of the witness in the transaction
    /// \return A pair containing the error code (if any) and the unpacked
    ///         witness data
    auto unpack_tnswap_receive_witness_data(const transaction::full_tx& tx,
                                            size_t idx)
        -> std::pair<std::optional<witness_error_code>,
                     tnswap_receive_witness_data>;

    /// \brief Unpack the refund witness data from a transaction
    ///
    /// \param tx transaction containing the witness
    /// \param idx index of the witness in the transaction
    /// \return A pair containing the error code (if any) and the unpacked
    ///         witness data
    auto unpack_tnswap_refund_witness_data(const transaction::full_tx& tx,
                                           size_t idx)
        -> std::pair<std::optional<witness_error_code>,
                     tnswap_refund_witness_data>;

    /// \brief Pack receive witness data into a buffer
    ///
    /// \param wit receive witness data to pack
    /// \return A buffer containing the serialized witness data
    auto pack_tnswap_receive_witness_data(
        const tnswap_receive_witness_data& wit) -> cbdc::buffer;

    /// \brief Pack refund witness data into a buffer
    ///
    /// \param wit refund witness data to pack
    /// \return A buffer containing the serialized witness data
    auto pack_tnswap_refund_witness_data(const tnswap_refund_witness_data& wit)
        -> cbdc::buffer;

    /// \brief Validate the length of the receive witness
    ///
    /// \param tx transaction containing the witness
    /// \param idx index of the witness in the transaction
    /// \return null if the witness length is valid, otherwise an error code
    auto check_tnswap_receive_witness_len(const transaction::full_tx& tx,
                                          size_t idx)
        -> std::optional<witness_error_code>;

    /// \brief Validate the length of the refund witness
    ///
    /// \param tx transaction containing the witness
    /// \param idx index of the witness in the transaction
    /// \return null if the witness length is valid, otherwise an error code
    auto check_tnswap_refund_witness_len(const transaction::full_tx& tx,
                                         size_t idx)
        -> std::optional<witness_error_code>;

    /// \brief Validate the receive witness in a transaction
    ///
    /// \param tx transaction containing the witness
    /// \param idx index of the witness in the transaction
    /// \return null if the witness is valid, otherwise an error code
    auto check_tnswap_receive_witness(const transaction::full_tx& tx,
                                      size_t idx)
        -> std::optional<witness_error_code>;

    /// \brief Validate the refund witness in a transaction
    ///
    /// \param tx transaction containing the witness
    /// \param idx index of the witness in the transaction
    /// \return null if the witness is valid, otherwise an error code
    auto check_tnswap_refund_witness(const transaction::full_tx& tx,
                                     size_t idx)
        -> std::optional<witness_error_code>;

    /// \brief Validate the signature in a receive witness
    ///
    /// \param tx transaction containing the witness
    /// \param idx index of the witness in the transaction
    /// \param validating_key public key used for signature validation
    /// \return null if the signature is valid, otherwise an error code
    auto check_tnswap_receive_witness_signature(const transaction::full_tx& tx,
                                                size_t idx,
                                                pubkey_t validating_key)
        -> std::optional<witness_error_code>;

    /// \brief Validate the signature in a refund witness
    ///
    /// \param tx transaction containing the witness
    /// \param idx index of the witness in the transaction
    /// \param validating_key public key used for signature validation
    /// \return null if the signature is valid, otherwise an error code
    auto check_tnswap_refund_witness_signature(const transaction::full_tx& tx,
                                               size_t idx,
                                               pubkey_t validating_key)
        -> std::optional<witness_error_code>;

    /// \brief Validate the signature
    ///
    /// \param tx transaction containing the witness
    /// \param idx index of the witness in the transaction
    /// \param validating_key public key used for signature validation
    /// \param prog_len Identifies program length of tnswap_refund or tnswap_receive
    /// \return null if the signature is valid, otherwise an error code
    auto check_signature(const cbdc::transaction::full_tx& tx,
                         size_t idx,
                         pubkey_t validating_key,
                         size_t prog_len) -> std::optional<witness_error_code>;

    /// \brief Generate the witness commitment for a TierNolan swap
    ///
    /// \param time_expiry expiration time of the swap
    /// \param sender_pubk public key of the sender
    /// \param payee public key of the payee
    /// \param sk_hash hash of the secret preimage
    /// \return A hash representing the witness commitment
    auto get_tnswap_wit_commit(uint64_t time_expiry,
                               pubkey_t sender_pubk,
                               pubkey_t payee,
                               skey_hash_t sk_hash) -> hash_t;

    /// \brief Generate the witness commitment for a receive witness
    ///
    /// \param wit receive witness data
    /// \return A hash representing the witness commitment
    auto get_tnswap_receive_wit_commit(const tnswap_receive_witness_data& wit)
        -> hash_t;

    /// \brief Generate the witness commitment for a refund witness
    ///
    /// \param wit refund witness data
    /// \return A hash representing the witness commitment
    auto get_tnswap_refund_wit_commit(const tnswap_refund_witness_data& wit)
        -> hash_t;

    /// \brief Validate the timelock for a refund witness
    ///
    /// \param data refund witness data
    /// \return null if the timelock is valid, otherwise an error code
    auto check_timelock(const tnswap_refund_witness_data& data)
        -> std::optional<witness_error_code>;
}

#endif // OPENCBDC_TX_SRC_TRANSACTION_TNSWAP_VALIDATION_H_
