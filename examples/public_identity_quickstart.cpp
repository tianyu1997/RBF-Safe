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
    if (argc != 3) {
        std::cerr << "usage: rbfsafe_public_identity_quickstart "
                     "<new-trust-bundle-file> <new-transfer-journal-directory>\n";
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
                                               ServiceKeyState::Active);
    if (!service_key) {
        std::cerr << service_key.error().describe() << '\n';
        return 1;
    }
    auto trust_bundle = ServiceTrustBundle::create(1, "", {service_key.value()});
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

    std::cout << "key=" << service_key.value().id << '\n'
              << "bundle=" << trust_bundle.value().id() << '\n'
              << "request=" << request.value().id << '\n'
              << "receipt=" << receipt.value().id << '\n'
              << "attestation=" << receipt.value().service_attestation->id << '\n'
              << "transfer=" << verified.value().id << '\n'
              << "record=" << record.value().id << '\n'
              << "authentication=ed25519\n"
              << "runtime_executable=false\n";
    return 0;
}
