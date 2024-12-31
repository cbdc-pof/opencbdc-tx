#include "tnswap_validation.hpp"

#include "util/common/hash.hpp"
#include "util/serialization/format.hpp"

#include <chrono>
#include <cstring>

namespace cbdc::transaction::validation {
    static const auto secp_context
        = std::unique_ptr<secp256k1_context,
                          decltype(&secp256k1_context_destroy)>(
            secp256k1_context_create(SECP256K1_CONTEXT_VERIFY),
            &secp256k1_context_destroy);

    auto unpack_tnswap_receive_witness_data(const transaction::full_tx& tx,
                                            size_t idx)
        -> std::pair<std::optional<witness_error_code>,
                     tnswap_receive_witness_data> {
        const auto& wit = tx.m_witness[idx];
        std::pair<std::optional<witness_error_code>,
                  tnswap_receive_witness_data>
            ret{};
        auto& err = ret.first;
        auto& wit_data = ret.second;

        // Check the length of the witness data using existing function
        const auto witness_len_err = check_tnswap_receive_witness_len(tx, idx);
        if(witness_len_err) {
            err = witness_len_err;
            std::cout << "Found Wit prog tnswap_receive len error\n";
            return ret;
        }

        // Check the witness program type
        const auto witness_program_type
            = static_cast<cbdc::transaction::validation::witness_program_type>(
                wit[0]);

        if(witness_program_type
           != cbdc::transaction::validation::witness_program_type::
               tnswap_receive) {
            std::cout << "Found Wit prog tnswap_receive Diff type\n";
            err = witness_error_code::malformed;
            return ret;
        }

        auto curr_index = sizeof(witness_program_type);

        // Proceed with unpacking data since length has already been validated

        std::memcpy(&wit_data.m_expiry_time,
                    &wit[curr_index],
                    sizeof(wit_data.m_expiry_time));
        curr_index += sizeof(wit_data.m_expiry_time);
        std::memcpy(wit_data.m_sender_pubkey_hash.data(),
                    &wit[curr_index],
                    sizeof(wit_data.m_sender_pubkey_hash));
        curr_index += sizeof(wit_data.m_sender_pubkey_hash);
        std::memcpy(wit_data.m_receiver_pubkey.data(),
                    &wit[curr_index],
                    sizeof(wit_data.m_receiver_pubkey));
        curr_index += sizeof(wit_data.m_receiver_pubkey);
        std::memcpy(wit_data.m_sk.data(),
                    &wit[curr_index],
                    sizeof(wit_data.m_sk));

        err = std::nullopt; // No error occurred
        return ret;
    }

    auto pack_tnswap_refund_witness_data(const tnswap_refund_witness_data& wit)
        -> cbdc::buffer {
        cbdc::buffer buffer{};
        buffer.append(&wit.m_expiry_time, sizeof(wit.m_expiry_time));
        buffer.append(wit.m_sender_pubkey.data(), wit.m_sender_pubkey.size());
        buffer.append(wit.m_receiver_pubkey_hash.data(),
                      wit.m_receiver_pubkey_hash.size());
        buffer.append(wit.m_sk_hash.data(), wit.m_sk_hash.size());

        return buffer;
    }

    auto pack_tnswap_receive_witness_data(
        const tnswap_receive_witness_data& wit) -> cbdc::buffer {
        cbdc::buffer buffer{};
        buffer.append(&wit.m_expiry_time, sizeof(wit.m_expiry_time));
        buffer.append(wit.m_sender_pubkey_hash.data(),
                      wit.m_sender_pubkey_hash.size());
        buffer.append(wit.m_receiver_pubkey.data(),
                      wit.m_receiver_pubkey.size());
        buffer.append(wit.m_sk.data(), wit.m_sk.size());

        return buffer;
    }

    auto get_tnswap_wit_commit(const uint64_t time_expiry,
                               const pubkey_t sender_pubk,
                               const pubkey_t payee,
                               const skey_hash_t sk_hash) -> hash_t {
        hash_t ret;

        const auto sender_pk_hash = hash_data(sender_pubk);
        const auto receiver_pk_hash = hash_data(payee);

        cbdc::buffer buffer{};
        buffer.append(&time_expiry, sizeof(time_expiry));
        buffer.append(sender_pk_hash.data(), sender_pk_hash.size());
        buffer.append(receiver_pk_hash.data(), receiver_pk_hash.size());
        buffer.append(sk_hash.data(), sk_hash.size());
        CSHA256 sha;

        sha.Write(buffer.c_ptr(), buffer.size());
        sha.Finalize(ret.data());
        return ret;
    }

    auto get_tnswap_receive_wit_commit(const tnswap_receive_witness_data& wit)
        -> hash_t {
        hash_t ret;
        cbdc::buffer buffer{};
        buffer.append(&wit.m_expiry_time, sizeof(wit.m_expiry_time));
        buffer.append(wit.m_sender_pubkey_hash.data(),
                      wit.m_sender_pubkey_hash.size());
        hash_t rec_pubkey_hash = hash_data(wit.m_receiver_pubkey);
        buffer.append(rec_pubkey_hash.data(), rec_pubkey_hash.size());
        hash_t sk_hash = hash_data(wit.m_sk);
        buffer.append(sk_hash.data(), sk_hash.size());
        CSHA256 sha;

        sha.Write(buffer.c_ptr(), buffer.size());
        sha.Finalize(ret.data());
        return ret;
    }

    auto get_tnswap_refund_wit_commit(const tnswap_refund_witness_data& wit)
        -> hash_t {
        hash_t ret;
        cbdc::buffer buffer{};

        buffer.append(&wit.m_expiry_time, sizeof(wit.m_expiry_time));
        hash_t pubkey_hash = hash_data(wit.m_sender_pubkey);
        buffer.append(pubkey_hash.data(), pubkey_hash.size());
        buffer.append(wit.m_receiver_pubkey_hash.data(),
                      wit.m_receiver_pubkey_hash.size());
        buffer.append(wit.m_sk_hash.data(), wit.m_sk_hash.size());
        CSHA256 sha;

        sha.Write(buffer.c_ptr(), buffer.size());
        sha.Finalize(ret.data());
        return ret;
    }

    auto unpack_tnswap_refund_witness_data(const transaction::full_tx& tx,
                                           size_t idx)
        -> std::pair<std::optional<witness_error_code>,
                     tnswap_refund_witness_data> {
        const auto& wit = tx.m_witness[idx];
        std::pair<std::optional<witness_error_code>,
                  tnswap_refund_witness_data>
            ret{};
        auto& wit_data = ret.second;
        auto& err = ret.first;

        // Check the length of the witness data using existing function
        const auto witness_len_err = check_tnswap_refund_witness_len(tx, idx);
        if(witness_len_err) {
            err = witness_len_err; // Assign the error code from optional
            return ret;
        }

        // Check the witness program type
        const auto witness_program_type
            = static_cast<cbdc::transaction::validation::witness_program_type>(
                wit[0]);

        if(witness_program_type
           != cbdc::transaction::validation::witness_program_type::
               tnswap_refund) {
            err = witness_error_code::malformed;
            return ret;
        }

        auto curr_index = sizeof(witness_program_type);

        // Proceed with unpacking data since length has already been validated
        std::memcpy(&wit_data.m_expiry_time,
                    &wit[curr_index],
                    sizeof(wit_data.m_expiry_time));
        curr_index += sizeof(wit_data.m_expiry_time);

        std::memcpy(wit_data.m_sender_pubkey.data(),
                    &wit[curr_index],
                    sizeof(wit_data.m_sender_pubkey));
        curr_index += sizeof(wit_data.m_sender_pubkey);

        std::memcpy(wit_data.m_receiver_pubkey_hash.data(),
                    &wit[curr_index],
                    sizeof(wit_data.m_receiver_pubkey_hash));
        curr_index += sizeof(wit_data.m_receiver_pubkey_hash);

        std::memcpy(wit_data.m_sk_hash.data(),
                    &wit[curr_index],
                    sizeof(wit_data.m_sk_hash));

        err = std::nullopt; // No error occurred
        return ret;
    }

    auto check_tnswap_receive_witness_len(const transaction::full_tx& tx,
                                          size_t idx)
        -> std::optional<witness_error_code> {
        const auto& wit = tx.m_witness[idx];
        if(wit.size() != tnswap_receive_witness_len) {
            return witness_error_code::malformed;
        }

        return std::nullopt;
    }

    auto check_tnswap_refund_witness_len(const transaction::full_tx& tx,
                                         size_t idx)
        -> std::optional<witness_error_code> {
        const auto& wit = tx.m_witness[idx];
        if(wit.size() != tnswap_refund_witness_len) {
            return witness_error_code::malformed;
        }

        return std::nullopt;
    }

    auto check_timelock(const tnswap_refund_witness_data& data)
        -> std::optional<witness_error_code> {
        auto now = std::chrono::system_clock::now();
        auto now_timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch())
                .count());
        if(now_timestamp <= data.m_expiry_time) {
            std::cerr<<"Witness Failed with Time Early Error \n";
            return witness_error_code::time_early_error;
        }

        return std::nullopt;
    }

    auto
    check_signature(const cbdc::transaction::full_tx& tx,
                    size_t idx,
                    pubkey_t validating_key,
                    size_t prog_len) -> std::optional<witness_error_code> {
        const auto& wit = tx.m_witness[idx];
        secp256k1_xonly_pubkey pubkey{};

        if(secp256k1_xonly_pubkey_parse(secp_context.get(),
                                        &pubkey,
                                        validating_key.data())
           != 1) {
            return witness_error_code::invalid_public_key;
        }

        const auto sighash = cbdc::transaction::tx_id(tx);
        std::array<unsigned char, sig_len> sig_arr{};
        std::memcpy(sig_arr.data(), &wit[prog_len], sizeof(sig_arr));

        if(secp256k1_schnorrsig_verify(secp_context.get(),
                                       sig_arr.data(),
                                       sighash.data(),
                                       &pubkey)
           != 1) {
            return witness_error_code::invalid_signature;
        }

        return std::nullopt;
    }

    auto check_tnswap_receive_witness_signature(
        const cbdc::transaction::full_tx& tx,
        size_t idx,
        pubkey_t validating_key) -> std::optional<witness_error_code> {
        return check_signature(tx,
                               idx,
                               validating_key,
                               tnswap_receive_witness_prog_len);
    }

    auto check_tnswap_refund_witness_signature(
        const cbdc::transaction::full_tx& tx,
        size_t idx,
        pubkey_t validating_key) -> std::optional<witness_error_code> {
        return check_signature(tx,
                               idx,
                               validating_key,
                               tnswap_refund_witness_prog_len);
    }

    auto check_tnswap_receive_witness(const transaction::full_tx& tx,
                                      size_t idx)
        -> std::optional<witness_error_code> {
        auto maybe_error = unpack_tnswap_receive_witness_data(tx, idx);
        if(maybe_error.first) {
            return maybe_error.first;
        }
        auto& wit = maybe_error.second;

        hash_t wit_commit = get_tnswap_receive_wit_commit(wit);

        const auto& witness_program_commitment
            = tx.m_inputs[idx].m_prevout_data.m_witness_program_commitment;

        if(wit_commit != witness_program_commitment) {
            std::cout << "Mismatch in witness commitment: Received = "
                      << cbdc::to_string(wit_commit) << ", Expected = "
                      << cbdc::to_string(witness_program_commitment) << "\n";
            return witness_error_code::program_mismatch;
        }

        const auto witness_sig_err
            = check_tnswap_receive_witness_signature(tx,
                                                     idx,
                                                     wit.m_receiver_pubkey);
        if(witness_sig_err) {
            return witness_sig_err;
        }

        return std::nullopt;
    }

    auto check_tnswap_refund_witness(const transaction::full_tx& tx,
                                     size_t idx)
        -> std::optional<witness_error_code> {
        
        auto maybe_error = unpack_tnswap_refund_witness_data(tx, idx);
        if(maybe_error.first) {
            return maybe_error.first;
        }
        auto& wit = maybe_error.second;

        hash_t wit_commit = get_tnswap_refund_wit_commit(wit);

        auto time_err = check_timelock(wit);
        if(time_err) {
            return time_err;
        }

        const auto& witness_program_commitment
            = tx.m_inputs[idx].m_prevout_data.m_witness_program_commitment;

        if(wit_commit != witness_program_commitment) {
            return witness_error_code::program_mismatch;
        }

        const auto witness_sig_err
            = check_tnswap_refund_witness_signature(tx,
                                                    idx,
                                                    wit.m_sender_pubkey);
        if(witness_sig_err) {
            return witness_sig_err;
        }

        return std::nullopt;
    }

}
