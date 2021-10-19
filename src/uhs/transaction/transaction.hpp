// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENCBDC_TX_SRC_TRANSACTION_TRANSACTION_H_
#define OPENCBDC_TX_SRC_TRANSACTION_TRANSACTION_H_

#include "crypto/sha256.h"
#include "util/common/hash.hpp"
#include "util/common/keys.hpp"
#include "util/serialization/format.hpp"
#include "util/serialization/util.hpp"

#include <cstdint>
#include <optional>

namespace cbdc::transaction {
    /// \brief The unique identifier of a specific \ref output from
    ///        a transaction
    ///
    /// Uniquely identifies an \ref output from a previous transaction.
    /// Output owners construct \ref input s in new transactions out of outputs
    /// and their corresponding \ref out_point s.
    ///
    /// \see \ref cbdc::operator<<(serializer&, const transaction::out_point&)
    struct out_point {
        /// The hash of the transaction which created the out_point
        hash_t m_tx_id{};

        /// The index of the output in the transaction's output list
        /// \see transaction::full_tx::m_outputs
        uint64_t m_index{0};

        auto operator==(const out_point& rhs) const -> bool;
        auto operator<(const out_point& rhs) const -> bool;

        out_point(const hash_t& hash, uint64_t index);

        out_point() = default;
    };

    /// \brief An output of a transaction
    ///
    /// An output created by a transaction.
    /// Its owner can spend it as an \ref input in a transaction.
    ///
    /// \see \ref cbdc::operator<<(serializer&, const transaction::output&)
    struct output {
        /// Hash of the witness program
        hash_t m_witness_program_commitment{};

        /// The integral value of the output, in atomic units of currency
        uint64_t m_value{0};

        auto operator==(const output& rhs) const -> bool;
        auto operator!=(const output& rhs) const -> bool;

        output() = default;
    };

    /// Calculate UHS ID from output nested hash and value.
    /// \param nested_hash hash of outpoint and witness program commitment.
    /// \param value value of the output.
    /// \return UHS ID.
    auto calculate_uhs_id(hash_t nested_hash, uint64_t value) -> hash_t;

    /// UHS element as represented in compact transactions.
    struct uhs_element {
        /// UHS ID.
        hash_t m_id{};
        /// Nested hash committing to the outpoit and witness program
        /// commitment.
        hash_t m_data{};
        /// Value of the output.
        uint64_t m_value{};

        auto operator==(const uhs_element& rhs) const -> bool;
    };

    /// \brief An input for a new transaction
    ///
    /// An \ref out_point and associated \ref output which a client intends to
    /// spend in a new transaction.
    ///
    /// \see \ref cbdc::operator<<(serializer&, const transaction::input&)
    struct input {
        /// The unique identifier of the output
        out_point m_prevout;

        /// The output's data
        output m_prevout_data;

        auto operator==(const input& rhs) const -> bool;
        auto operator!=(const input& rhs) const -> bool;

        /// Converts an input to a UHS element.
        /// \return UHS element.
        [[nodiscard]] auto to_uhs_element() const -> uhs_element;

        input() = default;
    };

    /// \brief A complete transaction
    ///
    /// Complete set of transaction data:
    ///   - the set of specific outputs the client wishes to spend (inputs)
    ///   - the set of new outputs the client wishes to produce
    ///   - the set of witness programs matching the declared commitments
    ///     of each associated output being spent
    ///
    /// \see \ref cbdc::operator<<(serializer&, const transaction::full_tx&)
    struct full_tx {
        /// The set of inputs for the transaction
        std::vector<input> m_inputs{};

        /// The set of new outputs created by the transaction
        std::vector<output> m_outputs{};

        /// The set of witnesses
        std::vector<witness_t> m_witness{};

        auto operator==(const full_tx& rhs) const -> bool;

        full_tx() = default;
    };

    /// Sentinel attestation type. Public key of the sentinel and signature of
    /// a compact transaction hash.
    using sentinel_attestation = std::pair<pubkey_t, signature_t>;

    /// \brief A condensed, hash-only transaction representation
    ///
    /// The minimum amount of data necessary for the transaction processor to
    /// update the UHS with the changes from a \ref full_tx.
    ///
    /// \see \ref cbdc::operator<<(serializer&, const transaction::compact_tx&)
    struct compact_tx {
        /// The hash of the full transaction returned by \ref tx_id
        hash_t m_id{};

        /// The set of UHS elements to be used as the transaction's inputs
        std::vector<uhs_element> m_inputs;

        /// The set of new UHS elements to be created by the transaction
        std::vector<uhs_element> m_uhs_outputs;

        /// Signatures from sentinels attesting the compact TX is valid.
        std::unordered_map<pubkey_t, signature_t, hashing::null>
            m_attestations;

        /// Equality of two compact transactions. Only compares the transaction
        /// IDs.
        auto operator==(const compact_tx& tx) const noexcept -> bool;

        compact_tx() = default;

        /// Constructor. Generates a compact transaction from a full
        /// transaction.
        /// \param tx full transaction from which to generate a compact
        ///           transaction.
        explicit compact_tx(const full_tx& tx);

        /// Sign the compact transaction and return the signature.
        /// \param ctx secp256k1 context with which to sign the transaction.
        /// \param key private key with which to sign the transaction.
        /// \return sentinel attestation containing the signature and
        ///         associated public key.
        [[nodiscard]] auto sign(secp256k1_context* ctx,
                                const privkey_t& key) const
            -> sentinel_attestation;

        /// Verify the given attestation contains a valid signature that
        /// matches the compact transaction.
        /// \param ctx secp256k1 contact with which to validate the signature.
        /// \param att sentinel attestation containing a public key and
        ///            signature.
        /// \return true if the given attestation is valid for this compact
        ///         transaction.
        [[nodiscard]] auto verify(secp256k1_context* ctx,
                                  const sentinel_attestation& att) const
            -> bool;

        /// Return the hash of the compact transaction, without the sentinel
        /// attestations included. Used as the message which is signed in
        /// sentinel attestations.
        /// \return
        [[nodiscard]] auto hash() const -> hash_t;
    };

    struct compact_tx_hasher {
        auto operator()(compact_tx const& tx) const noexcept -> size_t;
    };

    /// \brief Calculates the unique hash of a full transaction
    ///
    /// Returns a cryptographic hash of the inputs concatenated with the
    /// outputs (which are first transformed into inputs). Because output
    /// owners cannot reuse outputs across different transactions, this method
    /// will always generate a unique identifier for valid transactions.
    ///
    /// \param tx the \ref full_tx to hash
    /// \return the resultant hash of the transaction
    [[nodiscard]] auto tx_id(const full_tx& tx) noexcept -> hash_t;

    /// Converts the output at the specified index to an input
    /// \param tx the transaction from which to read outputs
    /// \param i index of the target output
    /// \param txid the txid of the transaction
    /// \return resultant input, or std::nullopt if i is invalid.
    auto input_from_output(const full_tx& tx, size_t i, const hash_t& txid)
        -> std::optional<input>;

    /// Calls input_from_output after calculating the TXID
    /// \param tx the transaction from which to read outputs
    /// \param i index of the target output
    /// \return result of input_from_output(tx, i, tx_id(tx))
    auto input_from_output(const full_tx& tx, size_t i)
        -> std::optional<input>;

    /// Calculate a UHS ID from an output and outpoint.
    /// \param entropy unique randomness to generate a collision-free UHS ID.
    ///                Currently a TX ID.
    /// \param i index of the output in the transaction.
    /// \param output output for the UHS ID.
    /// \return UHS element.
    auto uhs_id_from_output(const hash_t& entropy,
                            uint64_t i,
                            const output& output) -> uhs_element;
}

#endif // OPENCBDC_TX_SRC_TRANSACTION_TRANSACTION_H_
