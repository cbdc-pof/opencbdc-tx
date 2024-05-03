#include "uhs/transaction/transaction.hpp"
#include "uhs/transaction/validation.hpp"
#include "uhs/transaction/wallet.hpp"
#include "util/common/config.hpp"
#include "util/serialization/buffer_serializer.hpp"
#include "util/serialization/format.hpp"
#include "util/serialization/ostream_serializer.hpp"
#include "uhs/twophase/sentinel_2pc/tx_history_archive/tx_history.hpp"

#include <filesystem>
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <vector>
#include <chrono>
#include <string>
using namespace std;
using namespace chrono;

bool isValidHex(const std::string& str) {
    return !str.empty() && std::all_of(str.begin(), str.end(), [](char c) {
        return std::isxdigit(static_cast<unsigned char>(c));
    });
}

auto main([[maybe_unused]]int argc, [[maybe_unused]]char** argv) -> int {
    shared_ptr<cbdc::logging::log> logger = std::make_shared <cbdc::logging::log>(cbdc::logging::log_level::trace);
    string dbDir("tha_test");
    cbdc::config::options opts;
    opts.m_sentinel_loglevels.push_back(cbdc::logging::log_level::trace);
    if(argc > 1) dbDir = argv[1];
    opts.tha_type = string("leveldb");
    opts.tha_parameter = dbDir;
    cbdc::sentinel_2pc::tx_history_archiver tha(cbdc::sentinel_2pc::INVALID_SENTINEL_ID + 1, opts);
    cbdc::sentinel_2pc::tx_state last_status;
    uint64_t timestamp;


    while (true) {        
        cbdc::transaction::full_tx readTx;
        cout << "> ";
        string input;
        getline(cin, input);

        // Parsing the input into an array of strings
        istringstream iss(input);
        vector<string> tokens;
        string token;
        while (iss >> token) {
            tokens.push_back(token);
        }

        if(tokens.size() == 2) {
            string command = tokens[0];
            string txid = tokens[1];

            if (txid.size() >= 2 && txid.compare(0, 2, "0x") == 0) 
                txid.erase(0, 2); // Erase the first two characters in case they are "0x"

            if((txid.size() == 64) && isValidHex(txid))
            {
                if((command == "p") && (tha.get_transaction(txid, last_status, readTx, timestamp))) {
                    cout << "Read TX: ";
                    cout << cbdc::sentinel_2pc::tx_history_archiver::tx_to_str_pres(readTx, last_status, timestamp) << endl;
                }
                else if((command == "d") && (tha.delete_transaction(txid))) {
                    cout << "Transaction deleted." << endl;
                }
                else {
                    cout << "Transaction with ID " << txid << " not found" << endl;
                }
                continue;

            }
        }
        else if(tokens[0] == "q") {
            cout << "Exit" << endl;
            exit(0);
        }
        cout << "Enter valid command (d for delete, p for print, q for quit) followed by hexadecimal transaction Id" << std::endl;

    }

    return 0;
}
