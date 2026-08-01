cmake_minimum_required(VERSION 3.23)
project(
    RBFSafe
    VERSION 4.7.0
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
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
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

add_library(rbfsafe_geometry
    src/sha256.cpp
    src/json.cpp
    src/model.cpp
    src/geometry.cpp
    src/certificate.cpp)
add_library(RBFSafe::geometry ALIAS rbfsafe_geometry)
set_target_properties(rbfsafe_geometry PROPERTIES EXPORT_NAME geometry)
target_include_directories(rbfsafe_geometry
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
rbfsafe_configure_target(rbfsafe_geometry)

add_library(rbfsafe_lect src/lect.cpp)
add_library(RBFSafe::lect ALIAS rbfsafe_lect)
set_target_properties(rbfsafe_lect PROPERTIES EXPORT_NAME lect)
target_include_directories(rbfsafe_lect
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_lect PUBLIC rbfsafe_geometry)
rbfsafe_configure_target(rbfsafe_lect)

add_library(rbfsafe_atlas
    src/atlas.cpp
    src/atlas_identity.cpp
    src/persistence.cpp
    src/region_index.cpp
    src/scene_delta.cpp
    src/trajectory.cpp)
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
    src/moving_obstacle_occupancy.cpp
    src/occupancy.cpp
    src/occupancy_persistence.cpp)
add_library(RBFSafe::occupancy ALIAS rbfsafe_occupancy)
set_target_properties(rbfsafe_occupancy PROPERTIES EXPORT_NAME occupancy)
target_include_directories(rbfsafe_occupancy
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_occupancy PUBLIC rbfsafe_atlas)
rbfsafe_configure_target(rbfsafe_occupancy)

add_library(rbfsafe_update src/dynamic.cpp src/version_store.cpp)
add_library(RBFSafe::update ALIAS rbfsafe_update)
set_target_properties(rbfsafe_update PROPERTIES EXPORT_NAME update)
target_include_directories(rbfsafe_update
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_update PUBLIC rbfsafe_atlas)
rbfsafe_configure_target(rbfsafe_update)

add_library(rbfsafe_ik src/safe_ik.cpp)
add_library(RBFSafe::ik ALIAS rbfsafe_ik)
set_target_properties(rbfsafe_ik PROPERTIES EXPORT_NAME ik)
target_include_directories(rbfsafe_ik
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_ik PUBLIC rbfsafe_atlas)
rbfsafe_configure_target(rbfsafe_ik)

add_library(rbfsafe_corridor src/corridor.cpp src/corridor_persistence.cpp)
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
    src/higher_order.cpp
    src/portal.cpp
    src/region_database.cpp
    src/region_database_persistence.cpp
    src/region_identity.cpp)
add_library(RBFSafe::regions ALIAS rbfsafe_regions)
set_target_properties(rbfsafe_regions PROPERTIES EXPORT_NAME regions)
target_include_directories(rbfsafe_regions
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_regions PUBLIC rbfsafe_corridor)
rbfsafe_configure_target(rbfsafe_regions)

add_library(rbfsafe_planning src/planning.cpp)
add_library(RBFSafe::planning ALIAS rbfsafe_planning)
set_target_properties(rbfsafe_planning PROPERTIES EXPORT_NAME planning)
target_include_directories(rbfsafe_planning
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_planning PUBLIC rbfsafe_atlas)
rbfsafe_configure_target(rbfsafe_planning)

add_library(rbfsafe_optimization src/optimization.cpp)
add_library(RBFSafe::optimization ALIAS rbfsafe_optimization)
set_target_properties(rbfsafe_optimization PROPERTIES EXPORT_NAME optimization)
target_include_directories(rbfsafe_optimization
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_optimization PUBLIC rbfsafe_regions)
rbfsafe_configure_target(rbfsafe_optimization)

add_library(rbfsafe_shield src/shield.cpp)
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
    src/calibration.cpp
    src/calibration_lifecycle.cpp
    src/calibration_lifecycle_persistence.cpp
    src/policy.cpp
    src/policy_persistence.cpp)
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
    src/fleet_schedule_archive.cpp
    src/fleet_schedule_persistence.cpp
    src/memory.cpp
    src/memory_persistence.cpp
    src/memory_store.cpp)
add_library(RBFSafe::memory ALIAS rbfsafe_memory)
set_target_properties(rbfsafe_memory PROPERTIES EXPORT_NAME memory)
target_include_directories(rbfsafe_memory
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_memory PUBLIC rbfsafe_atlas)
rbfsafe_configure_target(rbfsafe_memory)

add_library(rbfsafe_trust src/trust.cpp)
add_library(RBFSafe::trust ALIAS rbfsafe_trust)
set_target_properties(rbfsafe_trust PROPERTIES EXPORT_NAME trust)
target_include_directories(rbfsafe_trust
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_trust PUBLIC rbfsafe_memory)
rbfsafe_configure_target(rbfsafe_trust)

add_library(rbfsafe_remote src/remote.cpp src/remote_persistence.cpp)
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
    src/identity.cpp
    src/identity_checkpoint.cpp
    src/identity_history.cpp
    src/identity_persistence.cpp
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
    src/coordination.cpp
    src/coordination_history.cpp
    src/coordination_rotating_history.cpp
    src/coordination_reservation.cpp
    src/coordination_reservation_persistence.cpp
    src/coordination_persistence.cpp)
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
    src/deployment.cpp
    src/deployment_persistence.cpp)
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
    src/execution.cpp
    src/execution_persistence.cpp
    src/execution_ledger.cpp
    src/execution_ledger_persistence.cpp)
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
    src/transparency.cpp
    src/transparency_persistence.cpp)
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
    src/witness.cpp
    src/witness_persistence.cpp)
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
    src/provenance.cpp)
add_library(RBFSafe::provenance ALIAS rbfsafe_provenance)
set_target_properties(rbfsafe_provenance PROPERTIES EXPORT_NAME provenance)
target_include_directories(rbfsafe_provenance
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_provenance PUBLIC rbfsafe_identity)
rbfsafe_configure_target(rbfsafe_provenance)

add_library(rbfsafe INTERFACE)
add_library(RBFSafe::rbfsafe ALIAS rbfsafe)
set_target_properties(rbfsafe PROPERTIES EXPORT_NAME rbfsafe)
target_link_libraries(rbfsafe INTERFACE rbfsafe_optimization rbfsafe_policy rbfsafe_witness rbfsafe_provenance rbfsafe_coordination rbfsafe_update rbfsafe_planning)

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

    add_library(rbfsafe_ompl src/ompl.cpp)
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

if(RBFSAFE_BUILD_EXAMPLES)
    add_executable(rbfsafe_quickstart examples/quickstart.cpp)
    target_link_libraries(rbfsafe_quickstart PRIVATE RBFSafe::rbfsafe)
    rbfsafe_configure_target(rbfsafe_quickstart)
    add_executable(rbfsafe_hipac_quickstart examples/hipac_quickstart.cpp)
    target_link_libraries(rbfsafe_hipac_quickstart PRIVATE RBFSafe::corridor)
    rbfsafe_configure_target(rbfsafe_hipac_quickstart)
    add_executable(rbfsafe_safe_ik_quickstart examples/safe_ik_quickstart.cpp)
    target_link_libraries(rbfsafe_safe_ik_quickstart PRIVATE RBFSafe::ik)
    rbfsafe_configure_target(rbfsafe_safe_ik_quickstart)
    add_executable(rbfsafe_dynamic_update_quickstart examples/dynamic_update_quickstart.cpp)
    target_link_libraries(rbfsafe_dynamic_update_quickstart PRIVATE RBFSafe::update)
    rbfsafe_configure_target(rbfsafe_dynamic_update_quickstart)
    add_executable(rbfsafe_region_database_quickstart examples/region_database_quickstart.cpp)
    target_link_libraries(rbfsafe_region_database_quickstart PRIVATE RBFSafe::regions)
    rbfsafe_configure_target(rbfsafe_region_database_quickstart)
    add_executable(rbfsafe_planning_quickstart examples/planning_quickstart.cpp)
    target_link_libraries(rbfsafe_planning_quickstart PRIVATE RBFSafe::planning)
    rbfsafe_configure_target(rbfsafe_planning_quickstart)
    add_executable(rbfsafe_optimization_quickstart examples/optimization_quickstart.cpp)
    target_link_libraries(rbfsafe_optimization_quickstart PRIVATE RBFSafe::optimization)
    rbfsafe_configure_target(rbfsafe_optimization_quickstart)
    add_executable(rbfsafe_shield_quickstart examples/shield_quickstart.cpp)
    target_link_libraries(rbfsafe_shield_quickstart PRIVATE RBFSafe::shield)
    rbfsafe_configure_target(rbfsafe_shield_quickstart)
    add_executable(rbfsafe_policy_safety_quickstart examples/policy_safety_quickstart.cpp)
    target_link_libraries(rbfsafe_policy_safety_quickstart PRIVATE RBFSafe::policy)
    rbfsafe_configure_target(rbfsafe_policy_safety_quickstart)
    add_executable(rbfsafe_calibrated_policy_quickstart examples/calibrated_policy_quickstart.cpp)
    target_link_libraries(rbfsafe_calibrated_policy_quickstart PRIVATE RBFSafe::policy)
    rbfsafe_configure_target(rbfsafe_calibrated_policy_quickstart)
    add_executable(rbfsafe_calibration_lifecycle_quickstart examples/calibration_lifecycle_quickstart.cpp)
    target_link_libraries(rbfsafe_calibration_lifecycle_quickstart PRIVATE RBFSafe::policy)
    rbfsafe_configure_target(rbfsafe_calibration_lifecycle_quickstart)
    add_executable(rbfsafe_safety_memory_quickstart examples/safety_memory_quickstart.cpp)
    target_link_libraries(rbfsafe_safety_memory_quickstart PRIVATE RBFSafe::memory)
    rbfsafe_configure_target(rbfsafe_safety_memory_quickstart)
    add_executable(rbfsafe_safety_memory_store_quickstart examples/safety_memory_store_quickstart.cpp)
    target_link_libraries(rbfsafe_safety_memory_store_quickstart PRIVATE RBFSafe::memory)
    rbfsafe_configure_target(rbfsafe_safety_memory_store_quickstart)
    add_executable(rbfsafe_fleet_schedule_archive_quickstart examples/fleet_schedule_archive_quickstart.cpp)
    target_link_libraries(rbfsafe_fleet_schedule_archive_quickstart PRIVATE RBFSafe::memory)
    rbfsafe_configure_target(rbfsafe_fleet_schedule_archive_quickstart)
    add_executable(rbfsafe_artifact_attestation_quickstart examples/artifact_attestation_quickstart.cpp)
    target_link_libraries(rbfsafe_artifact_attestation_quickstart PRIVATE RBFSafe::trust)
    rbfsafe_configure_target(rbfsafe_artifact_attestation_quickstart)
    add_executable(rbfsafe_remote_artifact_quickstart examples/remote_artifact_quickstart.cpp)
    target_link_libraries(rbfsafe_remote_artifact_quickstart PRIVATE RBFSafe::remote)
    rbfsafe_configure_target(rbfsafe_remote_artifact_quickstart)
    add_executable(rbfsafe_public_identity_quickstart examples/public_identity_quickstart.cpp)
    target_link_libraries(rbfsafe_public_identity_quickstart PRIVATE RBFSafe::identity)
    rbfsafe_configure_target(rbfsafe_public_identity_quickstart)
    add_executable(rbfsafe_deployment_profile_quickstart examples/deployment_profile_quickstart.cpp)
    target_link_libraries(rbfsafe_deployment_profile_quickstart PRIVATE RBFSafe::deployment)
    rbfsafe_configure_target(rbfsafe_deployment_profile_quickstart)
    add_executable(rbfsafe_bounded_execution_session_quickstart examples/bounded_execution_session_quickstart.cpp)
    target_link_libraries(rbfsafe_bounded_execution_session_quickstart PRIVATE RBFSafe::execution)
    rbfsafe_configure_target(rbfsafe_bounded_execution_session_quickstart)
    add_executable(rbfsafe_provenance_quickstart examples/provenance_quickstart.cpp)
    target_link_libraries(rbfsafe_provenance_quickstart PRIVATE RBFSafe::provenance)
    rbfsafe_configure_target(rbfsafe_provenance_quickstart)
    add_executable(rbfsafe_continuous_occupancy_quickstart
        examples/continuous_occupancy_quickstart.cpp)
    target_link_libraries(rbfsafe_continuous_occupancy_quickstart
        PRIVATE RBFSafe::occupancy)
    rbfsafe_configure_target(rbfsafe_continuous_occupancy_quickstart)
    add_executable(rbfsafe_continuous_robot_scene_occupancy_quickstart
        examples/continuous_robot_scene_occupancy_quickstart.cpp)
    target_link_libraries(rbfsafe_continuous_robot_scene_occupancy_quickstart
        PRIVATE RBFSafe::occupancy)
    rbfsafe_configure_target(
        rbfsafe_continuous_robot_scene_occupancy_quickstart)
    add_executable(rbfsafe_occupancy_publication_quickstart
        examples/occupancy_publication_quickstart.cpp)
    target_link_libraries(rbfsafe_occupancy_publication_quickstart
        PRIVATE RBFSafe::coordination)
    rbfsafe_configure_target(rbfsafe_occupancy_publication_quickstart)

    add_executable(rbfsafe_occupancy_publication_history_quickstart
        examples/occupancy_publication_history_quickstart.cpp)
    target_link_libraries(rbfsafe_occupancy_publication_history_quickstart
        PRIVATE RBFSafe::coordination)
    rbfsafe_configure_target(rbfsafe_occupancy_publication_history_quickstart)
    add_executable(rbfsafe_rotating_occupancy_publication_history_quickstart
        examples/rotating_occupancy_publication_history_quickstart.cpp)
    target_link_libraries(
        rbfsafe_rotating_occupancy_publication_history_quickstart
        PRIVATE RBFSafe::coordination)
    rbfsafe_configure_target(
        rbfsafe_rotating_occupancy_publication_history_quickstart)
    add_executable(rbfsafe_coordinated_reservation_quickstart
        examples/coordinated_reservation_quickstart.cpp)
    target_link_libraries(rbfsafe_coordinated_reservation_quickstart
        PRIVATE RBFSafe::coordination)
    rbfsafe_configure_target(rbfsafe_coordinated_reservation_quickstart)
    if(RBFSAFE_BUILD_OMPL)
        add_executable(rbfsafe_ompl_quickstart examples/ompl_quickstart.cpp)
        target_link_libraries(rbfsafe_ompl_quickstart PRIVATE RBFSafe::ompl)
        rbfsafe_configure_target(rbfsafe_ompl_quickstart)
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

    install(TARGETS rbfsafe_geometry rbfsafe_lect rbfsafe_atlas rbfsafe_occupancy rbfsafe_update rbfsafe_ik rbfsafe_corridor rbfsafe_regions rbfsafe_planning rbfsafe_optimization rbfsafe_shield rbfsafe_policy rbfsafe_memory rbfsafe_trust rbfsafe_remote rbfsafe_identity rbfsafe_coordination rbfsafe_deployment rbfsafe_execution rbfsafe_transparency rbfsafe_witness rbfsafe_provenance rbfsafe
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
