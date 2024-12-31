
# Creating a Binary for atomic swap using lua
add_executable(lua_aswap lua_atomic_swap_client.cpp
                         wallet.cpp)


target_link_libraries(lua_aswap broker
                                directory
                                runtime_locking_shard
                                ticket_machine
                                parsec
                                agent
                                rpc
                                network
                                common
                                serialization
                                crypto
                                secp256k1
                                ${LUA_LIBRARY}
                                ${CMAKE_THREAD_LIBS_INIT})

# Creating a Binary for atomic swap using lua on CBDC2
add_executable(lua_cbdc2_swap lua_atomic_swap_client_basic.cpp
wallet.cpp)
target_link_libraries(lua_cbdc2_swap broker
                                directory
                                runtime_locking_shard
                                ticket_machine
                                parsec
                                agent
                                rpc
                                network
                                common
                                serialization
                                crypto
                                secp256k1
                                ${LUA_LIBRARY}
                                ${CMAKE_THREAD_LIBS_INIT})



# Creating a Binary for Load test with Lua bench
add_executable(lua_bench lua_bench.cpp
                                wallet.cpp)
target_link_libraries(lua_bench broker
                                directory
                                runtime_locking_shard
                                ticket_machine
                                parsec
                                agent
                                rpc
                                network
                                common
                                serialization
                                crypto
                                secp256k1
                                ${LUA_LIBRARY}
                                ${CMAKE_THREAD_LIBS_INIT})

