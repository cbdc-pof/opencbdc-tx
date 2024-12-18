project(client)

add_library(client atomizer_client.cpp
                   client.cpp 
                   twophase_client.cpp client_transaction_handler.cpp)

target_link_libraries(client bech32)

add_executable(client-cli client-cli.cpp )
target_link_libraries(client-cli client
                                 atomizer
                                 watchtower
                                 sentinel
                                 sentinel_interface
                                 transaction
                                 locking_shard
                                 coordinator
                                 rpc
                                 network
                                 common
                                 serialization
                                 crypto
                                 secp256k1
                                 ${NURAFT_LIBRARY}
                                 ${CMAKE_THREAD_LIBS_INIT})

add_executable(tn_swap_iteration_basic tn_swap_iteration_basic.cpp client_transaction_handler.cpp)
target_link_libraries(tn_swap_iteration_basic sentinel
                              sentinel_interface
                              locking_shard
                              coordinator
                              transaction
                              rpc
                              network
                              common
                              serialization
                              crypto
                              ${NURAFT_LIBRARY}
                              secp256k1
                              ${CMAKE_THREAD_LIBS_INIT})
                                 