project(sentinel_2pc)

find_path(CASS_INCL_DIR NAMES cassandra.h keyspaces/datastax/include/ .. ../keyspaces/datastax/include/)
add_library(sentinel_2pc controller.cpp
                         server.cpp
                         tx_history_archive/tx_db.cpp
                         tx_history_archive/tx_leveldb.cpp
                         tx_history_archive/tx_keyspacesdb.cpp
                         tx_history_archive/tx_history.cpp)

add_executable(sentineld-2pc sentineld_2pc.cpp)
target_link_libraries(sentineld-2pc sentinel_2pc
                                    sentinel
                                    sentinel_interface
                                    transaction
                                    coordinator
                                    raft
                                    rpc
                                    network
                                    crypto
                                    serialization
                                    common
                                    ${NURAFT_LIBRARY}
                                    secp256k1
                                    ${LEVELDB_LIBRARY}
                                    ${CMAKE_THREAD_LIBS_INIT}
                                    ${CASS_LIBRARY})
