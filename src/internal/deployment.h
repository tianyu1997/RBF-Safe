#pragma once

#include <rbfsafe/deployment.h>

#include <string>

namespace rbfsafe::internal {

std::string deployment_profile_identity(const DeploymentProfile& profile);
std::string deployment_profile_approval_message(const DeploymentProfileApproval& approval);
std::string deployment_profile_approval_identity(const DeploymentProfileApproval& approval);
std::string deployment_profile_approval_set_identity(const DeploymentProfileApprovalSet& approval_set);
std::string deployment_runtime_snapshot_identity(const DeploymentRuntimeSnapshot& snapshot);
std::string deployment_profile_assessment_identity(const DeploymentProfileAssessment& assessment);

} // namespace rbfsafe::internal
