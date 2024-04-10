project(tha-reader)
include_directories(../../src ../../3rdparty ../../3rdparty/secp256k1/include ../../src/uhs/twophase/sentinel_2pc)

add_executable(tha-reader tha-reader.cpp 
                    ../../src/uhs/twophase/sentinel_2pc/tx_history_archive/tx_db.cpp
                    ../../src/uhs/twophase/sentinel_2pc/tx_history_archive/tx_leveldb.cpp  
                    ../../src/uhs/twophase/sentinel_2pc/tx_history_archive/tx_keyspacesdb.cpp 
                    ../../src/uhs/twophase/sentinel_2pc/tx_history_archive/tx_history.cpp)
target_link_libraries(tha-reader transaction
                                   network
                                   common
                                   serialization
                                   crypto
                                   benchmark::benchmark
                                   util
                                   secp256k1
                                   ${LEVELDB_LIBRARY}
                                   ${CMAKE_THREAD_LIBS_INIT}
                                   ${CASS_LIBRARY})
