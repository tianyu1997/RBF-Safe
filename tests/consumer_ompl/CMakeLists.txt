cmake_minimum_required(VERSION 3.23)
project(RBFSafeOmplConsumer LANGUAGES CXX)

find_package(RBFSafe 2.0 REQUIRED COMPONENTS ompl)

add_executable(rbfsafe_ompl_consumer main.cpp)
target_link_libraries(rbfsafe_ompl_consumer PRIVATE RBFSafe::ompl)
target_compile_features(rbfsafe_ompl_consumer PRIVATE cxx_std_20)
