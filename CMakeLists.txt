cmake_minimum_required(VERSION 3.10)
project(hft_platform)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto -DNDEBUG -fomit-frame-pointer") # -fno-rtti -fno-exceptions
set(CMAKE_CXX_FLAGS_DEBUG "-g -fsanitize=address -fno-omit-frame-pointer")

# add_definitions(-DBOOST_NO_EXCEPTIONS)

find_package(Boost 1.83 REQUIRED COMPONENTS thread fiber)
find_package(spdlog REQUIRED)

# Generate flatbuffers code from the schema
set(SCHEMA_FILE ${CMAKE_SOURCE_DIR}/common/schema/marketdata.fbs)
set(GEN_DIR ${CMAKE_SOURCE_DIR}/common/src/gen)
add_custom_command(
    OUTPUT ${GEN_DIR}/marketdata_generated.h
    COMMAND flatc --cpp --gen-object-api -o ${GEN_DIR} ${SCHEMA_FILE}
    DEPENDS ${SCHEMA_FILE}
    COMMENT "Generating FlatBuffers code from schema"
)
add_custom_target(code_generator DEPENDS ${GEN_DIR}/marketdata_generated.h)

set(CMAKE_AR "/usr/bin/ar")
set(CMAKE_RANLIB "/usr/bin/ranlib")

# definitions
set(USE_MIMALLOC OFF)

# PostgreSQL
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQXX REQUIRED libpqxx)

# make static library 
file(GLOB_RECURSE COMMON_SOURCES "common/src/*.cpp" "common/src/**/*.cpp" "common/src/*.hpp" "common/src/**/*.hpp")
add_library(hft_common STATIC ${COMMON_SOURCES})

target_include_directories(hft_common PUBLIC common/src common/src/gen common/src/types ${LIBPQXX_INCLUDE_DIRS})
target_link_libraries(hft_common PRIVATE ${Boost_LIBRARIES} spdlog::spdlog ${LIBPQXX_LIBRARIES} atomic)

add_custom_command(TARGET hft_common POST_BUILD
    COMMAND ranlib ${CMAKE_CURRENT_BINARY_DIR}/libhft_common.a
    COMMENT "Running ranlib on libhft_common.a"
)

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_OUTPUT)
if(IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Server Target
file(GLOB_RECURSE SERVER_SOURCES "server/src/*.cpp" "server/src/**/*.cpp" "server/src/*.hpp" "server/src/**/*.hpp")
add_executable(hft_server ${SERVER_SOURCES})
target_compile_definitions(hft_server PRIVATE)
target_link_libraries(hft_server PRIVATE hft_common ${Boost_LIBRARIES} spdlog::spdlog ${LIBPQXX_LIBRARIES} atomic)
target_include_directories(hft_server PRIVATE  common/src server/src server/src/types)

# Trader Target
file(GLOB_RECURSE TRADER_SOURCES "trader/src/*.cpp" "trader/src/**/*.cpp" "trader/src/*.hpp" "trader/src/**/*.hpp")
add_executable(hft_trader ${TRADER_SOURCES})
target_compile_definitions(hft_trader PRIVATE)
target_link_libraries(hft_trader PRIVATE hft_common ${Boost_LIBRARIES} spdlog::spdlog ${LIBPQXX_LIBRARIES} atomic)
target_include_directories(hft_trader PRIVATE common/src trader/src trader/src/types)


# mimalloc
if(USE_MIMALLOC)
    find_package(mimalloc REQUIRED)
    target_link_libraries(hft_server PRIVATE mimalloc)
    target_link_libraries(hft_trader PRIVATE mimalloc)
endif()

add_dependencies(hft_server code_generator)
add_dependencies(hft_trader code_generator)

add_custom_command(
    TARGET hft_trader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/common/config/server_config.ini ${CMAKE_BINARY_DIR}
)

add_custom_command(
    TARGET hft_trader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/common/config/trader_config.ini ${CMAKE_BINARY_DIR}
)
