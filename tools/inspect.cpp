#include <rbfsafe/rbfsafe.h>

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

bool bounded_file_contains(const std::filesystem::path& path, const std::string& marker,
                           std::uintmax_t maximum_bytes = 65'536) {
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error || bytes > maximum_bytes)
        return false;
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    std::string text(static_cast<std::size_t>(bytes), '\0');
    if (!text.empty())
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
    return input && text.find(marker) != std::string::npos;
}

bool is_service_trust_history(const std::filesystem::path& directory) {
    return bounded_file_contains(directory / "manifest.json", "\"rbfsafe-service-trust-history\"");
}

bool is_service_trust_checkpoint(const std::filesystem::path& path) {
    return bounded_file_contains(path, "\"rbfsafe-service-trust-checkpoint\"");
}

bool is_reviewed_deployment_profile(const std::filesystem::path& path) {
    return bounded_file_contains(path, "\"rbfsafe-reviewed-deployment-profile\"");
}

bool is_bounded_execution_session(const std::filesystem::path& path) {
    return bounded_file_contains(path, "\"rbfsafe-bounded-execution-session\"", 67'108'864);
}

bool is_execution_ledger(const std::filesystem::path& path) {
    return bounded_file_contains(path / "manifest.json", "\"rbfsafe-execution-ledger\"");
}

bool is_transparency_log(const std::filesystem::path& path) {
    return bounded_file_contains(path / "manifest.json", "\"rbfsafe-transparency-log\"");
}

bool is_transparency_gossip_archive(const std::filesystem::path& path) {
    return bounded_file_contains(path / "manifest.json", "\"rbfsafe-transparency-gossip-archive\"");
}

bool is_verifiable_provenance_bundle(const std::filesystem::path& path) {
    return bounded_file_contains(path, "\"rbfsafe-verifiable-provenance-bundle\"", 16'777'216);
}

bool decode_uint64(std::string_view text, std::uint64_t& result) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

int hex_digit(char value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    return -1;
}

bool decode_public_key(std::string_view text,
                       std::array<std::byte, rbfsafe::kEd25519PublicKeyBytes>& result) {
    if (text.size() != result.size() * 2U)
        return false;
    for (std::size_t index = 0; index < result.size(); ++index) {
        const int high = hex_digit(text[index * 2U]);
        const int low = hex_digit(text[index * 2U + 1U]);
        if (high < 0 || low < 0)
            return false;
        result[index] = static_cast<std::byte>((high << 4) | low);
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: rbfsafe-inspect <database-archive-or-profile> [query values or profile]\n"
                  << "       rbfsafe-inspect <trust-history> <expected-root> <expected-head>\n"
                  << "       rbfsafe-inspect <trust-history> <expected-root> <checkpoint> "
                     "<expected-checkpoint>\n"
                  << "       rbfsafe-inspect <checkpoint> <trust-history> <expected-root> "
                     "<expected-checkpoint>\n"
                  << "       rbfsafe-inspect <reviewed-deployment-profile> <trust-history> "
                     "<expected-root> <checkpoint> <expected-checkpoint>\n"
                  << "       rbfsafe-inspect <bounded-execution-session> <reviewed-profile> "
                     "<atlas> <trust-history> <expected-root> <checkpoint> "
                     "<expected-checkpoint>\n"
                  << "       rbfsafe-inspect <execution-ledger> <bounded-execution-session> "
                     "<reviewed-profile> <atlas> <trust-history> <expected-root> <checkpoint> "
                     "<expected-checkpoint>\n"
                  << "       rbfsafe-inspect <transparency-log> <namespace> <signer-service> "
                     "<signer-key-id> <signer-public-key-hex> <expected-checkpoint>\n"
                  << "       rbfsafe-inspect <transparency-gossip-archive> <namespace> "
                     "<signer-service> <signer-key-id> <signer-public-key-hex> "
                     "<expected-gossip-head> <trust-history> <expected-root> <checkpoint> "
                     "<expected-checkpoint> <expected-bundle>\n"
                  << "       rbfsafe-inspect <provenance-bundle> <trust-history> "
                     "<expected-root> <checkpoint> <expected-checkpoint> <evaluated-at-ns>\n";
        return 2;
    }
    if (is_verifiable_provenance_bundle(std::filesystem::path(argv[1]))) {
        if (argc != 7) {
            std::cerr << "provenance inspection requires trust history, expected trust root, "
                         "trust checkpoint, expected checkpoint, and caller-supplied evaluation time\n";
            return 2;
        }
        std::uint64_t evaluated_at_ns = 0;
        if (!decode_uint64(argv[6], evaluated_at_ns)) {
            std::cerr << "provenance evaluation time must be an unsigned decimal nanosecond value\n";
            return 2;
        }
        auto provenance = rbfsafe::VerifiableProvenanceBundle::load(std::filesystem::path(argv[1]));
        if (!provenance) {
            std::cerr << provenance.error().describe() << '\n';
            return 1;
        }
        auto checkpoint = rbfsafe::ServiceTrustCheckpoint::load(std::filesystem::path(argv[4]));
        if (!checkpoint) {
            std::cerr << checkpoint.error().describe() << '\n';
            return 1;
        }
        auto history = rbfsafe::ServiceTrustHistory::open(std::filesystem::path(argv[2]), argv[3],
                                                          checkpoint.value(), argv[5]);
        if (!history) {
            std::cerr << history.error().describe() << '\n';
            return 1;
        }
        auto bundle = history.value().bundle(provenance.value().trust_bundle_id());
        if (!bundle) {
            std::cerr << bundle.error().describe() << '\n';
            return 1;
        }
        auto audit =
            rbfsafe::replay_verifiable_provenance(provenance.value(), bundle.value(), evaluated_at_ns);
        if (!audit) {
            std::cerr << audit.error().describe() << '\n';
            return 1;
        }
        std::cout << "RBF-Safe verifiable provenance bundle\n"
                  << "schema: " << provenance.value().storage_schema() << '\n'
                  << "bundle: " << provenance.value().id() << '\n'
                  << "subject service: " << provenance.value().subject_key().service_id << '\n'
                  << "subject key: " << provenance.value().subject_key().id << '\n'
                  << "trust bundle: " << provenance.value().trust_bundle_id() << '\n'
                  << "hardware status: "
                  << rbfsafe::hardware_provenance_status_name(audit.value().hardware.status) << '\n'
                  << "hardware statements: " << audit.value().hardware.authenticated_statement_count << '\n'
                  << "distinct attesters: " << audit.value().hardware.distinct_attester_count << '\n'
                  << "freshness status: "
                  << rbfsafe::external_time_freshness_status_name(audit.value().freshness.status) << '\n'
                  << "time sources: " << audit.value().freshness.authenticated_source_count << '\n'
                  << "evaluated at ns: " << audit.value().freshness.evaluated_at_ns << '\n'
                  << "ready: " << (audit.value().ready() ? "true" : "false") << '\n'
                  << "evidence: unknown\n"
                  << "runtime executable: false\n";
        return 0;
    }
    if (is_transparency_gossip_archive(std::filesystem::path(argv[1]))) {
        if (argc != 12) {
            std::cerr << "transparency-gossip inspection requires namespace, signer service, "
                         "signer key ID, signer public-key hex, expected gossip head, trust history, "
                         "expected trust root, trust checkpoint, expected checkpoint, and expected bundle\n";
            return 2;
        }
        std::array<std::byte, rbfsafe::kEd25519PublicKeyBytes> public_key{};
        if (!decode_public_key(argv[5], public_key)) {
            std::cerr << "transparency signer public key must contain 64 lowercase hex characters\n";
            return 2;
        }
        auto identity = rbfsafe::TransparencyLogIdentity::create(argv[2], argv[3], argv[4], public_key);
        if (!identity) {
            std::cerr << identity.error().describe() << '\n';
            return 1;
        }
        auto checkpoint = rbfsafe::ServiceTrustCheckpoint::load(std::filesystem::path(argv[9]));
        if (!checkpoint) {
            std::cerr << checkpoint.error().describe() << '\n';
            return 1;
        }
        auto history = rbfsafe::ServiceTrustHistory::open(std::filesystem::path(argv[7]), argv[8],
                                                          checkpoint.value(), argv[10]);
        if (!history) {
            std::cerr << history.error().describe() << '\n';
            return 1;
        }
        auto bundle = history.value().bundle(argv[11]);
        if (!bundle) {
            std::cerr << bundle.error().describe() << '\n';
            return 1;
        }
        auto archive = rbfsafe::TransparencyGossipArchive::open(
            std::filesystem::path(argv[1]), identity.value(), bundle.value(), argv[11], argv[6]);
        if (!archive) {
            std::cerr << archive.error().describe() << '\n';
            return 1;
        }
        auto audit = archive.value().audit();
        if (!audit) {
            std::cerr << audit.error().describe() << '\n';
            return 1;
        }
        std::cout << "RBF-Safe transparency gossip archive\n"
                  << "schema: 1\n"
                  << "log: " << identity.value().id << '\n'
                  << "trust bundle: " << archive.value().trust_bundle_id() << '\n'
                  << "trust sequence: " << archive.value().trust_bundle_sequence() << '\n'
                  << "head: "
                  << (archive.value().current_record_id().empty() ? "-" : archive.value().current_record_id())
                  << '\n'
                  << "records: " << archive.value().records().size() << '\n'
                  << "authenticated gossip: " << audit.value().authenticated_gossip_count << '\n'
                  << "checkpoints: " << audit.value().unique_checkpoint_count << '\n'
                  << "status: " << rbfsafe::transparency_gossip_status_name(audit.value().status) << '\n'
                  << "linked pairs: " << audit.value().linked_checkpoint_pairs << '\n'
                  << "unlinked pairs: " << audit.value().unlinked_checkpoint_pairs << '\n'
                  << "conflicts: " << audit.value().conflicts.size() << '\n';
        for (const auto& conflict : audit.value().conflicts) {
            std::cout << "conflict: " << conflict.id
                      << " type=" << rbfsafe::transparency_gossip_conflict_type_name(conflict.type)
                      << " first=" << conflict.first_checkpoint_id
                      << " second=" << conflict.second_checkpoint_id << '\n';
        }
        std::cout << "evidence: unknown\n"
                  << "runtime executable: false\n";
        return 0;
    }
    if (is_transparency_log(std::filesystem::path(argv[1]))) {
        if (argc != 7) {
            std::cerr << "transparency-log inspection requires namespace, signer service, "
                         "signer key ID, signer public-key hex, and expected checkpoint ID\n";
            return 2;
        }
        std::array<std::byte, rbfsafe::kEd25519PublicKeyBytes> public_key{};
        if (!decode_public_key(argv[5], public_key)) {
            std::cerr << "transparency signer public key must contain 64 lowercase hex characters\n";
            return 2;
        }
        auto identity = rbfsafe::TransparencyLogIdentity::create(argv[2], argv[3], argv[4], public_key);
        if (!identity) {
            std::cerr << identity.error().describe() << '\n';
            return 1;
        }
        auto log = rbfsafe::TransparencyLog::open(std::filesystem::path(argv[1]), identity.value(), argv[6]);
        if (!log) {
            std::cerr << log.error().describe() << '\n';
            return 1;
        }
        auto audit = log.value().audit();
        if (!audit) {
            std::cerr << audit.error().describe() << '\n';
            return 1;
        }
        std::cout << "RBF-Safe transparency log\n"
                  << "schema: 1\n"
                  << "log: " << identity.value().id << '\n'
                  << "namespace: " << identity.value().log_namespace << '\n'
                  << "checkpoint: "
                  << (audit.value().current_checkpoint_id.empty() ? "-" : audit.value().current_checkpoint_id)
                  << '\n'
                  << "root: "
                  << (audit.value().current_root_hash.empty() ? "-" : audit.value().current_root_hash) << '\n'
                  << "records: " << audit.value().verified_records << '\n'
                  << "deployment anchors: " << audit.value().deployment_anchor_count << '\n'
                  << "runtime observations: " << audit.value().runtime_observation_count << '\n';
        for (const auto& record : log.value().records()) {
            std::cout << "record: " << record.id << " sequence=" << record.sequence
                      << " kind=" << rbfsafe::transparency_leaf_kind_name(record.leaf.kind)
                      << " leaf=" << record.leaf.id << " checkpoint=" << record.checkpoint.id << '\n';
        }
        std::cout << "evidence: unknown\n"
                  << "runtime executable: false\n";
        return 0;
    }
    if (is_execution_ledger(std::filesystem::path(argv[1]))) {
        if (argc != 9) {
            std::cerr << "execution-ledger inspection requires bounded session, reviewed profile, "
                         "Atlas, trust history, expected root, checkpoint, and expected checkpoint ID\n";
            return 2;
        }
        auto checkpoint = rbfsafe::ServiceTrustCheckpoint::load(std::filesystem::path(argv[7]));
        if (!checkpoint) {
            std::cerr << checkpoint.error().describe() << '\n';
            return 1;
        }
        auto history = rbfsafe::ServiceTrustHistory::open(std::filesystem::path(argv[5]), argv[6],
                                                          checkpoint.value(), argv[8]);
        if (!history) {
            std::cerr << history.error().describe() << '\n';
            return 1;
        }
        auto reviewed = rbfsafe::ReviewedDeploymentProfile::load(
            std::filesystem::path(argv[3]), history.value(), checkpoint.value(), argv[8]);
        if (!reviewed) {
            std::cerr << reviewed.error().describe() << '\n';
            return 1;
        }
        auto atlas = rbfsafe::SafeAtlas::load(std::filesystem::path(argv[4]));
        if (!atlas) {
            std::cerr << atlas.error().describe() << '\n';
            return 1;
        }
        auto session = rbfsafe::BoundedExecutionSession::load(std::filesystem::path(argv[2]),
                                                              reviewed.value(), history.value(),
                                                              checkpoint.value(), argv[8], atlas.value());
        if (!session) {
            std::cerr << session.error().describe() << '\n';
            return 1;
        }
        auto ledger = rbfsafe::ExecutionLedger::open(std::filesystem::path(argv[1]), session.value(),
                                                     reviewed.value(), history.value(), atlas.value());
        if (!ledger) {
            std::cerr << ledger.error().describe() << '\n';
            return 1;
        }
        auto audit = ledger.value().audit(session.value(), reviewed.value(), history.value(), atlas.value());
        if (!audit) {
            std::cerr << audit.error().describe() << '\n';
            return 1;
        }
        std::cout << "RBF-Safe execution ledger\n"
                  << "schema: 1\n"
                  << "ledger: " << ledger.value().id() << '\n'
                  << "session: " << ledger.value().session_id() << '\n'
                  << "head: " << ledger.value().current_record_id() << '\n'
                  << "status: " << rbfsafe::execution_ledger_status_name(audit.value().status) << '\n'
                  << "records: " << audit.value().verified_records << '\n'
                  << "authorizations: " << audit.value().authorization_count << '\n'
                  << "completions: " << audit.value().completion_count << '\n'
                  << "verified checkpoints: " << audit.value().verified_checkpoints << '\n'
                  << "latest checkpoint: "
                  << (audit.value().latest_checkpoint_id.empty() ? "-" : audit.value().latest_checkpoint_id)
                  << '\n'
                  << "evidence: unknown\n"
                  << "runtime executable: false\n";
        return 0;
    }
    if (is_bounded_execution_session(std::filesystem::path(argv[1]))) {
        if (argc != 8) {
            std::cerr << "bounded execution-session inspection requires reviewed profile, Atlas, "
                         "trust history, expected root, checkpoint, and expected checkpoint ID\n";
            return 2;
        }
        auto checkpoint = rbfsafe::ServiceTrustCheckpoint::load(std::filesystem::path(argv[6]));
        if (!checkpoint) {
            std::cerr << checkpoint.error().describe() << '\n';
            return 1;
        }
        auto history = rbfsafe::ServiceTrustHistory::open(std::filesystem::path(argv[4]), argv[5],
                                                          checkpoint.value(), argv[7]);
        if (!history) {
            std::cerr << history.error().describe() << '\n';
            return 1;
        }
        auto reviewed = rbfsafe::ReviewedDeploymentProfile::load(
            std::filesystem::path(argv[2]), history.value(), checkpoint.value(), argv[7]);
        if (!reviewed) {
            std::cerr << reviewed.error().describe() << '\n';
            return 1;
        }
        auto atlas = rbfsafe::SafeAtlas::load(std::filesystem::path(argv[3]));
        if (!atlas) {
            std::cerr << atlas.error().describe() << '\n';
            return 1;
        }
        auto session = rbfsafe::BoundedExecutionSession::load(std::filesystem::path(argv[1]),
                                                              reviewed.value(), history.value(),
                                                              checkpoint.value(), argv[7], atlas.value());
        if (!session) {
            std::cerr << session.error().describe() << '\n';
            return 1;
        }
        std::cout << "RBF-Safe bounded execution session\n"
                  << "schema: 1\n"
                  << "session: " << session.value().id() << '\n'
                  << "request: " << session.value().request().id << '\n'
                  << "profile: " << session.value().request().reviewed_profile_id << '\n'
                  << "atlas: " << session.value().request().atlas_id << '\n'
                  << "commands: " << session.value().command_sequence().commands.size() << '\n'
                  << "approvals: " << session.value().approval_set().approvals.size() << '\n'
                  << "controller: " << session.value().request().controller.service_id << '\n'
                  << "monitor: " << session.value().request().runtime_monitor.service_id << '\n'
                  << "monitor state: "
                  << rbfsafe::execution_monitor_state_name(
                         session.value().monitor_acknowledgement().observation.monitor_state)
                  << '\n'
                  << "valid from monotonic ns: " << session.value().valid_from_monotonic_ns() << '\n'
                  << "start deadline monotonic ns: " << session.value().start_deadline_monotonic_ns() << '\n'
                  << "valid through monotonic ns: " << session.value().valid_through_monotonic_ns() << '\n'
                  << "caller pinned: true\n"
                  << "profile and signatures verified: true\n"
                  << "session evidence: unknown\n"
                  << "runtime executable: false\n"
                  << "exact command input required: true\n";
        return 0;
    }
    if (is_reviewed_deployment_profile(std::filesystem::path(argv[1]))) {
        if (argc != 6) {
            std::cerr << "reviewed deployment-profile inspection requires trust history, "
                         "expected root, checkpoint, and expected checkpoint ID\n";
            return 2;
        }
        auto checkpoint = rbfsafe::ServiceTrustCheckpoint::load(std::filesystem::path(argv[4]));
        if (!checkpoint) {
            std::cerr << checkpoint.error().describe() << '\n';
            return 1;
        }
        auto history = rbfsafe::ServiceTrustHistory::open(std::filesystem::path(argv[2]), argv[3],
                                                          checkpoint.value(), argv[5]);
        if (!history) {
            std::cerr << history.error().describe() << '\n';
            return 1;
        }
        auto reviewed = rbfsafe::ReviewedDeploymentProfile::load(
            std::filesystem::path(argv[1]), history.value(), checkpoint.value(), argv[5]);
        if (!reviewed) {
            std::cerr << reviewed.error().describe() << '\n';
            return 1;
        }
        const auto& profile = reviewed.value().profile();
        const auto& approval_set = reviewed.value().approval_set();
        std::cout << "RBF-Safe reviewed deployment profile\n"
                  << "schema: " << profile.storage_schema << '\n'
                  << "profile: " << profile.id << '\n'
                  << "deployment: " << profile.deployment_id << '\n'
                  << "robot: " << profile.robot_digest << '\n'
                  << "controller: " << profile.controller_digest << '\n'
                  << "platform: " << profile.platform_digest << '\n'
                  << "runtime: " << profile.runtime_digest << '\n'
                  << "trust root: " << profile.trust_root_bundle_id << '\n'
                  << "trust checkpoint: " << profile.trust_checkpoint_id << '\n'
                  << "trust bundle: " << profile.trust_bundle_id << '\n'
                  << "trust sequence: " << profile.trust_bundle_sequence << '\n'
                  << "minimum approvals: " << profile.review_policy.minimum_approvals << '\n'
                  << "distinct services: "
                  << (profile.review_policy.require_distinct_services ? "true" : "false") << '\n'
                  << "approval set: " << approval_set.id << '\n'
                  << "approvals: " << approval_set.approvals.size() << '\n';
        for (const auto role : profile.review_policy.required_roles)
            std::cout << "required role: " << rbfsafe::deployment_review_role_name(role) << '\n';
        for (const auto& approval : approval_set.approvals) {
            std::cout << "approval: " << approval.id << '\n'
                      << "  signer service: " << approval.signer_service_id << '\n'
                      << "  signer key: " << approval.signer_key_id << '\n'
                      << "  role: " << rbfsafe::deployment_review_role_name(approval.role) << '\n';
        }
        std::cout << "caller pinned: true\n"
                  << "checkpoint verified: true\n"
                  << "review signatures verified: true\n"
                  << "runtime executable: false\n";
        return 0;
    }
    if (is_service_trust_history(std::filesystem::path(argv[1]))) {
        if (argc != 4 && argc != 5) {
            std::cerr << "service trust-history inspection requires root/head or "
                         "root/checkpoint/checkpoint-ID arguments\n";
            return 2;
        }
        rbfsafe::Result<rbfsafe::ServiceTrustHistory> history =
            argc == 4
                ? rbfsafe::ServiceTrustHistory::open(std::filesystem::path(argv[1]), argv[2], argv[3])
                : [&]() {
                      auto checkpoint = rbfsafe::ServiceTrustCheckpoint::load(std::filesystem::path(argv[3]));
                      if (!checkpoint)
                          return rbfsafe::Result<rbfsafe::ServiceTrustHistory>::failure(
                              checkpoint.error().code, checkpoint.error().message,
                              checkpoint.error().context);
                      return rbfsafe::ServiceTrustHistory::open(std::filesystem::path(argv[1]), argv[2],
                                                                checkpoint.value(), argv[4]);
                  }();
        if (!history) {
            std::cerr << history.error().describe() << '\n';
            return 1;
        }
        std::cout << "RBF-Safe service trust history\n"
                  << "schema: " << history.value().storage_schema() << '\n'
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
            if (record.authorization_set) {
                std::cout << "  authorization set: " << record.authorization_set->id << '\n'
                          << "  signatures: " << record.authorization_set->authorizations.size() << '\n';
                for (const auto& authorization : record.authorization_set->authorizations) {
                    std::cout << "  signer service: " << authorization.signer_service_id << '\n'
                              << "  signer key: " << authorization.signer_key_id << '\n';
                }
            }
        }
        std::cout << "caller pinned: true\n"
                  << "expected head verified: true\n"
                  << "checkpoint verified: " << (argc == 5 ? "true" : "false") << '\n'
                  << "runtime executable: false\n";
        return 0;
    }
    if (is_service_trust_checkpoint(std::filesystem::path(argv[1]))) {
        if (argc != 5) {
            std::cerr << "service trust-checkpoint inspection requires history, "
                         "expected root, and expected checkpoint ID\n";
            return 2;
        }
        auto checkpoint = rbfsafe::ServiceTrustCheckpoint::load(std::filesystem::path(argv[1]));
        if (!checkpoint) {
            std::cerr << checkpoint.error().describe() << '\n';
            return 1;
        }
        auto history = rbfsafe::ServiceTrustHistory::open(std::filesystem::path(argv[2]), argv[3],
                                                          checkpoint.value(), argv[4]);
        if (!history) {
            std::cerr << history.error().describe() << '\n';
            return 1;
        }
        std::cout << "RBF-Safe service trust checkpoint\n"
                  << "schema: " << checkpoint.value().storage_schema << '\n'
                  << "checkpoint: " << checkpoint.value().id << '\n'
                  << "root: " << checkpoint.value().root_bundle_id << '\n'
                  << "head: " << checkpoint.value().head_bundle_id << '\n'
                  << "sequence: " << checkpoint.value().head_sequence << '\n'
                  << "record: " << checkpoint.value().head_record_id << '\n'
                  << "signatures: " << checkpoint.value().signatures.size() << '\n';
        for (const auto& signature : checkpoint.value().signatures) {
            std::cout << "signer service: " << signature.signer_service_id << '\n'
                      << "signer key: " << signature.signer_key_id << '\n';
        }
        std::cout << "history schema: " << history.value().storage_schema() << '\n'
                  << "caller pinned: true\n"
                  << "checkpoint verified: true\n"
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
        std::cout << "rotation minimum signatures: "
                  << trust_bundle.value().rotation_policy().minimum_signatures << '\n'
                  << "rotation distinct services: "
                  << (trust_bundle.value().rotation_policy().require_distinct_services ? "true" : "false")
                  << '\n';
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
