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
        .def("forward_kinematics", [](const SerialRobotModel& robot, const Configuration& q) {
            return unwrap(robot.forward_kinematics(view(q)));
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
        .def_readonly("clearance_lower_bound", &Certificate::clearance_lower_bound);

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

    py::class_<SaveOptions>(module, "SaveOptions")
        .def(py::init<>())
        .def_readwrite("overwrite", &SaveOptions::overwrite);

    py::class_<SafeAtlas>(module, "SafeAtlas")
        .def_static("load", [](const std::filesystem::path& path) { return unwrap(SafeAtlas::load(path)); })
        .def_property_readonly("dimension", &SafeAtlas::dimension)
        .def_property_readonly("robot_digest", &SafeAtlas::robot_digest)
        .def_property_readonly("scene_digest", &SafeAtlas::scene_digest)
        .def_property_readonly("regions", &SafeAtlas::regions)
        .def_property_readonly("certificates", &SafeAtlas::certificates)
        .def_property_readonly("lect", &SafeAtlas::lect)
        .def("regions_at",
             [](const SafeAtlas& atlas, const Configuration& q) { return unwrap(atlas.regions_at(q)); })
        .def("contains", [](const SafeAtlas& atlas, const Configuration& q) { return atlas.contains(q); })
        .def("nearest_region", [](const SafeAtlas& atlas,
                                  const Configuration& q) { return unwrap(atlas.nearest_region(view(q))); })
        .def("connected",
             [](const SafeAtlas& atlas, const Configuration& first, const Configuration& second) {
                 return unwrap(atlas.connected(view(first), view(second)));
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
}
