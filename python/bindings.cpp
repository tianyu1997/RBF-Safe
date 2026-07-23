#include <rbfsafe/rbfsafe.h>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "binding_support.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace py = pybind11;
using namespace rbfsafe;
using namespace rbfsafe::python_binding;

PYBIND11_MODULE(_rbfsafe, module) {
    module.doc() = "RBF-Safe conservative geometric safety certificates";
    module.attr("__version__") = kVersion;

    auto base_error = py::register_exception<RBFSafeException>(module, "RBFSafeError");
    py::register_exception<IdentityMismatchException>(module, "IdentityMismatchError", base_error.ptr());
    py::register_exception<IncompatibleFormatException>(module, "IncompatibleFormatError", base_error.ptr());
    py::register_exception<CorruptDataException>(module, "CorruptDataError", base_error.ptr());
    py::register_exception<CancelledException>(module, "CancelledError", base_error.ptr());
    py::register_exception<InternalException>(module, "InternalError", base_error.ptr());

    py::enum_<StatusCode>(module, "StatusCode")
        .value("OK", StatusCode::Ok)
        .value("INVALID_ARGUMENT", StatusCode::InvalidArgument)
        .value("DIMENSION_MISMATCH", StatusCode::DimensionMismatch)
        .value("RESOURCE_LIMIT", StatusCode::ResourceLimit)
        .value("IDENTITY_MISMATCH", StatusCode::IdentityMismatch)
        .value("INCOMPATIBLE_FORMAT", StatusCode::IncompatibleFormat)
        .value("CORRUPT_DATA", StatusCode::CorruptData)
        .value("IO_ERROR", StatusCode::IoError)
        .value("CANCELLED", StatusCode::Cancelled)
        .value("INTERNAL_ERROR", StatusCode::InternalError);

    py::class_<Interval>(module, "Interval")
        .def(py::init<double, double>(), py::arg("lower") = 0.0, py::arg("upper") = 0.0)
        .def_readwrite("lower", &Interval::lower)
        .def_readwrite("upper", &Interval::upper)
        .def_property_readonly("width", &Interval::width)
        .def_property_readonly("center", &Interval::center)
        .def("contains", &Interval::contains, py::arg("value"), py::arg("tolerance") = 0.0)
        .def("overlaps", &Interval::overlaps, py::arg("other"), py::arg("tolerance") = 0.0)
        .def("valid", &Interval::valid)
        .def("__repr__", [](const Interval& value) {
            return "Interval(" + std::to_string(value.lower) + ", " + std::to_string(value.upper) + ")";
        });

    py::class_<CspaceAabb>(module, "CspaceAabb")
        .def(py::init<std::vector<Interval>>(), py::arg("axes"))
        .def_property_readonly("axes", [](const CspaceAabb& value) { return value.axes(); })
        .def_property_readonly("dimension", &CspaceAabb::dimension)
        .def_property_readonly("volume", &CspaceAabb::volume)
        .def_property_readonly("center", &CspaceAabb::center)
        .def(
            "contains",
            [](const CspaceAabb& value, const Configuration& q, double tolerance) {
                return value.contains(view(q), tolerance);
            },
            py::arg("configuration"), py::arg("tolerance") = 0.0)
        .def("valid", &CspaceAabb::valid);

    py::class_<CspaceObb>(module, "CspaceObb")
        .def(py::init([](Configuration center, std::vector<double> basis, Configuration half_widths) {
                 return unwrap(
                     CspaceObb::create(std::move(center), std::move(basis), std::move(half_widths)));
             }),
             py::arg("center"), py::arg("basis"), py::arg("half_widths"))
        .def_property_readonly("center", &CspaceObb::center)
        .def_property_readonly("basis", &CspaceObb::basis)
        .def_property_readonly("half_widths", &CspaceObb::half_widths)
        .def_property_readonly("dimension", &CspaceObb::dimension)
        .def_property_readonly("volume", &CspaceObb::volume)
        .def_property_readonly("enclosing_aabb", &CspaceObb::enclosing_aabb)
        .def(
            "contains",
            [](const CspaceObb& value, const Configuration& q, double tolerance) {
                return value.contains(view(q), tolerance);
            },
            py::arg("configuration"), py::arg("tolerance") = 0.0)
        .def("valid", &CspaceObb::valid);

    py::class_<ObbGenerator>(module, "ObbGenerator")
        .def_static(
            "segment_tube",
            [](const Configuration& first, const Configuration& second, double lateral_half_width,
               double longitudinal_margin) {
                return unwrap(ObbGenerator::segment_tube(view(first), view(second), lateral_half_width,
                                                         longitudinal_margin));
            },
            py::arg("first"), py::arg("second"), py::arg("lateral_half_width"),
            py::arg("longitudinal_margin") = 0.0);

    py::enum_<ValidationDisposition>(module, "ValidationDisposition")
        .value("CERTIFIED_FREE", ValidationDisposition::CertifiedFree)
        .value("UNDETERMINED", ValidationDisposition::Undetermined);

    py::class_<ObbValidation>(module, "ObbValidation")
        .def_readonly("disposition", &ObbValidation::disposition)
        .def_readonly("clearance_lower_bound", &ObbValidation::clearance_lower_bound)
        .def_readonly("conservative_enclosure", &ObbValidation::conservative_enclosure);

    py::class_<ObbGrowthOptions>(module, "ObbGrowthOptions")
        .def(py::init<>())
        .def_readwrite("initial_lateral_half_width", &ObbGrowthOptions::initial_lateral_half_width)
        .def_readwrite("maximum_lateral_half_width", &ObbGrowthOptions::maximum_lateral_half_width)
        .def_readwrite("longitudinal_margin", &ObbGrowthOptions::longitudinal_margin)
        .def_readwrite("maximum_iterations", &ObbGrowthOptions::maximum_iterations)
        .def_readwrite("maximum_validations", &ObbGrowthOptions::maximum_validations)
        .def_readwrite("obstacle_padding", &ObbGrowthOptions::obstacle_padding)
        .def_readwrite("cancellation", &ObbGrowthOptions::cancellation);

    py::class_<ObbGrowthResult>(module, "ObbGrowthResult")
        .def_readonly("certified", &ObbGrowthResult::certified)
        .def_readonly("region", &ObbGrowthResult::region)
        .def_readonly("validation", &ObbGrowthResult::validation)
        .def_readonly("achieved_lateral_half_width", &ObbGrowthResult::achieved_lateral_half_width)
        .def_readonly("validations", &ObbGrowthResult::validations)
        .def_readonly("growth_attempts", &ObbGrowthResult::growth_attempts);

    py::class_<ObbGrower>(module, "ObbGrower")
        .def(py::init<>())
        .def(
            "grow",
            [](const ObbGrower& grower, const SerialRobotModel& robot, const SceneSnapshot& scene,
               const Configuration& first, const Configuration& second, const ObbGrowthOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return grower.grow(robot, scene, view(first), view(second), options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("robot"), py::arg("scene"), py::arg("first"), py::arg("second"),
            py::arg("options") = ObbGrowthOptions{});

    py::class_<WorkspaceAabb>(module, "WorkspaceAabb")
        .def(py::init([](std::array<double, 3> lower, std::array<double, 3> upper) {
                 WorkspaceAabb value{lower, upper};
                 if (!value.valid())
                     throw py::value_error("invalid workspace AABB");
                 return value;
             }),
             py::arg("lower"), py::arg("upper"))
        .def_readwrite("lower", &WorkspaceAabb::lower)
        .def_readwrite("upper", &WorkspaceAabb::upper)
        .def("valid", &WorkspaceAabb::valid);

    py::class_<EnvelopeOptions>(module, "EnvelopeOptions")
        .def(py::init<>())
        .def_readwrite("obstacle_padding", &EnvelopeOptions::obstacle_padding);

    py::enum_<JointType>(module, "JointType")
        .value("REVOLUTE", JointType::Revolute)
        .value("PRISMATIC", JointType::Prismatic);

    py::class_<DhJoint>(module, "DhJoint")
        .def(py::init<double, double, double, double, JointType>(), py::arg("alpha") = 0.0,
             py::arg("a") = 0.0, py::arg("d") = 0.0, py::arg("theta") = 0.0,
             py::arg("type") = JointType::Revolute)
        .def_readwrite("alpha", &DhJoint::alpha)
        .def_readwrite("a", &DhJoint::a)
        .def_readwrite("d", &DhJoint::d)
        .def_readwrite("theta", &DhJoint::theta)
        .def_readwrite("type", &DhJoint::type);

    py::class_<Pose3d>(module, "Pose3d")
        .def(py::init<std::array<double, 3>, std::array<double, 4>>(),
             py::arg("position") = std::array<double, 3>{},
             py::arg("orientation") = std::array<double, 4>{0.0, 0.0, 0.0, 1.0})
        .def_readwrite("position", &Pose3d::position)
        .def_readwrite("orientation", &Pose3d::orientation)
        .def("valid", &Pose3d::valid, py::arg("tolerance") = 1e-9);

    py::class_<SerialRobotModel>(module, "SerialRobotModel")
        .def(py::init([](std::string name, std::vector<DhJoint> joints, std::vector<Interval> limits,
                         std::vector<double> radii, std::optional<DhJoint> tool) {
                 return unwrap(SerialRobotModel::create(std::move(name), std::move(joints), std::move(limits),
                                                        std::move(radii), tool));
             }),
             py::arg("name"), py::arg("joints"), py::arg("joint_limits"), py::arg("link_radii"),
             py::arg("tool_frame") = std::nullopt)
        .def_static(
            "from_json",
            [](const std::filesystem::path& path) { return unwrap(SerialRobotModel::from_json(path)); })
        .def_property_readonly("name", &SerialRobotModel::name)
        .def_property_readonly("dimension", &SerialRobotModel::dimension)
        .def_property_readonly("link_count", &SerialRobotModel::link_count)
        .def_property_readonly("joints", &SerialRobotModel::joints)
        .def_property_readonly("joint_limits", &SerialRobotModel::joint_limits)
        .def_property_readonly("link_radii", &SerialRobotModel::link_radii)
        .def_property_readonly("digest", &SerialRobotModel::digest)
        .def("forward_kinematics",
             [](const SerialRobotModel& robot, const Configuration& q) {
                 return unwrap(robot.forward_kinematics(view(q)));
             })
        .def("end_effector_pose", [](const SerialRobotModel& robot, const Configuration& q) {
            return unwrap(robot.end_effector_pose(view(q)));
        });

    py::class_<SceneObstacle>(module, "SceneObstacle")
        .def(py::init<std::string, WorkspaceAabb>(), py::arg("id"), py::arg("bounds"))
        .def_readwrite("id", &SceneObstacle::id)
        .def_readwrite("bounds", &SceneObstacle::bounds);

    py::class_<SceneSnapshot>(module, "SceneSnapshot")
        .def(py::init([](std::vector<SceneObstacle> obstacles, std::string version) {
                 return unwrap(SceneSnapshot::create(std::move(obstacles), std::move(version)));
             }),
             py::arg("obstacles") = std::vector<SceneObstacle>{}, py::arg("version") = "1")
        .def_static("from_json",
                    [](const std::filesystem::path& path) { return unwrap(SceneSnapshot::from_json(path)); })
        .def_property_readonly("obstacles", &SceneSnapshot::obstacles)
        .def_property_readonly("version", &SceneSnapshot::version)
        .def_property_readonly("digest", &SceneSnapshot::digest);

    py::enum_<SceneChangeKind>(module, "SceneChangeKind")
        .value("ADDED", SceneChangeKind::Added)
        .value("REMOVED", SceneChangeKind::Removed)
        .value("MODIFIED", SceneChangeKind::Modified);

    py::class_<SceneObstacleChange>(module, "SceneObstacleChange")
        .def_readonly("kind", &SceneObstacleChange::kind)
        .def_readonly("obstacle_id", &SceneObstacleChange::obstacle_id)
        .def_readonly("before", &SceneObstacleChange::before)
        .def_readonly("after", &SceneObstacleChange::after);

    py::class_<SceneDelta>(module, "SceneDelta")
        .def_readonly("from_version", &SceneDelta::from_version)
        .def_readonly("to_version", &SceneDelta::to_version)
        .def_readonly("from_digest", &SceneDelta::from_digest)
        .def_readonly("to_digest", &SceneDelta::to_digest)
        .def_readonly("changes", &SceneDelta::changes)
        .def_readonly("digest", &SceneDelta::digest)
        .def_property_readonly("geometry_changed", &SceneDelta::geometry_changed);

    module.def("compare_scenes", [](const SceneSnapshot& before, const SceneSnapshot& after) {
        return unwrap(compare_scenes(before, after));
    });

    py::enum_<EvidenceLevel>(module, "EvidenceLevel")
        .value("UNKNOWN", EvidenceLevel::Unknown)
        .value("POINT_CHECKED", EvidenceLevel::PointChecked)
        .value("CERTIFIED_REGION", EvidenceLevel::CertifiedRegion)
        .value("CERTIFIED_CONNECTIVITY", EvidenceLevel::CertifiedConnectivity)
        .value("RUNTIME_EXECUTABLE", EvidenceLevel::RuntimeExecutable);

    py::class_<ValidationPolicy>(module, "ValidationPolicy")
        .def_readonly("algorithm", &ValidationPolicy::algorithm)
        .def_readonly("algorithm_version", &ValidationPolicy::algorithm_version)
        .def_readonly("obstacle_padding", &ValidationPolicy::obstacle_padding);

    py::class_<Certificate>(module, "Certificate")
        .def_readonly("id", &Certificate::id)
        .def_readonly("level", &Certificate::level)
        .def_readonly("robot_digest", &Certificate::robot_digest)
        .def_readonly("scene_digest", &Certificate::scene_digest)
        .def_readonly("policy", &Certificate::policy)
        .def_readonly("clearance_lower_bound", &Certificate::clearance_lower_bound)
        .def_readonly("subject_digest", &Certificate::subject_digest)
        .def_readonly("parent_certificate_id", &Certificate::parent_certificate_id)
        .def_readonly("transition_digest", &Certificate::transition_digest);

    py::class_<LectNodeKey>(module, "LectNodeKey")
        .def(py::init<std::string>(), py::arg("path") = "")
        .def_property_readonly("path", &LectNodeKey::path)
        .def_property_readonly("depth", &LectNodeKey::depth)
        .def("valid", &LectNodeKey::valid)
        .def("__str__", &LectNodeKey::to_string);

    py::enum_<SplitStrategy>(module, "SplitStrategy")
        .value("NORMALIZED_LONGEST_AXIS", SplitStrategy::NormalizedLongestAxis);

    py::class_<SplitPolicy>(module, "SplitPolicy")
        .def(py::init<>())
        .def_readwrite("strategy", &SplitPolicy::strategy)
        .def_readwrite("minimum_normalized_width", &SplitPolicy::minimum_normalized_width);

    py::class_<LectNode>(module, "LectNode")
        .def_readonly("key", &LectNode::key)
        .def_readonly("box", &LectNode::box)
        .def_readonly("leaf", &LectNode::leaf)
        .def_readonly("split_dimension", &LectNode::split_dimension)
        .def_readonly("left", &LectNode::left)
        .def_readonly("right", &LectNode::right);

    py::class_<LectTree>(module, "LectTree")
        .def_static(
            "create",
            [](CspaceAabb root, SplitPolicy policy) {
                return unwrap(LectTree::create(std::move(root), policy));
            },
            py::arg("root"), py::arg("policy") = SplitPolicy{})
        .def_property_readonly("root_domain", &LectTree::root_domain)
        .def_property_readonly("size", &LectTree::size)
        .def("node", [](const LectTree& tree, const LectNodeKey& key) { return unwrap(tree.node(key)); })
        .def("split", [](LectTree& tree, const LectNodeKey& key) { return unwrap(tree.split(key)); })
        .def("locate",
             [](const LectTree& tree, const Configuration& q) { return unwrap(tree.locate(view(q))); })
        .def("leaf_keys", &LectTree::leaf_keys)
        .def("save",
             [](const LectTree& tree, const std::filesystem::path& path) { unwrap_void(tree.save(path)); });

    py::class_<LectSnapshot>(module, "LectSnapshot")
        .def_static("open",
                    [](const std::filesystem::path& path) { return unwrap(LectSnapshot::open(path)); })
        .def_property_readonly("root_domain", &LectSnapshot::root_domain)
        .def_property_readonly("size", &LectSnapshot::size)
        .def("locate",
             [](const LectSnapshot& tree, const Configuration& q) { return unwrap(tree.locate(view(q))); })
        .def("leaf_keys", &LectSnapshot::leaf_keys);

    py::class_<CancellationToken>(module, "CancellationToken")
        .def(py::init<>())
        .def("cancel", &CancellationToken::cancel)
        .def_property_readonly("cancelled", &CancellationToken::cancelled);

    py::class_<BuildOptions>(module, "BuildOptions")
        .def(py::init<>())
        .def_readwrite("maximum_depth", &BuildOptions::maximum_depth)
        .def_readwrite("maximum_nodes", &BuildOptions::maximum_nodes)
        .def_readwrite("minimum_normalized_width", &BuildOptions::minimum_normalized_width)
        .def_readwrite("adjacency_tolerance", &BuildOptions::adjacency_tolerance)
        .def_readwrite("obstacle_padding", &BuildOptions::obstacle_padding)
        .def_readwrite("threads", &BuildOptions::threads)
        .def_readwrite("cancellation", &BuildOptions::cancellation);

    py::class_<BuildStats>(module, "BuildStats")
        .def_readonly("input_samples", &BuildStats::input_samples)
        .def_readonly("unique_samples", &BuildStats::unique_samples)
        .def_readonly("nodes_visited", &BuildStats::nodes_visited)
        .def_readonly("certified_nodes", &BuildStats::certified_nodes)
        .def_readonly("unresolved_nodes", &BuildStats::unresolved_nodes)
        .def_readonly("merged_regions", &BuildStats::merged_regions);

    py::class_<SafeRegion>(module, "SafeRegion")
        .def_readonly("id", &SafeRegion::id)
        .def_readonly("bounds", &SafeRegion::bounds)
        .def_readonly("certificate_index", &SafeRegion::certificate_index)
        .def_readonly("component", &SafeRegion::component)
        .def_readonly("source_node", &SafeRegion::source_node);

    py::class_<LinkEnvelope>(module, "LinkEnvelope")
        .def_property_readonly("links", [](const LinkEnvelope& value) { return value.links; });

    py::class_<RegionDependency>(module, "RegionDependency")
        .def_readonly("region_id", &RegionDependency::region_id)
        .def_readonly("envelope", &RegionDependency::envelope);

    py::class_<AtlasRepairDomain>(module, "AtlasRepairDomain")
        .def_readonly("id", &AtlasRepairDomain::id)
        .def_readonly("bounds", &AtlasRepairDomain::bounds)
        .def_readonly("source_node", &AtlasRepairDomain::source_node);

    py::class_<AtlasVersionInfo>(module, "AtlasVersionInfo")
        .def_readonly("sequence", &AtlasVersionInfo::sequence)
        .def_readonly("id", &AtlasVersionInfo::id)
        .def_readonly("parent_id", &AtlasVersionInfo::parent_id)
        .def_readonly("scene_version", &AtlasVersionInfo::scene_version)
        .def_readonly("scene_digest", &AtlasVersionInfo::scene_digest)
        .def_readonly("transition_digest", &AtlasVersionInfo::transition_digest);

    py::class_<SaveOptions>(module, "SaveOptions")
        .def(py::init<>())
        .def_readwrite("overwrite", &SaveOptions::overwrite);

    py::class_<AtlasRoute>(module, "AtlasRoute")
        .def_readonly("waypoints", &AtlasRoute::waypoints)
        .def_readonly("region_sequence", &AtlasRoute::region_sequence)
        .def_readonly("certificate", &AtlasRoute::certificate);

    py::class_<SafeAtlas>(module, "SafeAtlas")
        .def_static("load", [](const std::filesystem::path& path) { return unwrap(SafeAtlas::load(path)); })
        .def_property_readonly("dimension", &SafeAtlas::dimension)
        .def_property_readonly("robot_digest", &SafeAtlas::robot_digest)
        .def_property_readonly("scene_digest", &SafeAtlas::scene_digest)
        .def_property_readonly("regions", &SafeAtlas::regions)
        .def_property_readonly("certificates", &SafeAtlas::certificates)
        .def_property_readonly("dependencies", &SafeAtlas::dependencies)
        .def_property_readonly("repair_domains", &SafeAtlas::repair_domains)
        .def_property_readonly("lect", &SafeAtlas::lect)
        .def_property_readonly("version_info", &SafeAtlas::version_info)
        .def_property_readonly("transition", &SafeAtlas::transition)
        .def_property_readonly("storage_schema", &SafeAtlas::storage_schema)
        .def("regions_at",
             [](const SafeAtlas& atlas, const Configuration& q) { return unwrap(atlas.regions_at(q)); })
        .def("contains", [](const SafeAtlas& atlas, const Configuration& q) { return atlas.contains(q); })
        .def("nearest_region", [](const SafeAtlas& atlas,
                                  const Configuration& q) { return unwrap(atlas.nearest_region(view(q))); })
        .def("connected",
             [](const SafeAtlas& atlas, const Configuration& first, const Configuration& second) {
                 return unwrap(atlas.connected(view(first), view(second)));
             })
        .def("route",
             [](const SafeAtlas& atlas, const Configuration& first, const Configuration& second) {
                 return unwrap(atlas.route(view(first), view(second)));
             })
        .def("verify_compatible",
             [](const SafeAtlas& atlas, const SerialRobotModel& robot, const SceneSnapshot& scene) {
                 unwrap_void(atlas.verify_compatible(robot, scene));
             })
        .def(
            "save",
            [](const SafeAtlas& atlas, const std::filesystem::path& path, bool overwrite) {
                unwrap_void(atlas.save(path, SaveOptions{overwrite}));
            },
            py::arg("path"), py::arg("overwrite") = false);

    py::enum_<SafeIkStatus>(module, "SafeIkStatus")
        .value("SAFE_CONNECTED", SafeIkStatus::SafeConnected)
        .value("SAFE_UNCONNECTED", SafeIkStatus::SafeUnconnected)
        .value("SEED_NOT_CERTIFIED", SafeIkStatus::SeedNotCertified)
        .value("NO_SOLUTION", SafeIkStatus::NoSolution);

    py::class_<SafeIkOptions>(module, "SafeIkOptions")
        .def(py::init<>())
        .def_readwrite("position_tolerance", &SafeIkOptions::position_tolerance)
        .def_readwrite("orientation_tolerance", &SafeIkOptions::orientation_tolerance)
        .def_readwrite("orientation_weight", &SafeIkOptions::orientation_weight)
        .def_readwrite("damping", &SafeIkOptions::damping)
        .def_readwrite("finite_difference_step", &SafeIkOptions::finite_difference_step)
        .def_readwrite("maximum_step_norm", &SafeIkOptions::maximum_step_norm)
        .def_readwrite("minimum_step_norm", &SafeIkOptions::minimum_step_norm)
        .def_readwrite("maximum_iterations", &SafeIkOptions::maximum_iterations)
        .def_readwrite("maximum_region_attempts", &SafeIkOptions::maximum_region_attempts)
        .def_readwrite("maximum_line_search_steps", &SafeIkOptions::maximum_line_search_steps)
        .def_readwrite("require_connectivity", &SafeIkOptions::require_connectivity)
        .def_readwrite("cancellation", &SafeIkOptions::cancellation);

    py::class_<SafeIkStats>(module, "SafeIkStats")
        .def_readonly("region_attempts", &SafeIkStats::region_attempts)
        .def_readonly("iterations", &SafeIkStats::iterations)
        .def_readonly("pose_evaluations", &SafeIkStats::pose_evaluations)
        .def_readonly("disconnected_solutions", &SafeIkStats::disconnected_solutions);

    py::class_<SafeIkReport>(module, "SafeIkReport")
        .def_readonly("status", &SafeIkReport::status)
        .def_readonly("solution", &SafeIkReport::solution)
        .def_readonly("region_id", &SafeIkReport::region_id)
        .def_readonly("region_certificate", &SafeIkReport::region_certificate)
        .def_readonly("connectivity_route", &SafeIkReport::connectivity_route)
        .def_readonly("pose_evidence", &SafeIkReport::pose_evidence)
        .def_readonly("position_error", &SafeIkReport::position_error)
        .def_readonly("orientation_error", &SafeIkReport::orientation_error)
        .def_readonly("stats", &SafeIkReport::stats);

    py::class_<SafeIkSolver>(module, "SafeIkSolver")
        .def(py::init<>())
        .def(
            "solve",
            [](const SafeIkSolver& solver, const SerialRobotModel& robot, const SceneSnapshot& scene,
               const SafeAtlas& atlas, const Pose3d& target, const Configuration& current,
               const SafeIkOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return solver.solve(robot, scene, atlas, target, view(current), options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("robot"), py::arg("scene"), py::arg("atlas"), py::arg("target"), py::arg("current"),
            py::arg("options") = SafeIkOptions{});

    py::enum_<TrajectoryAuditStatus>(module, "TrajectoryAuditStatus")
        .value("CERTIFIED", TrajectoryAuditStatus::Certified)
        .value("PARTIAL", TrajectoryAuditStatus::Partial)
        .value("INVALID", TrajectoryAuditStatus::Invalid);

    py::class_<TrajectoryInterval>(module, "TrajectoryInterval")
        .def_readonly("segment_index", &TrajectoryInterval::segment_index)
        .def_readonly("start_fraction", &TrajectoryInterval::start_fraction)
        .def_readonly("end_fraction", &TrajectoryInterval::end_fraction)
        .def_readonly("start_included", &TrajectoryInterval::start_included)
        .def_readonly("end_included", &TrajectoryInterval::end_included);

    py::class_<TrajectoryAuditOptions>(module, "TrajectoryAuditOptions")
        .def(py::init<>())
        .def_readwrite("maximum_region_tests", &TrajectoryAuditOptions::maximum_region_tests)
        .def_readwrite("cancellation", &TrajectoryAuditOptions::cancellation);

    py::class_<TrajectoryAuditReport>(module, "TrajectoryAuditReport")
        .def_readonly("status", &TrajectoryAuditReport::status)
        .def_readonly("coverage_ratio", &TrajectoryAuditReport::coverage_ratio)
        .def_readonly("waypoint_count", &TrajectoryAuditReport::waypoint_count)
        .def_readonly("segment_count", &TrajectoryAuditReport::segment_count)
        .def_readonly("region_tests", &TrajectoryAuditReport::region_tests)
        .def_readonly("region_sequence", &TrajectoryAuditReport::region_sequence)
        .def_readonly("uncovered_intervals", &TrajectoryAuditReport::uncovered_intervals);

    py::class_<TrajectoryAuditor>(module, "TrajectoryAuditor")
        .def(py::init<>())
        .def(
            "audit",
            [](const TrajectoryAuditor& auditor, const SafeAtlas& atlas,
               const std::vector<Configuration>& trajectory, const TrajectoryAuditOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return auditor.audit(
                        atlas, std::span<const Configuration>(trajectory.data(), trajectory.size()), options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("atlas"), py::arg("trajectory"), py::arg("options") = TrajectoryAuditOptions{});

    py::enum_<ShieldActionType>(module, "ShieldActionType")
        .value("JOINT_DELTA", ShieldActionType::JointDelta)
        .value("END_EFFECTOR_POSE", ShieldActionType::EndEffectorPose)
        .value("TRAJECTORY", ShieldActionType::Trajectory);

    py::enum_<ShieldOutcome>(module, "ShieldOutcome")
        .value("ACCEPT", ShieldOutcome::Accept)
        .value("REPAIR", ShieldOutcome::Repair)
        .value("REJECT", ShieldOutcome::Reject);

    py::enum_<ShieldReason>(module, "ShieldReason")
        .value("CERTIFIED", ShieldReason::Certified)
        .value("JOINT_TARGET_REPAIRED", ShieldReason::JointTargetRepaired)
        .value("SAFE_IK_ROUTE", ShieldReason::SafeIkRoute)
        .value("TRAJECTORY_REPAIRED", ShieldReason::TrajectoryRepaired)
        .value("CURRENT_STATE_NOT_CERTIFIED", ShieldReason::CurrentStateNotCertified)
        .value("TARGET_NOT_CERTIFIED", ShieldReason::TargetNotCertified)
        .value("REPAIR_DISABLED", ShieldReason::RepairDisabled)
        .value("REPAIR_LIMIT_EXCEEDED", ShieldReason::RepairLimitExceeded)
        .value("NO_SAFE_IK_SOLUTION", ShieldReason::NoSafeIkSolution)
        .value("DISCONNECTED", ShieldReason::Disconnected);

    py::class_<JointDeltaAction>(module, "JointDeltaAction")
        .def(py::init<>())
        .def(py::init<Configuration>(), py::arg("delta"))
        .def_readwrite("delta", &JointDeltaAction::delta);

    py::class_<EndEffectorAction>(module, "EndEffectorAction")
        .def(py::init<>())
        .def(py::init<Pose3d>(), py::arg("target"))
        .def_readwrite("target", &EndEffectorAction::target);

    py::class_<TrajectoryAction>(module, "TrajectoryAction")
        .def(py::init<>())
        .def(py::init<std::vector<Configuration>>(), py::arg("waypoints"))
        .def_readwrite("waypoints", &TrajectoryAction::waypoints);

    py::class_<ShieldOptions>(module, "ShieldOptions")
        .def(py::init<>())
        .def_readwrite("allow_repair", &ShieldOptions::allow_repair)
        .def_readwrite("maximum_waypoint_repair_distance", &ShieldOptions::maximum_waypoint_repair_distance)
        .def_readwrite("maximum_total_repair_distance", &ShieldOptions::maximum_total_repair_distance)
        .def_readwrite("maximum_input_waypoints", &ShieldOptions::maximum_input_waypoints)
        .def_readwrite("maximum_output_waypoints", &ShieldOptions::maximum_output_waypoints)
        .def_readwrite("maximum_repair_region_tests", &ShieldOptions::maximum_repair_region_tests)
        .def_readwrite("audit", &ShieldOptions::audit)
        .def_readwrite("safe_ik", &ShieldOptions::safe_ik)
        .def_readwrite("cancellation", &ShieldOptions::cancellation);

    py::class_<ShieldDecision>(module, "ShieldDecision")
        .def_readonly("action_type", &ShieldDecision::action_type)
        .def_readonly("outcome", &ShieldDecision::outcome)
        .def_readonly("reason", &ShieldDecision::reason)
        .def_readonly("id", &ShieldDecision::id)
        .def_readonly("action_digest", &ShieldDecision::action_digest)
        .def_readonly("robot_digest", &ShieldDecision::robot_digest)
        .def_readonly("scene_digest", &ShieldDecision::scene_digest)
        .def_readonly("requested_target", &ShieldDecision::requested_target)
        .def_readonly("output_trajectory", &ShieldDecision::output_trajectory)
        .def_readonly("audit", &ShieldDecision::audit)
        .def_readonly("connectivity_certificate", &ShieldDecision::connectivity_certificate)
        .def_readonly("repair_distance", &ShieldDecision::repair_distance)
        .def_readonly("evidence", &ShieldDecision::evidence);

    py::class_<ShieldBatchOptions>(module, "ShieldBatchOptions")
        .def(py::init<>())
        .def_readwrite("maximum_actions", &ShieldBatchOptions::maximum_actions)
        .def_readwrite("action", &ShieldBatchOptions::action)
        .def_readwrite("cancellation", &ShieldBatchOptions::cancellation);

    py::class_<ShieldBatchReport>(module, "ShieldBatchReport")
        .def_readonly("decisions", &ShieldBatchReport::decisions)
        .def_readonly("selected_index", &ShieldBatchReport::selected_index);

    py::class_<ShieldTelemetrySnapshot>(module, "ShieldTelemetrySnapshot")
        .def_readonly("total_actions", &ShieldTelemetrySnapshot::total_actions)
        .def_readonly("accepted_actions", &ShieldTelemetrySnapshot::accepted_actions)
        .def_readonly("repaired_actions", &ShieldTelemetrySnapshot::repaired_actions)
        .def_readonly("rejected_actions", &ShieldTelemetrySnapshot::rejected_actions)
        .def_readonly("joint_actions", &ShieldTelemetrySnapshot::joint_actions)
        .def_readonly("end_effector_actions", &ShieldTelemetrySnapshot::end_effector_actions)
        .def_readonly("trajectory_actions", &ShieldTelemetrySnapshot::trajectory_actions)
        .def_readonly("repair_attempts", &ShieldTelemetrySnapshot::repair_attempts)
        .def_readonly("successful_repairs", &ShieldTelemetrySnapshot::successful_repairs)
        .def_readonly("input_waypoints", &ShieldTelemetrySnapshot::input_waypoints)
        .def_readonly("output_waypoints", &ShieldTelemetrySnapshot::output_waypoints)
        .def_readonly("batches", &ShieldTelemetrySnapshot::batches);

    py::class_<RuntimeShield>(module, "RuntimeShield")
        .def(py::init<>())
        .def(
            "check_action",
            [](RuntimeShield& shield, const SerialRobotModel& robot, const SceneSnapshot& scene,
               const SafeAtlas& atlas, const Configuration& current, const ShieldAction& action,
               const ShieldOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return shield.check_action(robot, scene, atlas, view(current), action, options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("robot"), py::arg("scene"), py::arg("atlas"), py::arg("current"), py::arg("action"),
            py::arg("options") = ShieldOptions{})
        .def(
            "check_joint_action",
            [](RuntimeShield& shield, const SerialRobotModel& robot, const SceneSnapshot& scene,
               const SafeAtlas& atlas, const Configuration& current, const JointDeltaAction& action,
               const ShieldOptions& options) {
                return unwrap(
                    shield.check_action(robot, scene, atlas, view(current), ShieldAction{action}, options));
            },
            py::arg("robot"), py::arg("scene"), py::arg("atlas"), py::arg("current"), py::arg("action"),
            py::arg("options") = ShieldOptions{})
        .def(
            "check_end_effector_action",
            [](RuntimeShield& shield, const SerialRobotModel& robot, const SceneSnapshot& scene,
               const SafeAtlas& atlas, const Configuration& current, const EndEffectorAction& action,
               const ShieldOptions& options) {
                return unwrap(
                    shield.check_action(robot, scene, atlas, view(current), ShieldAction{action}, options));
            },
            py::arg("robot"), py::arg("scene"), py::arg("atlas"), py::arg("current"), py::arg("action"),
            py::arg("options") = ShieldOptions{})
        .def(
            "check_trajectory_action",
            [](RuntimeShield& shield, const SerialRobotModel& robot, const SceneSnapshot& scene,
               const SafeAtlas& atlas, const Configuration& current, const TrajectoryAction& action,
               const ShieldOptions& options) {
                return unwrap(
                    shield.check_action(robot, scene, atlas, view(current), ShieldAction{action}, options));
            },
            py::arg("robot"), py::arg("scene"), py::arg("atlas"), py::arg("current"), py::arg("action"),
            py::arg("options") = ShieldOptions{})
        .def(
            "check_actions",
            [](RuntimeShield& shield, const SerialRobotModel& robot, const SceneSnapshot& scene,
               const SafeAtlas& atlas, const Configuration& current, const std::vector<ShieldAction>& actions,
               const ShieldBatchOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return shield.check_actions(robot, scene, atlas, view(current), actions, options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("robot"), py::arg("scene"), py::arg("atlas"), py::arg("current"), py::arg("actions"),
            py::arg("options") = ShieldBatchOptions{})
        .def_property_readonly("telemetry", &RuntimeShield::telemetry)
        .def("reset_telemetry", &RuntimeShield::reset_telemetry);

    bind_policy(module);
    bind_calibration(module);
    bind_memory(module);
    bind_trust(module);

    py::enum_<MonitorState>(module, "MonitorState")
        .value("INACTIVE", MonitorState::Inactive)
        .value("ON_CERTIFIED_PLAN", MonitorState::OnCertifiedPlan)
        .value("CERTIFIED_DEVIATION", MonitorState::CertifiedDeviation)
        .value("UNCERTIFIED_STATE", MonitorState::UncertifiedState);

    py::class_<RuntimeMonitorOptions>(module, "RuntimeMonitorOptions")
        .def(py::init<>())
        .def_readwrite("tracking_tolerance", &RuntimeMonitorOptions::tracking_tolerance)
        .def_readwrite("maximum_plan_waypoints", &RuntimeMonitorOptions::maximum_plan_waypoints);

    py::class_<MonitorObservation>(module, "MonitorObservation")
        .def_readonly("state", &MonitorObservation::state)
        .def_readonly("decision_id", &MonitorObservation::decision_id)
        .def_readonly("timestamp", &MonitorObservation::timestamp)
        .def_readonly("distance_to_plan", &MonitorObservation::distance_to_plan)
        .def_readonly("evidence", &MonitorObservation::evidence);

    py::class_<RuntimeMonitorStats>(module, "RuntimeMonitorStats")
        .def_readonly("observations", &RuntimeMonitorStats::observations)
        .def_readonly("on_plan", &RuntimeMonitorStats::on_plan)
        .def_readonly("certified_deviations", &RuntimeMonitorStats::certified_deviations)
        .def_readonly("uncertified_states", &RuntimeMonitorStats::uncertified_states);

    py::class_<RuntimeShieldMonitor>(module, "RuntimeShieldMonitor")
        .def(py::init([](const SafeAtlas& atlas, const RuntimeMonitorOptions& options) {
                 return std::make_unique<RuntimeShieldMonitor>(std::make_shared<SafeAtlas>(atlas), options);
             }),
             py::arg("atlas"), py::arg("options") = RuntimeMonitorOptions{})
        .def("arm", [](RuntimeShieldMonitor& monitor,
                       const ShieldDecision& decision) { unwrap_void(monitor.arm(decision)); })
        .def("disarm", &RuntimeShieldMonitor::disarm)
        .def("observe",
             [](RuntimeShieldMonitor& monitor, const Configuration& configuration, double timestamp) {
                 return unwrap(monitor.observe(view(configuration), timestamp));
             })
        .def_property_readonly("stats", &RuntimeShieldMonitor::stats);

    module.def("shield_action_type_name", &shield_action_type_name);
    module.def("shield_outcome_name", &shield_outcome_name);
    module.def("shield_reason_name", &shield_reason_name);
    module.def("monitor_state_name", &monitor_state_name);

    py::enum_<HipacBuildStatus>(module, "HipacBuildStatus")
        .value("CERTIFIED", HipacBuildStatus::Certified)
        .value("PARTIAL", HipacBuildStatus::Partial)
        .value("INVALID", HipacBuildStatus::Invalid);

    py::class_<HipacOptions>(module, "HipacOptions")
        .def(py::init<>())
        .def_readwrite("minimum_lateral_half_width", &HipacOptions::minimum_lateral_half_width)
        .def_readwrite("maximum_lateral_half_width", &HipacOptions::maximum_lateral_half_width)
        .def_readwrite("longitudinal_margin", &HipacOptions::longitudinal_margin)
        .def_readwrite("growth_iterations", &HipacOptions::growth_iterations)
        .def_readwrite("maximum_subdivision_depth", &HipacOptions::maximum_subdivision_depth)
        .def_readwrite("maximum_validations", &HipacOptions::maximum_validations)
        .def_readwrite("portal_tolerance", &HipacOptions::portal_tolerance)
        .def_readwrite("obstacle_padding", &HipacOptions::obstacle_padding)
        .def_readwrite("cancellation", &HipacOptions::cancellation);

    py::class_<HipacBuildStats>(module, "HipacBuildStats")
        .def_readonly("validations", &HipacBuildStats::validations)
        .def_readonly("recursive_splits", &HipacBuildStats::recursive_splits)
        .def_readonly("certified_cells", &HipacBuildStats::certified_cells)
        .def_readonly("failed_leaf_segments", &HipacBuildStats::failed_leaf_segments)
        .def_readonly("growth_attempts", &HipacBuildStats::growth_attempts)
        .def_readonly("portals", &HipacBuildStats::portals);

    py::class_<CertifiedObbRegion>(module, "CertifiedObbRegion")
        .def_readonly("id", &CertifiedObbRegion::id)
        .def_readonly("bounds", &CertifiedObbRegion::bounds)
        .def_readonly("certificate", &CertifiedObbRegion::certificate)
        .def_readonly("component", &CertifiedObbRegion::component)
        .def_readonly("segment_index", &CertifiedObbRegion::segment_index)
        .def_readonly("start_fraction", &CertifiedObbRegion::start_fraction)
        .def_readonly("end_fraction", &CertifiedObbRegion::end_fraction)
        .def_readonly("entry", &CertifiedObbRegion::entry)
        .def_readonly("exit", &CertifiedObbRegion::exit);

    py::class_<PortalRegion>(module, "PortalRegion")
        .def_readonly("id", &PortalRegion::id)
        .def_readonly("left_region", &PortalRegion::left_region)
        .def_readonly("right_region", &PortalRegion::right_region)
        .def_readonly("witness", &PortalRegion::witness)
        .def_readonly("certificate", &PortalRegion::certificate);

    py::class_<CertifiedRoute>(module, "CertifiedRoute")
        .def_readonly("waypoints", &CertifiedRoute::waypoints)
        .def_readonly("region_sequence", &CertifiedRoute::region_sequence)
        .def_readonly("portal_sequence", &CertifiedRoute::portal_sequence)
        .def_readonly("certificate", &CertifiedRoute::certificate);

    py::class_<HipacCorridor>(module, "HipacCorridor")
        .def_static("load",
                    [](const std::filesystem::path& path) { return unwrap(HipacCorridor::load(path)); })
        .def_property_readonly("dimension", &HipacCorridor::dimension)
        .def_property_readonly("robot_digest", &HipacCorridor::robot_digest)
        .def_property_readonly("scene_digest", &HipacCorridor::scene_digest)
        .def_property_readonly("regions", &HipacCorridor::regions)
        .def_property_readonly("portals", &HipacCorridor::portals)
        .def("regions_at", [](const HipacCorridor& corridor,
                              const Configuration& q) { return unwrap(corridor.regions_at(view(q))); })
        .def("contains",
             [](const HipacCorridor& corridor, const Configuration& q) { return corridor.contains(view(q)); })
        .def("connected",
             [](const HipacCorridor& corridor, const Configuration& first, const Configuration& second) {
                 return unwrap(corridor.connected(view(first), view(second)));
             })
        .def("route",
             [](const HipacCorridor& corridor, const Configuration& first, const Configuration& second) {
                 return unwrap(corridor.route(view(first), view(second)));
             })
        .def("verify_compatible",
             [](const HipacCorridor& corridor, const SerialRobotModel& robot, const SceneSnapshot& scene) {
                 unwrap_void(corridor.verify_compatible(robot, scene));
             })
        .def(
            "save",
            [](const HipacCorridor& corridor, const std::filesystem::path& path, bool overwrite) {
                unwrap_void(corridor.save(path, SaveOptions{overwrite}));
            },
            py::arg("path"), py::arg("overwrite") = false);

    py::class_<HipacBuildReport>(module, "HipacBuildReport")
        .def_readonly("status", &HipacBuildReport::status)
        .def_readonly("coverage_ratio", &HipacBuildReport::coverage_ratio)
        .def_readonly("waypoint_count", &HipacBuildReport::waypoint_count)
        .def_readonly("segment_count", &HipacBuildReport::segment_count)
        .def_readonly("uncovered_intervals", &HipacBuildReport::uncovered_intervals)
        .def_readonly("corridor", &HipacBuildReport::corridor)
        .def_readonly("stats", &HipacBuildReport::stats);

    py::class_<HipacCorridorBuilder>(module, "HipacCorridorBuilder")
        .def(py::init<>())
        .def(
            "build",
            [](const HipacCorridorBuilder& builder, const SerialRobotModel& robot, const SceneSnapshot& scene,
               const std::vector<Configuration>& path, const HipacOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return builder.build(robot, scene,
                                         std::span<const Configuration>(path.data(), path.size()), options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("robot"), py::arg("scene"), py::arg("path"), py::arg("options") = HipacOptions{});

    py::class_<CspaceZonotope>(module, "CspaceZonotope")
        .def(py::init([](Configuration center, std::size_t generator_count, std::vector<double> generators) {
                 return unwrap(
                     CspaceZonotope::create(std::move(center), generator_count, std::move(generators)));
             }),
             py::arg("center"), py::arg("generator_count"), py::arg("generators"))
        .def_property_readonly("dimension", &CspaceZonotope::dimension)
        .def_property_readonly("generator_count", &CspaceZonotope::generator_count)
        .def_property_readonly("center", &CspaceZonotope::center)
        .def_property_readonly("generators", &CspaceZonotope::generators)
        .def_property_readonly("enclosing_aabb", &CspaceZonotope::enclosing_aabb)
        .def(
            "contains",
            [](const CspaceZonotope& region, const Configuration& configuration, double tolerance,
               std::size_t maximum_iterations) {
                return unwrap(region.contains(view(configuration), tolerance, maximum_iterations));
            },
            py::arg("configuration"), py::arg("tolerance") = 1e-10, py::arg("maximum_iterations") = 512)
        .def("valid", &CspaceZonotope::valid);

    py::class_<CspaceTaylorRegion>(module, "CspaceTaylorRegion")
        .def(py::init([](Configuration center, std::size_t variable_count, std::vector<double> linear,
                         Configuration remainder_radii) {
                 return unwrap(CspaceTaylorRegion::create(std::move(center), variable_count,
                                                          std::move(linear), std::move(remainder_radii)));
             }),
             py::arg("center"), py::arg("variable_count"), py::arg("linear"), py::arg("remainder_radii"))
        .def_static(
            "from_zonotope",
            [](const CspaceZonotope& region) { return unwrap(CspaceTaylorRegion::from_zonotope(region)); })
        .def_property_readonly("dimension", &CspaceTaylorRegion::dimension)
        .def_property_readonly("variable_count", &CspaceTaylorRegion::variable_count)
        .def_property_readonly("center", &CspaceTaylorRegion::center)
        .def_property_readonly("linear", &CspaceTaylorRegion::linear)
        .def_property_readonly("remainder_radii", &CspaceTaylorRegion::remainder_radii)
        .def_property_readonly("enclosing_aabb", &CspaceTaylorRegion::enclosing_aabb)
        .def(
            "contains",
            [](const CspaceTaylorRegion& region, const Configuration& configuration, double tolerance,
               std::size_t maximum_iterations) {
                return unwrap(region.contains(view(configuration), tolerance, maximum_iterations));
            },
            py::arg("configuration"), py::arg("tolerance") = 1e-10, py::arg("maximum_iterations") = 512)
        .def("valid", &CspaceTaylorRegion::valid);

    py::class_<HigherOrderValidation>(module, "HigherOrderValidation")
        .def_readonly("disposition", &HigherOrderValidation::disposition)
        .def_readonly("clearance_lower_bound", &HigherOrderValidation::clearance_lower_bound)
        .def_readonly("conservative_enclosure", &HigherOrderValidation::conservative_enclosure)
        .def_readonly("envelope", &HigherOrderValidation::envelope);

    py::class_<HigherOrderRegionValidator>(module, "HigherOrderRegionValidator")
        .def(py::init<EnvelopeOptions>(), py::arg("options") = EnvelopeOptions{})
        .def("validate",
             [](const HigherOrderRegionValidator& validator, const SerialRobotModel& robot,
                const SceneSnapshot& scene,
                const CspaceZonotope& region) { return unwrap(validator.validate(robot, scene, region)); })
        .def("validate", [](const HigherOrderRegionValidator& validator, const SerialRobotModel& robot,
                            const SceneSnapshot& scene, const CspaceTaylorRegion& region) {
            return unwrap(validator.validate(robot, scene, region));
        });

    module.def(
        "make_higher_order_region_certificate",
        [](const SerialRobotModel& robot, const SceneSnapshot& scene, const CspaceZonotope& region,
           const HigherOrderRegionValidator& validator, const HigherOrderValidation& validation,
           double obstacle_padding) {
            return unwrap(make_higher_order_region_certificate(robot, scene, region, validator, validation,
                                                               obstacle_padding));
        },
        py::arg("robot"), py::arg("scene"), py::arg("region"), py::arg("validator"), py::arg("validation"),
        py::arg("obstacle_padding") = 0.0);
    module.def(
        "make_higher_order_region_certificate",
        [](const SerialRobotModel& robot, const SceneSnapshot& scene, const CspaceTaylorRegion& region,
           const HigherOrderRegionValidator& validator, const HigherOrderValidation& validation,
           double obstacle_padding) {
            return unwrap(make_higher_order_region_certificate(robot, scene, region, validator, validation,
                                                               obstacle_padding));
        },
        py::arg("robot"), py::arg("scene"), py::arg("region"), py::arg("validator"), py::arg("validation"),
        py::arg("obstacle_padding") = 0.0);

    py::enum_<RegionType>(module, "RegionType")
        .value("AABB", RegionType::Aabb)
        .value("OBB", RegionType::Obb)
        .value("PORTAL", RegionType::Portal)
        .value("TRAJECTORY_TUBE", RegionType::TrajectoryTube)
        .value("ZONOTOPE", RegionType::Zonotope)
        .value("TAYLOR", RegionType::Taylor);

    py::class_<PortalIntersectionOptions>(module, "PortalIntersectionOptions")
        .def(py::init<>())
        .def_readwrite("maximum_iterations", &PortalIntersectionOptions::maximum_iterations)
        .def_readwrite("feasibility_tolerance", &PortalIntersectionOptions::feasibility_tolerance)
        .def_readwrite("cancellation", &PortalIntersectionOptions::cancellation);

    py::class_<CspacePortal>(module, "CspacePortal")
        .def_static(
            "intersect",
            [](const CspaceObb& first, const CspaceObb& second, const PortalIntersectionOptions& options) {
                return unwrap(CspacePortal::intersect(first, second, options));
            },
            py::arg("first"), py::arg("second"), py::arg("options") = PortalIntersectionOptions{})
        .def_property_readonly("dimension", &CspacePortal::dimension)
        .def_property_readonly("constraint_count", &CspacePortal::constraint_count)
        .def_property_readonly("normals", &CspacePortal::normals)
        .def_property_readonly("offsets", &CspacePortal::offsets)
        .def_property_readonly("witness", &CspacePortal::witness)
        .def_property_readonly("enclosing_aabb", &CspacePortal::enclosing_aabb)
        .def(
            "contains",
            [](const CspacePortal& portal, const Configuration& configuration, double tolerance) {
                return portal.contains(view(configuration), tolerance);
            },
            py::arg("configuration"), py::arg("tolerance") = 0.0)
        .def("valid", &CspacePortal::valid);

    py::class_<PortalGeometry>(module, "PortalGeometry")
        .def_readonly("left_region", &PortalGeometry::left_region)
        .def_readonly("right_region", &PortalGeometry::right_region)
        .def_readonly("intersection", &PortalGeometry::intersection);

    py::class_<TrajectoryTubeGeometry>(module, "TrajectoryTubeGeometry")
        .def_readonly("cell_ids", &TrajectoryTubeGeometry::cell_ids)
        .def_readonly("portal_ids", &TrajectoryTubeGeometry::portal_ids)
        .def_readonly("centerline", &TrajectoryTubeGeometry::centerline);

    py::class_<RegionRecord>(module, "RegionRecord")
        .def_readonly("id", &RegionRecord::id)
        .def_readonly("geometry", &RegionRecord::geometry)
        .def_readonly("certificate_index", &RegionRecord::certificate_index)
        .def_readonly("component", &RegionRecord::component)
        .def_readonly("dependency", &RegionRecord::dependency)
        .def_readonly("source", &RegionRecord::source)
        .def_property_readonly("type",
                               [](const RegionRecord& record) { return region_type(record.geometry); });

    py::class_<CertifiedRegionInput>(module, "CertifiedRegionInput")
        .def(py::init<RegionGeometry, Certificate, LinkEnvelope, std::string>(), py::arg("geometry"),
             py::arg("certificate"), py::arg("dependency"), py::arg("source") = "")
        .def_readwrite("geometry", &CertifiedRegionInput::geometry)
        .def_readwrite("certificate", &CertifiedRegionInput::certificate)
        .def_readwrite("dependency", &CertifiedRegionInput::dependency)
        .def_readwrite("source", &CertifiedRegionInput::source);

    py::class_<RegionQueryOptions>(module, "RegionQueryOptions")
        .def(py::init<>())
        .def_readwrite("include_portals", &RegionQueryOptions::include_portals)
        .def_readwrite("include_trajectory_tubes", &RegionQueryOptions::include_trajectory_tubes)
        .def_readwrite("tolerance", &RegionQueryOptions::tolerance);

    py::class_<PortalDiscoveryOptions>(module, "PortalDiscoveryOptions")
        .def(py::init<>())
        .def_readwrite("maximum_candidate_pairs", &PortalDiscoveryOptions::maximum_candidate_pairs)
        .def_readwrite("maximum_portals", &PortalDiscoveryOptions::maximum_portals)
        .def_readwrite("maximum_iterations", &PortalDiscoveryOptions::maximum_iterations)
        .def_readwrite("feasibility_tolerance", &PortalDiscoveryOptions::feasibility_tolerance)
        .def_readwrite("cancellation", &PortalDiscoveryOptions::cancellation);

    py::class_<PortalDiscoveryStats>(module, "PortalDiscoveryStats")
        .def_readonly("candidate_pairs", &PortalDiscoveryStats::candidate_pairs)
        .def_readonly("aabb_rejections", &PortalDiscoveryStats::aabb_rejections)
        .def_readonly("feasibility_tests", &PortalDiscoveryStats::feasibility_tests)
        .def_readonly("portals_created", &PortalDiscoveryStats::portals_created);

    py::class_<ObbAtlasBuildOptions>(module, "ObbAtlasBuildOptions")
        .def(py::init<>())
        .def_readwrite("initial_half_width", &ObbAtlasBuildOptions::initial_half_width)
        .def_readwrite("maximum_half_width", &ObbAtlasBuildOptions::maximum_half_width)
        .def_readwrite("bridge_longitudinal_margin", &ObbAtlasBuildOptions::bridge_longitudinal_margin)
        .def_readwrite("nearest_bridge_neighbors", &ObbAtlasBuildOptions::nearest_bridge_neighbors)
        .def_readwrite("growth_iterations", &ObbAtlasBuildOptions::growth_iterations)
        .def_readwrite("maximum_samples", &ObbAtlasBuildOptions::maximum_samples)
        .def_readwrite("maximum_pair_evaluations", &ObbAtlasBuildOptions::maximum_pair_evaluations)
        .def_readwrite("maximum_validations", &ObbAtlasBuildOptions::maximum_validations)
        .def_readwrite("obstacle_padding", &ObbAtlasBuildOptions::obstacle_padding)
        .def_readwrite("portal", &ObbAtlasBuildOptions::portal)
        .def_readwrite("cancellation", &ObbAtlasBuildOptions::cancellation);

    py::class_<ObbAtlasBuildStats>(module, "ObbAtlasBuildStats")
        .def_readonly("unique_samples", &ObbAtlasBuildStats::unique_samples)
        .def_readonly("point_regions", &ObbAtlasBuildStats::point_regions)
        .def_readonly("bridge_regions", &ObbAtlasBuildStats::bridge_regions)
        .def_readonly("rejected_candidates", &ObbAtlasBuildStats::rejected_candidates)
        .def_readonly("validations", &ObbAtlasBuildStats::validations)
        .def_readonly("growth_attempts", &ObbAtlasBuildStats::growth_attempts)
        .def_readonly("portal", &ObbAtlasBuildStats::portal);

    py::class_<RegionDatabase>(module, "RegionDatabase")
        .def_static("load",
                    [](const std::filesystem::path& path) { return unwrap(RegionDatabase::load(path)); })
        .def_static(
            "from_atlas",
            [](const SafeAtlas& atlas, std::string scene_version, const PortalDiscoveryOptions& options) {
                return unwrap(RegionDatabase::from_atlas(atlas, std::move(scene_version), options));
            },
            py::arg("atlas"), py::arg("scene_version") = "", py::arg("options") = PortalDiscoveryOptions{})
        .def_static(
            "from_corridor",
            [](const HipacCorridor& corridor, std::string scene_version,
               const PortalDiscoveryOptions& options) {
                return unwrap(RegionDatabase::from_corridor(corridor, std::move(scene_version), options));
            },
            py::arg("corridor"), py::arg("scene_version"), py::arg("options") = PortalDiscoveryOptions{})
        .def_static(
            "create",
            [](const SerialRobotModel& robot, const SceneSnapshot& scene,
               std::vector<CertifiedRegionInput> regions, const PortalDiscoveryOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return RegionDatabase::create(robot, scene, std::move(regions), options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("robot"), py::arg("scene"), py::arg("regions"),
            py::arg("options") = PortalDiscoveryOptions{})
        .def_property_readonly("dimension", &RegionDatabase::dimension)
        .def_property_readonly("robot_digest", &RegionDatabase::robot_digest)
        .def_property_readonly("scene_digest", &RegionDatabase::scene_digest)
        .def_property_readonly("scene_version", &RegionDatabase::scene_version)
        .def_property_readonly("records", &RegionDatabase::records)
        .def_property_readonly("certificates", &RegionDatabase::certificates)
        .def("region",
             [](const RegionDatabase& database, RegionId id) { return unwrap(database.region(id)); })
        .def("certificate", [](const RegionDatabase& database,
                               const std::string& id) { return unwrap(database.certificate(id)); })
        .def(
            "regions_at",
            [](const RegionDatabase& database, const Configuration& configuration,
               const RegionQueryOptions& options) {
                return unwrap(database.regions_at(view(configuration), options));
            },
            py::arg("configuration"), py::arg("options") = RegionQueryOptions{})
        .def(
            "contains",
            [](const RegionDatabase& database, const Configuration& configuration,
               const RegionQueryOptions& options) { return database.contains(view(configuration), options); },
            py::arg("configuration"), py::arg("options") = RegionQueryOptions{})
        .def(
            "nearest_region",
            [](const RegionDatabase& database, const Configuration& configuration,
               const RegionQueryOptions& options) {
                return unwrap(database.nearest_region(view(configuration), options));
            },
            py::arg("configuration"), py::arg("options") = RegionQueryOptions{})
        .def("connected",
             [](const RegionDatabase& database, const Configuration& first, const Configuration& second) {
                 return unwrap(database.connected(view(first), view(second)));
             })
        .def("verify_compatible",
             [](const RegionDatabase& database, const SerialRobotModel& robot, const SceneSnapshot& scene) {
                 unwrap_void(database.verify_compatible(robot, scene));
             })
        .def(
            "save",
            [](const RegionDatabase& database, const std::filesystem::path& path, bool overwrite) {
                unwrap_void(database.save(path, SaveOptions{overwrite}));
            },
            py::arg("path"), py::arg("overwrite") = false);

    py::class_<ObbAtlasBuildResult>(module, "ObbAtlasBuildResult")
        .def_readonly("database", &ObbAtlasBuildResult::database)
        .def_readonly("stats", &ObbAtlasBuildResult::stats);

    py::class_<ObbAtlasBuilder>(module, "ObbAtlasBuilder")
        .def(py::init<>())
        .def(
            "build",
            [](const ObbAtlasBuilder& builder, const SerialRobotModel& robot, const SceneSnapshot& scene,
               std::vector<Configuration> samples, const ObbAtlasBuildOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return builder.build(robot, scene, std::move(samples), options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("robot"), py::arg("scene"), py::arg("samples"),
            py::arg("options") = ObbAtlasBuildOptions{});

    py::enum_<CertifiedSamplingPolicy>(module, "CertifiedSamplingPolicy")
        .value("UNIFORM_REGIONS", CertifiedSamplingPolicy::UniformRegions)
        .value("VOLUME_WEIGHTED", CertifiedSamplingPolicy::VolumeWeighted);

    py::class_<CertifiedSamplerOptions>(module, "CertifiedSamplerOptions")
        .def(py::init<>())
        .def_readwrite("policy", &CertifiedSamplerOptions::policy)
        .def_readwrite("seed", &CertifiedSamplerOptions::seed)
        .def_readwrite("maximum_attempts", &CertifiedSamplerOptions::maximum_attempts);

    py::class_<CertifiedSamplerStats>(module, "CertifiedSamplerStats")
        .def_readonly("samples_requested", &CertifiedSamplerStats::samples_requested)
        .def_readonly("samples_returned", &CertifiedSamplerStats::samples_returned)
        .def_readonly("near_samples_requested", &CertifiedSamplerStats::near_samples_requested)
        .def_readonly("rejected_attempts", &CertifiedSamplerStats::rejected_attempts);

    py::class_<CertifiedRegionSampler>(module, "CertifiedRegionSampler")
        .def_static(
            "create",
            [](const SafeAtlas& atlas, const CertifiedSamplerOptions& options) {
                return unwrap(
                    CertifiedRegionSampler::create(std::make_shared<const SafeAtlas>(atlas), options));
            },
            py::arg("atlas"), py::arg("options") = CertifiedSamplerOptions{})
        .def("sample", [](CertifiedRegionSampler& sampler) { return unwrap(sampler.sample()); })
        .def(
            "sample_near",
            [](CertifiedRegionSampler& sampler, const Configuration& reference, double distance) {
                return unwrap(sampler.sample_near(view(reference), distance));
            },
            py::arg("reference"), py::arg("maximum_distance"))
        .def_property_readonly("stats", [](const CertifiedRegionSampler& sampler) { return sampler.stats(); })
        .def("reset_stats", &CertifiedRegionSampler::reset_stats)
        .def("valid", &CertifiedRegionSampler::valid);

    py::enum_<RoadmapNodeKind>(module, "RoadmapNodeKind")
        .value("REGION_CENTER", RoadmapNodeKind::RegionCenter)
        .value("PORTAL_WITNESS", RoadmapNodeKind::PortalWitness);

    py::class_<CertifiedRoadmapNode>(module, "CertifiedRoadmapNode")
        .def_readonly("id", &CertifiedRoadmapNode::id)
        .def_readonly("kind", &CertifiedRoadmapNode::kind)
        .def_readonly("configuration", &CertifiedRoadmapNode::configuration)
        .def_readonly("support_regions", &CertifiedRoadmapNode::support_regions);

    py::class_<CertifiedRoadmapEdge>(module, "CertifiedRoadmapEdge")
        .def_readonly("first", &CertifiedRoadmapEdge::first)
        .def_readonly("second", &CertifiedRoadmapEdge::second)
        .def_readonly("covering_region", &CertifiedRoadmapEdge::covering_region);

    py::class_<CertifiedRoadmapOptions>(module, "CertifiedRoadmapOptions")
        .def(py::init<>())
        .def_readwrite("maximum_nodes", &CertifiedRoadmapOptions::maximum_nodes)
        .def_readwrite("maximum_edges", &CertifiedRoadmapOptions::maximum_edges)
        .def_readwrite("cancellation", &CertifiedRoadmapOptions::cancellation);

    py::class_<CertifiedRoadmapStats>(module, "CertifiedRoadmapStats")
        .def_readonly("region_nodes", &CertifiedRoadmapStats::region_nodes)
        .def_readonly("portal_nodes", &CertifiedRoadmapStats::portal_nodes)
        .def_readonly("edges", &CertifiedRoadmapStats::edges)
        .def_readonly("nonintersecting_adjacencies", &CertifiedRoadmapStats::nonintersecting_adjacencies);

    py::class_<CertifiedRoadmap>(module, "CertifiedRoadmap")
        .def_property_readonly("dimension", &CertifiedRoadmap::dimension)
        .def_property_readonly("robot_digest", &CertifiedRoadmap::robot_digest)
        .def_property_readonly("scene_digest", &CertifiedRoadmap::scene_digest)
        .def_property_readonly("nodes", &CertifiedRoadmap::nodes)
        .def_property_readonly("edges", &CertifiedRoadmap::edges)
        .def("nearest_node",
             [](const CertifiedRoadmap& roadmap, const Configuration& configuration) {
                 return unwrap(roadmap.nearest_node(view(configuration)));
             })
        .def("verify_compatible",
             [](const CertifiedRoadmap& roadmap, const SerialRobotModel& robot, const SceneSnapshot& scene) {
                 unwrap_void(roadmap.verify_compatible(robot, scene));
             })
        .def("valid", &CertifiedRoadmap::valid);

    py::class_<CertifiedRoadmapBuildResult>(module, "CertifiedRoadmapBuildResult")
        .def_readonly("roadmap", &CertifiedRoadmapBuildResult::roadmap)
        .def_readonly("stats", &CertifiedRoadmapBuildResult::stats);

    py::class_<CertifiedRoadmapBuilder>(module, "CertifiedRoadmapBuilder")
        .def(py::init<>())
        .def(
            "build",
            [](const CertifiedRoadmapBuilder& builder, const SafeAtlas& atlas,
               const CertifiedRoadmapOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return builder.build(atlas, options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("atlas"), py::arg("options") = CertifiedRoadmapOptions{});

    py::enum_<OptimizationBackend>(module, "OptimizationBackend")
        .value("TRAJOPT", OptimizationBackend::TrajOpt)
        .value("CHOMP", OptimizationBackend::Chomp)
        .value("STOMP", OptimizationBackend::Stomp)
        .value("MPC", OptimizationBackend::Mpc);

    py::class_<RegionConstraintResidual>(module, "RegionConstraintResidual")
        .def_readonly("satisfied", &RegionConstraintResidual::satisfied)
        .def_readonly("maximum_violation", &RegionConstraintResidual::maximum_violation)
        .def_readonly("squared_penalty", &RegionConstraintResidual::squared_penalty)
        .def_readonly("configuration_gradient", &RegionConstraintResidual::configuration_gradient)
        .def_readonly("auxiliary_gradient", &RegionConstraintResidual::auxiliary_gradient);

    py::class_<ConstraintProjectionOptions>(module, "ConstraintProjectionOptions")
        .def(py::init<>())
        .def_readwrite("maximum_iterations", &ConstraintProjectionOptions::maximum_iterations)
        .def_readwrite("tolerance", &ConstraintProjectionOptions::tolerance)
        .def_readwrite("cancellation", &ConstraintProjectionOptions::cancellation);

    py::class_<ConstraintProjection>(module, "ConstraintProjection")
        .def_readonly("converged", &ConstraintProjection::converged)
        .def_readonly("configuration", &ConstraintProjection::configuration)
        .def_readonly("auxiliary", &ConstraintProjection::auxiliary)
        .def_readonly("maximum_violation", &ConstraintProjection::maximum_violation)
        .def_readonly("iterations", &ConstraintProjection::iterations);

    py::class_<LinearRegionConstraint>(module, "LinearRegionConstraint")
        .def_readonly("region_id", &LinearRegionConstraint::region_id)
        .def_readonly("region_type", &LinearRegionConstraint::region_type)
        .def_readonly("certificate_id", &LinearRegionConstraint::certificate_id)
        .def_readonly("configuration_dimension", &LinearRegionConstraint::configuration_dimension)
        .def_readonly("auxiliary_dimension", &LinearRegionConstraint::auxiliary_dimension)
        .def_readonly("inequality_matrix", &LinearRegionConstraint::inequality_matrix)
        .def_readonly("inequality_upper", &LinearRegionConstraint::inequality_upper)
        .def_readonly("equality_matrix", &LinearRegionConstraint::equality_matrix)
        .def_readonly("equality_value", &LinearRegionConstraint::equality_value)
        .def_readonly("auxiliary_bounds", &LinearRegionConstraint::auxiliary_bounds)
        .def_property_readonly("variable_dimension", &LinearRegionConstraint::variable_dimension)
        .def_property_readonly("inequality_count", &LinearRegionConstraint::inequality_count)
        .def_property_readonly("equality_count", &LinearRegionConstraint::equality_count)
        .def(
            "evaluate",
            [](const LinearRegionConstraint& constraint, const Configuration& configuration,
               const std::vector<double>& auxiliary, double tolerance) {
                return unwrap(constraint.evaluate(view(configuration), auxiliary, tolerance));
            },
            py::arg("configuration"), py::arg("auxiliary") = std::vector<double>{},
            py::arg("tolerance") = 1e-10)
        .def(
            "project",
            [](const LinearRegionConstraint& constraint, const Configuration& configuration,
               const ConstraintProjectionOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return constraint.project(view(configuration), options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("configuration"), py::arg("options") = ConstraintProjectionOptions{})
        .def("valid", &LinearRegionConstraint::valid);

    module.def("compile_region_constraint", [](const RegionDatabase& database, RegionId region_id) {
        return unwrap(compile_region_constraint(database, region_id));
    });

    py::enum_<TrajectoryAssignmentStatus>(module, "TrajectoryAssignmentStatus")
        .value("COMPLETE", TrajectoryAssignmentStatus::Complete)
        .value("PARTIAL", TrajectoryAssignmentStatus::Partial)
        .value("INVALID", TrajectoryAssignmentStatus::Invalid);

    py::class_<TrajectoryAssignmentOptions>(module, "TrajectoryAssignmentOptions")
        .def(py::init<>())
        .def_readwrite("maximum_waypoints", &TrajectoryAssignmentOptions::maximum_waypoints)
        .def_readwrite("maximum_region_tests", &TrajectoryAssignmentOptions::maximum_region_tests)
        .def_readwrite("cancellation", &TrajectoryAssignmentOptions::cancellation);

    py::class_<TrajectoryRegionAssignment>(module, "TrajectoryRegionAssignment")
        .def_readonly("status", &TrajectoryRegionAssignment::status)
        .def_readonly("region_ids", &TrajectoryRegionAssignment::region_ids)
        .def_readonly("assigned_waypoints", &TrajectoryRegionAssignment::assigned_waypoints)
        .def_readonly("first_unassigned_waypoint", &TrajectoryRegionAssignment::first_unassigned_waypoint)
        .def_readonly("region_tests", &TrajectoryRegionAssignment::region_tests);

    module.def(
        "assign_trajectory_regions",
        [](const RegionDatabase& database, const std::vector<Configuration>& trajectory,
           const TrajectoryAssignmentOptions& options) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return assign_trajectory_regions(database, trajectory, options);
            }();
            return unwrap(std::move(result));
        },
        py::arg("database"), py::arg("trajectory"), py::arg("options") = TrajectoryAssignmentOptions{});

    py::class_<TrajectoryConstraintProgram>(module, "TrajectoryConstraintProgram")
        .def_readonly("backend", &TrajectoryConstraintProgram::backend)
        .def_readonly("configuration_dimension", &TrajectoryConstraintProgram::configuration_dimension)
        .def_readonly("region_ids", &TrajectoryConstraintProgram::region_ids)
        .def_readonly("stages", &TrajectoryConstraintProgram::stages)
        .def("valid", &TrajectoryConstraintProgram::valid);

    py::class_<ProgramEvaluation>(module, "ProgramEvaluation")
        .def_readonly("satisfied", &ProgramEvaluation::satisfied)
        .def_readonly("maximum_violation", &ProgramEvaluation::maximum_violation)
        .def_readonly("squared_penalty", &ProgramEvaluation::squared_penalty)
        .def_readonly("stages", &ProgramEvaluation::stages);

    module.def("compile_trajectory_constraints",
               [](const RegionDatabase& database, const std::vector<RegionId>& region_ids,
                  OptimizationBackend backend) {
                   return unwrap(compile_trajectory_constraints(database, region_ids, backend));
               });
    module.def(
        "evaluate_trajectory_constraints",
        [](const TrajectoryConstraintProgram& program, const std::vector<Configuration>& trajectory,
           const std::vector<std::vector<double>>& auxiliary, double tolerance) {
            return unwrap(evaluate_trajectory_constraints(program, trajectory, auxiliary, tolerance));
        },
        py::arg("program"), py::arg("trajectory"), py::arg("auxiliary") = std::vector<std::vector<double>>{},
        py::arg("tolerance") = 1e-10);
    module.def(
        "project_trajectory_constraints",
        [](const TrajectoryConstraintProgram& program, const std::vector<Configuration>& trajectory,
           const ConstraintProjectionOptions& options) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return project_trajectory_constraints(program, trajectory, options);
            }();
            return unwrap(std::move(result));
        },
        py::arg("program"), py::arg("trajectory"), py::arg("options") = ConstraintProjectionOptions{});

    py::class_<TrajOptRegionAdapter>(module, "TrajOptRegionAdapter")
        .def(py::init<>())
        .def("compile", [](const TrajOptRegionAdapter& adapter, const RegionDatabase& database,
                           const std::vector<RegionId>& region_ids) {
            return unwrap(adapter.compile(database, region_ids));
        });
    py::class_<ChompRegionAdapter>(module, "ChompRegionAdapter")
        .def(py::init<>())
        .def("compile", [](const ChompRegionAdapter& adapter, const RegionDatabase& database,
                           const std::vector<RegionId>& region_ids) {
            return unwrap(adapter.compile(database, region_ids));
        });
    py::class_<StompRegionAdapter>(module, "StompRegionAdapter")
        .def(py::init<>())
        .def("compile", [](const StompRegionAdapter& adapter, const RegionDatabase& database,
                           const std::vector<RegionId>& region_ids) {
            return unwrap(adapter.compile(database, region_ids));
        });
    py::class_<MpcRegionAdapter>(module, "MpcRegionAdapter")
        .def(py::init<>())
        .def("compile", [](const MpcRegionAdapter& adapter, const RegionDatabase& database,
                           const std::vector<RegionId>& region_ids) {
            return unwrap(adapter.compile(database, region_ids));
        });

    py::class_<AtlasBuildResult>(module, "AtlasBuildResult")
        .def_readonly("atlas", &AtlasBuildResult::atlas)
        .def_readonly("stats", &AtlasBuildResult::stats);

    py::class_<AtlasBuilder>(module, "AtlasBuilder")
        .def(py::init<>())
        .def(
            "build",
            [](const AtlasBuilder& builder, const SerialRobotModel& robot, const SceneSnapshot& scene,
               std::vector<Configuration> samples, const BuildOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return builder.build(robot, scene, std::move(samples), options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("robot"), py::arg("scene"), py::arg("samples"), py::arg("options") = BuildOptions{});

    py::class_<AtlasUpdateOptions>(module, "AtlasUpdateOptions")
        .def(py::init<>())
        .def_readwrite("maximum_repair_depth", &AtlasUpdateOptions::maximum_repair_depth)
        .def_readwrite("maximum_repair_nodes", &AtlasUpdateOptions::maximum_repair_nodes)
        .def_readwrite("maximum_validations", &AtlasUpdateOptions::maximum_validations)
        .def_readwrite("minimum_normalized_width", &AtlasUpdateOptions::minimum_normalized_width)
        .def_readwrite("adjacency_tolerance", &AtlasUpdateOptions::adjacency_tolerance)
        .def_readwrite("obstacle_padding", &AtlasUpdateOptions::obstacle_padding)
        .def_readwrite("repair_invalidated_regions", &AtlasUpdateOptions::repair_invalidated_regions)
        .def_readwrite("cancellation", &AtlasUpdateOptions::cancellation);

    py::class_<AtlasUpdateStats>(module, "AtlasUpdateStats")
        .def_readonly("regions_examined", &AtlasUpdateStats::regions_examined)
        .def_readonly("certificates_inherited", &AtlasUpdateStats::certificates_inherited)
        .def_readonly("regions_revalidated", &AtlasUpdateStats::regions_revalidated)
        .def_readonly("regions_invalidated", &AtlasUpdateStats::regions_invalidated)
        .def_readonly("repair_nodes_visited", &AtlasUpdateStats::repair_nodes_visited)
        .def_readonly("repaired_regions", &AtlasUpdateStats::repaired_regions)
        .def_readonly("unresolved_repair_nodes", &AtlasUpdateStats::unresolved_repair_nodes)
        .def_readonly("validations", &AtlasUpdateStats::validations);

    py::class_<AtlasUpdateResult>(module, "AtlasUpdateResult")
        .def_readonly("atlas", &AtlasUpdateResult::atlas)
        .def_readonly("delta", &AtlasUpdateResult::delta)
        .def_readonly("stats", &AtlasUpdateResult::stats)
        .def_readonly("retained_region_ids", &AtlasUpdateResult::retained_region_ids)
        .def_readonly("invalidated_region_ids", &AtlasUpdateResult::invalidated_region_ids)
        .def_readonly("repaired_region_ids", &AtlasUpdateResult::repaired_region_ids);

    py::class_<AtlasUpdater>(module, "AtlasUpdater")
        .def(py::init<>())
        .def(
            "update",
            [](const AtlasUpdater& updater, const SerialRobotModel& robot,
               const SceneSnapshot& previous_scene, const SceneSnapshot& next_scene,
               const SafeAtlas& previous_atlas, std::vector<Configuration> repair_samples,
               const AtlasUpdateOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return updater.update(robot, previous_scene, next_scene, previous_atlas,
                                          std::move(repair_samples), options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("robot"), py::arg("previous_scene"), py::arg("next_scene"), py::arg("previous_atlas"),
            py::arg("repair_samples") = std::vector<Configuration>{},
            py::arg("options") = AtlasUpdateOptions{});

    py::class_<AtlasVersionStore>(module, "AtlasVersionStore")
        .def_static("create",
                    [](const std::filesystem::path& path, const SafeAtlas& atlas) {
                        return unwrap(AtlasVersionStore::create(path, atlas));
                    })
        .def_static("open",
                    [](const std::filesystem::path& path) { return unwrap(AtlasVersionStore::open(path)); })
        .def_property_readonly("directory", &AtlasVersionStore::directory)
        .def_property_readonly("current_version_id", &AtlasVersionStore::current_version_id)
        .def_property_readonly("versions", &AtlasVersionStore::versions)
        .def("load_current", [](const AtlasVersionStore& store) { return unwrap(store.load_current()); })
        .def("load_version",
             [](const AtlasVersionStore& store, const std::string& version_id) {
                 return unwrap(store.load_version(version_id));
             })
        .def("publish",
             [](AtlasVersionStore& store, const SafeAtlas& atlas) { unwrap_void(store.publish(atlas)); })
        .def("rollback", [](AtlasVersionStore& store, const std::string& version_id) {
            unwrap_void(store.rollback(version_id));
        });
}
