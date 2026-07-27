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

std::array<std::byte, 32> example_key() {
    std::array<std::byte, 32> key{};
    for (std::size_t index = 0; index < key.size(); ++index)
        key[index] = static_cast<std::byte>(index + 1);
    return key;
}

} // namespace

int main(int argc, char** argv) {
    using namespace rbfsafe;
    if (argc != 2) {
        std::cerr << "usage: rbfsafe_remote_artifact_quickstart <new-journal-directory>\n";
        return 2;
    }
    const std::string payload_text = "immutable atlas payload\n";
    const auto payload = std::as_bytes(std::span(payload_text.data(), payload_text.size()));
    const auto key = example_key();

    SafetyMemory memory;
    auto artifact = memory.register_artifact(artifact_input());
    if (!artifact) {
        std::cerr << artifact.error().describe() << '\n';
        return 1;
    }
    auto request = prepare_artifact_publish(memory, artifact.value().id, payload, "artifact-service", 1,
                                            "application/vnd.rbfsafe.atlas");
    if (!request) {
        std::cerr << request.error().describe() << '\n';
        return 1;
    }
    auto unsigned_receipt = make_artifact_publish_receipt(request.value(), 101);
    if (!unsigned_receipt) {
        std::cerr << unsigned_receipt.error().describe() << '\n';
        return 1;
    }
    auto receipt = authenticate_artifact_publish_receipt(unsigned_receipt.value(), "service-key-1", key);
    if (!receipt) {
        std::cerr << receipt.error().describe() << '\n';
        return 1;
    }
    auto verified =
        verify_artifact_publish(memory, request.value(), receipt.value(), payload, "service-key-1", key);
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
    auto saved = journal.save(std::filesystem::path(argv[1]));
    if (!saved) {
        std::cerr << saved.error().describe() << '\n';
        return 1;
    }
    auto loaded = ArtifactTransferJournal::load(std::filesystem::path(argv[1]));
    if (!loaded) {
        std::cerr << loaded.error().describe() << '\n';
        return 1;
    }
    std::cout << "request=" << request.value().id << '\n'
              << "receipt=" << receipt.value().id << '\n'
              << "transfer=" << verified.value().id << '\n'
              << "record=" << record.value().id << '\n'
              << "journal=" << loaded.value().identity() << '\n'
              << "authentication=" << artifact_transfer_authentication_name(verified.value().authentication)
              << '\n'
              << "runtime_executable=false\n";
    return 0;
}
