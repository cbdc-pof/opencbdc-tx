// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "swap_wallet.hpp"

#include <thread>

namespace cbdc::transaction {

    auto swap_wallet::set_expiry(uint32_t mins) -> uint64_t {
        auto now = std::chrono::system_clock::now();

        auto now_plus_mins = now + std::chrono::minutes(mins);

        // Convert time_point to uint64_t timestamp (seconds since epoch)
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::minutes>(
                now_plus_mins.time_since_epoch())
                .count());
    }

    auto swap_wallet::generate_skey() -> std::pair<skey_t, skey_hash_t> {
        std::uniform_int_distribution<unsigned char> keygen;

        skey_t seckey;
        for(auto&& b : seckey) {
            b = keygen(*m_random_source);
        }
        auto sk_vec = to_vector(seckey);
        skey_hash_t ret = hash_data(sk_vec.data(), sk_vec.size());
        {
            std::unique_lock<std::shared_mutex> lg(m_sk_mut);
            m_skeys.insert({ret, seckey});
            m_current_skey_hash = ret;
        }

        return {seckey, ret};
    }

    // test function
    auto swap_wallet::getskey(skey_hash_t hash) -> std::optional<skey_t> {
        auto it = m_skeys.find(hash);
        if(it == m_skeys.end()) {
            return std::nullopt;
        }

        return it->second;
    }

    auto swap_wallet::export_raw_inputs(const transaction::full_tx& tx)
        -> std::vector<transaction::input> {
        const auto tx_id = transaction::tx_id(tx);
        std::cout << "preparing for exportng raw inputs..."
                  << tx.m_outputs.size() << std::endl;
        std::vector<transaction::input> new_utxos;
        {
            for(uint32_t i = 0; i < tx.m_outputs.size(); i++) {
                new_utxos.push_back(
                    transaction::input_from_output(tx, i, tx_id).value());
            }
        }
        std::cout << "Inputs Size: " << new_utxos.size() << std::endl;
        return new_utxos;
    }

    auto swap_wallet::create_txn_tnswap(const uint32_t amount,
                                        const pubkey_t& payee,
                                        const skey_hash_t s_hash,
                                        const uint64_t time_expiry)
        -> std::optional<full_tx> {
        auto maybe_tx = accumulate_inputs(amount);
        if(!maybe_tx.has_value()) {
            return std::nullopt;
        }

        auto& ret = maybe_tx.value().first;
        auto total_amount = maybe_tx.value().second;

        transaction::output destination_out;
        destination_out.m_value = amount;
        pubkey_t sender_pubkey = generate_key();

        /// user can multiple swap sessions going on, he can identify the
        /// session using pubkey
        m_sessions.insert({s_hash, sender_pubkey});

        destination_out.m_witness_program_commitment
            = transaction::validation::get_tnswap_wit_commit(time_expiry,
                                                             sender_pubkey,
                                                             payee,
                                                             s_hash);

        // TODO
        std::cout << "Storing Wit commit as: "
                  << cbdc::to_string(
                         destination_out.m_witness_program_commitment)
                  << std::endl;

        ret.m_outputs.push_back(destination_out);

        if(total_amount > amount) {
            // Add the change output if we need to
            transaction::output change_out;
            change_out.m_value = total_amount - amount;
            const auto pubkey = generate_key();
            change_out.m_witness_program_commitment
                = transaction::validation::get_p2pk_witness_commitment(pubkey);
            ret.m_outputs.push_back(change_out);
        }

        sign_tnswap(ret);

        return ret;
    }

    auto swap_wallet::create_txn_tnswap_receive(
        const transaction::input& prev_input,
        const uint64_t expiry_time,
        const pubkey_t& sender_payee,
        const pubkey_t& receiver_key,
        const skey_t& sk) -> std::optional<full_tx> {
        auto ret = full_tx();
        ret.m_inputs.push_back(prev_input);

        // The swap operation is applicable only for a single UTXO when there
        // is exactly one unit of currency.
        ret.m_witness.resize(1);

        transaction::output destination_out;
        destination_out.m_value = ret.m_inputs[0].m_prevout_data.m_value;

        destination_out.m_witness_program_commitment
            = transaction::validation::get_p2pk_witness_commitment(
                receiver_key);

        ret.m_outputs.push_back(destination_out);
        auto& witness = ret.m_witness[0];

        // signature must be part
        witness.resize(transaction::validation::tnswap_receive_witness_len);
        witness[0] = std::byte(
            transaction::validation::witness_program_type::tnswap_receive);

        transaction::validation::tnswap_receive_witness_data wit_data{};
        wit_data.m_expiry_time = expiry_time;
        wit_data.m_sender_pubkey_hash = hash_data(sender_payee);
        wit_data.m_receiver_pubkey = receiver_key;
        wit_data.m_sk = sk;

        auto hh
            = transaction::validation::get_tnswap_receive_wit_commit(wit_data);

        std::cout << "Hash of tnswap_receive_witness_data: "
                  << cbdc::to_string(hh) << std::endl
                  << "Receiver Payee: " << cbdc::to_string(receiver_key);

        auto buffer
            = transaction::validation::pack_tnswap_receive_witness_data(
                wit_data);
        std::memcpy(
            &witness[sizeof(transaction::validation::witness_program_type)],
            buffer.data(),
            buffer.size());

        sign_tnswap(ret,
                    receiver_key,
                    transaction::validation::tnswap_receive_witness_prog_len);

        return ret;
    }

    auto swap_wallet::create_txn_tnswap_refund(
        const transaction::input& prev_input,
        const uint64_t expiry_time,
        const pubkey_t& sender_payee,
        const pubkey_t& receiver_key,
        const skey_hash_t& sk_hash) -> std::optional<full_tx> {
        auto ret = full_tx();

        ret.m_inputs.push_back(prev_input);
        // The swap operation is applicable only for a single UTXO when there
        // is exactly one unit of currency.
        ret.m_witness.resize(1);
        transaction::output destination_out;
        destination_out.m_value = ret.m_inputs[0].m_prevout_data.m_value;
        destination_out.m_witness_program_commitment
            = transaction::validation::get_p2pk_witness_commitment(
                receiver_key);

        ret.m_outputs.push_back(destination_out);
        auto& witness = ret.m_witness[0];
        // sig must be part here
        witness.resize(transaction::validation::tnswap_refund_witness_len);
        witness[0] = std::byte(
            transaction::validation::witness_program_type::tnswap_refund);

        transaction::validation::tnswap_refund_witness_data wit_data{};
        wit_data.m_expiry_time = expiry_time;
        wit_data.m_sender_pubkey = sender_payee;
        wit_data.m_receiver_pubkey_hash = hash_data(receiver_key);
        wit_data.m_sk_hash = sk_hash;
        auto buffer = transaction::validation::pack_tnswap_refund_witness_data(
            wit_data);

        auto hh
            = transaction::validation::get_tnswap_refund_wit_commit(wit_data);

        std::cout << "Hash of tnswap_refund_witness_data: "
                  << cbdc::to_string(hh) << std::endl
                  << "Sender Payee: " << cbdc::to_string(sender_payee);

        std::memcpy(
            &witness[sizeof(transaction::validation::witness_program_type)],
            buffer.data(),
            buffer.size());

        sign_tnswap(ret,
                    sender_payee,
                    transaction::validation::tnswap_refund_witness_prog_len);
        return ret;
    }

    void swap_wallet::sign_tnswap(transaction::full_tx& tx,
                                  const pubkey_t& pubkey,
                                  size_t witness_prog_len) const {
        const auto sighash = transaction::tx_id(tx);

        // TODO should be present
        auto it = m_keys.find(pubkey);
        if(it == m_keys.end()) {
            std::cout << "Error: Key [" << cbdc::to_string(pubkey)
                      << "] must be present in wallet\n";
            assert(it != m_keys.end());
            return;
        }

        pubkey_t seckey = it->second;
        assert(tx.m_inputs.size() == 1);
        auto& sig = tx.m_witness[0];

        secp256k1_keypair keypair{};
        [[maybe_unused]] const auto ret
            = secp256k1_keypair_create(m_secp.get(), &keypair, seckey.data());
        assert(ret == 1);

        std::array<unsigned char, sig_len> sig_arr{};
        [[maybe_unused]] const auto sign_ret
            = secp256k1_schnorrsig_sign(m_secp.get(),
                                        sig_arr.data(),
                                        sighash.data(),
                                        &keypair,
                                        nullptr,
                                        nullptr);
        std::memcpy(&sig[witness_prog_len], sig_arr.data(), sizeof(sig_arr));
        assert(sign_ret == 1);
    }

    void swap_wallet::sign_tnswap_receive(transaction::full_tx& tx,
                                          const pubkey_t& pubkey) const {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        sign_tnswap(tx,
                    pubkey,
                    transaction::validation::tnswap_receive_witness_prog_len);
    }

    void swap_wallet::sign_tnswap_refund(transaction::full_tx& tx,
                                         const pubkey_t& pubkey) const {
        sign_tnswap(tx,
                    pubkey,
                    transaction::validation::tnswap_refund_witness_prog_len);
    }

    void swap_wallet::sign_tnswap(transaction::full_tx& tx) const {
        // TODO: other sighash types besides SIGHASH_ALL?
        const auto sighash = transaction::tx_id(tx);
        tx.m_witness.resize(tx.m_inputs.size());

        for(size_t i = 0; i < tx.m_inputs.size(); i++) {
            const auto& wit_commit
                = tx.m_inputs[i].m_prevout_data.m_witness_program_commitment;

            pubkey_t pubkey{};
            privkey_t seckey{};
            bool key_ours = false;
            {
                std::shared_lock<std::shared_mutex> sl(m_keys_mut);
                auto wit_prog = get_value<pubkey_t>(wit_commit);
                if(wit_prog.has_value()) {
                    key_ours = true;
                    pubkey = wit_prog.value();
                    seckey = m_keys.at(pubkey);
                }
            }

            if(key_ours) {
                auto& sig = tx.m_witness[i];
                sig.resize(transaction::validation::p2pk_witness_len);
                sig[0] = std::byte(
                    transaction::validation::witness_program_type::p2pk);
                std::memcpy(
                    &sig[sizeof(
                        transaction::validation::witness_program_type)],
                    pubkey.data(),
                    pubkey.size());

                secp256k1_keypair keypair{};
                [[maybe_unused]] const auto ret
                    = secp256k1_keypair_create(m_secp.get(),
                                               &keypair,
                                               seckey.data());
                assert(ret == 1);

                std::array<unsigned char, sig_len> sig_arr{};
                [[maybe_unused]] const auto sign_ret
                    = secp256k1_schnorrsig_sign(m_secp.get(),
                                                sig_arr.data(),
                                                sighash.data(),
                                                &keypair,
                                                nullptr,
                                                nullptr);
                std::memcpy(
                    &sig[transaction::validation::p2pk_witness_prog_len],
                    sig_arr.data(),
                    sizeof(sig_arr));
                assert(sign_ret == 1);
            }
        }
    }

    auto swap_wallet::get_pubkey_swap_session(skey_hash_t s_hash)
        -> std::optional<pubkey_t> {
        auto it = m_sessions.find(s_hash);
        if(it != m_sessions.end()) {
            return it->second;
        }
        return std::nullopt;
    }
} // namespace cbdc::transaction
