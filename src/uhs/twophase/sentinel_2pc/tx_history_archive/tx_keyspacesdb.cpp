#include "tx_keyspacesdb.hpp"
#include "tx_history.hpp"

#include <string>

using namespace std;
using namespace cbdc::sentinel_2pc;

// KeyspacesDBHandler class implementation
KeyspacesDBHandler::KeyspacesDBHandler(const config::options& opts, shared_ptr<logging::log> logger, 
        uint32_t sentinelId) : m_logger(logger) {

    if(sentinelId == INVALID_SENTINEL_ID) {
        m_isOk = false;
        return;
    }

// Create and configure the cluster
    m_cluster = cass_cluster_new();
    m_session = cass_session_new();

    // Add contact points (IP addresses of Keyspaces nodes)
    cass_cluster_set_contact_points(m_cluster, opts.m_tha_parameter.c_str());
    cass_cluster_set_port(m_cluster, opts.m_tha_port); //Connect using TLS protocol

    if(strcasecmp(opts.m_tha_ssl_version.c_str(), "none") == 0) {
        m_logger->info("Don't use SSL for Keyspaces connection");
    } else {
        auto sslVer = CASS_SSL_VERSION_TLS1_2;
        if(strcasecmp(opts.m_tha_ssl_version.c_str(), "TLS1") == 0) {
            sslVer = CASS_SSL_VERSION_TLS1;
        } else if(strcasecmp(opts.m_tha_ssl_version.c_str(), "TLS1_1") == 0) {
            sslVer = CASS_SSL_VERSION_TLS1_1;
        } else if(strcasecmp(opts.m_tha_ssl_version.c_str(), "TLS1_2") == 0) {
            sslVer = CASS_SSL_VERSION_TLS1_2;
        } else {
            m_logger->warn("Unsupported version of SSL library ", opts.m_tha_ssl_version, 
                " specified. Supported are TLS1, TLS1_1, TLS1_2(default) or 'none' to NOT use SSL");
        }
        m_logger->info("Use SSL for Keyspaces connection with TLSv1.", sslVer);

        // Enable SSL/TLS
        CassSsl* ssl = cass_ssl_new();
        cass_ssl_set_verify_flags(ssl, CASS_SSL_VERIFY_NONE); // Disable peer certificate verification
        cass_ssl_set_min_protocol_version (ssl, CASS_SSL_VERSION_TLS1_2);
        cass_cluster_set_ssl(m_cluster, ssl);
    }

    // Specify username/password
    cass_cluster_set_credentials(m_cluster, opts.m_tha_user.c_str(), opts.m_tha_password.c_str()); 

    // Connect to the cluster
    CassFuture* connect_future = cass_session_connect(m_session, m_cluster);

    // Wait for the connection to establish
    /////   cass_future_wait(connect_future);

    // Check connection status
    if (cass_future_error_code(connect_future) == CASS_OK) {
        std::cout << "Connected to Keyspaces cluster successfully!" << std::endl;
        m_isOk = true;
    } else {
        // Handle connection error
        const char* error_message;
        size_t error_message_length;
        cass_future_error_message(connect_future, &error_message, &error_message_length);
        std::cerr << "Connection error: " << std::string(error_message, error_message_length) << std::endl;
        m_isOk = false;
    }

    // Clean up resources
    cass_future_free(connect_future);
}

KeyspacesDBHandler::~KeyspacesDBHandler() {
    if(m_logger) m_logger.reset();
    if (m_session) cass_session_free(m_session);
    if (m_cluster) cass_cluster_free(m_cluster);
}

// Write a record to the database
bool KeyspacesDBHandler::writeRec(const string& key, const string& rec) {
    bool ret = false;
    if(!isOk()) return ret;

    const string binTx = tx_history_archiver::mem_to_hex_str(rec.c_str(), rec.size(), "0x");

    string command = "INSERT INTO " + tx_table_name + " (" + tx_key_column_name + "," 
        + tx_data_column_name + ") VALUES ('" + key + "'," + binTx + ")";

    CassResult*  res = executeCommand(command);
    if( res) {
        cass_result_free(res);
        ret = true;
    }
    else {
        m_logger->error("Failed to write the record with the key: ", key);
    }
    m_logger->trace("Added to DB record with key", key);
    return true;
}

// Read a record from the database
bool KeyspacesDBHandler::readRec(const string& key, string& rec) {
    if(!isOk()) return false;

    const string command = "SELECT " + tx_data_column_name + " FROM " + tx_table_name 
        + " WHERE " + tx_key_column_name + "='" + key + "'";

    CassResult*  res = executeCommand(command);
    if( !res ) {
        m_logger->error("Failed to read the record with the key: ", key);
        return false;
    }

    const CassRow* row = cass_result_first_row(res);

    if( !row ) {
        m_logger->trace("Record not found. Key: ", key);        
        return false;
    }

    const CassValue* value = cass_row_get_column(row, 0);
    if (cass_value_is_null(value)) {
        m_logger->error("Got empty record for key: ", key);
        return false;
    }

    // Extract blob data as bytes
    const cass_byte_t* bytes;
    size_t length;
    cass_value_get_bytes(value, &bytes, &length);

    rec = tx_history_archiver::mem_to_str_mem(reinterpret_cast<const char*>(bytes), length);

    cass_result_free(res);
    return true;
}

// Delete record from the DB
bool KeyspacesDBHandler::deleteRec(const string& key) {
    unsigned int ret = false;
    if(!isOk()) return ret;
    const string command = "DELETE FROM " + tx_table_name + " WHERE " + tx_key_column_name + "=\"" + key + "\";";
    CassResult*  res = executeCommand(command);
    if( res) {
        cass_result_free(res);
        ret = true;
    }
    else {
        m_logger->error("Failed to delete record from Keyspaces database: ", key.size());
    }
    return ret;
}

// Delete record with key beginning with prefix from the DB
unsigned int KeyspacesDBHandler::deleteRecByPrefix(const std::string& prefix) {
    unsigned int ret = 0;
    if(!isOk()) return ret;

    const string delTxCommand = "DELETE FROM " + tx_table_name + " WHERE " + tx_key_column_name + "=\'" + prefix + "\';";
    CassResult*  res = executeCommand(delTxCommand);
    if( res) {
        cass_result_free(res);
        ret = 1;
    }

    for (uint8_t status = 0; status <= (uint8_t)tx_state::execution_failed; ++status) {
        const string delStatusCommand = "DELETE FROM " + tx_table_name + " WHERE " + tx_key_column_name + "=\'" + prefix + "-" + std::to_string(status) + "\';";
        res = executeCommand(delStatusCommand);
        if( res) {
            cass_result_free(res);
            ++ret;
        }
    }

    return ret;
}

CassResult*  KeyspacesDBHandler::executeCommand(const std::string& command) {

    CassResult* result = nullptr;
    if(command.empty()) return result;

    // Prepare statement for the query
    CassStatement* statement = cass_statement_new(command.c_str(), 0);
cass_statement_set_consistency(statement, CASS_CONSISTENCY_LOCAL_QUORUM);    

    // Execute the query
    CassFuture* result_future = cass_session_execute(m_session, statement);

    // Wait for the future to complete
    cass_future_wait(result_future);

    // Check the result
    const size_t MAX_PRINT_SIZE = 150;
    if (cass_future_error_code(result_future) == CASS_OK) {
        m_logger->trace("DB Command executed successfully ", command.substr(0, min(command.length(), MAX_PRINT_SIZE)).c_str(), "...");

        // Process the result (retrieve and print data)
        result = (CassResult*) cass_future_get_result(result_future);
    } else {
        // Handle query execution error
        const char* error_message;
        size_t error_message_length;
        cass_future_error_message(result_future, &error_message, &error_message_length);
        string errStr(error_message, error_message_length);
        m_logger->error("Command ", command.c_str(), " failed with error ", errStr.c_str());
    }

    cass_statement_free(statement);
    cass_future_free(result_future);
    return result;
}
