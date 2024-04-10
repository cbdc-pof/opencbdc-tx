project(db-tester)

find_package(Threads)

include_directories(../../src ../../3rdparty ../../3rdparty/secp256k1/include ../../src/uhs/twophase/sentinel_2pc)

add_executable(db-tester db-tester.cpp 
                    ../../src/uhs/twophase/sentinel_2pc/tx_history_archive/tx_db.cpp 
                    ../../src/uhs/twophase/sentinel_2pc/tx_history_archive/tx_leveldb.cpp
                    ../../src/uhs/twophase/sentinel_2pc/tx_history_archive/tx_keyspacesdb.cpp
                    ../../src/uhs/twophase/sentinel_2pc/tx_history_archive/tx_history.cpp)
target_link_libraries(db-tester transaction
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
