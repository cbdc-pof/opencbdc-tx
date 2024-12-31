project(common)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS False)

add_library(common buffer.cpp
                   hash.cpp
                   hashmap.cpp
                   keys.cpp
                   config.cpp
                   logging.cpp
                   random_source.cpp
                   thread_pool.cpp)
