#include "tx_history.hpp"
#include "util/common/hash.hpp"
#include <string>
#include <chrono>
#include <ctime>

using namespace cbdc::sentinel_2pc;
using namespace cbdc::transaction;
using namespace std;

auto getMSSinceEpoch() -> uint64_t {
    auto now = chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return chrono::duration_cast<chrono::milliseconds>(duration).count();
}

// Construct tx_history_archiver and create/connect to DB in case initialize parameter is true
tx_history_archiver::tx_history_archiver(uint32_t sentinel_id,
                  const config::options& opts) : m_sentinel_id(sentinel_id) {

    uint32_t loglevels_size = (uint32_t)opts.m_sentinel_loglevels.size(); 
    if(loglevels_size == 0) { // Logger cannot be defined
        m_sentinel_id = INVALID_SENTINEL_ID;
        return;
    }

    m_logger = std::make_shared<cbdc::logging::log>(opts.m_sentinel_loglevels[min(sentinel_id, loglevels_size - 1)]);
    if(opts.m_tha_type == "none") {
        m_logger->info("tha_type parameter set to 'none'. THA functionality disabled.");
        m_sentinel_id = INVALID_SENTINEL_ID;
        return;
    }

    m_logger->info("THA config: Type:", opts.m_tha_type, "Parameter: ", opts.m_tha_parameter, "Port:", opts.m_tha_port,
        "User:", opts.m_tha_user, "Password:", opts.m_tha_password, "SSL:", opts.m_tha_ssl_version);

    m_db = DBHandler::createDBHandler(opts, m_logger, sentinel_id);
    m_logger->info("Initialize THA with sentinelId", std::to_string(sentinel_id), "Create TxHistory DB in folder", opts.m_tha_parameter.c_str());
}

auto tx_history_archiver::add_transaction(cbdc::transaction::full_tx tx) -> bool{
    if(m_sentinel_id == INVALID_SENTINEL_ID) return false;
    hash_t txId = transaction::tx_id(tx);
    string key = mem_to_hex_str (txId.data(), txId.size());

    // Create buffer with timestamp
    uint64_t ms = getMSSinceEpoch();
    string txData = tx_to_str_mem(tx, ms);

    return m_db->writeRec(key, txData);
}

auto tx_history_archiver::set_status(const hash_t& txid, const tx_state new_status) -> bool {
    if(m_sentinel_id == INVALID_SENTINEL_ID) return false;
    string strKey = mem_to_hex_str(txid.data(), txid.size());
    string key = build_status_key(strKey, new_status);

    uint64_t ms = getMSSinceEpoch();
    string tsStr = mem_to_str_mem(&ms, sizeof(uint64_t));

    return m_db->writeRec(key, tsStr);
}

auto tx_history_archiver::get_transaction_by_hash(const hash_t& hashKey, tx_state& last_status, cbdc::transaction::full_tx& tx, uint64_t& timestamp) -> bool {
    string key = mem_to_hex_str(hashKey.data(), hashKey.size());
    return get_transaction(key, last_status, tx, timestamp);
}

auto tx_history_archiver::get_transaction(const string& txid, tx_state& last_status, cbdc::transaction::full_tx& tx, uint64_t& timestamp) -> bool {
    if(m_sentinel_id == INVALID_SENTINEL_ID) return false;
    string valueBuf;

    if(!m_db->readRec(txid, valueBuf)) {
        m_logger->info("Transaction not found", txid);
        return false;
    }

    last_status = tx_state::initial;
    str_mem_to_tx(valueBuf, tx, timestamp);

    // Go through txId-status records in the DB to find the latest status
    if(get_status_rec(txid, tx_state::completed, last_status, timestamp)) return true;
    if(get_status_rec(txid, tx_state::execution_failed, last_status, timestamp)) return true;
    if(get_status_rec(txid, tx_state::validation_failed, last_status, timestamp)) return true;
    if(get_status_rec(txid, tx_state::execution, last_status, timestamp)) return true;
    if(get_status_rec(txid, tx_state::validated, last_status, timestamp)) return true;
    if(get_status_rec(txid, tx_state::unknown, last_status, timestamp)) return true;

    return true;
}

// Read a separate TX status record from DB
auto tx_history_archiver::get_status_rec(const string& txid, const tx_state test_status, tx_state& actual_status, uint64_t& timestamp) -> bool {
    if(m_sentinel_id == INVALID_SENTINEL_ID) return false;
    string statusKeyBuf = build_status_key(txid, test_status);
    string valueBuf;

    if(!m_db->readRec(statusKeyBuf, valueBuf)) {
        return false;
    }
    m_logger->info("Found transaction ", statusKeyBuf);
    actual_status = test_status;
    timestamp = *((uint64_t *)(valueBuf.data()));
    return true;
}

auto tx_history_archiver::delete_transaction_by_hash(const hash_t& hashKey) -> unsigned int {
    string key = mem_to_hex_str(hashKey.data(), hashKey.size());
    return delete_transaction(key);
}

auto tx_history_archiver::delete_transaction(const string & txidStr) -> unsigned int {
    return m_db->deleteRecByPrefix(txidStr);
}

// Build DB key used for separate TX status record
auto tx_history_archiver::build_status_key(const string& txid, tx_state status) -> string {
    string keyBuf = txid;
    keyBuf.append("-", 1);
    char statByte = std::to_string((int)status)[0]; 
    keyBuf.append(&statByte, sizeof(char));
    return keyBuf;
}

// Serialize TX to a memory wrapped into string object
auto tx_history_archiver::tx_to_str_mem(const cbdc::transaction::full_tx& tx, const uint64_t timestamp) -> string {
    size_t output_size = cbdc::hash_size + sizeof(uint64_t);
    size_t outputs_num = tx.m_outputs.size();
    size_t out_point_size = cbdc::hash_size + sizeof(uint64_t);
    size_t inputs_num = tx.m_inputs.size();
    size_t input_size = output_size + out_point_size;
    size_t witness_num = tx.m_witness.size();
    size_t witness_size = 0;
    for(auto w: tx.m_witness) witness_size += w.size() + sizeof(size_t); // For exevry witness add signature size + witness signature itself
    size_t tx_size = sizeof(uint64_t) + // timestamp
                     sizeof(size_t) + // # of inputs
                     inputs_num * input_size + // inputs
                     sizeof(size_t) + // # of outputs
                     outputs_num * output_size + // outputs
                     sizeof(size_t) + // # of witnesses
                     witness_size; // witnesses (don't have to multiply)

    string ret(tx_size, 0);
    unsigned char *ptr = (unsigned char *)ret.c_str();

    // Add timestamp
    append_mem(ptr, (unsigned char *)&timestamp, sizeof(uint64_t));

    // Add inputs
    append_mem(ptr, (unsigned char *)&inputs_num, sizeof(size_t));
    for(auto txIn: tx.m_inputs) {
        append_mem(ptr, (unsigned char *)txIn.m_prevout.m_tx_id.data(), cbdc::hash_size); // add prev_out (of type out_point)
        append_mem(ptr, (unsigned char *)&(txIn.m_prevout.m_index), sizeof(uint64_t));
        append_mem(ptr, (unsigned char *)txIn.m_prevout_data.m_witness_program_commitment.data(), cbdc::hash_size); // add prev_out_data (of type output) 
        append_mem(ptr, (unsigned char *)&txIn.m_prevout_data.m_value, sizeof(uint64_t)); 
    }

    // Add outputs
    append_mem(ptr, (unsigned char *)&outputs_num, sizeof(size_t));
    for(auto txOut: tx.m_outputs) {
        append_mem(ptr, (unsigned char *)txOut.m_witness_program_commitment.data(), cbdc::hash_size);
        append_mem(ptr, (unsigned char *)&txOut.m_value, sizeof(uint64_t)); 
    }

    // Add witnesses
    append_mem(ptr, (unsigned char *)&witness_num, sizeof(size_t));
    for(auto txWitness: tx.m_witness) {
        size_t cur_wit_size = txWitness.size();
        append_mem(ptr, (unsigned char *)&cur_wit_size, sizeof(size_t));
        append_mem(ptr, (unsigned char *)txWitness.data(), cur_wit_size);
    }
    return ret;
}

// Deserialize TX from the memory (which is wrapped into string object)
auto tx_history_archiver::str_mem_to_tx(std::string& in_buffer, cbdc::transaction::full_tx& tx, uint64_t& timestamp) -> bool {
    unsigned char* ptr = (unsigned char*)in_buffer.c_str();
    size_t len = in_buffer.size();
    timestamp = *mem_to_ptr<uint64_t>(ptr, len);
    size_t* inputs_num_ptr = mem_to_ptr<size_t>(ptr, len);
    if(!inputs_num_ptr) return false;

    // Read inputs
    for(size_t i = 0; i < *inputs_num_ptr; ++i) {
        input txIn;
        txIn.m_prevout.m_tx_id = *mem_to_ptr<hash_t>(ptr, len);
        txIn.m_prevout.m_index = *mem_to_ptr<uint64_t>(ptr, len);
        txIn.m_prevout_data.m_witness_program_commitment = *mem_to_ptr<hash_t>(ptr, len);
        txIn.m_prevout_data.m_value = *mem_to_ptr<uint64_t>(ptr, len);
        tx.m_inputs.push_back(txIn);
    }

    // Read outputs
    size_t* outputs_num_ptr = mem_to_ptr<size_t>(ptr, len);
    for(size_t i = 0; i < *outputs_num_ptr; ++i) {
        output txOut;
        txOut.m_witness_program_commitment = *mem_to_ptr<hash_t>(ptr, len);
        txOut.m_value = *mem_to_ptr<uint64_t>(ptr, len);
        tx.m_outputs.push_back(txOut);
    }

    // Read witnesses
    size_t* witness_num_ptr = mem_to_ptr<size_t>(ptr, len);
    for(size_t i = 0; i < *witness_num_ptr; ++i) {
        size_t* cur_witness_sz = mem_to_ptr<size_t>(ptr, len);
        byte* witness_ptr = mem_to_ptr<byte>(ptr, len, *cur_witness_sz);
        witness_t cur_witness(witness_ptr, witness_ptr + *cur_witness_sz);
        tx.m_witness.push_back(cur_witness);
    }
    return true;
}

// Present hexadecimal number in the human readable form with prefix
auto tx_history_archiver::mem_to_hex_str(const void* mem, size_t size, const string& prefix) -> std::string {
    stringstream ss;
    ss << prefix << std::hex << std::setfill('0');
    
    for (size_t i = 0; i < size; ++i) {
        const unsigned char* buf = (const unsigned char *)mem;
        ss << std::setw(2) << static_cast<unsigned int>(buf[i]);
    }
    
    return ss.str();
}

// Wrap a piece of memory into a string object
auto tx_history_archiver::mem_to_str_mem(const void* mem, size_t size) -> std::string {
    string ret(size, 0);
    memcpy((void *)ret.c_str(), mem, size);
    return ret;
}


// Append a piece of memory to the buffer and move the byffer pointer
auto tx_history_archiver::append_mem(unsigned char*& ptr, unsigned char *from, const size_t sz) -> void {
    memcpy(ptr, from, sz);
    ptr += sz;
}

// Read data of type T (and size sz) from the ptr, update ptr
template<typename T>
T* tx_history_archiver::mem_to_ptr(unsigned char*& ptr, size_t& len, const size_t sz) {
    size_t dataSize = sz ? sz : sizeof(T);
    if(len < dataSize) return nullptr;
    auto ret = reinterpret_cast<T*>(ptr);
    len -= dataSize;
    ptr += dataSize;
    return ret;
}

// Convert transaction into a human readable string
auto tx_history_archiver::tx_to_str_pres(const cbdc::transaction::full_tx& tx, const tx_state status, const uint64_t timestamp) -> std::string {
    cbdc::hash_t txId = tx_id(tx);

    stringstream outSS;
    auto inSize = tx.m_inputs.size();
    outSS << "Transaction: " << mem_to_hex_str(txId.data(), txId.size(), "0x") << " | Status: " << status_to_string(status) << " | Timestamp: " 
        << millisecondsToDateString(timestamp) << endl << "\tInputs (" << inSize << "):" << endl;

    for(size_t i = 0; i < inSize; ++i) {
        if(inSize > 1) outSS << "\t\t--- " << i+1 << " ---" << endl;
        outSS << "\t\tOutPoint:\tTX Id: " << mem_to_hex_str(tx.m_inputs[i].m_prevout.m_tx_id.data(), txId.size(), "0x") 
              << "\tIndex: " << tx.m_inputs[i].m_prevout.m_index << endl
              << "\t\tOutput:\tWitness_program_commitment: " << mem_to_hex_str(tx.m_inputs[i].m_prevout_data.m_witness_program_commitment.data(), txId.size(), "0x")
              << "\tValue: " << tx.m_inputs[i].m_prevout_data.m_value << endl;
    }

    auto outSize = tx.m_outputs.size();
    outSS << "\tOutputs (" << outSize << "):" << endl;
    for(size_t i = 0; i < outSize; ++i) {
        if(outSize > 1) outSS << "\t\t--- " << i+1 << " ---" << endl;
        outSS << "\t\tWitness_program_commitment: " << mem_to_hex_str(tx.m_outputs[i].m_witness_program_commitment.data(), txId.size(), "0x")
              << "\tValue: " << tx.m_outputs[i].m_value << endl;
    }

    auto wSize = tx.m_witness.size();
    outSS << "\tWitnesses (" << wSize << "):" << endl;
    for(size_t i = 0; i < wSize; ++i) {
        outSS << "\t\t" << i + 1 << ": " << mem_to_hex_str(tx.m_witness[i].data(), tx.m_witness.size(), "0x") << endl;
    }
    return outSS.str();
 }

auto tx_history_archiver::status_to_string(const tx_state status) -> string {
    switch (status)
    {
        case tx_state::initial:  return string("initial");
        case tx_state::execution:  return string("execution");
        case tx_state::validated:  return string("validated");
        case tx_state::completed:  return string("completed");
        case tx_state::validation_failed:  return string("validation_failed");
        case tx_state::execution_failed:  return string("execution_failed");
        default: return string("");
    };
}

string tx_history_archiver::millisecondsToDateString(uint64_t millisecs) {
    using namespace std::chrono;

    // Convert milliseconds to std::chrono::system_clock::time_point
    auto tp = time_point<system_clock>(millisecs * milliseconds(1));

    // Convert to time_t (seconds since epoch)
    auto time = system_clock::to_time_t(tp);

    // Convert time_t to struct tm in local timezone
    std::tm tm_time = *std::localtime(&time);

    // Format the date string
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_time);

    // Get milliseconds
    auto ms = duration_cast<milliseconds>(tp.time_since_epoch()) % 1000;

    // Concatenate milliseconds to the string
    std::string result(buffer);
    result += "." + std::to_string(ms.count());

    return result;
}
