#include <rbfsafe/rbfsafe.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::string digest(char value) { return std::string(64, value); }

rbfsafe::MemoryArtifactInput artifact_input() {
    rbfsafe::MemoryArtifactInput input;
    input.type = rbfsafe::MemoryArtifactType::SafeAtlas;
    input.deployment_id = "arm-a";
    input.robot_digest = digest('a');
    input.scene_digest = digest('b');
    input.task_id = "shelf-pick";
    input.content_digest = digest('c');
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
    if (argc != 3) {
        std::cerr << "usage: rbfsafe_artifact_attestation_quickstart "
                     "<payload-file> <new-attestation-file>\n";
        return 2;
    }
    SafetyMemory memory;
    auto artifact = memory.register_artifact(artifact_input());
    if (!artifact) {
        std::cerr << artifact.error().describe() << '\n';
        return 1;
    }
    const auto key = example_key();
    auto attestation =
        attest_artifact_file(artifact.value(), std::filesystem::path(argv[1]), "factory-service",
                             "example-key-1", key, 1, "application/vnd.rbfsafe.atlas");
    if (!attestation) {
        std::cerr << attestation.error().describe() << '\n';
        return 1;
    }
    auto saved = save_artifact_attestation(attestation.value(), std::filesystem::path(argv[2]));
    if (!saved) {
        std::cerr << saved.error().describe() << '\n';
        return 1;
    }
    auto loaded = load_artifact_attestation(std::filesystem::path(argv[2]));
    if (!loaded) {
        std::cerr << loaded.error().describe() << '\n';
        return 1;
    }
    auto verified = verify_artifact_file(artifact.value(), std::filesystem::path(argv[1]), loaded.value(),
                                         "factory-service", "example-key-1", key);
    if (!verified) {
        std::cerr << verified.error().describe() << '\n';
        return 1;
    }
    std::cout << "attestation=" << loaded.value().id << '\n'
              << "artifact=" << loaded.value().artifact_id << '\n'
              << "payload=" << loaded.value().payload_digest << '\n'
              << "bytes=" << loaded.value().payload_bytes << '\n'
              << "algorithm=" << artifact_authentication_algorithm_name(loaded.value().algorithm) << '\n'
              << "verified=true\n";
    return 0;
}
