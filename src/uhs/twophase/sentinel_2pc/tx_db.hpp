#ifndef OPENCBDC_TX_SRC_SENTINEL_2PC_TX_DB_H_
#define OPENCBDC_TX_SRC_SENTINEL_2PC_TX_DB_H_

#include "util/common/config.hpp"
#include <leveldb/db.h>
#include <string>

namespace cbdc::sentinel_2pc {

// Abstract interface for a key-value database
class DBHandler {
public:
    virtual ~DBHandler() = default;

    // Write a record to the database
    virtual bool writeRec(const std::string& key, const std::string& rec) = 0;

    // Read a record from the database
    virtual bool readRec(const std::string& txId, std::string& rec) = 0;

    // Delete record from the DB
    virtual bool deleteRec(const std::string& key) = 0;

    // Delete record with key beginning with prefix from the DB
    virtual unsigned int deleteRecByPrefix(const std::string& prefix) = 0;

    // Get DB instance status
    virtual bool isOk() = 0;

    // Factory method to create a DBHandler instance based on configuration
    static std::unique_ptr<DBHandler> createDBHandler(const std::string& dbType, const std::string& dbParam, std::shared_ptr<logging::log> logger);
};

// LevelDB implementation of DBHandler class
// TBD: Move it to a separate header file
class LevelDBHandler : public DBHandler {
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

#endif /// OPENCBDC_TX_SRC_SENTINEL_2PC_TX_DB_H_