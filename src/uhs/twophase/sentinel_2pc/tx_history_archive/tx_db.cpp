#include "tx_db.hpp"
#include "tx_leveldb.hpp"
#include "tx_keyspacesdb.hpp"
#include "tx_history.hpp"
#include <string>

using namespace std;
using namespace cbdc::sentinel_2pc;

unique_ptr<DBHandler> DBHandler::createDBHandler(const config::options& opts, 
                    shared_ptr<logging::log> logger, 
                    uint32_t sentinel_id) {
    const string dbType = opts.m_tha_type;
    if (dbType == "leveldb") {  
        return make_unique<LevelDBHandler>(opts, logger, sentinel_id);
    } else { // Default is Keyspaces
	return make_unique<KeyspacesDBHandler>(opts, logger, sentinel_id);
    }	
}
