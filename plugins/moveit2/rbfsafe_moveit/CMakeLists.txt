cmake_minimum_required(VERSION 3.22)
project(rbfsafe_moveit VERSION 3.0.0 LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(moveit_core REQUIRED)
find_package(moveit_msgs REQUIRED)
find_package(pluginlib REQUIRED)
find_package(rclcpp REQUIRED)
find_package(RBFSafe 3.0 CONFIG REQUIRED)

add_library(rbfsafe_moveit_plugins SHARED
  src/certified_constraint_sampler.cpp
  src/plugin_resources.cpp
  src/planning_adapters.cpp
  src/safe_ik_kinematics_plugin.cpp)
target_compile_features(rbfsafe_moveit_plugins PUBLIC cxx_std_20)
target_compile_options(rbfsafe_moveit_plugins PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
target_include_directories(rbfsafe_moveit_plugins PRIVATE include)
target_link_libraries(rbfsafe_moveit_plugins RBFSafe::rbfsafe)
ament_target_dependencies(rbfsafe_moveit_plugins
  geometry_msgs
  moveit_core
  moveit_msgs
  pluginlib
  rclcpp)

pluginlib_export_plugin_description_file(moveit_core rbfsafe_moveit_plugins.xml)

install(TARGETS rbfsafe_moveit_plugins
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})
install(FILES LICENSE README.md DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(srdfdom REQUIRED)
  find_package(urdfdom REQUIRED)
  ament_add_gtest(test_plugin_loader test/test_plugin_loader.cpp)
  target_compile_features(test_plugin_loader PUBLIC cxx_std_20)
  target_link_libraries(test_plugin_loader RBFSafe::rbfsafe)
  ament_target_dependencies(test_plugin_loader moveit_core moveit_msgs pluginlib rclcpp srdfdom urdfdom)
endif()

ament_package()
