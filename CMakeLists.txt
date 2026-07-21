cmake_minimum_required(VERSION 3.23)
project(
    RBFSafe
    VERSION 0.2.0
    DESCRIPTION "Deterministic certified safe-region atlases for serial robots"
    HOMEPAGE_URL "https://github.com/tianyu1997/RBF-Safe"
    LANGUAGES CXX)

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

add_library(rbfsafe_atlas src/atlas.cpp src/persistence.cpp src/trajectory.cpp)
add_library(RBFSafe::atlas ALIAS rbfsafe_atlas)
set_target_properties(rbfsafe_atlas PROPERTIES EXPORT_NAME atlas)
target_include_directories(rbfsafe_atlas
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbfsafe_atlas PUBLIC rbfsafe_geometry rbfsafe_lect)
rbfsafe_configure_target(rbfsafe_atlas)

add_library(rbfsafe INTERFACE)
add_library(RBFSafe::rbfsafe ALIAS rbfsafe)
set_target_properties(rbfsafe PROPERTIES EXPORT_NAME rbfsafe)
target_link_libraries(rbfsafe INTERFACE rbfsafe_atlas)

if(RBFSAFE_BUILD_TOOLS)
    add_executable(rbfsafe_inspect tools/inspect.cpp)
    target_link_libraries(rbfsafe_inspect PRIVATE RBFSafe::atlas)
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
endif()

if(RBFSAFE_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

if(RBFSAFE_BUILD_PYTHON)
    find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
    find_package(pybind11 CONFIG REQUIRED)
    pybind11_add_module(_rbfsafe python/bindings.cpp)
    target_link_libraries(_rbfsafe PRIVATE RBFSafe::rbfsafe)
    target_include_directories(_rbfsafe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    install(TARGETS _rbfsafe LIBRARY DESTINATION rbfsafe RUNTIME DESTINATION rbfsafe)
    install(DIRECTORY python/rbfsafe/ DESTINATION rbfsafe FILES_MATCHING PATTERN "*.py")
endif()

if(NOT SKBUILD)
    install(FILES LICENSE README.md CHANGELOG.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/RBFSafe)

    install(TARGETS rbfsafe_geometry rbfsafe_lect rbfsafe_atlas rbfsafe
        EXPORT RBFSafeTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
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
