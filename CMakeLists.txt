cmake_minimum_required(VERSION 3.23)
project(
    RBFSafe
    VERSION 5.0.0
    DESCRIPTION "Deterministic certified safe-region atlases for serial robots"
    HOMEPAGE_URL "https://github.com/tianyu1997/RBF-Safe"
    LANGUAGES C CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

option(RBFSAFE_BUILD_TESTS "Build RBF-Safe tests" ON)
option(RBFSAFE_BUILD_EXAMPLES "Build RBF-Safe examples" ON)
option(RBFSAFE_BUILD_TOOLS "Build RBF-Safe command-line tools" ON)
option(RBFSAFE_BUILD_PYTHON "Build the rbfsafe Python extension" OFF)
option(RBFSAFE_BUILD_OMPL "Build the optional OMPL adapter" OFF)
option(RBFSAFE_BUILD_BENCHMARKS "Build deterministic release benchmark and soak gates" OFF)
option(RBFSAFE_WARNINGS_AS_ERRORS "Treat RBF-Safe warnings as errors" OFF)

function(rbfsafe_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /FS)
        if(RBFSAFE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(RBFSAFE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_library(rbfsafe_core
    src/core/sha256.cpp
    src/core/json.cpp)
add_library(RBFSafe::core ALIAS rbfsafe_core)
set_target_properties(rbfsafe_core PROPERTIES EXPORT_NAME core)
target_include_directories(rbfsafe_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
rbfsafe_configure_target(rbfsafe_core)

add_library(rbfsafe_envelope
    src/envelope/workspace_envelope.cpp
    src/envelope/workspace_envelope_json.cpp)
add_library(RBFSafe::envelope ALIAS rbfsafe_envelope)
set_target_properties(rbfsafe_envelope PROPERTIES EXPORT_NAME envelope)
target_include_directories(rbfsafe_envelope
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_envelope PUBLIC rbfsafe_core)
rbfsafe_configure_target(rbfsafe_envelope)

add_library(rbfsafe_geometry
    src/geometry/model.cpp
    src/geometry/geometry.cpp
    src/geometry/certificate.cpp)
add_library(RBFSafe::geometry ALIAS rbfsafe_geometry)
set_target_properties(rbfsafe_geometry PROPERTIES EXPORT_NAME geometry)
target_include_directories(rbfsafe_geometry
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_geometry PUBLIC rbfsafe_core rbfsafe_envelope)
rbfsafe_configure_target(rbfsafe_geometry)

add_library(rbfsafe_lect src/lect/lect.cpp)
add_library(RBFSafe::lect ALIAS rbfsafe_lect)
set_target_properties(rbfsafe_lect PROPERTIES EXPORT_NAME lect)
target_include_directories(rbfsafe_lect
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_lect PUBLIC rbfsafe_core)
rbfsafe_configure_target(rbfsafe_lect)

add_library(rbfsafe_atlas
    src/atlas/atlas.cpp
    src/atlas/atlas_identity.cpp
    src/atlas/persistence.cpp
    src/atlas/region_index.cpp
    src/atlas/scene_delta.cpp
    src/atlas/trajectory.cpp)
add_library(RBFSafe::atlas ALIAS rbfsafe_atlas)
set_target_properties(rbfsafe_atlas PROPERTIES EXPORT_NAME atlas)
target_include_directories(rbfsafe_atlas
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_atlas PUBLIC rbfsafe_geometry rbfsafe_lect)
rbfsafe_configure_target(rbfsafe_atlas)

add_library(rbfsafe_occupancy
    src/assurance/occupancy/moving_obstacle_occupancy.cpp
    src/assurance/occupancy/occupancy.cpp
    src/assurance/occupancy/occupancy_persistence.cpp)
add_library(RBFSafe::occupancy ALIAS rbfsafe_occupancy)
set_target_properties(rbfsafe_occupancy PROPERTIES EXPORT_NAME occupancy)
target_include_directories(rbfsafe_occupancy
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_occupancy PUBLIC rbfsafe_atlas)
rbfsafe_configure_target(rbfsafe_occupancy)

add_library(rbfsafe_update src/applications/update/dynamic.cpp src/applications/update/version_store.cpp)
add_library(RBFSafe::update ALIAS rbfsafe_update)
set_target_properties(rbfsafe_update PROPERTIES EXPORT_NAME update)
target_include_directories(rbfsafe_update
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_update PUBLIC rbfsafe_atlas)
rbfsafe_configure_target(rbfsafe_update)

add_library(rbfsafe_ik src/applications/ik/safe_ik.cpp)
add_library(RBFSafe::ik ALIAS rbfsafe_ik)
set_target_properties(rbfsafe_ik PROPERTIES EXPORT_NAME ik)
target_include_directories(rbfsafe_ik
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_ik PUBLIC rbfsafe_atlas)
rbfsafe_configure_target(rbfsafe_ik)

add_library(rbfsafe_corridor src/applications/corridor/corridor.cpp src/applications/corridor/corridor_persistence.cpp)
add_library(RBFSafe::corridor ALIAS rbfsafe_corridor)
set_target_properties(rbfsafe_corridor PROPERTIES EXPORT_NAME corridor)
target_include_directories(rbfsafe_corridor
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_corridor PUBLIC rbfsafe_atlas)
rbfsafe_configure_target(rbfsafe_corridor)

add_library(rbfsafe_regions
    src/applications/regions/higher_order.cpp
    src/applications/regions/portal.cpp
    src/applications/regions/region_database.cpp
    src/applications/regions/region_database_persistence.cpp
    src/applications/regions/region_identity.cpp)
add_library(RBFSafe::regions ALIAS rbfsafe_regions)
set_target_properties(rbfsafe_regions PROPERTIES EXPORT_NAME regions)
target_include_directories(rbfsafe_regions
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_regions PUBLIC rbfsafe_corridor)
rbfsafe_configure_target(rbfsafe_regions)

add_library(rbfsafe_planning src/applications/planning/planning.cpp)
add_library(RBFSafe::planning ALIAS rbfsafe_planning)
set_target_properties(rbfsafe_planning PROPERTIES EXPORT_NAME planning)
target_include_directories(rbfsafe_planning
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_planning PUBLIC rbfsafe_atlas)
rbfsafe_configure_target(rbfsafe_planning)

add_library(rbfsafe_optimization src/applications/optimization/optimization.cpp)
add_library(RBFSafe::optimization ALIAS rbfsafe_optimization)
set_target_properties(rbfsafe_optimization PROPERTIES EXPORT_NAME optimization)
target_include_directories(rbfsafe_optimization
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_optimization PUBLIC rbfsafe_regions)
rbfsafe_configure_target(rbfsafe_optimization)

add_library(rbfsafe_shield src/applications/shield/shield.cpp)
add_library(RBFSafe::shield ALIAS rbfsafe_shield)
set_target_properties(rbfsafe_shield PROPERTIES EXPORT_NAME shield)
target_include_directories(rbfsafe_shield
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_shield PUBLIC rbfsafe_ik)
rbfsafe_configure_target(rbfsafe_shield)

add_library(rbfsafe_policy
    src/applications/policy/calibration.cpp
    src/applications/policy/calibration_lifecycle.cpp
    src/applications/policy/calibration_lifecycle_persistence.cpp
    src/applications/policy/policy.cpp
    src/applications/policy/policy_persistence.cpp)
add_library(RBFSafe::policy ALIAS rbfsafe_policy)
set_target_properties(rbfsafe_policy PROPERTIES EXPORT_NAME policy)
target_include_directories(rbfsafe_policy
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_policy PUBLIC rbfsafe_shield)
rbfsafe_configure_target(rbfsafe_policy)

add_library(rbfsafe_memory
    src/assurance/memory/fleet_schedule_archive.cpp
    src/assurance/memory/fleet_schedule_persistence.cpp
    src/assurance/memory/memory.cpp
    src/assurance/memory/memory_persistence.cpp
    src/assurance/memory/memory_store.cpp)
add_library(RBFSafe::memory ALIAS rbfsafe_memory)
set_target_properties(rbfsafe_memory PROPERTIES EXPORT_NAME memory)
target_include_directories(rbfsafe_memory
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_memory PUBLIC rbfsafe_atlas)
rbfsafe_configure_target(rbfsafe_memory)

add_library(rbfsafe_trust src/assurance/trust/trust.cpp)
add_library(RBFSafe::trust ALIAS rbfsafe_trust)
set_target_properties(rbfsafe_trust PROPERTIES EXPORT_NAME trust)
target_include_directories(rbfsafe_trust
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_trust PUBLIC rbfsafe_memory)
rbfsafe_configure_target(rbfsafe_trust)

add_library(rbfsafe_remote src/assurance/remote/remote.cpp src/assurance/remote/remote_persistence.cpp)
add_library(RBFSafe::remote ALIAS rbfsafe_remote)
set_target_properties(rbfsafe_remote PROPERTIES EXPORT_NAME remote)
target_include_directories(rbfsafe_remote
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_remote PUBLIC rbfsafe_trust)
rbfsafe_configure_target(rbfsafe_remote)

function(rbfsafe_verify_vendored_file path expected_sha256)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${path}" actual_sha256)
    if(NOT actual_sha256 STREQUAL expected_sha256)
        message(FATAL_ERROR
            "Vendored file ${path} does not match its reviewed upstream SHA-256")
    endif()
endfunction()

rbfsafe_verify_vendored_file(
    third_party/monocypher/monocypher.c
    afe2b098c8569577a84488e0b98d276d1fba6506adea68bb9241a52111734c59)
rbfsafe_verify_vendored_file(
    third_party/monocypher/monocypher.h
    f78bb31255cfb7beba66afd2137f5194c8a025cf40488b6cc1e295234d43f374)
rbfsafe_verify_vendored_file(
    third_party/monocypher/optional/monocypher-ed25519.c
    7c9b16056cbd27521919e8a6f56a228808b9e718afc42e3d33f28c08e5abdee2)
rbfsafe_verify_vendored_file(
    third_party/monocypher/optional/monocypher-ed25519.h
    bd546edcd468d64e28caa3dbf4b1d6bfad7435c0ce994723fd81aae26405121b)
rbfsafe_verify_vendored_file(
    third_party/monocypher/LICENCE.md
    5f8360e4c06ddcc584bdb4b210c6af824c4bb301e6a9a521869b6d90795ca4b3)

add_library(rbfsafe_monocypher OBJECT
    third_party/monocypher/monocypher.c
    third_party/monocypher/optional/monocypher-ed25519.c)
target_include_directories(rbfsafe_monocypher
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/monocypher)
set_target_properties(rbfsafe_monocypher PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(rbfsafe_monocypher PRIVATE /w)
else()
    target_compile_options(rbfsafe_monocypher PRIVATE -w)
endif()

add_library(rbfsafe_identity
    src/assurance/identity/identity.cpp
    src/assurance/identity/identity_checkpoint.cpp
    src/assurance/identity/identity_history.cpp
    src/assurance/identity/identity_persistence.cpp
    $<TARGET_OBJECTS:rbfsafe_monocypher>)
add_library(RBFSafe::identity ALIAS rbfsafe_identity)
set_target_properties(rbfsafe_identity PROPERTIES EXPORT_NAME identity)
target_include_directories(rbfsafe_identity
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/monocypher)
target_link_libraries(rbfsafe_identity PUBLIC rbfsafe_remote)
rbfsafe_configure_target(rbfsafe_identity)

add_library(rbfsafe_coordination
    src/assurance/coordination/coordination.cpp
    src/assurance/coordination/coordination_history.cpp
    src/assurance/coordination/coordination_rotating_history.cpp
    src/assurance/coordination/coordination_reservation.cpp
    src/assurance/coordination/coordination_reservation_persistence.cpp
    src/assurance/coordination/coordination_persistence.cpp)
add_library(RBFSafe::coordination ALIAS rbfsafe_coordination)
set_target_properties(rbfsafe_coordination PROPERTIES EXPORT_NAME coordination)
target_include_directories(rbfsafe_coordination
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_coordination
    PUBLIC rbfsafe_identity rbfsafe_occupancy)
rbfsafe_configure_target(rbfsafe_coordination)

add_library(rbfsafe_deployment
    src/assurance/deployment/deployment.cpp
    src/assurance/deployment/deployment_persistence.cpp)
add_library(RBFSafe::deployment ALIAS rbfsafe_deployment)
set_target_properties(rbfsafe_deployment PROPERTIES EXPORT_NAME deployment)
target_include_directories(rbfsafe_deployment
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_deployment PUBLIC rbfsafe_identity)
rbfsafe_configure_target(rbfsafe_deployment)

add_library(rbfsafe_execution
    src/assurance/execution/execution.cpp
    src/assurance/execution/execution_persistence.cpp
    src/assurance/execution/execution_ledger.cpp
    src/assurance/execution/execution_ledger_persistence.cpp)
add_library(RBFSafe::execution ALIAS rbfsafe_execution)
set_target_properties(rbfsafe_execution PROPERTIES EXPORT_NAME execution)
target_include_directories(rbfsafe_execution
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_execution PUBLIC rbfsafe_deployment rbfsafe_atlas)
rbfsafe_configure_target(rbfsafe_execution)

add_library(rbfsafe_transparency
    src/assurance/transparency/transparency.cpp
    src/assurance/transparency/transparency_persistence.cpp)
add_library(RBFSafe::transparency ALIAS rbfsafe_transparency)
set_target_properties(rbfsafe_transparency PROPERTIES EXPORT_NAME transparency)
target_include_directories(rbfsafe_transparency
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_transparency PUBLIC rbfsafe_execution)
rbfsafe_configure_target(rbfsafe_transparency)

add_library(rbfsafe_witness
    src/assurance/witness/witness.cpp
    src/assurance/witness/witness_persistence.cpp)
add_library(RBFSafe::witness ALIAS rbfsafe_witness)
set_target_properties(rbfsafe_witness PROPERTIES EXPORT_NAME witness)
target_include_directories(rbfsafe_witness
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_witness PUBLIC rbfsafe_transparency)
rbfsafe_configure_target(rbfsafe_witness)

add_library(rbfsafe_provenance
    src/assurance/provenance/provenance.cpp)
add_library(RBFSafe::provenance ALIAS rbfsafe_provenance)
set_target_properties(rbfsafe_provenance PROPERTIES EXPORT_NAME provenance)
target_include_directories(rbfsafe_provenance
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_provenance PUBLIC rbfsafe_identity)
rbfsafe_configure_target(rbfsafe_provenance)

add_library(rbfsafe_applications INTERFACE)
add_library(RBFSafe::applications ALIAS rbfsafe_applications)
set_target_properties(rbfsafe_applications PROPERTIES EXPORT_NAME applications)
target_link_libraries(rbfsafe_applications INTERFACE
    rbfsafe_corridor
    rbfsafe_regions
    rbfsafe_optimization
    rbfsafe_ik
    rbfsafe_planning
    rbfsafe_update
    rbfsafe_shield
    rbfsafe_policy)

add_library(rbfsafe_assurance INTERFACE)
add_library(RBFSafe::assurance ALIAS rbfsafe_assurance)
set_target_properties(rbfsafe_assurance PROPERTIES EXPORT_NAME assurance)
target_link_libraries(rbfsafe_assurance INTERFACE
    rbfsafe_occupancy
    rbfsafe_memory
    rbfsafe_trust
    rbfsafe_remote
    rbfsafe_identity
    rbfsafe_coordination
    rbfsafe_deployment
    rbfsafe_execution
    rbfsafe_transparency
    rbfsafe_witness
    rbfsafe_provenance)

add_library(rbfsafe INTERFACE)
add_library(RBFSafe::rbfsafe ALIAS rbfsafe)
set_target_properties(rbfsafe PROPERTIES EXPORT_NAME rbfsafe)
target_link_libraries(rbfsafe INTERFACE rbfsafe_applications rbfsafe_assurance)

set(RBFSAFE_OMPL_BUILT 0)
if(RBFSAFE_BUILD_OMPL)
    find_package(ompl CONFIG REQUIRED)
    if(NOT TARGET ompl::ompl)
        if(NOT OMPL_INCLUDE_DIRS OR NOT OMPL_LIBRARIES)
            message(FATAL_ERROR "The OMPL package did not provide ompl::ompl or legacy include/library variables")
        endif()
        set(RBFSAFE_OMPL_INCLUDE_DIRS "")
        foreach(ompl_include_directory IN LISTS OMPL_INCLUDE_DIRS)
            if(EXISTS "${ompl_include_directory}")
                list(APPEND RBFSAFE_OMPL_INCLUDE_DIRS "${ompl_include_directory}")
            endif()
        endforeach()
        add_library(ompl::ompl INTERFACE IMPORTED GLOBAL)
        set_target_properties(ompl::ompl PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${RBFSAFE_OMPL_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${OMPL_LIBRARIES}")
    endif()

    add_library(rbfsafe_ompl src/applications/planning/ompl.cpp)
    add_library(RBFSafe::ompl ALIAS rbfsafe_ompl)
    set_target_properties(rbfsafe_ompl PROPERTIES EXPORT_NAME ompl)
    target_include_directories(rbfsafe_ompl
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(rbfsafe_ompl PUBLIC rbfsafe_planning ompl::ompl)
    rbfsafe_configure_target(rbfsafe_ompl)
    set(RBFSAFE_OMPL_BUILT 1)
endif()

if(RBFSAFE_BUILD_TOOLS)
    add_executable(rbfsafe_inspect tools/inspect.cpp)
    target_link_libraries(rbfsafe_inspect PRIVATE RBFSafe::rbfsafe)
    rbfsafe_configure_target(rbfsafe_inspect)
    set_target_properties(rbfsafe_inspect PROPERTIES OUTPUT_NAME "rbfsafe-inspect")
    if(NOT SKBUILD)
        install(TARGETS rbfsafe_inspect RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

function(rbfsafe_add_example name source dependency)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${dependency})
    rbfsafe_configure_target(${name})
endfunction()

if(RBFSAFE_BUILD_EXAMPLES)
    rbfsafe_add_example(rbfsafe_quickstart examples/core/quickstart.cpp RBFSafe::rbfsafe)
    rbfsafe_add_example(rbfsafe_hipac_quickstart examples/core/hipac_quickstart.cpp RBFSafe::corridor)
    rbfsafe_add_example(rbfsafe_safe_ik_quickstart examples/core/safe_ik_quickstart.cpp RBFSafe::ik)
    rbfsafe_add_example(rbfsafe_dynamic_update_quickstart examples/core/dynamic_update_quickstart.cpp RBFSafe::update)
    rbfsafe_add_example(rbfsafe_region_database_quickstart examples/core/region_database_quickstart.cpp RBFSafe::regions)
    rbfsafe_add_example(rbfsafe_planning_quickstart examples/core/planning_quickstart.cpp RBFSafe::planning)
    rbfsafe_add_example(rbfsafe_optimization_quickstart examples/core/optimization_quickstart.cpp RBFSafe::optimization)
    rbfsafe_add_example(rbfsafe_shield_quickstart examples/core/shield_quickstart.cpp RBFSafe::shield)

    rbfsafe_add_example(rbfsafe_policy_safety_quickstart examples/policy/policy_safety_quickstart.cpp RBFSafe::policy)
    rbfsafe_add_example(rbfsafe_calibrated_policy_quickstart examples/policy/calibrated_policy_quickstart.cpp RBFSafe::policy)
    rbfsafe_add_example(rbfsafe_calibration_lifecycle_quickstart examples/policy/calibration_lifecycle_quickstart.cpp RBFSafe::policy)

    rbfsafe_add_example(rbfsafe_safety_memory_quickstart examples/artifacts/safety_memory_quickstart.cpp RBFSafe::memory)
    rbfsafe_add_example(rbfsafe_safety_memory_store_quickstart examples/artifacts/safety_memory_store_quickstart.cpp RBFSafe::memory)
    rbfsafe_add_example(rbfsafe_fleet_schedule_archive_quickstart examples/artifacts/fleet_schedule_archive_quickstart.cpp RBFSafe::memory)
    rbfsafe_add_example(rbfsafe_artifact_attestation_quickstart examples/artifacts/artifact_attestation_quickstart.cpp RBFSafe::trust)
    rbfsafe_add_example(rbfsafe_remote_artifact_quickstart examples/artifacts/remote_artifact_quickstart.cpp RBFSafe::remote)
    rbfsafe_add_example(rbfsafe_provenance_quickstart examples/artifacts/provenance_quickstart.cpp RBFSafe::provenance)

    rbfsafe_add_example(rbfsafe_public_identity_quickstart examples/deployment/public_identity_quickstart.cpp RBFSafe::identity)
    rbfsafe_add_example(rbfsafe_deployment_profile_quickstart examples/deployment/deployment_profile_quickstart.cpp RBFSafe::deployment)
    rbfsafe_add_example(rbfsafe_bounded_execution_session_quickstart examples/deployment/bounded_execution_session_quickstart.cpp RBFSafe::execution)

    rbfsafe_add_example(rbfsafe_continuous_occupancy_quickstart examples/coordination/continuous_occupancy_quickstart.cpp RBFSafe::occupancy)
    rbfsafe_add_example(rbfsafe_continuous_robot_scene_occupancy_quickstart examples/coordination/continuous_robot_scene_occupancy_quickstart.cpp RBFSafe::occupancy)
    rbfsafe_add_example(rbfsafe_occupancy_publication_quickstart examples/coordination/occupancy_publication_quickstart.cpp RBFSafe::coordination)
    rbfsafe_add_example(rbfsafe_occupancy_publication_history_quickstart examples/coordination/occupancy_publication_history_quickstart.cpp RBFSafe::coordination)
    rbfsafe_add_example(rbfsafe_rotating_occupancy_publication_history_quickstart examples/coordination/rotating_occupancy_publication_history_quickstart.cpp RBFSafe::coordination)
    rbfsafe_add_example(rbfsafe_coordinated_reservation_quickstart examples/coordination/coordinated_reservation_quickstart.cpp RBFSafe::coordination)

    if(RBFSAFE_BUILD_OMPL)
        rbfsafe_add_example(rbfsafe_ompl_quickstart examples/core/ompl_quickstart.cpp RBFSafe::ompl)
    endif()
endif()

if(RBFSAFE_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

if(RBFSAFE_BUILD_BENCHMARKS)
    add_executable(rbfsafe_release_benchmark benchmarks/release_benchmark.cpp)
    target_link_libraries(rbfsafe_release_benchmark PRIVATE RBFSafe::rbfsafe)
    rbfsafe_configure_target(rbfsafe_release_benchmark)
    set_target_properties(rbfsafe_release_benchmark PROPERTIES OUTPUT_NAME "rbfsafe-release-benchmark")
    if(RBFSAFE_BUILD_TESTS)
        add_test(NAME rbfsafe_release_benchmark_smoke
            COMMAND rbfsafe_release_benchmark
                --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/data/release-fixtures
                --iterations 128 --json)
        add_test(NAME rbfsafe_release_benchmark_soak
            COMMAND rbfsafe_release_benchmark
                --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/data/release-fixtures
                --iterations 8192 --json)
        set_tests_properties(rbfsafe_release_benchmark_soak PROPERTIES LABELS "soak" TIMEOUT 180)
    endif()
    if(NOT SKBUILD)
        install(TARGETS rbfsafe_release_benchmark RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

if(RBFSAFE_BUILD_PYTHON)
    find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
    find_package(pybind11 CONFIG REQUIRED)
    pybind11_add_module(_rbfsafe python/bindings.cpp python/bindings_policy.cpp python/bindings_calibration.cpp python/bindings_memory.cpp python/bindings_trust.cpp python/bindings_remote.cpp python/bindings_identity.cpp python/bindings_deployment.cpp python/bindings_execution.cpp python/bindings_transparency.cpp python/bindings_witness.cpp python/bindings_provenance.cpp python/bindings_occupancy.cpp python/bindings_coordination.cpp)
    target_link_libraries(_rbfsafe PRIVATE RBFSafe::rbfsafe)
    target_include_directories(_rbfsafe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    rbfsafe_configure_target(_rbfsafe)
    install(TARGETS _rbfsafe LIBRARY DESTINATION rbfsafe RUNTIME DESTINATION rbfsafe)
    install(DIRECTORY python/rbfsafe/ DESTINATION rbfsafe
        FILES_MATCHING
        PATTERN "*.py"
        PATTERN "__pycache__" EXCLUDE)
    install(FILES THIRD_PARTY_NOTICES.md
        DESTINATION rbfsafe)
    install(FILES third_party/monocypher/LICENCE.md
        DESTINATION rbfsafe
        RENAME MONOCYPHER_LICENSE.md)
endif()

if(NOT SKBUILD)
    install(FILES LICENSE THIRD_PARTY_NOTICES.md README.md CHANGELOG.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe)
    install(FILES third_party/monocypher/LICENCE.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/licenses/Monocypher
        RENAME LICENSE.md)

    install(TARGETS rbfsafe_core rbfsafe_envelope rbfsafe_geometry rbfsafe_lect rbfsafe_atlas rbfsafe_occupancy rbfsafe_update rbfsafe_ik rbfsafe_corridor rbfsafe_regions rbfsafe_planning rbfsafe_optimization rbfsafe_shield rbfsafe_policy rbfsafe_memory rbfsafe_trust rbfsafe_remote rbfsafe_identity rbfsafe_coordination rbfsafe_deployment rbfsafe_execution rbfsafe_transparency rbfsafe_witness rbfsafe_provenance rbfsafe_applications rbfsafe_assurance rbfsafe
        EXPORT RBFSafeTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    if(RBFSAFE_BUILD_OMPL)
        install(TARGETS rbfsafe_ompl
            EXPORT RBFSafeOmplTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
        install(EXPORT RBFSafeOmplTargets
            FILE RBFSafeOmplTargets.cmake
            NAMESPACE RBFSafe::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RBFSafe)
    endif()
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY data/release-fixtures/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures)
    install(DIRECTORY data/safety_memory_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/safety_memory_schema1)
    install(DIRECTORY data/safety_memory_store_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/safety_memory_store_schema1)
    install(DIRECTORY data/fleet_schedule_archive_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/fleet_schedule_archive_schema1)
    install(DIRECTORY data/artifact_attestation_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/artifact_attestation_schema1)
    install(DIRECTORY data/artifact_transfer_journal_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/artifact_transfer_journal_schema1)
    install(DIRECTORY data/artifact_transfer_journal_schema2/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/artifact_transfer_journal_schema2)
    install(DIRECTORY data/service_trust_bundle_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/service_trust_bundle_schema1)
    install(DIRECTORY data/service_trust_bundle_schema2/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/service_trust_bundle_schema2)
    install(DIRECTORY data/service_trust_bundle_schema3/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/service_trust_bundle_schema3)
    install(DIRECTORY data/service_trust_history_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/service_trust_history_schema1)
    install(DIRECTORY data/service_trust_history_schema2/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/service_trust_history_schema2)
    install(DIRECTORY data/service_trust_checkpoint_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/service_trust_checkpoint_schema1)
    install(DIRECTORY data/reviewed_deployment_profile_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/reviewed_deployment_profile_schema1)
    install(DIRECTORY data/bounded_execution_session_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/bounded_execution_session_schema1)
    install(DIRECTORY data/execution_ledger_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/execution_ledger_schema1)
    install(DIRECTORY data/transparency_log_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/transparency_log_schema1)
    install(DIRECTORY data/transparency_gossip_archive_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/transparency_gossip_archive_schema1)
    install(DIRECTORY data/provenance_bundle_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/provenance_bundle_schema1)
    install(DIRECTORY data/continuous_fleet_occupancy_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/continuous_fleet_occupancy_schema1)
    install(DIRECTORY data/continuous_fleet_occupancy_schema2/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/continuous_fleet_occupancy_schema2)
    install(DIRECTORY data/continuous_robot_scene_occupancy_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/continuous_robot_scene_occupancy_schema1)
    install(DIRECTORY data/occupancy_publication_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/occupancy_publication_schema1)
    install(DIRECTORY data/occupancy_publication_history_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/occupancy_publication_history_schema1)
    install(DIRECTORY data/rotating_occupancy_publication_history_schema1/
        DESTINATION
            ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/rotating_occupancy_publication_history_schema1)
    install(DIRECTORY data/coordinated_reservation_agreement_schema1/
        DESTINATION
            ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/coordinated_reservation_agreement_schema1)
    install(DIRECTORY data/policy_calibration_profile_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/policy_calibration_profile_schema1)
    install(DIRECTORY data/policy_calibration_lifecycle_schema1/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe/fixtures/policy_calibration_lifecycle_schema1)
    install(EXPORT RBFSafeTargets
        FILE RBFSafeTargets.cmake
        NAMESPACE RBFSafe::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RBFSafe)

    configure_package_config_file(
        cmake/RBFSafeConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/RBFSafeConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RBFSafe)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/RBFSafeConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/RBFSafeConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/RBFSafeConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RBFSafe)
endif()
