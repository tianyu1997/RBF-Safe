#pragma once

#include <rbfsafe/certificate.h>

#include <string>

namespace rbfsafe::internal {

Result<Certificate> make_subject_certificate(EvidenceLevel level, std::string robot_digest,
                                             std::string scene_digest, ValidationPolicy policy,
                                             std::string subject_digest, double clearance_lower_bound);

Result<Certificate> make_transition_certificate(const Certificate& parent, std::string scene_digest,
                                                std::string subject_digest, std::string transition_digest,
                                                double clearance_lower_bound);

std::string certificate_identity(const Certificate& certificate);
bool valid_sha256(const std::string& digest);
std::string cspace_aabb_subject_digest(const CspaceAabb& domain);

} // namespace rbfsafe::internal
