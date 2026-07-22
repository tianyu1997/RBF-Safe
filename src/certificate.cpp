#include <rbfsafe/certificate.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <cmath>

namespace {

rbfsafe::internal::Json certificate_identity_json(const rbfsafe::Certificate& certificate) {
    rbfsafe::internal::Json::Object identity{
        {"algorithm", certificate.policy.algorithm},
        {"algorithm_version", certificate.policy.algorithm_version},
        {"clearance_lower_bound", certificate.clearance_lower_bound},
        {"level", rbfsafe::evidence_level_name(certificate.level)},
        {"obstacle_padding", certificate.policy.obstacle_padding},
        {"robot_digest", certificate.robot_digest},
        {"scene_digest", certificate.scene_digest},
    };
    if (!certificate.subject_digest.empty())
        identity.emplace("subject_digest", certificate.subject_digest);
    if (!certificate.parent_certificate_id.empty()) {
        identity.emplace("parent_certificate_id", certificate.parent_certificate_id);
        identity.emplace("transition_digest", certificate.transition_digest);
    }
    return rbfsafe::internal::Json(std::move(identity));
}

} // namespace

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

Result<Certificate> internal::make_subject_certificate(EvidenceLevel level, std::string robot_digest,
                                                       std::string scene_digest, ValidationPolicy policy,
                                                       std::string subject_digest,
                                                       double clearance_lower_bound) {
    if ((level != EvidenceLevel::CertifiedRegion && level != EvidenceLevel::CertifiedConnectivity) ||
        !valid_sha256(robot_digest) || !valid_sha256(scene_digest) || !valid_sha256(subject_digest) ||
        policy.algorithm.empty() || policy.algorithm_version.empty() ||
        !std::isfinite(policy.obstacle_padding) || policy.obstacle_padding < 0.0 ||
        !std::isfinite(clearance_lower_bound) || clearance_lower_bound < 0.0) {
        return Result<Certificate>::failure(StatusCode::InvalidArgument,
                                            "subject certificate parameters are invalid");
    }
    Certificate certificate;
    certificate.level = level;
    certificate.robot_digest = std::move(robot_digest);
    certificate.scene_digest = std::move(scene_digest);
    certificate.policy = std::move(policy);
    certificate.clearance_lower_bound = clearance_lower_bound;
    certificate.subject_digest = std::move(subject_digest);
    certificate.id = certificate_identity(certificate);
    return certificate;
}

bool internal::valid_sha256(const std::string& digest) {
    return digest.size() == 64 && std::all_of(digest.begin(), digest.end(), [](char value) {
               return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
           });
}

std::string internal::certificate_identity(const Certificate& certificate) {
    return internal::sha256(certificate_identity_json(certificate).dump(false));
}

std::string internal::cspace_aabb_subject_digest(const CspaceAabb& domain) {
    internal::Json::Array axes;
    axes.reserve(domain.dimension());
    for (const auto& axis : domain.axes())
        axes.emplace_back(internal::Json::Array{axis.lower, axis.upper});
    return internal::sha256(internal::Json(internal::Json::Object{
                                               {"bounds", std::move(axes)},
                                               {"type", "cspace-aabb"},
                                           })
                                .dump(false));
}

Result<Certificate> internal::make_transition_certificate(const Certificate& parent, std::string scene_digest,
                                                          std::string subject_digest,
                                                          std::string transition_digest,
                                                          double clearance_lower_bound) {
    if (parent.level != EvidenceLevel::CertifiedRegion || !valid_sha256(parent.id) ||
        !valid_sha256(parent.robot_digest) || !valid_sha256(scene_digest) || !valid_sha256(subject_digest) ||
        !valid_sha256(transition_digest) || parent.subject_digest != subject_digest ||
        parent.policy.algorithm.empty() || parent.policy.algorithm_version.empty() ||
        !std::isfinite(parent.policy.obstacle_padding) || parent.policy.obstacle_padding < 0.0 ||
        !std::isfinite(clearance_lower_bound) || clearance_lower_bound < 0.0 ||
        certificate_identity(parent) != parent.id) {
        return Result<Certificate>::failure(StatusCode::InvalidArgument,
                                            "scene-transition certificate parameters are invalid");
    }
    Certificate certificate;
    certificate.level = EvidenceLevel::CertifiedRegion;
    certificate.robot_digest = parent.robot_digest;
    certificate.scene_digest = std::move(scene_digest);
    certificate.policy = parent.policy;
    certificate.clearance_lower_bound = clearance_lower_bound;
    certificate.subject_digest = std::move(subject_digest);
    certificate.parent_certificate_id = parent.id;
    certificate.transition_digest = std::move(transition_digest);
    certificate.id = certificate_identity(certificate);
    return certificate;
}

Result<Certificate> make_region_certificate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                            const CspaceAabb& domain, const RegionValidator& validator,
                                            const RegionValidation& validation, double obstacle_padding) {
    if (!domain.valid() || domain.dimension() != robot.dimension()) {
        return Result<Certificate>::failure(StatusCode::DimensionMismatch,
                                            "certificate domain does not match robot");
    }
    if (validation.disposition != ValidationDisposition::CertifiedFree) {
        return Result<Certificate>::failure(StatusCode::InvalidArgument,
                                            "an undetermined validation cannot create a region certificate");
    }
    if (!std::isfinite(validation.clearance_lower_bound) || validation.clearance_lower_bound < 0.0 ||
        !std::isfinite(obstacle_padding) || obstacle_padding < 0.0) {
        return Result<Certificate>::failure(StatusCode::InvalidArgument,
                                            "certificate parameters are invalid");
    }
    if (validation.envelope.links.size() != robot.link_count() ||
        !std::all_of(validation.envelope.links.begin(), validation.envelope.links.end(),
                     [](const auto& link) { return link.valid(); })) {
        return Result<Certificate>::failure(StatusCode::InternalError,
                                            "certified validation returned an incomplete link envelope");
    }
    return internal::make_subject_certificate(
        EvidenceLevel::CertifiedRegion, robot.digest(), scene.digest(),
        {validator.algorithm_name(), validator.algorithm_version(), obstacle_padding},
        internal::cspace_aabb_subject_digest(domain), validation.clearance_lower_bound);
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
    certificate.id = internal::certificate_identity(certificate);
    return certificate;
}

} // namespace rbfsafe
