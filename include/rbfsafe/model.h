#pragma once

#include <rbfsafe/result.h>
#include <rbfsafe/types.h>

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class JointType : std::uint8_t { Revolute = 0, Prismatic = 1 };

struct Pose3d {
    std::array<double, 3> position{};
    // Quaternion components use the common x, y, z, w order.
    std::array<double, 4> orientation{0.0, 0.0, 0.0, 1.0};

    bool valid(double tolerance = 1e-9) const noexcept;
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
    WorkspaceAabb bounds;
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
