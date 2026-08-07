#include "binding_support.h"

#include <rbfsafe/modules/assurance.h>

#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <array>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace rbfsafe::python_binding {

void bind_occupancy(py::module_& module) {
    py::class_<TimedConfiguration>(module, "TimedConfiguration")
        .def(py::init<std::uint64_t, Configuration>(), py::arg("tick") = 0,
             py::arg("configuration") = Configuration{})
        .def_readwrite("tick", &TimedConfiguration::tick)
        .def_readwrite("configuration", &TimedConfiguration::configuration);

    py::class_<TimedWorkspaceAabb>(module, "TimedWorkspaceAabb")
        .def(py::init<std::uint64_t, WorkspaceAabb>(), py::arg("tick") = 0,
             py::arg("bounds") = WorkspaceAabb{})
        .def_readwrite("tick", &TimedWorkspaceAabb::tick)
        .def_readwrite("bounds", &TimedWorkspaceAabb::bounds);

    py::class_<DeploymentFrameBounds>(module, "DeploymentFrameBounds")
        .def(py::init<>())
        .def_readwrite("rotation", &DeploymentFrameBounds::rotation)
        .def_readwrite("translation", &DeploymentFrameBounds::translation)
        .def_readwrite("translation_uncertainty", &DeploymentFrameBounds::translation_uncertainty)
        .def_readwrite("angular_uncertainty_radians", &DeploymentFrameBounds::angular_uncertainty_radians)
        .def("valid", &DeploymentFrameBounds::valid)
        .def("exact", &DeploymentFrameBounds::exact);

    py::class_<SweptLinkOccupancySlice>(module, "SweptLinkOccupancySlice")
        .def_readonly("id", &SweptLinkOccupancySlice::id)
        .def_readonly("trajectory_segment_index", &SweptLinkOccupancySlice::trajectory_segment_index)
        .def_readonly("begin_tick", &SweptLinkOccupancySlice::begin_tick)
        .def_readonly("end_tick", &SweptLinkOccupancySlice::end_tick)
        .def_readonly("configuration_domain", &SweptLinkOccupancySlice::configuration_domain)
        .def_readonly("link_envelopes", &SweptLinkOccupancySlice::link_envelopes);

    py::class_<ContinuousOccupancyBuildOptions>(module, "ContinuousOccupancyBuildOptions")
        .def(py::init<>())
        .def_readwrite("maximum_input_waypoints", &ContinuousOccupancyBuildOptions::maximum_input_waypoints)
        .def_readwrite("maximum_slices", &ContinuousOccupancyBuildOptions::maximum_slices)
        .def_readwrite("maximum_link_envelopes", &ContinuousOccupancyBuildOptions::maximum_link_envelopes)
        .def_readwrite("maximum_subdivision_depth",
                       &ContinuousOccupancyBuildOptions::maximum_subdivision_depth)
        .def_readwrite("maximum_normalized_joint_width",
                       &ContinuousOccupancyBuildOptions::maximum_normalized_joint_width)
        .def_readwrite("link_padding", &ContinuousOccupancyBuildOptions::link_padding)
        .def_readwrite("cancellation", &ContinuousOccupancyBuildOptions::cancellation);

    py::class_<ContinuousOccupancyReplayOptions>(module, "ContinuousOccupancyReplayOptions")
        .def(py::init<>())
        .def_readwrite("maximum_input_waypoints", &ContinuousOccupancyReplayOptions::maximum_input_waypoints)
        .def_readwrite("maximum_slices", &ContinuousOccupancyReplayOptions::maximum_slices)
        .def_readwrite("maximum_link_envelopes", &ContinuousOccupancyReplayOptions::maximum_link_envelopes)
        .def_readwrite("cancellation", &ContinuousOccupancyReplayOptions::cancellation);

    py::class_<RobotTrajectoryOccupancy>(module, "RobotTrajectoryOccupancy")
        .def_readonly("storage_schema", &RobotTrajectoryOccupancy::storage_schema)
        .def_readonly("id", &RobotTrajectoryOccupancy::id)
        .def_readonly("timeline_id", &RobotTrajectoryOccupancy::timeline_id)
        .def_readonly("workspace_frame_id", &RobotTrajectoryOccupancy::workspace_frame_id)
        .def_readonly("deployment_id", &RobotTrajectoryOccupancy::deployment_id)
        .def_readonly("robot_digest", &RobotTrajectoryOccupancy::robot_digest)
        .def_readonly("workspace_translation", &RobotTrajectoryOccupancy::workspace_translation)
        .def_readonly("workspace_rotation", &RobotTrajectoryOccupancy::workspace_rotation)
        .def_readonly("workspace_translation_uncertainty",
                      &RobotTrajectoryOccupancy::workspace_translation_uncertainty)
        .def_readonly("workspace_angular_uncertainty_radians",
                      &RobotTrajectoryOccupancy::workspace_angular_uncertainty_radians)
        .def_readonly("algorithm", &RobotTrajectoryOccupancy::algorithm)
        .def_readonly("algorithm_version", &RobotTrajectoryOccupancy::algorithm_version)
        .def_readonly("maximum_subdivision_depth", &RobotTrajectoryOccupancy::maximum_subdivision_depth)
        .def_readonly("maximum_normalized_joint_width",
                      &RobotTrajectoryOccupancy::maximum_normalized_joint_width)
        .def_readonly("link_padding", &RobotTrajectoryOccupancy::link_padding)
        .def_readonly("trajectory", &RobotTrajectoryOccupancy::trajectory)
        .def_readonly("slices", &RobotTrajectoryOccupancy::slices)
        .def("valid", &RobotTrajectoryOccupancy::valid)
        .def_property_readonly("evidence", &RobotTrajectoryOccupancy::evidence)
        .def_property_readonly("authorizes_execution", &RobotTrajectoryOccupancy::authorizes_execution);

    py::class_<MovingObstacleOccupancySlice>(module, "MovingObstacleOccupancySlice")
        .def_readonly("id", &MovingObstacleOccupancySlice::id)
        .def_readonly("trajectory_segment_index", &MovingObstacleOccupancySlice::trajectory_segment_index)
        .def_readonly("begin_tick", &MovingObstacleOccupancySlice::begin_tick)
        .def_readonly("end_tick", &MovingObstacleOccupancySlice::end_tick)
        .def_readonly("swept_bounds", &MovingObstacleOccupancySlice::swept_bounds);

    py::class_<MovingObstacleOccupancyBuildOptions>(module, "MovingObstacleOccupancyBuildOptions")
        .def(py::init<>())
        .def_readwrite("maximum_input_waypoints",
                       &MovingObstacleOccupancyBuildOptions::maximum_input_waypoints)
        .def_readwrite("maximum_slices", &MovingObstacleOccupancyBuildOptions::maximum_slices)
        .def_readwrite("obstacle_padding", &MovingObstacleOccupancyBuildOptions::obstacle_padding)
        .def_readwrite("cancellation", &MovingObstacleOccupancyBuildOptions::cancellation);

    py::class_<MovingObstacleOccupancyReplayOptions>(module, "MovingObstacleOccupancyReplayOptions")
        .def(py::init<>())
        .def_readwrite("maximum_input_waypoints",
                       &MovingObstacleOccupancyReplayOptions::maximum_input_waypoints)
        .def_readwrite("maximum_slices", &MovingObstacleOccupancyReplayOptions::maximum_slices)
        .def_readwrite("cancellation", &MovingObstacleOccupancyReplayOptions::cancellation);

    py::class_<MovingObstacleOccupancy>(module, "MovingObstacleOccupancy")
        .def_readonly("storage_schema", &MovingObstacleOccupancy::storage_schema)
        .def_readonly("id", &MovingObstacleOccupancy::id)
        .def_readonly("timeline_id", &MovingObstacleOccupancy::timeline_id)
        .def_readonly("workspace_frame_id", &MovingObstacleOccupancy::workspace_frame_id)
        .def_readonly("obstacle_id", &MovingObstacleOccupancy::obstacle_id)
        .def_readonly("algorithm", &MovingObstacleOccupancy::algorithm)
        .def_readonly("algorithm_version", &MovingObstacleOccupancy::algorithm_version)
        .def_readonly("obstacle_padding", &MovingObstacleOccupancy::obstacle_padding)
        .def_readonly("trajectory", &MovingObstacleOccupancy::trajectory)
        .def_readonly("slices", &MovingObstacleOccupancy::slices)
        .def("valid", &MovingObstacleOccupancy::valid)
        .def_property_readonly("evidence", &MovingObstacleOccupancy::evidence)
        .def_property_readonly("authorizes_execution", &MovingObstacleOccupancy::authorizes_execution);

    py::enum_<ContinuousOccupancyConflictReason>(module, "ContinuousOccupancyConflictReason")
        .value("SWEPT_ENVELOPE_OVERLAP", ContinuousOccupancyConflictReason::SweptEnvelopeOverlap)
        .value("SEPARATION_MARGIN_VIOLATED", ContinuousOccupancyConflictReason::SeparationMarginViolated);

    py::class_<ContinuousOccupancyConflict>(module, "ContinuousOccupancyConflict")
        .def_readonly("first_occupancy_id", &ContinuousOccupancyConflict::first_occupancy_id)
        .def_readonly("second_occupancy_id", &ContinuousOccupancyConflict::second_occupancy_id)
        .def_readonly("first_slice_id", &ContinuousOccupancyConflict::first_slice_id)
        .def_readonly("second_slice_id", &ContinuousOccupancyConflict::second_slice_id)
        .def_readonly("first_link_index", &ContinuousOccupancyConflict::first_link_index)
        .def_readonly("second_link_index", &ContinuousOccupancyConflict::second_link_index)
        .def_readonly("overlap_begin_tick", &ContinuousOccupancyConflict::overlap_begin_tick)
        .def_readonly("overlap_end_tick", &ContinuousOccupancyConflict::overlap_end_tick)
        .def_readonly("reason", &ContinuousOccupancyConflict::reason)
        .def_readonly("clearance_lower_bound", &ContinuousOccupancyConflict::clearance_lower_bound)
        .def_readonly("required_margin", &ContinuousOccupancyConflict::required_margin);

    py::enum_<ContinuousFleetOccupancyStatus>(module, "ContinuousFleetOccupancyStatus")
        .value("CERTIFIED_SEPARATED_UNDER_SWEPT_ENVELOPES",
               ContinuousFleetOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes)
        .value("POTENTIAL_CONFLICT", ContinuousFleetOccupancyStatus::PotentialConflict);

    py::class_<ContinuousFleetOccupancyOptions>(module, "ContinuousFleetOccupancyOptions")
        .def(py::init<>())
        .def_readwrite("minimum_separation", &ContinuousFleetOccupancyOptions::minimum_separation)
        .def_readwrite("maximum_occupancies", &ContinuousFleetOccupancyOptions::maximum_occupancies)
        .def_readwrite("maximum_conflicts", &ContinuousFleetOccupancyOptions::maximum_conflicts)
        .def_readwrite("maximum_slice_pair_evaluations",
                       &ContinuousFleetOccupancyOptions::maximum_slice_pair_evaluations)
        .def_readwrite("maximum_link_pair_evaluations",
                       &ContinuousFleetOccupancyOptions::maximum_link_pair_evaluations)
        .def_readwrite("cancellation", &ContinuousFleetOccupancyOptions::cancellation);

    py::class_<ContinuousFleetOccupancyReport>(module, "ContinuousFleetOccupancyReport")
        .def_readonly("id", &ContinuousFleetOccupancyReport::id)
        .def_readonly("timeline_id", &ContinuousFleetOccupancyReport::timeline_id)
        .def_readonly("workspace_frame_id", &ContinuousFleetOccupancyReport::workspace_frame_id)
        .def_readonly("status", &ContinuousFleetOccupancyReport::status)
        .def_readonly("minimum_separation", &ContinuousFleetOccupancyReport::minimum_separation)
        .def_readonly("occupancy_ids", &ContinuousFleetOccupancyReport::occupancy_ids)
        .def_readonly("conflicts", &ContinuousFleetOccupancyReport::conflicts)
        .def_readonly("slice_pair_evaluations", &ContinuousFleetOccupancyReport::slice_pair_evaluations)
        .def_readonly("link_pair_evaluations", &ContinuousFleetOccupancyReport::link_pair_evaluations)
        .def("valid", &ContinuousFleetOccupancyReport::valid)
        .def_property_readonly("evidence", &ContinuousFleetOccupancyReport::evidence)
        .def_property_readonly("authorizes_execution", &ContinuousFleetOccupancyReport::authorizes_execution);

    py::class_<ContinuousRobotSceneOccupancyConflict>(module, "ContinuousRobotSceneOccupancyConflict")
        .def_readonly("robot_occupancy_id", &ContinuousRobotSceneOccupancyConflict::robot_occupancy_id)
        .def_readonly("obstacle_occupancy_id", &ContinuousRobotSceneOccupancyConflict::obstacle_occupancy_id)
        .def_readonly("robot_slice_id", &ContinuousRobotSceneOccupancyConflict::robot_slice_id)
        .def_readonly("obstacle_slice_id", &ContinuousRobotSceneOccupancyConflict::obstacle_slice_id)
        .def_readonly("robot_link_index", &ContinuousRobotSceneOccupancyConflict::robot_link_index)
        .def_readonly("overlap_begin_tick", &ContinuousRobotSceneOccupancyConflict::overlap_begin_tick)
        .def_readonly("overlap_end_tick", &ContinuousRobotSceneOccupancyConflict::overlap_end_tick)
        .def_readonly("reason", &ContinuousRobotSceneOccupancyConflict::reason)
        .def_readonly("clearance_lower_bound", &ContinuousRobotSceneOccupancyConflict::clearance_lower_bound)
        .def_readonly("required_margin", &ContinuousRobotSceneOccupancyConflict::required_margin);

    py::enum_<ContinuousRobotSceneOccupancyStatus>(module, "ContinuousRobotSceneOccupancyStatus")
        .value("CERTIFIED_SEPARATED_UNDER_SWEPT_ENVELOPES",
               ContinuousRobotSceneOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes)
        .value("POTENTIAL_CONFLICT", ContinuousRobotSceneOccupancyStatus::PotentialConflict);

    py::class_<ContinuousRobotSceneOccupancyOptions>(module, "ContinuousRobotSceneOccupancyOptions")
        .def(py::init<>())
        .def_readwrite("minimum_separation", &ContinuousRobotSceneOccupancyOptions::minimum_separation)
        .def_readwrite("maximum_robot_occupancies",
                       &ContinuousRobotSceneOccupancyOptions::maximum_robot_occupancies)
        .def_readwrite("maximum_obstacle_occupancies",
                       &ContinuousRobotSceneOccupancyOptions::maximum_obstacle_occupancies)
        .def_readwrite("maximum_conflicts", &ContinuousRobotSceneOccupancyOptions::maximum_conflicts)
        .def_readwrite("maximum_slice_pair_evaluations",
                       &ContinuousRobotSceneOccupancyOptions::maximum_slice_pair_evaluations)
        .def_readwrite("maximum_link_evaluations",
                       &ContinuousRobotSceneOccupancyOptions::maximum_link_evaluations)
        .def_readwrite("cancellation", &ContinuousRobotSceneOccupancyOptions::cancellation);

    py::class_<ContinuousRobotSceneOccupancyReport>(module, "ContinuousRobotSceneOccupancyReport")
        .def_readonly("id", &ContinuousRobotSceneOccupancyReport::id)
        .def_readonly("timeline_id", &ContinuousRobotSceneOccupancyReport::timeline_id)
        .def_readonly("workspace_frame_id", &ContinuousRobotSceneOccupancyReport::workspace_frame_id)
        .def_readonly("begin_tick", &ContinuousRobotSceneOccupancyReport::begin_tick)
        .def_readonly("end_tick", &ContinuousRobotSceneOccupancyReport::end_tick)
        .def_readonly("status", &ContinuousRobotSceneOccupancyReport::status)
        .def_readonly("minimum_separation", &ContinuousRobotSceneOccupancyReport::minimum_separation)
        .def_readonly("robot_occupancy_ids", &ContinuousRobotSceneOccupancyReport::robot_occupancy_ids)
        .def_readonly("obstacle_occupancy_ids", &ContinuousRobotSceneOccupancyReport::obstacle_occupancy_ids)
        .def_readonly("conflicts", &ContinuousRobotSceneOccupancyReport::conflicts)
        .def_readonly("slice_pair_evaluations", &ContinuousRobotSceneOccupancyReport::slice_pair_evaluations)
        .def_readonly("link_evaluations", &ContinuousRobotSceneOccupancyReport::link_evaluations)
        .def("valid", &ContinuousRobotSceneOccupancyReport::valid)
        .def_property_readonly("evidence", &ContinuousRobotSceneOccupancyReport::evidence)
        .def_property_readonly("authorizes_execution",
                               &ContinuousRobotSceneOccupancyReport::authorizes_execution);

    py::class_<ContinuousFleetOccupancyBundleLoadOptions>(module, "ContinuousFleetOccupancyBundleLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_occupancies", &ContinuousFleetOccupancyBundleLoadOptions::maximum_occupancies)
        .def_readwrite("maximum_input_waypoints",
                       &ContinuousFleetOccupancyBundleLoadOptions::maximum_input_waypoints)
        .def_readwrite("maximum_dimension", &ContinuousFleetOccupancyBundleLoadOptions::maximum_dimension)
        .def_readwrite("maximum_slices", &ContinuousFleetOccupancyBundleLoadOptions::maximum_slices)
        .def_readwrite("maximum_link_envelopes",
                       &ContinuousFleetOccupancyBundleLoadOptions::maximum_link_envelopes)
        .def_readwrite("maximum_conflicts", &ContinuousFleetOccupancyBundleLoadOptions::maximum_conflicts)
        .def_readwrite("maximum_slice_pair_evaluations",
                       &ContinuousFleetOccupancyBundleLoadOptions::maximum_slice_pair_evaluations)
        .def_readwrite("maximum_link_pair_evaluations",
                       &ContinuousFleetOccupancyBundleLoadOptions::maximum_link_pair_evaluations)
        .def_readwrite("maximum_payload_bytes",
                       &ContinuousFleetOccupancyBundleLoadOptions::maximum_payload_bytes)
        .def_readwrite("cancellation", &ContinuousFleetOccupancyBundleLoadOptions::cancellation);

    py::class_<ContinuousFleetOccupancyBundle>(module, "ContinuousFleetOccupancyBundle")
        .def_static(
            "create",
            [](std::vector<RobotTrajectoryOccupancy> occupancies,
               const ContinuousFleetOccupancyOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return ContinuousFleetOccupancyBundle::create(std::move(occupancies), options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("occupancies"), py::arg("options") = ContinuousFleetOccupancyOptions{})
        .def_static(
            "load",
            [](const std::filesystem::path& path, const ContinuousFleetOccupancyBundleLoadOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return ContinuousFleetOccupancyBundle::load(path, options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("path"), py::arg("options") = ContinuousFleetOccupancyBundleLoadOptions{})
        .def_property_readonly("storage_schema", &ContinuousFleetOccupancyBundle::storage_schema)
        .def_property_readonly("id", &ContinuousFleetOccupancyBundle::id)
        .def_property_readonly("occupancies", &ContinuousFleetOccupancyBundle::occupancies)
        .def_property_readonly("report", &ContinuousFleetOccupancyBundle::report)
        .def("valid", &ContinuousFleetOccupancyBundle::valid)
        .def_property_readonly("evidence", &ContinuousFleetOccupancyBundle::evidence)
        .def_property_readonly("authorizes_execution", &ContinuousFleetOccupancyBundle::authorizes_execution)
        .def(
            "save",
            [](const ContinuousFleetOccupancyBundle& bundle, const std::filesystem::path& path,
               const SaveOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return bundle.save(path, options);
                }();
                unwrap_void(std::move(result));
            },
            py::arg("path"), py::arg("options") = SaveOptions{});

    py::class_<ContinuousRobotSceneOccupancyBundleLoadOptions>(
        module, "ContinuousRobotSceneOccupancyBundleLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_robot_occupancies",
                       &ContinuousRobotSceneOccupancyBundleLoadOptions::maximum_robot_occupancies)
        .def_readwrite("maximum_obstacle_occupancies",
                       &ContinuousRobotSceneOccupancyBundleLoadOptions::maximum_obstacle_occupancies)
        .def_readwrite("maximum_robot_input_waypoints",
                       &ContinuousRobotSceneOccupancyBundleLoadOptions::maximum_robot_input_waypoints)
        .def_readwrite("maximum_obstacle_input_waypoints",
                       &ContinuousRobotSceneOccupancyBundleLoadOptions::maximum_obstacle_input_waypoints)
        .def_readwrite("maximum_dimension",
                       &ContinuousRobotSceneOccupancyBundleLoadOptions::maximum_dimension)
        .def_readwrite("maximum_robot_slices",
                       &ContinuousRobotSceneOccupancyBundleLoadOptions::maximum_robot_slices)
        .def_readwrite("maximum_obstacle_slices",
                       &ContinuousRobotSceneOccupancyBundleLoadOptions::maximum_obstacle_slices)
        .def_readwrite("maximum_link_envelopes",
                       &ContinuousRobotSceneOccupancyBundleLoadOptions::maximum_link_envelopes)
        .def_readwrite("maximum_conflicts",
                       &ContinuousRobotSceneOccupancyBundleLoadOptions::maximum_conflicts)
        .def_readwrite("maximum_slice_pair_evaluations",
                       &ContinuousRobotSceneOccupancyBundleLoadOptions::maximum_slice_pair_evaluations)
        .def_readwrite("maximum_link_evaluations",
                       &ContinuousRobotSceneOccupancyBundleLoadOptions::maximum_link_evaluations)
        .def_readwrite("maximum_payload_bytes",
                       &ContinuousRobotSceneOccupancyBundleLoadOptions::maximum_payload_bytes)
        .def_readwrite("cancellation", &ContinuousRobotSceneOccupancyBundleLoadOptions::cancellation);

    py::class_<ContinuousRobotSceneOccupancyBundle>(module, "ContinuousRobotSceneOccupancyBundle")
        .def_static(
            "create",
            [](std::vector<RobotTrajectoryOccupancy> robot_occupancies,
               std::vector<MovingObstacleOccupancy> obstacle_occupancies,
               const ContinuousRobotSceneOccupancyOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return ContinuousRobotSceneOccupancyBundle::create(
                        std::move(robot_occupancies), std::move(obstacle_occupancies), options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("robot_occupancies"), py::arg("obstacle_occupancies"),
            py::arg("options") = ContinuousRobotSceneOccupancyOptions{})
        .def_static(
            "load",
            [](const std::filesystem::path& path,
               const ContinuousRobotSceneOccupancyBundleLoadOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return ContinuousRobotSceneOccupancyBundle::load(path, options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("path"), py::arg("options") = ContinuousRobotSceneOccupancyBundleLoadOptions{})
        .def_property_readonly("storage_schema", &ContinuousRobotSceneOccupancyBundle::storage_schema)
        .def_property_readonly("id", &ContinuousRobotSceneOccupancyBundle::id)
        .def_property_readonly("robot_occupancies", &ContinuousRobotSceneOccupancyBundle::robot_occupancies)
        .def_property_readonly("obstacle_occupancies",
                               &ContinuousRobotSceneOccupancyBundle::obstacle_occupancies)
        .def_property_readonly("report", &ContinuousRobotSceneOccupancyBundle::report)
        .def("valid", &ContinuousRobotSceneOccupancyBundle::valid)
        .def_property_readonly("evidence", &ContinuousRobotSceneOccupancyBundle::evidence)
        .def_property_readonly("authorizes_execution",
                               &ContinuousRobotSceneOccupancyBundle::authorizes_execution)
        .def(
            "save",
            [](const ContinuousRobotSceneOccupancyBundle& bundle, const std::filesystem::path& path,
               const SaveOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return bundle.save(path, options);
                }();
                unwrap_void(std::move(result));
            },
            py::arg("path"), py::arg("options") = SaveOptions{});

    module.def(
        "build_robot_trajectory_occupancy",
        [](const SerialRobotModel& robot, std::string timeline_id, std::string workspace_frame_id,
           std::string deployment_id, std::array<double, 3> workspace_translation,
           std::vector<TimedConfiguration> trajectory, const ContinuousOccupancyBuildOptions& options) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::build_robot_trajectory_occupancy(
                    robot, std::move(timeline_id), std::move(workspace_frame_id), std::move(deployment_id),
                    workspace_translation, trajectory, options);
            }();
            return unwrap(std::move(result));
        },
        py::arg("robot"), py::arg("timeline_id"), py::arg("workspace_frame_id"), py::arg("deployment_id"),
        py::arg("workspace_translation"), py::arg("trajectory"),
        py::arg("options") = ContinuousOccupancyBuildOptions{});

    module.def(
        "build_robot_trajectory_occupancy_in_frame",
        [](const SerialRobotModel& robot, std::string timeline_id, std::string workspace_frame_id,
           std::string deployment_id, const DeploymentFrameBounds& deployment_frame,
           std::vector<TimedConfiguration> trajectory, const ContinuousOccupancyBuildOptions& options) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::build_robot_trajectory_occupancy_in_frame(
                    robot, std::move(timeline_id), std::move(workspace_frame_id), std::move(deployment_id),
                    deployment_frame, trajectory, options);
            }();
            return unwrap(std::move(result));
        },
        py::arg("robot"), py::arg("timeline_id"), py::arg("workspace_frame_id"), py::arg("deployment_id"),
        py::arg("deployment_frame"), py::arg("trajectory"),
        py::arg("options") = ContinuousOccupancyBuildOptions{});

    module.def(
        "verify_robot_trajectory_occupancy",
        [](const SerialRobotModel& robot, const RobotTrajectoryOccupancy& occupancy,
           const ContinuousOccupancyReplayOptions& options) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::verify_robot_trajectory_occupancy(robot, occupancy, options);
            }();
            unwrap_void(std::move(result));
        },
        py::arg("robot"), py::arg("occupancy"), py::arg("options") = ContinuousOccupancyReplayOptions{});

    module.def(
        "build_moving_obstacle_occupancy",
        [](std::string timeline_id, std::string workspace_frame_id, std::string obstacle_id,
           std::vector<TimedWorkspaceAabb> trajectory, const MovingObstacleOccupancyBuildOptions& options) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::build_moving_obstacle_occupancy(std::move(timeline_id),
                                                                std::move(workspace_frame_id),
                                                                std::move(obstacle_id), trajectory, options);
            }();
            return unwrap(std::move(result));
        },
        py::arg("timeline_id"), py::arg("workspace_frame_id"), py::arg("obstacle_id"), py::arg("trajectory"),
        py::arg("options") = MovingObstacleOccupancyBuildOptions{});

    module.def(
        "verify_moving_obstacle_occupancy",
        [](const MovingObstacleOccupancy& occupancy, const MovingObstacleOccupancyReplayOptions& options) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::verify_moving_obstacle_occupancy(occupancy, options);
            }();
            unwrap_void(std::move(result));
        },
        py::arg("occupancy"), py::arg("options") = MovingObstacleOccupancyReplayOptions{});

    module.def(
        "analyze_continuous_fleet_occupancy",
        [](std::vector<RobotTrajectoryOccupancy> occupancies,
           const ContinuousFleetOccupancyOptions& options) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::analyze_continuous_fleet_occupancy(occupancies, options);
            }();
            return unwrap(std::move(result));
        },
        py::arg("occupancies"), py::arg("options") = ContinuousFleetOccupancyOptions{});
    module.def(
        "analyze_continuous_robot_scene_occupancy",
        [](std::vector<RobotTrajectoryOccupancy> robot_occupancies,
           std::vector<MovingObstacleOccupancy> obstacle_occupancies,
           const ContinuousRobotSceneOccupancyOptions& options) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::analyze_continuous_robot_scene_occupancy(robot_occupancies,
                                                                         obstacle_occupancies, options);
            }();
            return unwrap(std::move(result));
        },
        py::arg("robot_occupancies"), py::arg("obstacle_occupancies"),
        py::arg("options") = ContinuousRobotSceneOccupancyOptions{});
    module.def("continuous_occupancy_conflict_reason_name", &continuous_occupancy_conflict_reason_name);
    module.def("continuous_fleet_occupancy_status_name", &continuous_fleet_occupancy_status_name);
    module.def("continuous_robot_scene_occupancy_status_name", &continuous_robot_scene_occupancy_status_name);
}

} // namespace rbfsafe::python_binding
