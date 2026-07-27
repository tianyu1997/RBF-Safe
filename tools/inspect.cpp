#include <rbfsafe/rbfsafe.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

bool is_service_trust_history(const std::filesystem::path& directory) {
    std::error_code error;
    const auto manifest = directory / "manifest.json";
    const auto bytes = std::filesystem::file_size(manifest, error);
    if (error || bytes > 65'536)
        return false;
    std::ifstream input(manifest, std::ios::binary);
    if (!input)
        return false;
    std::string text(static_cast<std::size_t>(bytes), '\0');
    if (!text.empty())
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
    return input && text.find("\"rbfsafe-service-trust-history\"") != std::string::npos;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: rbfsafe-inspect <database-archive-or-profile> [query values or profile]\n"
                  << "       rbfsafe-inspect <trust-history> <expected-root> <expected-head>\n";
        return 2;
    }
    if (is_service_trust_history(std::filesystem::path(argv[1]))) {
        if (argc != 4) {
            std::cerr << "service trust-history inspection requires expected root and head bundle IDs\n";
            return 2;
        }
        auto history = rbfsafe::ServiceTrustHistory::open(std::filesystem::path(argv[1]), argv[2], argv[3]);
        if (!history) {
            std::cerr << history.error().describe() << '\n';
            return 1;
        }
        std::cout << "RBF-Safe service trust history\n"
                  << "schema: 1\n"
                  << "records: " << history.value().records().size() << '\n'
                  << "root: " << history.value().root_bundle_id() << '\n'
                  << "head: " << history.value().current_bundle_id() << '\n';
        for (const auto& record : history.value().records()) {
            std::cout << "rotation: " << record.id << '\n'
                      << "  sequence: " << record.sequence << '\n'
                      << "  parent: " << (record.parent_id.empty() ? "-" : record.parent_id) << '\n'
                      << "  type: " << rbfsafe::service_trust_rotation_event_type_name(record.type) << '\n'
                      << "  bundle: " << record.bundle_id << '\n';
            if (record.authorization) {
                std::cout << "  authorization: " << record.authorization->id << '\n'
                          << "  signer service: " << record.authorization->signer_service_id << '\n'
                          << "  signer key: " << record.authorization->signer_key_id << '\n';
            }
        }
        std::cout << "caller pinned: true\n"
                  << "expected head verified: true\n"
                  << "runtime executable: false\n";
        return 0;
    }
    if (argc >= 3) {
        auto lifecycle_profile = rbfsafe::PolicyCalibrationProfile::load(std::filesystem::path(argv[2]));
        if (lifecycle_profile) {
            auto lifecycle = rbfsafe::PolicyCalibrationLifecycle::load(std::filesystem::path(argv[1]),
                                                                       lifecycle_profile.value());
            if (lifecycle) {
                if (argc != 3) {
                    std::cerr << "policy calibration lifecycle accepts exactly one profile path\n";
                    return 2;
                }
                const auto summary = lifecycle.value().summary();
                std::cout << "RBF-Safe policy calibration lifecycle\n"
                          << "schema: 1\n"
                          << "profile: " << lifecycle.value().profile_id() << '\n'
                          << "state: "
                          << rbfsafe::policy_calibration_lifecycle_state_name(lifecycle.value().state())
                          << '\n'
                          << "generation: " << lifecycle.value().generation() << '\n'
                          << "head: " << lifecycle.value().current_event_id() << '\n'
                          << "latest report: "
                          << (lifecycle.value().latest_report_id().empty()
                                  ? "-"
                                  : lifecycle.value().latest_report_id())
                          << '\n'
                          << "assessments: " << summary.assessments << '\n'
                          << "stable: " << summary.stable << '\n'
                          << "insufficient data: " << summary.insufficient_data << '\n'
                          << "drift detected: " << summary.drift_detected << '\n'
                          << "transitions: " << summary.transitions << '\n';
                if (!lifecycle.value().reports().empty()) {
                    const auto report = lifecycle.value().latest_report();
                    std::cout << "window: " << report.value().window_id << '\n'
                              << "window sequence: " << report.value().window_sequence << '\n'
                              << "status: "
                              << rbfsafe::policy_calibration_drift_status_name(report.value().status) << '\n'
                              << "samples: " << report.value().sample_count << '\n'
                              << "total variation distance: " << report.value().total_variation_distance
                              << '\n'
                              << "expected calibration error: " << report.value().expected_calibration_error
                              << '\n'
                              << "overall success rate drop: " << report.value().overall_success_rate_drop
                              << '\n'
                              << "maximum bin success rate drop: "
                              << report.value().maximum_bin_success_rate_drop << '\n'
                              << "reasons:";
                    if (report.value().reasons.empty()) {
                        std::cout << " -";
                    } else {
                        for (const auto reason : report.value().reasons)
                            std::cout << ' ' << rbfsafe::policy_calibration_drift_reason_name(reason);
                    }
                    std::cout << '\n';
                }
                std::cout << "deployment ready: " << (lifecycle.value().deployment_ready() ? "true" : "false")
                          << '\n'
                          << "runtime executable: false\n";
                return 0;
            }
        }
    }
    auto calibration = rbfsafe::PolicyCalibrationProfile::load(std::filesystem::path(argv[1]));
    if (calibration) {
        if (argc > 3) {
            std::cerr << "policy calibration accepts at most one raw confidence query\n";
            return 2;
        }
        std::cout << "RBF-Safe policy calibration profile\n"
                  << "schema: 1\n"
                  << "profile: " << calibration.value().id() << '\n'
                  << "policy: " << calibration.value().policy_id() << '\n'
                  << "model: " << calibration.value().policy_model_digest() << '\n'
                  << "scope: " << calibration.value().scope_id() << '\n'
                  << "task: " << calibration.value().task_id() << '\n'
                  << "dataset: " << calibration.value().dataset_digest() << '\n'
                  << "method: " << calibration.value().method() << '@' << calibration.value().method_version()
                  << '\n'
                  << "samples: " << calibration.value().sample_count() << '\n'
                  << "bins: " << calibration.value().bins().size() << '\n'
                  << "expected calibration error: " << calibration.value().expected_calibration_error()
                  << '\n'
                  << "maximum calibration error: " << calibration.value().maximum_calibration_error() << '\n'
                  << "runtime executable: false\n";
        if (argc == 3) {
            try {
                std::size_t consumed = 0;
                const double confidence = std::stod(argv[2], &consumed);
                if (consumed != std::string(argv[2]).size() || !std::isfinite(confidence))
                    throw std::invalid_argument("invalid confidence");
                auto lookup = calibration.value().lookup(confidence);
                if (!lookup) {
                    std::cerr << lookup.error().describe() << '\n';
                    return 1;
                }
                std::cout << "lookup bin: " << lookup.value().bin_index << '\n'
                          << "raw confidence: " << lookup.value().raw_confidence << '\n'
                          << "calibrated confidence: " << lookup.value().calibrated_confidence << '\n'
                          << "conservative confidence: " << lookup.value().conservative_confidence << '\n'
                          << "bin samples: " << lookup.value().samples << '\n';
            } catch (const std::exception&) {
                std::cerr << "raw confidence must be a finite number in [0, 1]\n";
                return 2;
            }
        }
        return 0;
    }
    auto attestation = rbfsafe::load_artifact_attestation(std::filesystem::path(argv[1]));
    if (attestation) {
        if (argc > 2) {
            std::cerr << "configuration queries do not apply to artifact attestations\n";
            return 2;
        }
        std::cout << "RBF-Safe artifact attestation\n"
                  << "schema: 1\n"
                  << "attestation: " << attestation.value().id << '\n'
                  << "service: " << attestation.value().service_id << '\n'
                  << "key: " << attestation.value().key_id << '\n'
                  << "algorithm: "
                  << rbfsafe::artifact_authentication_algorithm_name(attestation.value().algorithm) << '\n'
                  << "artifact: " << attestation.value().artifact_id << '\n'
                  << "generation: " << attestation.value().artifact_generation << '\n'
                  << "payload: " << attestation.value().payload_digest << '\n'
                  << "bytes: " << attestation.value().payload_bytes << '\n'
                  << "verified: false\n";
        return 0;
    }
    auto trust_bundle = rbfsafe::ServiceTrustBundle::load(std::filesystem::path(argv[1]));
    if (trust_bundle) {
        if (argc > 2) {
            std::cerr << "configuration queries do not apply to service trust bundles\n";
            return 2;
        }
        std::cout << "RBF-Safe service trust bundle\n"
                  << "schema: " << trust_bundle.value().storage_schema() << '\n'
                  << "bundle: " << trust_bundle.value().id() << '\n'
                  << "sequence: " << trust_bundle.value().sequence() << '\n'
                  << "parent: "
                  << (trust_bundle.value().parent_id().empty() ? "-" : trust_bundle.value().parent_id())
                  << '\n'
                  << "keys: " << trust_bundle.value().keys().size() << '\n';
        for (const auto& key : trust_bundle.value().keys()) {
            std::cout << "key: " << key.id << '\n'
                      << "  service: " << key.service_id << '\n'
                      << "  algorithm: " << rbfsafe::artifact_authentication_algorithm_name(key.algorithm)
                      << '\n'
                      << "  state: " << rbfsafe::service_key_state_name(key.state) << '\n'
                      << "  sequence window: " << key.valid_from_sequence << "..";
            if (key.valid_through_sequence == 0)
                std::cout << "unbounded\n";
            else
                std::cout << key.valid_through_sequence << '\n';
            std::cout << "  operations:" << (key.allow_fetch ? " fetch" : "")
                      << (key.allow_publish ? " publish" : "") << (key.allow_rotate ? " rotate" : "") << '\n';
        }
        std::cout << "caller pinned: false\n"
                  << "runtime executable: false\n";
        return 0;
    }
    auto atlas = rbfsafe::SafeAtlas::load(std::filesystem::path(argv[1]));
    bool loaded_from_store = false;
    std::size_t stored_versions = 0;
    std::string store_head;
    if (!atlas) {
        auto store = rbfsafe::AtlasVersionStore::open(std::filesystem::path(argv[1]));
        if (store) {
            stored_versions = store.value().versions().size();
            store_head = store.value().current_version_id();
            atlas = store.value().load_current();
            loaded_from_store = static_cast<bool>(atlas);
        }
    }
    if (!atlas) {
        auto database = rbfsafe::RegionDatabase::load(std::filesystem::path(argv[1]));
        if (database) {
            std::cout << "RBF-Safe region database\n"
                      << "schema: 1\n"
                      << "dimension: " << database.value().dimension() << '\n'
                      << "records: " << database.value().records().size() << '\n'
                      << "certificates: " << database.value().certificates().size() << '\n'
                      << "robot: " << database.value().robot_digest() << '\n'
                      << "scene: " << database.value().scene_digest() << '\n'
                      << "scene version: " << database.value().scene_version() << '\n';
            if (argc > 2) {
                if (static_cast<std::size_t>(argc - 2) != database.value().dimension()) {
                    std::cerr << "query dimension does not match database dimension\n";
                    return 2;
                }
                rbfsafe::Configuration query;
                try {
                    for (int index = 2; index < argc; ++index) {
                        std::size_t consumed = 0;
                        const double value = std::stod(argv[index], &consumed);
                        if (consumed != std::string(argv[index]).size() || !std::isfinite(value))
                            throw std::invalid_argument("invalid coordinate");
                        query.push_back(value);
                    }
                } catch (const std::exception&) {
                    std::cerr << "query coordinates must be finite decimal numbers\n";
                    return 2;
                }
                auto records = database.value().regions_at(query);
                auto nearest = database.value().nearest_region(query);
                if (!records || !nearest) {
                    std::cerr << (!records ? records.error().describe() : nearest.error().describe()) << '\n';
                    return 1;
                }
                std::cout << "query contains: " << (!records.value().empty() ? "true" : "false")
                          << "\nquery regions:";
                for (const auto& record : records.value())
                    std::cout << ' ' << record.id;
                std::cout << '\n';
                if (nearest.value())
                    std::cout << "nearest region: " << nearest.value()->id << '\n';
            }
            return 0;
        }
        auto feedback = rbfsafe::PolicyFeedbackDatabase::load(std::filesystem::path(argv[1]));
        if (feedback) {
            if (argc > 2) {
                std::cerr << "configuration queries do not apply to policy feedback databases\n";
                return 2;
            }
            const auto summary = feedback.value().summary();
            std::cout << "RBF-Safe policy feedback\n"
                      << "schema: 1\n"
                      << "records: " << summary.records << '\n'
                      << "selected accepted: " << summary.selected_accepted << '\n'
                      << "selected repaired: " << summary.selected_repaired << '\n'
                      << "eligible not selected: " << summary.eligible_not_selected << '\n'
                      << "policy rejected: " << summary.policy_rejected << '\n'
                      << "shield rejected: " << summary.shield_rejected << '\n';
            return 0;
        }
        auto memory = rbfsafe::SafetyMemory::load(std::filesystem::path(argv[1]));
        if (memory) {
            if (argc > 2) {
                std::cerr << "configuration queries do not apply to safety memory databases\n";
                return 2;
            }
            const auto summary = memory.value().summary();
            std::cout << "RBF-Safe safety memory\n"
                      << "schema: 1\n"
                      << "artifacts: " << summary.artifacts << '\n'
                      << "active: " << summary.active << '\n'
                      << "stale: " << summary.stale << '\n'
                      << "quarantined: " << summary.quarantined << '\n'
                      << "retired: " << summary.retired << '\n'
                      << "events: " << summary.events << '\n'
                      << "recorded reuses: " << summary.recorded_reuses << '\n';
            return 0;
        }
        auto memory_store = rbfsafe::SafetyMemoryStore::open(std::filesystem::path(argv[1]));
        if (memory_store) {
            if (argc > 2) {
                std::cerr << "configuration queries do not apply to safety memory stores\n";
                return 2;
            }
            auto current_memory = memory_store.value().load_current();
            if (!current_memory) {
                std::cerr << current_memory.error().describe() << '\n';
                return 1;
            }
            const auto summary = current_memory.value().summary();
            std::cout << "RBF-Safe safety memory store\n"
                      << "schema: 1\n"
                      << "revisions: " << memory_store.value().revisions().size() << '\n'
                      << "current: " << memory_store.value().current_revision_id() << '\n'
                      << "memory identity: " << current_memory.value().identity() << '\n'
                      << "artifacts: " << summary.artifacts << '\n'
                      << "active: " << summary.active << '\n'
                      << "stale: " << summary.stale << '\n'
                      << "events: " << summary.events << '\n';
            return 0;
        }
        auto schedule_archive = rbfsafe::FleetScheduleArchive::load(std::filesystem::path(argv[1]));
        if (schedule_archive) {
            if (argc > 2) {
                std::cerr << "configuration queries do not apply to fleet schedule archives\n";
                return 2;
            }
            std::cout << "RBF-Safe fleet schedule archive\n"
                      << "schema: 1\n"
                      << "fleet: " << schedule_archive.value().fleet_id() << '\n'
                      << "versions: " << schedule_archive.value().versions().size() << '\n'
                      << "current: " << schedule_archive.value().current_version_id() << '\n';
            if (!schedule_archive.value().current_version_id().empty()) {
                auto version = schedule_archive.value().current_version();
                if (!version) {
                    std::cerr << version.error().describe() << '\n';
                    return 1;
                }
                std::cout << "memory: " << version.value().memory_id << '\n'
                          << "snapshot: " << version.value().fleet.id << '\n'
                          << "status: " << rbfsafe::fleet_schedule_status_name(version.value().report.status)
                          << '\n'
                          << "reservations: " << version.value().report.reservations.size() << '\n'
                          << "conflicts: " << version.value().report.conflicts.size() << '\n'
                          << "pair evaluations: " << version.value().report.pair_evaluations << '\n';
            }
            return 0;
        }
        auto transfer_journal = rbfsafe::ArtifactTransferJournal::load(std::filesystem::path(argv[1]));
        if (transfer_journal) {
            if (argc > 2) {
                std::cerr << "configuration queries do not apply to artifact transfer journals\n";
                return 2;
            }
            const bool public_key_journal =
                !transfer_journal.value().records().empty() &&
                !transfer_journal.value().records().back().transfer.verification_key_id.empty();
            std::cout << "RBF-Safe artifact transfer journal\n"
                      << "schema: " << (public_key_journal ? 2 : 1) << '\n'
                      << "records: " << transfer_journal.value().records().size() << '\n'
                      << "current: " << transfer_journal.value().current_record_id() << '\n'
                      << "identity: " << transfer_journal.value().identity() << '\n';
            if (!transfer_journal.value().records().empty()) {
                const auto& record = transfer_journal.value().records().back();
                std::cout << "latest: " << record.id << '\n'
                          << "sequence: " << record.sequence << '\n'
                          << "parent: " << record.parent_id << '\n'
                          << "operation: "
                          << rbfsafe::artifact_transfer_operation_name(record.transfer.operation) << '\n'
                          << "transfer: " << record.transfer.id << '\n'
                          << "service: " << record.transfer.service_id << '\n'
                          << "artifact: " << record.transfer.artifact_id << '\n'
                          << "payload: " << record.transfer.payload_digest << '\n'
                          << "bytes: " << record.transfer.payload_bytes << '\n'
                          << "authentication: "
                          << rbfsafe::artifact_transfer_authentication_name(record.transfer.authentication)
                          << '\n';
                if (!record.transfer.verification_key_id.empty()) {
                    std::cout << "verification key: " << record.transfer.verification_key_id << '\n'
                              << "trust bundle: " << record.transfer.trust_bundle_id << '\n';
                }
            }
            std::cout << "runtime executable: false\n";
            return 0;
        }
        auto corridor = rbfsafe::HipacCorridor::load(std::filesystem::path(argv[1]));
        if (!corridor) {
            std::cerr << "Atlas load failed: " << atlas.error().describe() << '\n'
                      << "region database load failed: " << database.error().describe() << '\n'
                      << "policy feedback load failed: " << feedback.error().describe() << '\n'
                      << "safety memory load failed: " << memory.error().describe() << '\n'
                      << "safety memory store load failed: " << memory_store.error().describe() << '\n'
                      << "fleet schedule archive load failed: " << schedule_archive.error().describe() << '\n'
                      << "artifact transfer journal load failed: " << transfer_journal.error().describe()
                      << '\n'
                      << "corridor load failed: " << corridor.error().describe() << '\n';
            return 1;
        }
        std::cout << "RBF-Safe corridor\n"
                  << "dimension: " << corridor.value().dimension() << '\n'
                  << "regions: " << corridor.value().regions().size() << '\n'
                  << "portals: " << corridor.value().portals().size() << '\n'
                  << "robot: " << corridor.value().robot_digest() << '\n'
                  << "scene: " << corridor.value().scene_digest() << '\n';
        if (argc > 2) {
            if (static_cast<std::size_t>(argc - 2) != corridor.value().dimension()) {
                std::cerr << "query dimension does not match corridor dimension\n";
                return 2;
            }
            rbfsafe::Configuration query;
            query.reserve(corridor.value().dimension());
            try {
                for (int index = 2; index < argc; ++index) {
                    std::size_t consumed = 0;
                    const double value = std::stod(argv[index], &consumed);
                    if (consumed != std::string(argv[index]).size() || !std::isfinite(value))
                        throw std::invalid_argument("invalid coordinate");
                    query.push_back(value);
                }
            } catch (const std::exception&) {
                std::cerr << "query coordinates must be finite decimal numbers\n";
                return 2;
            }
            auto regions = corridor.value().regions_at(query);
            if (!regions) {
                std::cerr << regions.error().describe() << '\n';
                return 1;
            }
            std::cout << "query contains: " << (!regions.value().empty() ? "true" : "false") << '\n'
                      << "query regions:";
            for (const auto id : regions.value())
                std::cout << ' ' << id;
            std::cout << '\n';
        }
        return 0;
    }
    if (loaded_from_store) {
        std::cout << "RBF-Safe version store\n"
                  << "versions: " << stored_versions << '\n'
                  << "current: " << store_head << '\n';
    }
    std::cout << "RBF-Safe atlas\n"
              << "schema: " << atlas.value().storage_schema() << '\n'
              << "dimension: " << atlas.value().dimension() << '\n'
              << "regions: " << atlas.value().regions().size() << '\n'
              << "certificates: " << atlas.value().certificates().size() << '\n'
              << "repair domains: " << atlas.value().repair_domains().size() << '\n'
              << "lect nodes: " << atlas.value().lect().size() << '\n'
              << "version: " << atlas.value().version_info().id << '\n'
              << "sequence: " << atlas.value().version_info().sequence << '\n'
              << "robot: " << atlas.value().robot_digest() << '\n'
              << "scene: " << atlas.value().scene_digest() << '\n';
    if (argc > 2) {
        if (static_cast<std::size_t>(argc - 2) != atlas.value().dimension()) {
            std::cerr << "query dimension does not match atlas dimension\n";
            return 2;
        }
        rbfsafe::Configuration query;
        query.reserve(atlas.value().dimension());
        try {
            for (int index = 2; index < argc; ++index) {
                std::size_t consumed = 0;
                const double value = std::stod(argv[index], &consumed);
                if (consumed != std::string(argv[index]).size() || !std::isfinite(value)) {
                    throw std::invalid_argument("invalid coordinate");
                }
                query.push_back(value);
            }
        } catch (const std::exception&) {
            std::cerr << "query coordinates must be finite decimal numbers\n";
            return 2;
        }
        auto regions = atlas.value().regions_at(query);
        auto nearest = atlas.value().nearest_region(query);
        if (!regions || !nearest) {
            std::cerr << (!regions ? regions.error().describe() : nearest.error().describe()) << '\n';
            return 1;
        }
        std::cout << "query contains: " << (!regions.value().empty() ? "true" : "false") << '\n'
                  << "query regions:";
        for (const auto& region : regions.value())
            std::cout << ' ' << region.id;
        std::cout << '\n';
        if (nearest.value())
            std::cout << "nearest region: " << nearest.value()->id << '\n';
    }
    return 0;
}
