#ifndef OPENCBDC_TX_SRC_SENTINEL_2PC_TX_KEYSPACESDB_H_
#define OPENCBDC_TX_SRC_SENTINEL_2PC_TX_KEYSPACESDB_H_

#include "tx_db.hpp"
#include "util/common/config.hpp"
#include "../keyspaces/datastax/include/cassandra.h"
#include <string>

namespace cbdc::sentinel_2pc {

const std::string tx_table_name ("bocopencbdc.txs");
const std::string tx_key_column_name ("tx_key");
const std::string tx_data_column_name ("tx");
const std::string tx_confirm_key("confirm");

// Keyspaces implementation of DBHandler class
class KeyspacesDBHandler : public DBHandler {
public:
    KeyspacesDBHandler(const config::options& opts, std::shared_ptr<logging::log> logger, uint32_t sentinelId);
    virtual ~KeyspacesDBHandler();

    // Write a record to the database
    virtual bool writeRec(const std::string& key, const std::string& rec) override;

    // Read a record from the database
    virtual bool readRec(const std::string& key, std::string& rec) override;

    // Delete record from the database
    virtual bool deleteRec(const std::string& key) override;

    // Delete record with key beginning with prefix from the DB
    virtual unsigned int deleteRecByPrefix(const std::string& prefix) override;

    // Get DB instance status
    virtual bool isOk() { return m_isOk; }

private:
    CassResult*  executeCommand(const std::string& command);
    CassCluster* m_cluster {nullptr};
    CassSession* m_session {nullptr};
    std::shared_ptr<logging::log> m_logger;
    bool m_isOk {false};
};

}

#endif /// OPENCBDC_TX_SRC_SENTINEL_2PC_TX_KEYSPACESDB_H_
