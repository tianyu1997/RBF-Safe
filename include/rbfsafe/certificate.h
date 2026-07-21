#pragma once

#include <rbfsafe/geometry.h>
#include <rbfsafe/result.h>

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
};

Result<Certificate> make_region_certificate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                            const RegionValidator& validator,
                                            const RegionValidation& validation, double obstacle_padding);

std::string evidence_level_name(EvidenceLevel level);

} // namespace rbfsafe
