# Find Protobuf
find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

# Find gRPC
find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")

# Set PROTOBUF_GENERATE_CPP_APPEND_PATH to TRUE
set(PROTOBUF_GENERATE_CPP_APPEND_PATH TRUE)

# Function to generate gRPC code
function(protobuf_generate_grpc_cpp SRCS HDRS)
  if(NOT ARGN)
    message(SEND_ERROR "Error: protobuf_generate_grpc_cpp() called without any proto files")
    return()
  endif()

  set(${SRCS})
  set(${HDRS})

  foreach(FIL ${ARGN})
    get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
    get_filename_component(FIL_WE ${FIL} NAME_WE)

    list(APPEND ${SRCS} "${CMAKE_CURRENT_BINARY_DIR}/proto/${FIL_WE}.grpc.pb.cc")
    list(APPEND ${HDRS} "${CMAKE_CURRENT_BINARY_DIR}/proto/${FIL_WE}.grpc.pb.h")

    add_custom_command(
      OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/proto/${FIL_WE}.grpc.pb.cc"
             "${CMAKE_CURRENT_BINARY_DIR}/proto/${FIL_WE}.grpc.pb.h"
      COMMAND protobuf::protoc
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}/proto"
           --cpp_out "${CMAKE_CURRENT_BINARY_DIR}/proto"
           --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
           ${ABS_FIL}
      DEPENDS ${ABS_FIL} protobuf::protoc gRPC::grpc_cpp_plugin
      COMMENT "Running gRPC C++ protocol buffer compiler on ${FIL}"
      VERBATIM)
  endforeach()

  set_source_files_properties(${${SRCS}} ${${HDRS}} PROPERTIES GENERATED TRUE)
  set(${SRCS} ${${SRCS}} PARENT_SCOPE)
  set(${HDRS} ${${HDRS}} PARENT_SCOPE)
endfunction()