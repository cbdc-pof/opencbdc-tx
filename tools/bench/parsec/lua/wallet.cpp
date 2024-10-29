// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet.hpp"

#include "parsec/util.hpp"
#include "util/common/config.hpp"
#include "util/common/random_source.hpp"
#include "util/serialization/format.hpp"

#include <future>
#include <secp256k1_schnorrsig.h>

namespace cbdc::parsec {
    account_wallet::account_wallet(std::shared_ptr<logging::log> log,
                                   std::shared_ptr<broker::interface> broker,
                                   std::shared_ptr<agent::rpc::client> agent,
                                   cbdc::buffer pay_contract_key)
        : m_log(std::move(log)),
          m_agent(std::move(agent)),
          m_broker(std::move(broker)),
          m_pay_contract_key(std::move(pay_contract_key)) {
        auto rnd = cbdc::random_source(cbdc::config::random_source);
        m_privkey = rnd.random_hash();
        m_pubkey = cbdc::pubkey_from_privkey(m_privkey, m_secp.get());
        constexpr auto account_prefix = "account_";
        m_account_key.append(account_prefix, std::strlen(account_prefix));
        auto puk = cbdc::to_string(m_pubkey);
        m_account_key.append(m_pubkey.data(), m_pubkey.size());
    }

    auto account_wallet::init(uint64_t value,
                              const std::function<void(bool)>& result_callback)
        -> bool {
        auto init_account = cbdc::buffer();
        auto ser = cbdc::buffer_serializer(init_account);
        ser << value << m_sequence;
        auto res = put_row(m_broker,
                           m_account_key,
                           init_account,
                           [&, result_callback, value](bool ret) {
                               if(ret) {
                                   m_balance = value;
                               }
                               result_callback(ret);
                           });
        return res;
    }

    auto account_wallet::generate_sk_and_returned_hash() -> std::pair<std::string, std::string> {
        auto rnd = cbdc::random_source(cbdc::config::random_source);
        auto sk = to_string(rnd.random_hash());
        auto unsigned_str = std::vector<std::byte>(sk.length());
        std::memcpy(unsigned_str.data(), sk.c_str(), sk.length());
        std::cout<<"sk len "<<sk.length()<<std::endl;
        auto s_hash = cbdc::hash_data(unsigned_str.data(), unsigned_str.size());
        std::cout<<"s_hash len "<<unsigned_str.size()<<std::endl;
        return {to_string(s_hash), sk};
    }

    auto account_wallet::pay(pubkey_t to,
                             uint64_t amount,
                             const std::function<void(bool)>& result_callback)
        -> bool {
        if(amount > m_balance) {
            return false;
        }
        auto params = make_pay_params(to, amount);
        return execute_params(params, false, result_callback);
    }



    auto account_wallet::get_pubkey() const -> pubkey_t {
        return m_pubkey;
    }

    auto account_wallet::update_balance(
        const std::function<void(bool)>& result_callback) -> bool {
        auto params = make_pay_params(pubkey_t{}, 0);
        return execute_params(params, false, result_callback);
    }

    auto account_wallet::make_pay_params(pubkey_t to, uint64_t amount) const
        -> cbdc::buffer {
        auto params = cbdc::buffer();
        params.append(m_pubkey.data(), m_pubkey.size());
        params.append(to.data(), to.size());
        params.append(&amount, sizeof(amount));
        params.append(&m_sequence, sizeof(m_sequence));

        auto sig_payload = cbdc::buffer();
        sig_payload.append(to.data(), to.size());
        sig_payload.append(&amount, sizeof(amount));
        sig_payload.append(&m_sequence, sizeof(m_sequence));

        auto sha = CSHA256();
        sha.Write(sig_payload.c_ptr(), sig_payload.size());
        auto sighash = cbdc::hash_t();
        sha.Finalize(sighash.data());

        secp256k1_keypair keypair{};
        [[maybe_unused]] auto ret = secp256k1_keypair_create(m_secp.get(),
                                                             &keypair,
                                                             m_privkey.data());

        cbdc::signature_t sig{};
        ret = secp256k1_schnorrsig_sign(m_secp.get(),
                                        sig.data(),
                                        sighash.data(),
                                        &keypair,
                                        nullptr,
                                        nullptr);
        params.append(sig.data(), sig.size());
        return params;
    }

    auto account_wallet::execute_params(
        cbdc::buffer params,
        bool dry_run,
        const std::function<void(bool)>& result_callback) -> bool {
        auto send_success = m_agent->exec(
            m_pay_contract_key,
            std::move(params),
            dry_run,
            [&, result_callback](agent::interface::exec_return_type res) {
                auto success = std::holds_alternative<agent::return_type>(res);
                if(success) {
                    auto updates = std::get<agent::return_type>(res);
                    auto it = updates.find(m_account_key);
                    assert(it != updates.end());
                    auto deser = cbdc::buffer_serializer(it->second);
                    deser >> m_balance >> m_sequence;
                }
                result_callback(success);
            });
        return send_success;
    }

    auto account_wallet::get_balance() const -> uint64_t {
        return m_balance;
    }


    auto account_wallet::init(cbdc::parsec::account_wallet::CBDC_TAG cbdc, uint64_t value,
                const std::function<void(bool)>& result_callback)
        -> bool {
        auto init_account = cbdc::buffer();
        auto ser = cbdc::buffer_serializer(init_account);
        ser << value << m_sequence;

        auto cbdc_tag = tag_to_string(cbdc);
        auto new_account_key(m_account_key);
        new_account_key.append(cbdc_tag.c_str(), cbdc_tag.length());
        
        //std::cout<<"cbdc tag len:  "<<cbdc_tag.length() << " Total key length "<< new_account_key.size()<<std::endl;

        auto res = put_row(m_broker,
                           new_account_key,
                           init_account,
                           [&, result_callback, value](bool ret) {
                               if(ret) {
                                   m_balance = value;
                               }
                               result_callback(ret);
                           });
        return res;
    }

    auto account_wallet::make_pay_params(cbdc::parsec::account_wallet::CBDC_TAG cbdc, pubkey_t to, uint64_t amount, std::string s_hash, uint64_t time_expiry) const
        -> cbdc::buffer {
        auto params = cbdc::buffer();
        auto cbdc_tag = tag_to_string(cbdc);
        params.append(cbdc_tag.c_str(), cbdc_tag.length());
        params.append(m_pubkey.data(), m_pubkey.size());
        params.append(to.data(), to.size());
        params.append(&amount, sizeof(amount));
        params.append(&m_sequence, sizeof(m_sequence));
        params.append(s_hash.c_str(), s_hash.length());
        params.append(&time_expiry, sizeof(time_expiry));
        std::cout<<"Passing params are cbdc1 ("<<cbdc_tag.length()<<"), from ("<<m_pubkey.size()<<") to ("<<to.size()<<"), amount ("<<sizeof(amount)<<") m_sequence "
        <<sizeof(m_sequence)<<", s_hash "<<s_hash.length()<<", timeexpiry "<<sizeof(time_expiry)<<std::endl;
        std::cout<<"Total Len is "<<params.size()<<std::endl;
        return params;
    }

    auto account_wallet::execute_params(
        cbdc::buffer params,
        cbdc::buffer contract_key,
        const std::function<void(agent::interface::exec_return_type)>& result_callback) -> bool {
        
        auto send_success = m_agent->exec(
            contract_key,
            std::move(params),
            false,
            result_callback);
        return send_success;
    }

    auto account_wallet::create_swap_transaction(
        cbdc::buffer contract_key,
        cbdc::parsec::account_wallet::CBDC_TAG cbdc,
        pubkey_t to,
        uint64_t amount,
        uint64_t time_exp,
        std::string s_hash,
        const std::function<void(bool)>& result_callback) -> bool {
        if(amount > m_balance) {
            std::cout << "Swap Init tx Error because of insufficient balance"
                      << std::endl;
            return false;
        }
        auto params = make_pay_params(cbdc, to, amount, s_hash, time_exp);
        return execute_params(
            params,
            contract_key,
            [&, this, result_callback](
                agent::interface::exec_return_type res) {
                auto success = std::holds_alternative<agent::return_type>(res);
                if(success) {
                    auto updates = std::get<agent::return_type>(res);
                    auto new_account_key(m_account_key);
                    auto cbdc_tag = tag_to_string(cbdc);
                    new_account_key.append(cbdc_tag.c_str(),
                                           cbdc_tag.length());
                    auto it = updates.find(new_account_key);
                    if(it != updates.end()) {
                        auto deser = cbdc::buffer_serializer(it->second);
                        deser >> m_balance >> m_sequence;
                        std::cout
                            << "swap transaction is created successfully for "
                            << cbdc_tag << ", and bal: " << m_balance
                            << ", and seq:" << m_sequence << std::endl;
                    } else {
                        std::cout << "Failure " << std::endl;
                    }
                }
                result_callback(success);
            });
    }

    auto account_wallet::execute_swap_contract(
        cbdc::buffer contract_key,
        cbdc::parsec::account_wallet::CBDC_TAG cbdc,
        std::string sk,
        const std::function<void(bool)>& result_callback) -> bool {
        auto params = cbdc::buffer();
        auto cbdc_tag = tag_to_string(cbdc);
        params.append(cbdc_tag.c_str(), cbdc_tag.length());
        params.append(sk.c_str(), sk.length());
        //params.append(m_pubkey.data(), m_pubkey.size());
        return execute_params(
            params,
            contract_key,
            [this, cbdc_tag,result_callback](
                agent::interface::exec_return_type res) {
                auto success = std::holds_alternative<agent::return_type>(res);
                if(success) {
                    auto updates = std::get<agent::return_type>(res);
                    auto new_account_key(m_account_key);
                    new_account_key.append(cbdc_tag.c_str(),
                                           cbdc_tag.length());
                    auto it = updates.find(new_account_key);
                    if (it != updates.end()) {
                        auto deser = cbdc::buffer_serializer(it->second);
                        deser >> m_balance >> m_sequence;
                        std::cout
                            << "Swap Execute transaction is successful for "
                            << cbdc_tag << ", and bal: " << m_balance
                            << ", and seq:" << m_sequence << std::endl;
                    } else {
                        std::cout << "Failure " << std::endl;
                    }
                }
                result_callback(success);
            });
    }

    auto account_wallet::get_secret_key(
        cbdc::buffer contract_key,
        cbdc::parsec::account_wallet::CBDC_TAG cbdc,
        std::string s_hash,
        const std::function<void(std::string)>& result_callback) -> bool {
        auto params = cbdc::buffer();
        auto cbdc_tag = tag_to_string(cbdc);
        params.append(cbdc_tag.c_str(), cbdc_tag.length());
        params.append(s_hash.c_str(), s_hash.length());
        return execute_params(
            params,
            contract_key,
            [cbdc_tag, s_hash, result_callback](
                agent::interface::exec_return_type res) {
                auto success = std::holds_alternative<agent::return_type>(res);
                if(success) {
                    auto updates = std::get<agent::return_type>(res);
                    auto new_account_key = cbdc::buffer();
                    /** Important note: Key Format is known to User or wallet **/
                    auto strk = std::string("swap_"); 
                    new_account_key.append(strk.c_str(),
                                           strk.length());
                    new_account_key.append(cbdc_tag.c_str(),
                                           cbdc_tag.length());
                    new_account_key.append(s_hash.c_str(),
                                           s_hash.length());                       
                    auto it = updates.find(new_account_key);
                    if (it != updates.end()) {
                        auto sk = std::string(it->second.c_str());
                        std::cout
                            << "get Secret  transaction is successful for "
                            << cbdc_tag << ", "<<sk<<std::endl;
                        result_callback(sk);
                    } else {
                        std::cout << "Failure " << std::endl;
                        result_callback(std::string());
                    }
                } else 
                result_callback(std::string());
            });
    }
}
