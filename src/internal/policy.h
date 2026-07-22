#pragma once

#include <rbfsafe/policy.h>

#include <string>

namespace rbfsafe::internal {

Result<void> validate_policy_feedback_record(const PolicyFeedbackRecord& record);
std::string policy_feedback_record_identity(const PolicyFeedbackRecord& record);

} // namespace rbfsafe::internal
