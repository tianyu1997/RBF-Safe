#pragma once

#include <rbfsafe/atlas.h>
#include <rbfsafe/model.h>
#include <rbfsafe/result.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace rbfsafe {

enum class SafeIkStatus : std::uint8_t {
    SafeConnected = 0,
    SafeUnconnected = 1,
    SeedNotCertified = 2,
    NoSolution = 3,
};

struct SafeIkOptions {
    double position_tolerance = 1e-4;
    double orientation_tolerance = 1e-3;
    double orientation_weight = 0.25;
    double damping = 1e-3;
    double finite_difference_step = 1e-6;
    double maximum_step_norm = 0.25;
    double minimum_step_norm = 1e-12;
    std::size_t maximum_iterations = 128;
    std::size_t maximum_region_attempts = 256;
    std::size_t maximum_line_search_steps = 8;
    bool require_connectivity = true;
    CancellationToken cancellation;
};

struct SafeIkStats {
    std::size_t region_attempts = 0;
    std::size_t iterations = 0;
    std::size_t pose_evaluations = 0;
    std::size_t disconnected_solutions = 0;
};

struct SafeIkReport {
    SafeIkStatus status = SafeIkStatus::NoSolution;
    Configuration solution;
    RegionId region_id = 0;
    std::optional<Certificate> region_certificate;
    std::optional<AtlasRoute> connectivity_route;
    EvidenceLevel pose_evidence = EvidenceLevel::Unknown;
    double position_error = 0.0;
    double orientation_error = 0.0;
    SafeIkStats stats;
};

class SafeIkSolver {
  public:
    Result<SafeIkReport> solve(const SerialRobotModel& robot, const SceneSnapshot& scene,
                               const SafeAtlas& atlas, const Pose3d& target, std::span<const double> current,
                               const SafeIkOptions& options = {}) const;
};

} // namespace rbfsafe
