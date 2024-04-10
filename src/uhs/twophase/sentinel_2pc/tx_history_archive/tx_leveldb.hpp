#ifndef OPENCBDC_TX_SRC_SENTINEL_2PC_TX_LEVELDB_H_
#define OPENCBDC_TX_SRC_SENTINEL_2PC_TX_LEVELDB_H_

#include "tx_db.hpp"
#include <leveldb/db.h>

namespace cbdc::sentinel_2pc {

// LevelDB implementation of DBHandler class
class LevelDBHandler : public cbdc::sentinel_2pc::DBHandler {
public:
    LevelDBHandler(const std::string& dbPath, std::shared_ptr<logging::log> logger);
    virtual ~LevelDBHandler();

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
    leveldb::DB* m_db;
    leveldb::Options m_opt;
    leveldb::WriteOptions m_write_opt;
    leveldb::ReadOptions m_read_opt;
    std::shared_ptr<logging::log> m_logger;
    bool m_isOk {false};
};

}

#endif /// OPENCBDC_TX_SRC_SENTINEL_2PC_TX_LEVELDB_H_