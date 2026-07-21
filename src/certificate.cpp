#include <rbfsafe/certificate.h>

#include "internal/json.h"
#include "internal/sha256.h"

#include <cmath>

namespace rbfsafe {

std::string evidence_level_name(EvidenceLevel level) {
    switch (level) {
    case EvidenceLevel::Unknown:
        return "unknown";
    case EvidenceLevel::PointChecked:
        return "point_checked";
    case EvidenceLevel::CertifiedRegion:
        return "certified_region";
    case EvidenceLevel::CertifiedConnectivity:
        return "certified_connectivity";
    case EvidenceLevel::RuntimeExecutable:
        return "runtime_executable";
    }
    return "unknown";
}

Result<Certificate> make_region_certificate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                            const RegionValidator& validator,
                                            const RegionValidation& validation, double obstacle_padding) {
    if (validation.disposition != ValidationDisposition::CertifiedFree) {
        return Result<Certificate>::failure(StatusCode::InvalidArgument,
                                            "an undetermined validation cannot create a region certificate");
    }
    if (!std::isfinite(validation.clearance_lower_bound) || validation.clearance_lower_bound < 0.0 ||
        !std::isfinite(obstacle_padding) || obstacle_padding < 0.0) {
        return Result<Certificate>::failure(StatusCode::InvalidArgument,
                                            "certificate parameters are invalid");
    }
    Certificate certificate;
    certificate.level = EvidenceLevel::CertifiedRegion;
    certificate.robot_digest = robot.digest();
    certificate.scene_digest = scene.digest();
    certificate.policy = {validator.algorithm_name(), validator.algorithm_version(), obstacle_padding};
    certificate.clearance_lower_bound = validation.clearance_lower_bound;
    internal::Json identity(internal::Json::Object{
        {"algorithm", certificate.policy.algorithm},
        {"algorithm_version", certificate.policy.algorithm_version},
        {"clearance_lower_bound", certificate.clearance_lower_bound},
        {"level", evidence_level_name(certificate.level)},
        {"obstacle_padding", obstacle_padding},
        {"robot_digest", certificate.robot_digest},
        {"scene_digest", certificate.scene_digest},
    });
    certificate.id = internal::sha256(identity.dump(false));
    return certificate;
}

} // namespace rbfsafe
