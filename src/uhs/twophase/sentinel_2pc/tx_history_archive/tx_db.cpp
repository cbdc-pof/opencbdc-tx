#include "tx_db.hpp"
#include "tx_leveldb.hpp"
#include "tx_keyspacesdb.hpp"
#include "tx_history.hpp"
#include <string>

using namespace std;
using namespace cbdc::sentinel_2pc;

unique_ptr<DBHandler> DBHandler::createDBHandler(const string& dbType, 
                    const string& db_param, 
                    shared_ptr<logging::log> logger) {
    if (dbType == "levelDB") {
        return make_unique<LevelDBHandler>(db_param, logger);
    }
    if (dbType == "Keyspaces") {
        logger->error("Keyspaces DB is not supported yet");
        return nullptr;
    ///        return make_unique<KeyspacesDBHandler>(db_param, logger);
    }
    // Add more conditions for other database types if needed
    else {
        // Default to LevelDB if dbType is unrecognized
        return make_unique<LevelDBHandler>(db_param, logger);
    }
}
