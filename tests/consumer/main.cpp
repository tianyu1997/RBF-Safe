#include <rbfsafe/rbfsafe.h>

int main() {
    const rbfsafe::Interval interval{-1.0, 1.0};
    const rbfsafe::TrajectoryAuditOptions options;
    const rbfsafe::TrajectoryAuditReport report;
    const rbfsafe::HipacOptions hipac_options;
    const rbfsafe::SafeIkOptions safe_ik_options;
    const rbfsafe::AtlasUpdateOptions update_options;
    const rbfsafe::AtlasUpdater updater;
    const rbfsafe::ObbAtlasBuildOptions obb_atlas_options;
    const rbfsafe::CertifiedSamplerOptions sampler_options;
    const rbfsafe::CertifiedRoadmapOptions roadmap_options;
    const rbfsafe::ConstraintProjectionOptions projection_options;
    const rbfsafe::ShieldOptions shield_options;
    const rbfsafe::PolicyGateOptions policy_options;
    const rbfsafe::CalibratedPolicyGateOptions calibrated_policy_options;
    const rbfsafe::PolicyCalibrationLifecycleLoadOptions calibration_lifecycle_options;
    const rbfsafe::SafetyMemoryLoadOptions memory_load_options;
    const rbfsafe::SafetyMemoryStoreOpenOptions memory_store_options;
    const rbfsafe::SafetyMemory memory;
    const rbfsafe::FleetScheduleOptions fleet_options;
    const rbfsafe::FleetScheduleArchiveLoadOptions fleet_archive_options;
    const rbfsafe::ArtifactVerificationOptions artifact_verification_options;
    const rbfsafe::RemoteArtifactOptions remote_options;
    const rbfsafe::ArtifactTransferJournalLoadOptions transfer_journal_options;
    const rbfsafe::ServiceTrustBundleLoadOptions trust_bundle_options;
    const rbfsafe::ServiceTrustHistoryLoadOptions trust_history_options;
    const rbfsafe::BoundedExecutionSessionLoadOptions execution_load_options;
    const rbfsafe::ExecutionLedgerLoadOptions execution_ledger_load_options;
    const rbfsafe::TransparencyLogLoadOptions transparency_load_options;
    const auto fleet_archive = rbfsafe::FleetScheduleArchive::create("consumer-fleet");
    (void)updater;
    return RBFSAFE_VERSION_MAJOR == 3 && RBFSAFE_VERSION_MINOR == 13 && RBFSAFE_VERSION_PATCH == 0 &&
                   interval.contains(0.0) && options.maximum_region_tests > 0 &&
                   hipac_options.maximum_validations > 0 && safe_ik_options.maximum_iterations > 0 &&
                   update_options.maximum_validations > 0 && obb_atlas_options.maximum_validations > 0 &&
                   sampler_options.maximum_attempts > 0 && roadmap_options.maximum_nodes > 0 &&
                   projection_options.maximum_iterations > 0 && shield_options.maximum_input_waypoints > 0 &&
                   policy_options.maximum_proposals > 0 && memory_load_options.maximum_artifacts > 0 &&
                   calibrated_policy_options.minimum_total_samples > 0 &&
                   calibration_lifecycle_options.maximum_reports > 0 &&
                   memory_store_options.maximum_revisions > 0 && memory.identity().size() == 64 &&
                   fleet_options.maximum_pair_evaluations > 0 && fleet_archive_options.maximum_versions > 0 &&
                   fleet_archive && fleet_archive.value().valid() &&
                   artifact_verification_options.maximum_payload_bytes > 0 &&
                   remote_options.maximum_payload_bytes > 0 && transfer_journal_options.maximum_records > 0 &&
                   trust_bundle_options.maximum_keys > 0 && trust_history_options.maximum_bundles > 0 &&
                   execution_load_options.maximum_commands > 0 &&
                   execution_ledger_load_options.maximum_records > 0 &&
                   transparency_load_options.maximum_records > 0 &&
                   rbfsafe::execution_ledger_status_name(rbfsafe::ExecutionLedgerStatus::Open) == "open" &&
                   rbfsafe::transparency_leaf_kind_name(rbfsafe::TransparencyLeafKind::RuntimeObservation) ==
                       "runtime_observation" &&
                   rbfsafe::artifact_transfer_operation_name(rbfsafe::ArtifactTransferOperation::Fetch) ==
                       "fetch" &&
                   rbfsafe::artifact_authentication_algorithm_name(
                       rbfsafe::ArtifactAuthenticationAlgorithm::Ed25519) == "ed25519" &&
                   rbfsafe::service_key_state_name(rbfsafe::ServiceKeyState::Active) == "active" &&
                   rbfsafe::service_trust_rotation_event_type_name(
                       rbfsafe::ServiceTrustRotationEventType::SuccessorAuthorized) ==
                       "successor_authorized" &&
                   report.status == rbfsafe::TrajectoryAuditStatus::Invalid
               ? 0
               : 1;
}
