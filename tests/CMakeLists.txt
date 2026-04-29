# Test helper library (common test utilities)
add_library(test_utils
    test_util.cpp
)
target_link_libraries(test_utils PUBLIC common GTest::gtest)

# ============================================================
# Unit Tests: common module
# ============================================================
add_executable(test_types test_common_types.cpp)
target_link_libraries(test_types common GTest::gtest GTest::gtest_main)
add_test(NAME test_types COMMAND test_types)

add_executable(test_crc32 test_common_crc32.cpp)
target_link_libraries(test_crc32 common GTest::gtest GTest::gtest_main)
add_test(NAME test_crc32 COMMAND test_crc32)

add_executable(test_id_generator test_common_id_generator.cpp)
target_link_libraries(test_id_generator common GTest::gtest GTest::gtest_main)
add_test(NAME test_id_generator COMMAND test_id_generator)

add_executable(test_config test_common_config.cpp)
target_link_libraries(test_config common GTest::gtest GTest::gtest_main)
add_test(NAME test_config COMMAND test_config)

# ============================================================
# Unit Tests: meta module
# ============================================================
add_executable(test_inode_table test_meta_inode_table.cpp)
target_link_libraries(test_inode_table meta_lib GTest::gtest GTest::gtest_main)
add_test(NAME test_inode_table COMMAND test_inode_table)

add_executable(test_namespace_manager test_meta_namespace_manager.cpp)
target_link_libraries(test_namespace_manager meta_lib GTest::gtest GTest::gtest_main)
add_test(NAME test_namespace_manager COMMAND test_namespace_manager)

add_executable(test_block_map test_meta_block_map.cpp)
target_link_libraries(test_block_map meta_lib GTest::gtest GTest::gtest_main)
add_test(NAME test_block_map COMMAND test_block_map)

add_executable(test_block_allocator test_meta_block_allocator.cpp)
target_link_libraries(test_block_allocator meta_lib GTest::gtest GTest::gtest_main)
add_test(NAME test_block_allocator COMMAND test_block_allocator)

add_executable(test_node_manager test_meta_node_manager.cpp)
target_link_libraries(test_node_manager meta_lib GTest::gtest GTest::gtest_main)
add_test(NAME test_node_manager COMMAND test_node_manager)

# ============================================================
# Unit Tests: data module
# ============================================================
add_executable(test_segment_io test_data_segment_io.cpp)
target_link_libraries(test_segment_io data_lib GTest::gtest GTest::gtest_main)
add_test(NAME test_segment_io COMMAND test_segment_io)

# ============================================================
# Integration Tests: gRPC services
# ============================================================
add_executable(test_meta_service test_meta_service.cpp)
target_link_libraries(test_meta_service meta_lib GTest::gtest GTest::gtest_main)
add_test(NAME test_meta_service COMMAND test_meta_service)

add_executable(test_data_service test_data_service.cpp)
target_link_libraries(test_data_service data_lib GTest::gtest GTest::gtest_main)
add_test(NAME test_data_service COMMAND test_data_service)

add_executable(test_node_service test_node_service.cpp)
target_link_libraries(test_node_service meta_lib GTest::gtest GTest::gtest_main)
add_test(NAME test_node_service COMMAND test_node_service)

# ============================================================
# End-to-End Test: full write pipeline
# ============================================================
add_executable(test_e2e_pipeline test_e2e_pipeline.cpp)
target_link_libraries(test_e2e_pipeline data_lib GTest::gtest GTest::gtest_main)
add_test(NAME test_e2e_pipeline COMMAND test_e2e_pipeline)