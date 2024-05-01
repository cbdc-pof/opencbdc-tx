#include "tx_db.hpp"
#include "tx_leveldb.hpp"
#include "tx_keyspacesdb.hpp"
#include "tx_history.hpp"
#include <string>

using namespace std;
using namespace cbdc::sentinel_2pc;

unique_ptr<DBHandler> DBHandler::createDBHandler(const string& dbType, 
                    const string& db_param, 
                    shared_ptr<logging::log> logger,
                    uint32_t sentinel_id) {
    if (dbType == "Keyspaces") {
        logger->error("Keyspaces DB is not supported yet");
        return nullptr;
    ///        return make_unique<KeyspacesDBHandler>(db_param, logger);
    }
    else {  /// LevelDB is default
        stringstream ss;
        ss << db_param << "_" << sentinel_id;
        return make_unique<LevelDBHandler>(ss.str(), logger);
    }
}
