#pragma once

#include <rbfsafe/modules/envelope.h>

// Robot and scene models.

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rbfsafe {

enum class JointType : std::uint8_t { Revolute = 0, Prismatic = 1 };

struct Pose3d {
    std::array<double, 3> position{};
    // Quaternion components use the common x, y, z, w order.
    std::array<double, 4> orientation{0.0, 0.0, 0.0, 1.0};

    bool valid(double tolerance = 1e-9) const noexcept;
};

// A 6-by-N geometric Jacobian expressed in the robot workspace frame.
// Rows 0..2 map joint rates to end-effector linear velocity; rows 3..5
// map joint rates to angular velocity. Values use row-major storage.
struct GeometricJacobian {
    std::size_t columns = 0;
    std::vector<double> values;

    bool valid() const noexcept;
    double at(std::size_t row, std::size_t column) const;
};

struct DhJoint {
    double alpha = 0.0;
    double a = 0.0;
    double d = 0.0;
    double theta = 0.0;
    JointType type = JointType::Revolute;
};

class SerialRobotModel {
  public:
    SerialRobotModel() = default;
    SerialRobotModel(std::string name, std::vector<DhJoint> joints, std::vector<Interval> joint_limits,
                     std::vector<double> link_radii, std::optional<DhJoint> tool_frame = std::nullopt);

    static Result<SerialRobotModel> create(std::string name, std::vector<DhJoint> joints,
                                           std::vector<Interval> joint_limits, std::vector<double> link_radii,
                                           std::optional<DhJoint> tool_frame = std::nullopt);
    static Result<SerialRobotModel> from_json(const std::filesystem::path& path);

    Result<void> validate() const;
    const std::string& name() const noexcept { return name_; }
    std::size_t dimension() const noexcept { return joints_.size(); }
    std::size_t link_count() const noexcept { return link_radii_.size(); }
    const std::vector<DhJoint>& joints() const noexcept { return joints_; }
    const std::vector<Interval>& joint_limits() const noexcept { return joint_limits_; }
    const std::vector<double>& link_radii() const noexcept { return link_radii_; }
    const std::optional<DhJoint>& tool_frame() const noexcept { return tool_frame_; }
    CspaceAabb configuration_domain() const { return CspaceAabb(joint_limits_); }

    Result<std::vector<std::array<double, 3>>>
    forward_kinematics(std::span<const double> configuration) const;
    Result<Pose3d> end_effector_pose(std::span<const double> configuration) const;
    Result<GeometricJacobian> end_effector_geometric_jacobian(std::span<const double> configuration) const;
    std::string canonical_json() const;
    std::string digest() const;

  private:
    std::string name_;
    std::vector<DhJoint> joints_;
    std::vector<Interval> joint_limits_;
    std::vector<double> link_radii_;
    std::optional<DhJoint> tool_frame_;
};

struct SceneObstacle {
    std::string id;
    WorkspaceEnvelope bounds;

    SceneObstacle() = default;
    SceneObstacle(std::string obstacle_id, WorkspaceEnvelope envelope)
        : id(std::move(obstacle_id)), bounds(std::move(envelope)) {}
    SceneObstacle(std::string obstacle_id, WorkspaceAabb envelope)
        : id(std::move(obstacle_id)), bounds(envelope) {}
    SceneObstacle(std::string obstacle_id, WorkspaceObb envelope)
        : id(std::move(obstacle_id)), bounds(std::move(envelope)) {}
    SceneObstacle(std::string obstacle_id, WorkspaceKdop envelope)
        : id(std::move(obstacle_id)), bounds(std::move(envelope)) {}
    SceneObstacle(std::string obstacle_id, WorkspaceSupportHull envelope)
        : id(std::move(obstacle_id)), bounds(std::move(envelope)) {}
};

class SceneSnapshot {
  public:
    SceneSnapshot() = default;
    explicit SceneSnapshot(std::vector<SceneObstacle> obstacles, std::string version = "1");

    static Result<SceneSnapshot> create(std::vector<SceneObstacle> obstacles, std::string version = "1");
    static Result<SceneSnapshot> from_json(const std::filesystem::path& path);

    Result<void> validate() const;
    const std::vector<SceneObstacle>& obstacles() const noexcept { return obstacles_; }
    const std::string& version() const noexcept { return version_; }
    std::string canonical_json() const;
    std::string digest() const;

  private:
    std::vector<SceneObstacle> obstacles_;
    std::string version_ = "1";
};

} // namespace rbfsafe

// Kinematics and conservative geometry algorithms.

#include <memory>
#include <string>
#include <vector>

namespace rbfsafe {

enum class EndpointAabbSource : std::uint8_t {
    IfkAa = 0,
    CritSample = 1,
};

const char* endpoint_aabb_source_name(EndpointAabbSource source) noexcept;

struct EnvelopeOptions {
    double obstacle_padding = 0.0;
    WorkspaceEnvelopeType workspace_envelope_type = WorkspaceEnvelopeType::Aabb;
    std::size_t kdop_k = 26;
    EndpointAabbSource endpoint_aabb_source = EndpointAabbSource::IfkAa;
};

struct EndpointAabbResult {
    // Paired layout: [link 0 proximal, link 0 distal, link 1 proximal, ...].
    std::vector<WorkspaceAabb> endpoints;
    EndpointAabbSource source = EndpointAabbSource::IfkAa;
    bool certified = true;
    std::size_t evaluated_configurations = 0;
};

struct LinkEnvelope {
    std::vector<WorkspaceAabb> links;
};

struct WorkspaceLinkEnvelope {
    std::vector<WorkspaceEnvelope> links;
    EndpointAabbSource endpoint_aabb_source = EndpointAabbSource::IfkAa;
    bool endpoint_bounds_certified = true;
    std::size_t evaluated_configurations = 0;
};

// Computes paired endpoint AABBs with the source selected in options. IFK-AA
// is conservative. CritSample is a deterministic, non-certified diagnostic
// source derived from {lo, hi, k*pi/2} joint candidates.
Result<EndpointAabbResult> compute_endpoint_aabbs(const SerialRobotModel& robot, const CspaceAabb& domain,
                                                  const EnvelopeOptions& options = {});

// Generalized link-envelope entry point. Callers must inspect
// endpoint_bounds_certified before treating the result as conservative.
Result<WorkspaceLinkEnvelope> compute_workspace_link_envelope(const SerialRobotModel& robot,
                                                              const CspaceAabb& domain,
                                                              const EnvelopeOptions& options = {});

Result<LinkEnvelope> compute_ifk_aa_link_envelope(const SerialRobotModel& robot, const CspaceAabb& domain,
                                                  const EnvelopeOptions& options = {});

// Computes a conservative per-link envelope using AABB, OBB, a standard
// 6/14/18/26-DOP, or a support hull. The legacy LinkEnvelope API remains AABB
// for persistence compatibility.
Result<WorkspaceLinkEnvelope> compute_ifk_aa_workspace_link_envelope(const SerialRobotModel& robot,
                                                                     const CspaceAabb& domain,
                                                                     const EnvelopeOptions& options = {});

Result<bool> configuration_is_collision_free(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                             std::span<const double> configuration,
                                             double obstacle_padding = 0.0);

enum class ValidationDisposition : std::uint8_t {
    CertifiedFree = 0,
    Undetermined = 1,
};

struct RegionValidation {
    ValidationDisposition disposition = ValidationDisposition::Undetermined;
    double clearance_lower_bound = 0.0;
    // CertifiedFree results must provide one valid conservative workspace
    // AABB per robot link. Schema-2 Atlases persist this dependency for safe
    // scene-delta invalidation.
    LinkEnvelope envelope;
};

class RegionValidator {
  public:
    virtual ~RegionValidator() = default;
    virtual Result<RegionValidation> validate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                              const CspaceAabb& domain) const = 0;
    virtual std::string algorithm_name() const = 0;
    virtual std::string algorithm_version() const = 0;
};

class IfkAaLinkAabbValidator final : public RegionValidator {
  public:
    explicit IfkAaLinkAabbValidator(EnvelopeOptions options = {}) : options_(options) {}

    Result<RegionValidation> validate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                      const CspaceAabb& domain) const override;
    std::string algorithm_name() const override { return "ifk-aa-link-iaabb"; }
    std::string algorithm_version() const override { return "1"; }
    const EnvelopeOptions& options() const noexcept { return options_; }

  private:
    EnvelopeOptions options_;
};

class IfkAaWorkspaceEnvelopeValidator final : public RegionValidator {
  public:
    explicit IfkAaWorkspaceEnvelopeValidator(EnvelopeOptions options = {}) : options_(options) {}

    Result<RegionValidation> validate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                      const CspaceAabb& domain) const override;
    std::string algorithm_name() const override;
    std::string algorithm_version() const override { return "1"; }
    const EnvelopeOptions& options() const noexcept { return options_; }

  private:
    EnvelopeOptions options_;
};

} // namespace rbfsafe

// Geometric safety certificates and validators.

#include <cstdint>
#include <string>

namespace rbfsafe {

enum class EvidenceLevel : std::uint8_t {
    Unknown = 0,
    PointChecked = 1,
    CertifiedRegion = 2,
    CertifiedConnectivity = 3,
    RuntimeExecutable = 4,
};

struct ValidationPolicy {
    std::string algorithm;
    std::string algorithm_version;
    double obstacle_padding = 0.0;
};

struct Certificate {
    std::string id;
    EvidenceLevel level = EvidenceLevel::Unknown;
    std::string robot_digest;
    std::string scene_digest;
    ValidationPolicy policy;
    double clearance_lower_bound = 0.0;
    std::string subject_digest;
    // Scene-transition certificates retain the exact parent proof and delta
    // that justified reusing it. Both fields are either empty or SHA-256
    // digests; direct validations leave them empty.
    std::string parent_certificate_id;
    std::string transition_digest;
};

Result<Certificate> make_region_certificate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                            const CspaceAabb& domain, const RegionValidator& validator,
                                            const RegionValidation& validation, double obstacle_padding);

// Legacy unbound helper retained for source compatibility. AtlasBuilder and
// dynamic update code use the domain-bound overload above.
Result<Certificate> make_region_certificate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                            const RegionValidator& validator,
                                            const RegionValidation& validation, double obstacle_padding);

std::string evidence_level_name(EvidenceLevel level);

} // namespace rbfsafe
