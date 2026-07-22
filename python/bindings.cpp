#include <rbfsafe/rbfsafe.h>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace py = pybind11;
using namespace rbfsafe;

namespace {

class RBFSafeException : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
class IdentityMismatchException final : public RBFSafeException {
  public:
    using RBFSafeException::RBFSafeException;
};
class IncompatibleFormatException final : public RBFSafeException {
  public:
    using RBFSafeException::RBFSafeException;
};
class CorruptDataException final : public RBFSafeException {
  public:
    using RBFSafeException::RBFSafeException;
};
class CancelledException final : public RBFSafeException {
  public:
    using RBFSafeException::RBFSafeException;
};
class InternalException final : public RBFSafeException {
  public:
    using RBFSafeException::RBFSafeException;
};

[[noreturn]] void throw_error(const Error& error) {
    switch (error.code) {
    case StatusCode::InvalidArgument:
    case StatusCode::DimensionMismatch:
        throw py::value_error(error.describe());
    case StatusCode::IoError:
        PyErr_SetString(PyExc_OSError, error.describe().c_str());
        throw py::error_already_set();
    case StatusCode::ResourceLimit:
        PyErr_SetString(PyExc_MemoryError, error.describe().c_str());
        throw py::error_already_set();
    case StatusCode::IdentityMismatch:
        throw IdentityMismatchException(error.describe());
    case StatusCode::IncompatibleFormat:
        throw IncompatibleFormatException(error.describe());
    case StatusCode::CorruptData:
        throw CorruptDataException(error.describe());
    case StatusCode::Cancelled:
        throw CancelledException(error.describe());
    case StatusCode::InternalError:
        throw InternalException(error.describe());
    default:
        throw RBFSafeException(error.describe());
    }
}

template <typename T> T unwrap(Result<T> result) {
    if (!result)
        throw_error(result.error());
    return std::move(result).value();
}

void unwrap_void(Result<void> result) {
    if (!result)
        throw_error(result.error());
}

std::span<const double> view(const Configuration& values) {
    return std::span<const double>(values.data(), values.size());
}

} // namespace

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
