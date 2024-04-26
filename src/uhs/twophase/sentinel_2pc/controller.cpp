// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "controller.hpp"

#include "uhs/twophase/coordinator/format.hpp"
#include "util/rpc/tcp_server.hpp"
#include "util/serialization/util.hpp"
#include "tx_history_archive/tx_leveldb.hpp"
#include "tx_history_archive/tx_keyspacesdb.hpp"
#include <utility>

namespace cbdc::sentinel_2pc {
    controller::controller(uint32_t sentinel_id,
                           const config::options& opts,
                           std::shared_ptr<logging::log> logger)
        : m_sentinel_id(sentinel_id),
          m_opts(opts),
          m_logger(std::move(logger)),
          m_tha(sentinel_id, opts),
          m_coordinator_client(
              opts.m_coordinator_endpoints[sentinel_id
                                           % static_cast<uint32_t>(
                                               opts.m_coordinator_endpoints
                                                   .size())]) {}

    auto controller::init() -> bool {
        if(m_opts.m_sentinel_endpoints.empty()) {
            m_logger->error("No sentinel endpoints are defined.");
            return false;
        }

        if(m_sentinel_id >= m_opts.m_sentinel_endpoints.size()) {
            m_logger->error(
                "The sentinel ID is too large for the number of sentinels.");
            return false;
        }

        auto skey = m_opts.m_sentinel_private_keys.find(m_sentinel_id);
        if(skey == m_opts.m_sentinel_private_keys.end()) {
            if(m_opts.m_attestation_threshold > 0) {
                m_logger->error("No private key specified");
                return false;
            }
        } else {
            m_privkey = skey->second;

            auto pubkey = pubkey_from_privkey(m_privkey, m_secp.get());
            m_logger->info("Sentinel public key:", cbdc::to_string(pubkey));
        }

        auto retry_delay = std::chrono::seconds(1);
        auto retry_threshold = 4;
        while(!m_coordinator_client.init() && retry_threshold-- > 0) {
            m_logger->warn("Failed to start coordinator client.");

            std::this_thread::sleep_for(retry_delay);
            if(retry_threshold > 0) {
                retry_delay *= 2;
                m_logger->warn("Retrying...");
            }
        }

        for(const auto& ep : m_opts.m_sentinel_endpoints) {
            if(ep == m_opts.m_sentinel_endpoints[m_sentinel_id]) {
                continue;
            }
            auto client = std::make_unique<sentinel::rpc::client>(
                std::vector<network::endpoint_t>{ep},
                m_logger);
            if(!client->init(false)) {
                m_logger->warn("Failed to start sentinel client");
            }
            m_sentinel_clients.emplace_back(std::move(client));
        }

        constexpr size_t dist_lower_bound = 0;
        const size_t dist_upper_bound
            = m_sentinel_clients.empty() ? 0 : m_sentinel_clients.size() - 1;
        m_dist = decltype(m_dist)(dist_lower_bound, dist_upper_bound);

        auto rpc_server = std::make_unique<cbdc::rpc::tcp_server<
            cbdc::rpc::async_server<cbdc::sentinel::request,
                                    cbdc::sentinel::response>>>(
            m_opts.m_sentinel_endpoints[m_sentinel_id]);
        if(!rpc_server->init()) {
            m_logger->error("Failed to start sentinel RPC server");
            return false;
        }

        m_rpc_server = std::make_unique<decltype(m_rpc_server)::element_type>(
            this,
            std::move(rpc_server));

        return true;
    }

    auto controller::execute_transaction(
        transaction::full_tx tx,
        execute_result_callback_type result_callback) -> bool {
        auto tx_id = transaction::tx_id(tx);

        m_logger->trace("Tx status set to initial", to_string(tx_id));
        m_tha.add_transaction(tx);
        const auto validation_err = transaction::validation::check_tx(tx);
        if(validation_err.has_value()) {
            m_logger->debug(
                "Rejected, validation_failed status (",
                transaction::validation::to_string(validation_err.value()),
                ")",
                to_string(tx_id));
            result_callback(cbdc::sentinel::execute_response{
                cbdc::sentinel::tx_status::static_invalid,
                validation_err});
            return true;
        }

        auto compact_tx = cbdc::transaction::compact_tx(tx);

        if(m_opts.m_attestation_threshold > 0) {
            auto attestation = compact_tx.sign(m_secp.get(), m_privkey);
            compact_tx.m_attestations.insert(attestation);
        }

        gather_attestations(tx, std::move(result_callback), compact_tx, {});

        return true;
    }

    void
    controller::result_handler(std::optional<bool> res,
                               const execute_result_callback_type& res_cb,
                               hash_t ctx_id,
                               controller* ctrlInst) {
        if(res.has_value()) {
            auto resp = cbdc::sentinel::execute_response{
                cbdc::sentinel::tx_status::confirmed,
                std::nullopt};
            if(!res.value()) {
                resp.m_tx_status = cbdc::sentinel::tx_status::state_invalid;
                if(ctrlInst) {
                    ctrlInst->m_tha.set_status(ctx_id, tx_state::execution_failed);
                    ctrlInst->m_logger->error("Execution failed tx", to_string(ctx_id));
                }
            }
            else 
            if(ctrlInst) {
                    ctrlInst->m_tha.set_status(ctx_id, tx_state::completed);
                    ctrlInst->m_logger->trace("Completed tx", to_string(ctx_id));
            }
            res_cb(resp);
        } else {
            if(ctrlInst) {
                ctrlInst->m_tha.set_status(ctx_id, tx_state::unknown);
                ctrlInst->m_logger->trace("Unknown status for tx", to_string(ctx_id));
            }
            res_cb(std::nullopt);
        }
    }

    auto controller::validate_transaction(
        transaction::full_tx tx,
        validate_result_callback_type result_callback) -> bool {
        const auto validation_err = transaction::validation::check_tx(tx);
        auto tx_id = transaction::tx_id(tx);
        if(validation_err.has_value()) {
            result_callback(std::nullopt);
            m_logger->debug("Tx status: validation_failed", to_string(tx_id));
	    m_tha.set_status(tx_id, tx_state::validation_failed);
            return true;
        }
        auto compact_tx = cbdc::transaction::compact_tx(tx);
        auto attestation = compact_tx.sign(m_secp.get(), m_privkey);
        result_callback(std::move(attestation));
        return true;
    }

    void controller::validate_result_handler(
        validate_result v_res,
        const transaction::full_tx& tx,
        execute_result_callback_type result_callback,
        transaction::compact_tx ctx,
        std::unordered_set<size_t> requested) {
        if(!v_res.has_value()) {
            m_logger->error(to_string(ctx.m_id),
                            "invalid (Tx status: validation_failed) according to remote sentinel");
	    m_tha.set_status(ctx.m_id, tx_state::validation_failed);
            result_callback(std::nullopt);
            return;
        }
        ctx.m_attestations.insert(std::move(v_res.value()));
        gather_attestations(tx,
                            std::move(result_callback),
                            ctx,
                            std::move(requested));
    }

    void controller::gather_attestations(
        const transaction::full_tx& tx,
        execute_result_callback_type result_callback,
        const transaction::compact_tx& ctx,
        std::unordered_set<size_t> requested) {
        if(ctx.m_attestations.size() < m_opts.m_attestation_threshold) {
            auto success = false;
            while(!success) {
                auto sentinel_id = m_dist(m_rand);
                if(requested.find(sentinel_id) != requested.end()) {
                    continue;
                }
                success
                    = m_sentinel_clients[sentinel_id]->validate_transaction(
                        tx,
                        [=, this](validate_result v_res) {
                            auto r = requested;
                            r.insert(sentinel_id);
                            validate_result_handler(v_res,
                                                    tx,
                                                    result_callback,
                                                    ctx,
                                                    r);
                        });
            }
            return;
        }

        m_logger->debug("Accepted (tx status: validated)", to_string(ctx.m_id));
        m_tha.set_status(ctx.m_id, tx_state::validated);
        send_compact_tx(ctx, std::move(result_callback));
    }

    void
    controller::send_compact_tx(const transaction::compact_tx& ctx,
                                execute_result_callback_type result_callback) {
        auto cb =
            [&, res_cb = std::move(result_callback), ctx_id = ctx.m_id](std::optional<bool> res) {
                result_handler(res, res_cb, ctx_id, this);
            };

        // TODO: add a "retry" error response to offload sentinels from this
        //       infinite retry responsibility.
        while(!m_coordinator_client.execute_transaction(ctx, cb)) {
            // TODO: the network currently doesn't provide a callback for
            //       reconnection events so we have to sleep here to
            //       prevent a needless spin. Instead, add such a callback
            //       or queue to the network to remove this sleep.
            static constexpr auto retry_delay = std::chrono::milliseconds(100);
            std::this_thread::sleep_for(retry_delay);
        };
        m_logger->trace("Tx status: execution", to_string(ctx.m_id));
        m_tha.set_status(ctx.m_id, tx_state::execution);
    }
}
