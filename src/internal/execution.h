#pragma once

#include <rbfsafe/execution.h>

#include <string>

namespace rbfsafe::internal {

std::string execution_endpoint_key_identity(const ExecutionEndpointKey& key);
std::string execution_command_digest(const ExecutionCommand& command);
std::string execution_command_sequence_identity(const ExecutionCommandSequence& sequence);
std::string execution_session_request_identity(const ExecutionSessionRequest& request);
std::string execution_session_approval_message(const ExecutionSessionApproval& approval);
std::string execution_session_approval_identity(const ExecutionSessionApproval& approval);
std::string execution_session_approval_set_identity(const ExecutionSessionApprovalSet& approval_set);
std::string
execution_controller_acknowledgement_message(const ExecutionControllerAcknowledgement& acknowledgement);
std::string
execution_controller_acknowledgement_identity(const ExecutionControllerAcknowledgement& acknowledgement);
std::string execution_runtime_observation_identity(const ExecutionRuntimeObservation& observation);
std::string execution_monitor_acknowledgement_message(const ExecutionMonitorAcknowledgement& acknowledgement);
std::string
execution_monitor_acknowledgement_identity(const ExecutionMonitorAcknowledgement& acknowledgement);
std::string bounded_execution_session_identity(const BoundedExecutionSession& session);
std::string execution_command_authorization_identity(const ExecutionCommandAuthorization& authorization);

} // namespace rbfsafe::internal
