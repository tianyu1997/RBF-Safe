#pragma once

#include <rbfsafe/modules/assurance.h>

#include <string>

namespace rbfsafe::internal {

std::string hardware_key_attestation_message(const HardwareKeyAttestationStatement& statement);
std::string hardware_key_attestation_identity(const HardwareKeyAttestationStatement& statement);
std::string hardware_key_provenance_policy_identity(const HardwareKeyProvenancePolicy& policy);
std::string hardware_key_provenance_report_identity(const HardwareKeyProvenanceReport& report);
std::string external_time_assertion_message(const ExternalTimeAssertion& assertion);
std::string external_time_assertion_identity(const ExternalTimeAssertion& assertion);
std::string external_time_freshness_policy_identity(const ExternalTimeFreshnessPolicy& policy);
std::string external_time_freshness_report_identity(const ExternalTimeFreshnessReport& report);
std::string verifiable_provenance_bundle_identity(const VerifiableProvenanceBundle& bundle);
std::string verifiable_provenance_audit_report_identity(const VerifiableProvenanceAuditReport& report);

} // namespace rbfsafe::internal
