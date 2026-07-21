#pragma once

#include <rbfsafe/certificate.h>

#include <string>

namespace rbfsafe::internal {

Result<Certificate> make_subject_certificate(EvidenceLevel level, std::string robot_digest,
                                             std::string scene_digest, ValidationPolicy policy,
                                             std::string subject_digest, double clearance_lower_bound);

} // namespace rbfsafe::internal
