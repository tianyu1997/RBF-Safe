#include <rbfsafe/rbfsafe.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

constexpr const char* kPayloadDigest = "4753e499617943c6b3dfa197752908e793797df2a669c7696ab3e53e534df4bd";

std::string digest(char value) { return std::string(64, value); }

rbfsafe::MemoryArtifactInput artifact_input() {
    rbfsafe::MemoryArtifactInput input;
    input.type = rbfsafe::MemoryArtifactType::SafeAtlas;
    input.deployment_id = "arm-a";
    input.robot_digest = digest('a');
    input.scene_digest = digest('b');
    input.task_id = "shelf-pick";
    input.content_digest = kPayloadDigest;
    input.locator = "artifacts/shelf-atlas";
    input.evidence = rbfsafe::EvidenceLevel::CertifiedRegion;
    return input;
}

} // namespace

int main(int argc, char** argv) {
    using namespace rbfsafe;
    if (argc != 5) {
        std::cerr << "usage: rbfsafe_public_identity_quickstart "
                     "<new-root-bundle-file> <new-transfer-journal-directory> "
                     "<new-trust-history-directory> <new-checkpoint-file>\n";
        return 2;
    }

    std::array<std::byte, kEd25519SeedBytes> seed{};
    for (std::size_t index = 0; index < seed.size(); ++index)
        seed[index] = static_cast<std::byte>(index + 1);
    auto key_pair = ed25519_key_pair_from_seed(seed);
    if (!key_pair) {
        std::cerr << key_pair.error().describe() << '\n';
        return 1;
    }
    auto service_key = make_service_public_key("artifact-service", key_pair.value().public_key, 1, 0,
                                               ServiceKeyState::Active, true, true, true);
    if (!service_key) {
        std::cerr << service_key.error().describe() << '\n';
        return 1;
    }
    std::array<std::byte, kEd25519SeedBytes> governance_seed{};
    for (std::size_t index = 0; index < governance_seed.size(); ++index)
        governance_seed[index] = static_cast<std::byte>(index + 33);
    auto governance_pair = ed25519_key_pair_from_seed(governance_seed);
    if (!governance_pair) {
        std::cerr << governance_pair.error().describe() << '\n';
        return 1;
    }
    auto governance_key = make_service_public_key("rotation-governance", governance_pair.value().public_key,
                                                  1, 0, ServiceKeyState::Active, false, false, true);
    if (!governance_key) {
        std::cerr << governance_key.error().describe() << '\n';
        return 1;
    }
    ServiceTrustRotationPolicy rotation_policy;
    rotation_policy.minimum_signatures = 2;
    rotation_policy.require_distinct_services = true;
    auto trust_bundle = ServiceTrustBundle::create_with_rotation_policy(
        1, "", {service_key.value(), governance_key.value()}, rotation_policy);
    if (!trust_bundle) {
        std::cerr << trust_bundle.error().describe() << '\n';
        return 1;
    }

    SafetyMemory memory;
    auto artifact = memory.register_artifact(artifact_input());
    if (!artifact) {
        std::cerr << artifact.error().describe() << '\n';
        return 1;
    }
    const std::string payload_text = "immutable atlas payload\n";
    const auto payload = std::as_bytes(std::span(payload_text.data(), payload_text.size()));
    auto request =
        prepare_artifact_publish(memory, artifact.value().id, payload, "artifact-service", 31,
                                 "application/vnd.rbfsafe.atlas", ArtifactTransferAuthentication::Ed25519);
    if (!request) {
        std::cerr << request.error().describe() << '\n';
        return 1;
    }
    auto unsigned_receipt = make_artifact_publish_receipt(request.value(), 101);
    if (!unsigned_receipt) {
        std::cerr << unsigned_receipt.error().describe() << '\n';
        return 1;
    }
    auto receipt = sign_artifact_publish_receipt(unsigned_receipt.value(), service_key.value().id,
                                                 key_pair.value().secret_key);
    if (!receipt) {
        std::cerr << receipt.error().describe() << '\n';
        return 1;
    }
    auto verified = verify_artifact_publish_offline(memory, request.value(), receipt.value(), payload,
                                                    trust_bundle.value());
    if (!verified) {
        std::cerr << verified.error().describe() << '\n';
        return 1;
    }
    ArtifactTransferJournal journal;
    auto record = journal.append(verified.value(), "");
    if (!record) {
        std::cerr << record.error().describe() << '\n';
        return 1;
    }
    auto bundle_saved = trust_bundle.value().save(std::filesystem::path(argv[1]));
    if (!bundle_saved) {
        std::cerr << bundle_saved.error().describe() << '\n';
        return 1;
    }
    auto journal_saved = journal.save(std::filesystem::path(argv[2]));
    if (!journal_saved) {
        std::cerr << journal_saved.error().describe() << '\n';
        return 1;
    }

    std::array<std::byte, kEd25519SeedBytes> successor_seed{};
    for (std::size_t index = 0; index < successor_seed.size(); ++index)
        successor_seed[index] = static_cast<std::byte>(index + 65);
    auto successor_pair = ed25519_key_pair_from_seed(successor_seed);
    if (!successor_pair) {
        std::cerr << successor_pair.error().describe() << '\n';
        return 1;
    }
    auto successor_key = make_service_public_key("artifact-service", successor_pair.value().public_key, 2, 0,
                                                 ServiceKeyState::Active, true, true, true);
    if (!successor_key) {
        std::cerr << successor_key.error().describe() << '\n';
        return 1;
    }
    auto retired_key = service_key.value();
    retired_key.state = ServiceKeyState::Retired;
    retired_key.valid_through_sequence = 1;
    auto successor = rotate_service_trust_bundle(
        trust_bundle.value(), {retired_key, successor_key.value(), governance_key.value()});
    if (!successor) {
        std::cerr << successor.error().describe() << '\n';
        return 1;
    }
    auto service_authorization = authorize_service_trust_bundle_successor(
        trust_bundle.value(), successor.value(), service_key.value().service_id, service_key.value().id,
        key_pair.value().secret_key);
    if (!service_authorization) {
        std::cerr << service_authorization.error().describe() << '\n';
        return 1;
    }
    auto governance_authorization = authorize_service_trust_bundle_successor(
        trust_bundle.value(), successor.value(), governance_key.value().service_id, governance_key.value().id,
        governance_pair.value().secret_key);
    if (!governance_authorization) {
        std::cerr << governance_authorization.error().describe() << '\n';
        return 1;
    }
    auto authorizations = assemble_service_trust_bundle_authorizations(
        trust_bundle.value(), successor.value(),
        {governance_authorization.value(), service_authorization.value()});
    if (!authorizations) {
        std::cerr << authorizations.error().describe() << '\n';
        return 1;
    }
    auto history = ServiceTrustHistory::create(std::filesystem::path(argv[3]), trust_bundle.value(),
                                               trust_bundle.value().id());
    if (!history) {
        std::cerr << history.error().describe() << '\n';
        return 1;
    }
    auto rotation =
        history.value().publish(successor.value(), authorizations.value(), trust_bundle.value().id());
    if (!rotation) {
        std::cerr << rotation.error().describe() << '\n';
        return 1;
    }
    auto service_checkpoint_signature =
        sign_service_trust_checkpoint(history.value(), successor_key.value().service_id,
                                      successor_key.value().id, successor_pair.value().secret_key);
    auto governance_checkpoint_signature =
        sign_service_trust_checkpoint(history.value(), governance_key.value().service_id,
                                      governance_key.value().id, governance_pair.value().secret_key);
    if (!service_checkpoint_signature || !governance_checkpoint_signature) {
        std::cerr << "failed to sign the trust checkpoint\n";
        return 1;
    }
    auto checkpoint = assemble_service_trust_checkpoint(
        history.value(), {governance_checkpoint_signature.value(), service_checkpoint_signature.value()});
    if (!checkpoint) {
        std::cerr << checkpoint.error().describe() << '\n';
        return 1;
    }
    auto checkpoint_saved = checkpoint.value().save(std::filesystem::path(argv[4]));
    if (!checkpoint_saved) {
        std::cerr << checkpoint_saved.error().describe() << '\n';
        return 1;
    }

    std::cout << "key=" << service_key.value().id << '\n'
              << "bundle=" << trust_bundle.value().id() << '\n'
              << "request=" << request.value().id << '\n'
              << "receipt=" << receipt.value().id << '\n'
              << "attestation=" << receipt.value().service_attestation->id << '\n'
              << "transfer=" << verified.value().id << '\n'
              << "record=" << record.value().id << '\n'
              << "authorization_set=" << authorizations.value().id << '\n'
              << "rotation=" << rotation.value().id << '\n'
              << "trust_head=" << history.value().current_bundle_id() << '\n'
              << "checkpoint=" << checkpoint.value().id << '\n'
              << "rotation_quorum=2\n"
              << "authentication=ed25519\n"
              << "runtime_executable=false\n";
    return 0;
}
