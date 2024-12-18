// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "uhs/transaction/validation.hpp"
#include "uhs/transaction/swap_wallet.hpp"
#include<iostream>
#include <gtest/gtest.h>
#include <variant>
#include<optional>

class TnSwapWitnessValidationTest  : public ::testing::Test {
  protected:
    void SetUp() override {
        

        auto mint_tx1 = wallet1.mint_new_coins(3, 100);
        wallet1.confirm_transaction(mint_tx1);
        auto mint_tx2 = wallet2.mint_new_coins(1, 100);
        wallet2.confirm_transaction(mint_tx2);
        m_pub_wallet2 = wallet2.generate_key();
        std::cout<<"Inside TnSwapWitnessValidationTest receiver Key :"<<cbdc::to_string(m_pub_wallet2);
        auto [x, xhash] = wallet1.generate_skey();
        m_skey = x;
        m_skey_hash = xhash;
        std::cout<<"\nCreating Intiator Transaction\n";    
        m_valid_create_tx = wallet1.create_txn_tnswap(m_expiry_time,
                           m_pub_wallet2, m_skey_hash, m_expiry_time).value();

        std::cout<<"Creating Receive Transaction\n";


        m_valid_create_receive_tx = wallet2.create_txn_tnswap_receive(cbdc::transaction::swap_wallet::export_raw_inputs(m_valid_create_tx), m_expiry_time, wallet1.get_pubkey_swap_session(m_skey_hash).value(), 
            m_pub_wallet2, m_skey).value();

        std::cout<<"Setup Done\n";
            
    }

    cbdc::transaction::full_tx m_valid_create_tx{};
    cbdc::transaction::full_tx m_valid_create_receive_tx{};

    std::unique_ptr<secp256k1_context, decltype(&secp256k1_context_destroy)>
        m_secp{secp256k1_context_create(SECP256K1_CONTEXT_SIGN
                                        | SECP256K1_CONTEXT_VERIFY),
               &secp256k1_context_destroy};
    cbdc::privkey_t m_priv0{cbdc::hash_from_hex(
        "0000000000000001000000000000000000000000000000000000000000000000")};
    cbdc::pubkey_t m_pub0{cbdc::pubkey_from_privkey(m_priv0, m_secp.get())};
    cbdc::privkey_t m_priv1{cbdc::hash_from_hex(
        "1000000000000001000000000000000000000000000000000000000000000000")};
    cbdc::pubkey_t m_pub1{cbdc::pubkey_from_privkey(m_priv1, m_secp.get())};
    std::unordered_set<cbdc::pubkey_t, cbdc::hashing::null> m_pubkeys{m_pub0,
                                                                      m_pub1};
    cbdc::skey_t m_skey{};
    cbdc::pubkey_t m_pub_wallet2{};

    cbdc::skey_hash_t m_skey_hash{};
    
    uint64_t m_expiry_time = 100;
    uint64_t m_amount = 100;

    cbdc::transaction::swap_wallet wallet1{};
    cbdc::transaction::swap_wallet wallet2{};
    

};


TEST_F(TnSwapWitnessValidationTest, PackUnpackWitnessCommitment) {
    /// Get Witness Commit 
    auto wit_commit = cbdc::transaction::validation::get_tnswap_wit_commit(m_expiry_time, m_pub0, m_pub1, m_skey_hash);

    /// Build Wit Commitment
    cbdc::transaction::validation::tnswap_receive_witness_data wit_data{};
    wit_data.m_expiry_time = m_expiry_time;
    wit_data.m_sender_pubkey_hash = cbdc::hash_data(m_pub0);
    wit_data.m_receiver_pubkey = m_pub1;
    wit_data.m_sk = m_skey;

    /// Calc Wit Commitment//
    auto tnswap_receive_wit_commit = get_tnswap_receive_wit_commit(wit_data);
    // Check Wit Commit 
    ASSERT_EQ(tnswap_receive_wit_commit, wit_commit);
}

TEST_F(TnSwapWitnessValidationTest, PackUnpackValidReceiveWitness) {
       /// PACKING txn
    auto idx = 0; // Assume valid witness index
    cbdc::transaction::full_tx ret{};
    ret.m_witness.resize(2);
    auto& witness = ret.m_witness[idx];
    // sig must be part here
    witness.resize(cbdc::transaction::validation::tnswap_receive_witness_len);
    witness[0] = std::byte(
        cbdc::transaction::validation::witness_program_type::tnswap_receive);

    cbdc::transaction::validation::tnswap_receive_witness_data wit_data{};
    wit_data.m_expiry_time = m_expiry_time;
    wit_data.m_sender_pubkey_hash = cbdc::hash_data(m_pub0);
    wit_data.m_receiver_pubkey = m_pub_wallet2;
    wit_data.m_sk = m_skey;
    auto buffer
        = cbdc::transaction::validation::pack_tnswap_receive_witness_data(
            wit_data);
    std::memcpy(
        &witness[sizeof(cbdc::transaction::validation::witness_program_type)],
        buffer.data(),
        buffer.size());
   

    auto result = cbdc::transaction::validation::unpack_tnswap_receive_witness_data(ret, idx);
    if (result.first.has_value()) {
        std::cout<<"Error is :"<<static_cast<int>(*result.first)<<std::endl;
    }
    ASSERT_FALSE(result.first.has_value()); // No error expected
    auto &witd = result.second;
     // Check that wit_data fields match expectations
    ASSERT_EQ(witd.m_expiry_time, m_expiry_time /* Expected expiry time */);
    ASSERT_EQ(witd.m_sk, m_skey/* Expected sender hash */);
    ASSERT_EQ(witd.m_receiver_pubkey, m_pub_wallet2/* Expected receiver pubkey */);
}

TEST_F(TnSwapWitnessValidationTest, CheckReceiveWitnessCommitment) {
    auto idx = 0; // Assume valid witness index
    auto result = cbdc::transaction::validation::check_tnswap_receive_witness(m_valid_create_receive_tx, idx);
    ASSERT_FALSE(result.has_value()); // No error expected
}


TEST_F(TnSwapWitnessValidationTest, PackUnpackRefundWitness) {

    /// PACKING txn
    auto idx = 0; // Assume valid witness index
    cbdc::transaction::full_tx ret{};
    ret.m_witness.resize(2);
    auto& witness = ret.m_witness[idx];
    // sig must be part here
    witness.resize(cbdc::transaction::validation::tnswap_refund_witness_len);
    witness[0] = std::byte(
        cbdc::transaction::validation::witness_program_type::tnswap_refund);

    cbdc::transaction::validation::tnswap_refund_witness_data wit_data{};
    wit_data.m_expiry_time = m_expiry_time;
    wit_data.m_sender_pubkey = m_pub0;
    wit_data.m_receiver_pubkey_hash = cbdc::hash_data(m_pub1);
    wit_data.m_sk_hash = m_skey_hash;
    auto buffer = cbdc::transaction::validation::pack_tnswap_refund_witness_data(
        wit_data);
    std::memcpy(
        &witness[sizeof(cbdc::transaction::validation::witness_program_type)],
        buffer.data(),
        buffer.size());

    auto result = cbdc::transaction::validation::unpack_tnswap_refund_witness_data(ret, idx);
    if (result.first.has_value()) {
        std::cout<<"Error is :"<<static_cast<int>(*result.first)<<std::endl;
    }
    ASSERT_FALSE(result.first.has_value()); // No error expected
     // Check that wit_data fields match expectations
     auto &witd = result.second;
     // Check that wit_data fields match expectations
    ASSERT_EQ(witd.m_expiry_time, m_expiry_time /* Expected expiry time */);
    ASSERT_EQ(witd.m_sk_hash, m_skey_hash/* Expected sender hash */);
    ASSERT_EQ(witd.m_sender_pubkey, m_pub0/* Expected receiver pubkey */);

}



TEST_F(TnSwapWitnessValidationTest, UnpackValidRefundWitness) {
    auto idx = 0; // Assume valid witness index
     auto txn  = wallet1.create_txn_tnswap_refund(cbdc::transaction::swap_wallet::export_raw_inputs(m_valid_create_tx), m_expiry_time, wallet1.get_pubkey_swap_session(m_skey_hash).value(), 
            m_pub_wallet2, m_skey_hash).value();

    auto result = cbdc::transaction::validation::unpack_tnswap_refund_witness_data(txn, idx);
    if (result.first.has_value()) {
        std::cout<<"Error is :"<<static_cast<int>(*result.first)<<std::endl;
    }
    ASSERT_FALSE(result.first.has_value()); // No error expected
    const auto& wit_data = result.second;

    // Check that wit_data fields match expectations
    ASSERT_EQ(wit_data.m_expiry_time, m_expiry_time /* Expected expiry time */);
    ASSERT_EQ(wit_data.m_sk_hash, m_skey_hash/* Expected sender hash */);
    ASSERT_EQ(wit_data.m_sender_pubkey, wallet1.get_pubkey_swap_session(m_skey_hash).value()/* Expected receiver pubkey */);

    cbdc::hash_t wit_commit = cbdc::transaction::validation::get_tnswap_refund_wit_commit(wit_data);

    
    const auto& witness_program_commitment
        = txn.m_inputs[idx].m_prevout_data.m_witness_program_commitment;

    ASSERT_EQ(wit_commit, witness_program_commitment);
}

TEST_F(TnSwapWitnessValidationTest, CheckRefundWitnessCommitment) {
    auto idx = 0; // Assume valid witness index
     auto txn  = wallet1.create_txn_tnswap_refund(cbdc::transaction::swap_wallet::export_raw_inputs(m_valid_create_tx), m_expiry_time, wallet1.get_pubkey_swap_session(m_skey_hash).value(), 
            m_pub_wallet2, m_skey_hash).value();

    auto result = cbdc::transaction::validation::check_tnswap_refund_witness(txn, idx);
    if (result.has_value()) {
        std::cout<<"Error is :"<<static_cast<int>(*result)<<std::endl;
    }

    ASSERT_FALSE(result.has_value()); // No error expected
}

TEST_F(TnSwapWitnessValidationTest, validRefund) {
     auto txn  = wallet1.create_txn_tnswap_refund(cbdc::transaction::swap_wallet::export_raw_inputs(m_valid_create_tx), m_expiry_time, wallet1.get_pubkey_swap_session(m_skey_hash).value(), 
            m_pub_wallet2, m_skey_hash).value();

    auto err = cbdc::transaction::validation::check_tx(txn);
    ASSERT_FALSE(err.has_value());
}

TEST_F(TnSwapWitnessValidationTest, validReceive) {
    //  auto txn  = wallet1.create_txn_tnswap_refund(cbdc::transaction::swap_wallet::export_raw_inputs(m_valid_create_tx), m_expiry_time, wallet1.get_pubkey_swap_session(m_skey_hash).value(), 
    //         m_pub_wallet2, m_skey_hash).value();

    auto err = cbdc::transaction::validation::check_tx(m_valid_create_receive_tx);
    ASSERT_FALSE(err.has_value());
}
