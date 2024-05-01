#ifndef OPENCBDC_TX_SRC_SENTINEL_2PC_TX_DB_H_
#define OPENCBDC_TX_SRC_SENTINEL_2PC_TX_DB_H_

#include "util/common/config.hpp"
#include <string>

namespace cbdc::sentinel_2pc {

class LevelDBHandler;
class KeyspacesDBHandler;

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
    static std::unique_ptr<DBHandler> createDBHandler(const std::string& dbType, const std::string& dbParam, std::shared_ptr<logging::log> logger, uint32_t sentinel_id);
};

}

#endif /// OPENCBDC_TX_SRC_SENTINEL_2PC_TX_DB_H_