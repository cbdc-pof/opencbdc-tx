#ifndef OPENCBDC_TX_SRC_SENTINEL_2PC_TX_HISTORY_H_
#define OPENCBDC_TX_SRC_SENTINEL_2PC_TX_HISTORY_H_

#include "uhs/transaction/transaction.hpp"
#include "uhs/transaction/messages.hpp"
#include "uhs/twophase/coordinator/client.hpp"
#include "uhs/sentinel/async_interface.hpp"
#include "util/common/config.hpp"
#include "util/common/hashmap.hpp"
#include "util/common/buffer.hpp"
#include "tx_db.hpp"
#include <leveldb/db.h>
#include <string>

namespace cbdc::sentinel_2pc {
const uint32_t  INVALID_SENTINEL_ID = 99999;

// Transaction status
enum class tx_state {
    /// tx initial state, no action has been performed yet
    initial,
    /// tx has been validated successfully
    validated,
    /// tx was sent to coordinator for execution
    execution,
    /// tx had been executed successfully
    completed,
    /// tx returned from coordinator without status 
    unknown,
    /// tx validation failed
    validation_failed,
    /// failure during transaction execution
    execution_failed
};

/// Manages a sentinel server for the two-phase commit architecture.
class tx_history_archiver {
  public:
      tx_history_archiver() = delete;
      tx_history_archiver(const tx_history_archiver&) = delete;
      auto operator=(const tx_history_archiver&) -> tx_history_archiver& = delete;
      tx_history_archiver(tx_history_archiver&&) = delete;
      auto operator=(tx_history_archiver&&) -> tx_history_archiver& = delete;

      /// Constructor.
      /// \param sentinel_id the ID of the sentinel this TH history archiver is attached to. If it is 0 - do not initialize DB
      /// \param opts config options that contain parameters to be used for DB initialization: tha_type (only "leveldb" is fully supported as of now), 
      ///              tha_parameter, tha_port, tha_user, tha_password
      tx_history_archiver(uint32_t sentinel_id, const config::options& opts);

      ~tx_history_archiver() { if(m_logger) m_logger.reset(); }

      /// Initializes the archiver. Connects to the DB server (if required by DB choice)
      /// \return true if initialization succeeded.
      /// \param sentinel_id the ID of the sentinel this TH history archiver is attached to. If it is 0 - do not initialize DB
      auto init(uint32_t sentinel_id, const std::string& db_param = "") -> bool;

      /// Adds new transaction to the archive
      /// \param tx transaction to add.
      /// \return true if transaction was successfully added
      ///         false in case anything is wrong
      auto add_transaction(cbdc::transaction::full_tx tx) -> bool;

      /// Adds new transaction to the archive
      /// \param txid Hash key of the transaction to be returned
      /// \param last_status last transaction status
      /// \param tx transaction body
      /// \param timestamp last transaction modification timestamp in millisec since the epoch
      /// \return true if transaction was found
      ///         false in case anything is wrong
      auto get_transaction_by_hash(const hash_t& txid, tx_state& last_status, cbdc::transaction::full_tx& tx, uint64_t& timestamp) -> bool;

      // The same as get get_transaction_by_hash, but uses string representation of txid
      auto get_transaction(const std::string& txid, tx_state& last_status, cbdc::transaction::full_tx& tx, uint64_t& timestamp) -> bool;

      /// Change transaction status
      /// \param txid Hash key of the transaction to modify the status.
      /// \param new_status new status.
      /// \return true if the transaction's status was successfully modified
      ///         false in case anything is wrong
      auto set_status(const hash_t& txid, const tx_state new_status) -> bool;

      /// Delete transaction record and it's related status records
      /// \param txid Hash key of the transaction to be deleted
      /// \return number of deleted transaction and status records 
      auto delete_transaction_by_hash(const hash_t& txid) -> unsigned int;
      auto delete_transaction(const std::string & txidStr) -> unsigned int;

      static auto tx_to_str_mem(const cbdc::transaction::full_tx& tx, const uint64_t timestamp) -> std::string;  // Serialize TX to a memory wrapped into string object
      static auto str_mem_to_tx(std::string& out_buffer, cbdc::transaction::full_tx& tx, uint64_t& timestamp) -> bool; // Deserialize TX from the memory (wrapped into string object)
      static auto mem_to_hex_str(const void* mem, size_t size = cbdc::hash_size, const std::string& prefix = "") -> std::string; // Present hexadecimal number in the human readable form with prefix
      static auto mem_to_str_mem(const void* mem, size_t size) -> std::string;      // Wrap a piece of memory into a string object 
      static auto append_mem(unsigned char*&ptr, unsigned char *from, const size_t sz) -> void;  // Append a piece of memory to the buffer and move the byffer pointer
      template<typename T> static T* mem_to_ptr(unsigned char*& ptr, size_t& len, const size_t sz = 0);  // Read data of type T (and size sz) from the ptr, update ptr
      static auto tx_to_str_pres(const cbdc::transaction::full_tx& tx, const tx_state status, const uint64_t timestamp) -> std::string;  // Convert transaction into a human readable string
      static auto status_to_string(const tx_state status) -> std::string;  // Convert tx_state to string
      static auto millisecondsToDateString(uint64_t milliseconds) -> std::string; // Convert msec to YYY-MM-DD:hh:mm:ss.msecs


    private:

      auto get_status_rec(const std::string& txid, const tx_state test_status, tx_state& actual_status, uint64_t& timestamp) -> bool; // Read a separate TX status record from DB
      auto build_status_key(const std::string& txid, tx_state status) -> std::string; // Build DB key used for separate TX status record

      uint32_t m_sentinel_id {0};
      cbdc::config::options m_opts;
      std::shared_ptr<logging::log> m_logger;
      std::string m_db_param;
      std::unique_ptr<DBHandler> m_db{};
  };
}

#endif // OPENCBDC_TX_SRC_SENTINEL_2PC_TX_HISTORY_H_
    