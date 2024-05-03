#include "tx_leveldb.hpp"
#include <leveldb/db.h>
#include <string>

using namespace leveldb;
using namespace std;
using namespace cbdc::sentinel_2pc;

// LevelDBHandler class implementation
LevelDBHandler::LevelDBHandler(const string& dbPath, shared_ptr<logging::log> logger) : m_logger(logger) {
    // Open the LevelDB database
    m_opt.create_if_missing = true;
    Status status = DB::Open(m_opt, dbPath, &m_db);
    if (!status.ok()) {
        m_logger->error("Failed to open LevelDB database with path: ", dbPath, " error:", status.ToString());
    }
    else {
        m_isOk = true;
        m_logger->info("LevelDB instance created in the folder " + dbPath);
    }
}

LevelDBHandler::~LevelDBHandler() {
    countRecords();
    if(m_logger) m_logger.reset();
    if(m_db && m_isOk) delete m_db;
}

// Write a record to the database
bool LevelDBHandler::writeRec(const string& key, const string& rec) {
    if(!isOk()) return false;

    Status status = m_db->Put(m_write_opt, key, rec);

    if (!status.ok()) {
        m_logger->error("Failed to write record to LevelDB database: " + status.ToString());
        return false;
    }
    m_logger->trace("Added record to LevelDB. Key", key);
    return true;
}

// Read a record from the database
bool LevelDBHandler::readRec(const string& key, string& rec) {
    if(!isOk()) return false;
    string value;
    ReadOptions readOptions;
    Status status = m_db->Get(m_read_opt, key, &rec);
    if (status.IsNotFound()) {
        m_logger->trace("Record not found. Key: " + key);        
        return false;
    }
    if (!status.ok()) {
        m_logger->error("Failed to read record from LevelDB database: " + status.ToString());
        return false;
    }
    // Copy the value to the buffer
    return true;
}

// Delete record from the DB
bool LevelDBHandler::deleteRec(const string& key) {
    if(!isOk()) return false;
    Status status = m_db->Delete(m_write_opt, key);
    if (!status.ok()) {
        m_logger->error("Failed to delete record from LevelDB database: " + status.ToString());
        return false;
    }
    return true;
}

// Delete record with key beginning with prefix from the DB
unsigned int LevelDBHandler::deleteRecByPrefix(const std::string& prefix) {
    if(!isOk()) return 0;
    unsigned int ret = 0;
    Iterator* it = m_db->NewIterator(m_read_opt);
    for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix); it->Next()) {
        m_db->Delete(m_write_opt, it->key());
        ++ret;
    }
    delete it;
    return ret;
}

unsigned int LevelDBHandler::countRecords() {
    if(!isOk()) return 0;

    leveldb::Iterator* it = m_db->NewIterator(leveldb::ReadOptions());
    unsigned int recordCount = 0;
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        recordCount++;
    }
    delete it;
    m_logger->info("Number of records in this LevelDB instance: ", recordCount);
    return recordCount;
}
