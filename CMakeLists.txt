cmake_minimum_required(VERSION 3.16)
project(openfs VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")

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

# Data library
add_library(data_lib
    src/data/segment_engine.cpp
    src/data/segment_writer.cpp
    src/data/segment_reader.cpp
    src/data/segment_sealer.cpp
    src/data/data_node.cpp
    src/data/data_service_impl.cpp
    ${PROTO_SRCS} ${GRPC_SRCS}
)
target_link_libraries(data_lib PUBLIC common protobuf::libprotobuf gRPC::grpc++ Threads::Threads)

# Meta library
add_library(meta_lib
    src/meta/meta_service_impl.cpp
    src/meta/inode_table.cpp
    src/meta/namespace_manager.cpp
    src/meta/meta_node.cpp
    ${PROTO_SRCS} ${GRPC_SRCS}
)
target_link_libraries(meta_lib PUBLIC common protobuf::libprotobuf gRPC::grpc++ Threads::Threads)

# Executables
add_executable(data_node src/data/main.cpp)
target_link_libraries(data_node data_lib)

add_executable(meta_node src/meta/main.cpp)
target_link_libraries(meta_node meta_lib)

install(TARGETS data_node meta_node RUNTIME DESTINATION bin)
install(FILES configs/data_node.conf configs/meta_node.conf DESTINATION etc/openfs)