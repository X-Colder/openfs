cmake_minimum_required(VERSION 3.16)
project(openfs VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# Platform detection
# ============================================================
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")
endif()

# Linux-specific settings
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(__LINUX__)
    # Check for liburing (io_uring) availability
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBURING liburing)
        if(LIBURING_FOUND)
            message(STATUS "Found liburing: ${LIBURING_LIBRARIES}")
            add_compile_definitions(HAVE_LIBURING)
        else()
            message(STATUS "liburing not found, io_uring support disabled")
        endif()
    endif()
endif()

# Homebrew prefix on macOS
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()

find_package(Threads REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

message(STATUS "Using protobuf ${Protobuf_VERSION}")
message(STATUS "Using gRPC ${gRPC_VERSION}")

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/proto)

# Proto output directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/proto)

# Proto files
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(PROTO_FILES
    ${PROTO_DIR}/common.proto
    ${PROTO_DIR}/data_service.proto
    ${PROTO_DIR}/meta_service.proto
    ${PROTO_DIR}/node_service.proto
)

# Generate protobuf + gRPC sources
set(PROTO_SRCS)
set(PROTO_HDRS)
set(GRPC_SRCS)
set(GRPC_HDRS)

foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    set(PB_CC "${CMAKE_CURRENT_BINARY_DIR}/proto/${PROTO_NAME}.pb.cc")
    set(PB_H  "${CMAKE_CURRENT_BINARY_DIR}/proto/${PROTO_NAME}.pb.h")
    set(GRPC_CC "${CMAKE_CURRENT_BINARY_DIR}/proto/${PROTO_NAME}.grpc.pb.cc")
    set(GRPC_H  "${CMAKE_CURRENT_BINARY_DIR}/proto/${PROTO_NAME}.grpc.pb.h")

    add_custom_command(
        OUTPUT ${PB_CC} ${PB_H} ${GRPC_CC} ${GRPC_H}
        COMMAND protobuf::protoc
        ARGS --proto_path=${PROTO_DIR}
             --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/proto
             --grpc_out=${CMAKE_CURRENT_BINARY_DIR}/proto
             --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
             ${PROTO_FILE}
        DEPENDS ${PROTO_FILE} protobuf::protoc gRPC::grpc_cpp_plugin
        COMMENT "Generating protobuf/gRPC code for ${PROTO_NAME}"
        VERBATIM
    )
    list(APPEND PROTO_SRCS ${PB_CC})
    list(APPEND PROTO_HDRS ${PB_H})
    list(APPEND GRPC_SRCS ${GRPC_CC})
    list(APPEND GRPC_HDRS ${GRPC_H})
endforeach()

# Common library (no gRPC dependency)
add_library(common
    src/common/types.cpp
    src/common/crc32.cpp
    src/common/id_generator.cpp
    src/common/config.cpp
    src/common/logging.cpp
)
target_link_libraries(common PUBLIC spdlog::spdlog Threads::Threads)

# Proto library (shared protobuf + gRPC generated code)
add_library(proto_lib
    ${PROTO_SRCS} ${GRPC_SRCS}
)
target_link_libraries(proto_lib PUBLIC protobuf::libprotobuf gRPC::grpc++)

# Meta library
add_library(meta_lib
    src/meta/meta_service_impl.cpp
    src/meta/inode_table.cpp
    src/meta/namespace_manager.cpp
    src/meta/block_map.cpp
    src/meta/block_allocator.cpp
    src/meta/node_manager.cpp
    src/meta/meta_node.cpp
    src/meta/meta_node_client.cpp
)
target_link_libraries(meta_lib PUBLIC common proto_lib Threads::Threads)

# Data library (new block-level disk management)
add_library(data_lib
    src/data/block_bitmap.cpp
    src/data/wal_manager.cpp
    src/data/block_io_engine.cpp
    src/data/disk_manager.cpp
    src/data/disk_pool.cpp
    src/data/segment_engine.cpp
    src/data/segment_writer.cpp
    src/data/segment_reader.cpp
    src/data/segment_sealer.cpp
    src/data/data_node.cpp
    src/data/data_service_impl.cpp
)
target_link_libraries(data_lib PUBLIC common meta_lib Threads::Threads)

# Linux-specific link libraries
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(LIBURING_FOUND)
        target_link_libraries(data_lib PUBLIC ${LIBURING_LIBRARIES})
        target_include_directories(data_lib PUBLIC ${LIBURING_INCLUDE_DIRS})
    endif()
endif()

# Executables
add_executable(data_node src/data/main.cpp)
target_link_libraries(data_node data_lib)

add_executable(meta_node src/meta/main.cpp)
target_link_libraries(meta_node meta_lib)

install(TARGETS data_node meta_node RUNTIME DESTINATION bin)
install(FILES configs/data_node.conf configs/meta_node.conf DESTINATION etc/openfs)

# ============================================================
# Testing
# ============================================================
option(BUILD_TESTS "Build tests" ON)

if(BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()