function(rbfsafe_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE RBFSafe::rbfsafe)
    target_compile_definitions(${name} PRIVATE RBFSAFE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data")
    rbfsafe_configure_target(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

rbfsafe_add_test(rbfsafe_test_geometry test_geometry.cpp)
rbfsafe_add_test(rbfsafe_test_golden test_golden.cpp)
target_include_directories(rbfsafe_test_golden PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
rbfsafe_add_test(rbfsafe_test_lect test_lect.cpp)
rbfsafe_add_test(rbfsafe_test_atlas test_atlas.cpp)
rbfsafe_add_test(rbfsafe_test_trajectory test_trajectory.cpp)
rbfsafe_add_test(rbfsafe_test_persistence test_persistence.cpp)
rbfsafe_add_test(rbfsafe_test_obb test_obb.cpp)
rbfsafe_add_test(rbfsafe_test_hipac test_hipac.cpp)
target_include_directories(rbfsafe_test_hipac PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
rbfsafe_add_test(rbfsafe_test_safe_ik test_safe_ik.cpp)
rbfsafe_add_test(rbfsafe_test_dynamic test_dynamic.cpp)
rbfsafe_add_test(rbfsafe_test_higher_order test_higher_order.cpp)
rbfsafe_add_test(rbfsafe_test_region_database test_region_database.cpp)
rbfsafe_add_test(rbfsafe_test_planning test_planning.cpp)
rbfsafe_add_test(rbfsafe_test_optimization test_optimization.cpp)
rbfsafe_add_test(rbfsafe_test_shield test_shield.cpp)
rbfsafe_add_test(rbfsafe_test_policy test_policy.cpp)
rbfsafe_add_test(rbfsafe_test_calibration test_calibration.cpp)
rbfsafe_add_test(rbfsafe_test_memory test_memory.cpp)
target_include_directories(rbfsafe_test_memory PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
rbfsafe_add_test(rbfsafe_test_trust test_trust.cpp)
rbfsafe_add_test(rbfsafe_test_remote test_remote.cpp)
rbfsafe_add_test(rbfsafe_test_identity test_identity.cpp)
rbfsafe_add_test(rbfsafe_test_deployment test_deployment.cpp)
rbfsafe_add_test(rbfsafe_test_execution test_execution.cpp)
rbfsafe_add_test(rbfsafe_test_execution_ledger test_execution_ledger.cpp)

if(TARGET rbfsafe_inspect)
    add_test(
        NAME rbfsafe_execution_ledger_fixture_inspect
        COMMAND rbfsafe_inspect
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/execution_ledger_schema1
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/bounded_execution_session_schema1/session.json
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/bounded_execution_session_schema1/profile.json
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/bounded_execution_session_schema1/atlas
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/bounded_execution_session_schema1/trust-history
            3b295bc13d0831ace4bc8a73349dc87f249d09c238468c4058f506a94554c780
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/bounded_execution_session_schema1/checkpoint.json
            3ebcb9e144577ba8b828f8b728c43b90f1b7412d09212cfec40e69fa1d3f9e01)
    add_test(
        NAME rbfsafe_execution_fixture_inspect
        COMMAND rbfsafe_inspect
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/bounded_execution_session_schema1/session.json
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/bounded_execution_session_schema1/profile.json
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/bounded_execution_session_schema1/atlas
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/bounded_execution_session_schema1/trust-history
            3b295bc13d0831ace4bc8a73349dc87f249d09c238468c4058f506a94554c780
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/bounded_execution_session_schema1/checkpoint.json
            3ebcb9e144577ba8b828f8b728c43b90f1b7412d09212cfec40e69fa1d3f9e01)
endif()

if(RBFSAFE_BUILD_OMPL)
    add_executable(rbfsafe_test_ompl test_ompl.cpp)
    target_link_libraries(rbfsafe_test_ompl PRIVATE RBFSafe::ompl)
    rbfsafe_configure_target(rbfsafe_test_ompl)
    add_test(NAME rbfsafe_test_ompl COMMAND rbfsafe_test_ompl)
endif()

add_executable(rbfsafe_test_internal test_internal.cpp)
target_link_libraries(rbfsafe_test_internal PRIVATE RBFSafe::geometry)
target_include_directories(rbfsafe_test_internal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
rbfsafe_configure_target(rbfsafe_test_internal)
add_test(NAME rbfsafe_test_internal COMMAND rbfsafe_test_internal)

add_executable(rbfsafe_test_region_index test_region_index.cpp)
target_link_libraries(rbfsafe_test_region_index PRIVATE RBFSafe::atlas)
target_include_directories(rbfsafe_test_region_index PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
rbfsafe_configure_target(rbfsafe_test_region_index)
add_test(NAME rbfsafe_test_region_index COMMAND rbfsafe_test_region_index)
