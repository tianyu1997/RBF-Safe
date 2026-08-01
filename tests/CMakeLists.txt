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
rbfsafe_add_test(rbfsafe_test_transparency test_transparency.cpp)
target_include_directories(rbfsafe_test_transparency PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
rbfsafe_add_test(rbfsafe_test_provenance test_provenance.cpp)
rbfsafe_add_test(rbfsafe_test_occupancy test_occupancy.cpp)
rbfsafe_add_test(rbfsafe_test_coordination test_coordination.cpp)

if(TARGET rbfsafe_inspect)
    add_test(
        NAME rbfsafe_transparency_fixture_inspect
        COMMAND rbfsafe_inspect
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/transparency_log_schema1
            rbfsafe-public-deployments-v1
            transparency-log
            02348249fded6a7cf712333d50a6318aaa1309056318af5c049e4ce296cf10e8
            6c8d14b047593d7117dc445957d8bdd6772c303d064297fdc8e329927d370bda
            86d47335bee5850b9c3a404e123d50bdb751cabee03a76de6361b8e25f03772f)
    add_test(
        NAME rbfsafe_transparency_gossip_fixture_inspect
        COMMAND rbfsafe_inspect
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/transparency_gossip_archive_schema1
            rbfsafe-public-deployments-v1
            transparency-log
            02348249fded6a7cf712333d50a6318aaa1309056318af5c049e4ce296cf10e8
            6c8d14b047593d7117dc445957d8bdd6772c303d064297fdc8e329927d370bda
            fd5ac959b484ada7ea2ce15e7cc8bccf41d8b6eaa368d9dafc2aedcdb0036514
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/bounded_execution_session_schema1/trust-history
            3b295bc13d0831ace4bc8a73349dc87f249d09c238468c4058f506a94554c780
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/bounded_execution_session_schema1/checkpoint.json
            3ebcb9e144577ba8b828f8b728c43b90f1b7412d09212cfec40e69fa1d3f9e01
            3b295bc13d0831ace4bc8a73349dc87f249d09c238468c4058f506a94554c780)
    add_test(
        NAME rbfsafe_provenance_fixture_inspect
        COMMAND rbfsafe_inspect
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/provenance_bundle_schema1/provenance.json
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/provenance_bundle_schema1/trust-history
            af93de1f517b91d348732b72bd08becb9411ee08c1151f5f66da0291740a2865
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/provenance_bundle_schema1/checkpoint.json
            6aa7aa5c91644205fa67e0caf6858a7f11ba0bb03aec2c548ba941624984137b
            1000000)
    add_test(
        NAME rbfsafe_continuous_occupancy_fixture_inspect
        COMMAND rbfsafe_inspect
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/continuous_fleet_occupancy_schema1/occupancy.json)
    add_test(
        NAME rbfsafe_continuous_occupancy_schema2_fixture_inspect
        COMMAND rbfsafe_inspect
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/continuous_fleet_occupancy_schema2/occupancy.json)
    add_test(
        NAME rbfsafe_continuous_robot_scene_occupancy_fixture_inspect
        COMMAND rbfsafe_inspect
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/continuous_robot_scene_occupancy_schema1/occupancy.json)
    add_test(
        NAME rbfsafe_occupancy_publication_fixture_inspect
        COMMAND rbfsafe_inspect
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/occupancy_publication_schema1/publication.json
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/continuous_fleet_occupancy_schema2/occupancy.json
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/occupancy_publication_schema1/trust-bundle.json
            fixture-cell-occupancy-stream-v1
            fixture-occupancy-publisher
            89e2700e95a4558f0e238a1b505f92ecbccf5435c3a263c485a086a6daf8661d
            -
            16)
    add_test(
        NAME rbfsafe_occupancy_publication_history_fixture_inspect
        COMMAND rbfsafe_inspect
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/occupancy_publication_history_schema1
            fixture-cell-occupancy-stream-v1
            fixture-occupancy-publisher
            89e2700e95a4558f0e238a1b505f92ecbccf5435c3a263c485a086a6daf8661d
            90f3620a182c6f34088cfc1b4cc15a676eeed9d69ea37222b4a04a0ddc494251
            83a6952083ac661aacff43168473c1938e29adfe738275d2230458dd6074dfb9
            31)
    add_test(
        NAME rbfsafe_rotating_occupancy_publication_history_fixture_inspect
        COMMAND rbfsafe_inspect
            ${CMAKE_CURRENT_SOURCE_DIR}/../data/rotating_occupancy_publication_history_schema1
            rotating-cell-stream-v1
            rotating-occupancy-publisher
            9767a5e8912af9192237924232daaf746c0107e98cc4a458ee8b773b6b0da051
            938c63c1c1bdf8c309189c4ca93b1093aa68f2b72adba49ba2dbe81a940d1517
            fc449d962cec661b52277926efa2a1d0f075a389fd488eeb7b7f9f5bd441429b
            829328cdffeca14a187430a3964651ef37ec249669dcbcf94c92f5a0d564b601
            31)
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
