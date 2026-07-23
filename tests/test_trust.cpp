#include "test_support.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
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

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace

int main() {
    using namespace rbfsafe;
    SafetyMemory memory;
    auto artifact = memory.register_artifact(artifact_input());
    CHECK(artifact);

    const std::string payload_text = "immutable atlas payload\n";
    const auto payload = std::as_bytes(std::span(payload_text.data(), payload_text.size()));
    std::array<std::byte, 32> key{};
    for (std::size_t index = 0; index < key.size(); ++index)
        key[index] = static_cast<std::byte>(index + 1);
    std::array<std::byte, 32> wrong_key{};
    wrong_key.fill(std::byte{0x7f});
    std::array<std::byte, 31> short_key{};

    CHECK(!attest_artifact(artifact.value(), payload, "factory-service", "rotation-7", short_key, 12));
    auto attestation = attest_artifact(artifact.value(), payload, "factory-service", "rotation-7", key, 12,
                                       "application/vnd.rbfsafe.atlas");
    CHECK(attestation);
    CHECK(valid_artifact_attestation(attestation.value()));
    CHECK(attestation.value().sequence == 12);
    CHECK(attestation.value().artifact_id == artifact.value().id);
    CHECK(attestation.value().artifact_generation == 1);
    CHECK(attestation.value().payload_bytes == payload.size());
    CHECK(attestation.value().id.size() == 64);
    CHECK(attestation.value().authentication_tag.size() == 64);
    CHECK(verify_artifact(artifact.value(), payload, attestation.value(), "factory-service", "rotation-7",
                          key));

    auto fixed = load_artifact_attestation(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) /
                                           "artifact_attestation_schema1" / "attestation.json");
    CHECK(fixed);
    CHECK(fixed.value().id == "899b221e1f50f23caa9d7a328cb5ad09f07545d31c5d6a69d57dda420d956e52");
    CHECK(fixed.value().authentication_tag ==
          "632675274bb35c76bd7819b27e4309acaa5f21ed125121d3a11a26e96461615c");
    CHECK(verify_artifact_file(artifact.value(),
                               std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "artifact_attestation_schema1" /
                                   "payload.bin",
                               fixed.value(), "factory-service", "example-key-1", key));

    auto wrong_service =
        verify_artifact(artifact.value(), payload, attestation.value(), "other-service", "rotation-7", key);
    CHECK(!wrong_service);
    CHECK(wrong_service.error().code == StatusCode::IdentityMismatch);
    auto wrong_authentication = verify_artifact(artifact.value(), payload, attestation.value(),
                                                "factory-service", "rotation-7", wrong_key);
    CHECK(!wrong_authentication);
    CHECK(wrong_authentication.error().code == StatusCode::IdentityMismatch);
    const std::string altered_text = "altered atlas payload\n";
    const auto altered = std::as_bytes(std::span(altered_text.data(), altered_text.size()));
    auto wrong_payload =
        verify_artifact(artifact.value(), altered, attestation.value(), "factory-service", "rotation-7", key);
    CHECK(!wrong_payload);
    CHECK(wrong_payload.error().code == StatusCode::IdentityMismatch);

    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("rbfsafe-trust-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temporary);
    const auto payload_path = temporary / "atlas.bin";
    {
        std::ofstream output(payload_path, std::ios::binary);
        output.write(payload_text.data(), static_cast<std::streamsize>(payload_text.size()));
    }
    auto file_attestation = attest_artifact_file(artifact.value(), payload_path, "factory-service",
                                                 "rotation-7", key, 13, "application/vnd.rbfsafe.atlas");
    CHECK(file_attestation);
    CHECK(file_attestation.value().payload_digest == attestation.value().payload_digest);
    CHECK(verify_artifact_file(artifact.value(), payload_path, file_attestation.value(), "factory-service",
                               "rotation-7", key));

    ArtifactVerificationOptions one_byte;
    one_byte.maximum_payload_bytes = 1;
    auto size_limited = verify_artifact_file(artifact.value(), payload_path, file_attestation.value(),
                                             "factory-service", "rotation-7", key, one_byte);
    CHECK(!size_limited);
    CHECK(size_limited.error().code == StatusCode::ResourceLimit);
    ArtifactVerificationOptions cancelled;
    cancelled.cancellation.cancel();
    auto cancelled_result = verify_artifact_file(artifact.value(), payload_path, file_attestation.value(),
                                                 "factory-service", "rotation-7", key, cancelled);
    CHECK(!cancelled_result);
    CHECK(cancelled_result.error().code == StatusCode::Cancelled);

    const auto attestation_path = temporary / "atlas.attestation.json";
    CHECK(save_artifact_attestation(file_attestation.value(), attestation_path));
    CHECK(!save_artifact_attestation(file_attestation.value(), attestation_path));
    SaveOptions overwrite;
    overwrite.overwrite = true;
    CHECK(save_artifact_attestation(file_attestation.value(), attestation_path, overwrite));
    auto loaded = load_artifact_attestation(attestation_path);
    CHECK(loaded);
    CHECK(loaded.value().id == file_attestation.value().id);
    CHECK(verify_artifact_file(artifact.value(), payload_path, loaded.value(), "factory-service",
                               "rotation-7", key));

    const std::string saved_json = read_text(attestation_path);
    std::string altered_tag_json = saved_json;
    const auto tag_position = altered_tag_json.find(file_attestation.value().authentication_tag);
    CHECK(tag_position != std::string::npos);
    altered_tag_json[tag_position] = altered_tag_json[tag_position] == '0' ? '1' : '0';
    write_text(attestation_path, altered_tag_json);
    auto unauthenticated = load_artifact_attestation(attestation_path);
    CHECK(unauthenticated);
    auto unauthenticated_verification = verify_artifact_file(
        artifact.value(), payload_path, unauthenticated.value(), "factory-service", "rotation-7", key);
    CHECK(!unauthenticated_verification);
    CHECK(unauthenticated_verification.error().code == StatusCode::IdentityMismatch);

    std::string unknown_schema_json = saved_json;
    const auto schema_position = unknown_schema_json.find("\"schema\": 1");
    CHECK(schema_position != std::string::npos);
    unknown_schema_json.replace(schema_position, std::string("\"schema\": 1").size(), "\"schema\": 99");
    write_text(attestation_path, unknown_schema_json);
    auto unknown_schema = load_artifact_attestation(attestation_path);
    CHECK(!unknown_schema);
    CHECK(unknown_schema.error().code == StatusCode::IncompatibleFormat);
    write_text(attestation_path, saved_json);

    auto metadata_limited = load_artifact_attestation(attestation_path, 1);
    CHECK(!metadata_limited);
    CHECK(metadata_limited.error().code == StatusCode::ResourceLimit);

    auto stale = memory.transition(artifact.value().id, artifact.value().generation,
                                   MemoryArtifactState::Stale, "scene changed");
    CHECK(stale);
    auto stale_verification =
        verify_artifact(stale.value(), payload, attestation.value(), "factory-service", "rotation-7", key);
    CHECK(!stale_verification);
    CHECK(stale_verification.error().code == StatusCode::IdentityMismatch);

    CHECK(artifact_authentication_algorithm_name(ArtifactAuthenticationAlgorithm::HmacSha256) ==
          "hmac_sha256");
    std::filesystem::remove_all(temporary);
    return EXIT_SUCCESS;
}
