#include "test_support.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    const std::string altered_text = "altered atlas payload\n";
    const auto altered = std::as_bytes(std::span(altered_text.data(), altered_text.size()));
    std::array<std::byte, 32> key{};
    for (std::size_t index = 0; index < key.size(); ++index)
        key[index] = static_cast<std::byte>(index + 1);
    std::array<std::byte, 32> wrong_key{};
    wrong_key.fill(std::byte{0x7f});

    auto publish = prepare_artifact_publish(memory, artifact.value().id, payload, "artifact-service", 10,
                                            "application/vnd.rbfsafe.atlas");
    CHECK(publish);
    CHECK(valid_artifact_publish_request(publish.value()));
    CHECK(publish.value().memory_id == memory.identity());
    CHECK(publish.value().payload_digest == kPayloadDigest);

    auto unsigned_receipt = make_artifact_publish_receipt(publish.value(), 101);
    CHECK(unsigned_receipt);
    auto receipt = authenticate_artifact_publish_receipt(unsigned_receipt.value(), "service-key-1", key);
    CHECK(receipt);
    CHECK(valid_artifact_publish_receipt(receipt.value()));
    auto verified_publish =
        verify_artifact_publish(memory, publish.value(), receipt.value(), payload, "service-key-1", key);
    CHECK(verified_publish);
    CHECK(valid_verified_artifact_transfer(verified_publish.value()));
    CHECK(verified_publish.value().operation == ArtifactTransferOperation::Publish);
    CHECK(verified_publish.value().authentication == ArtifactTransferAuthentication::HmacSha256);
    CHECK(verified_publish.value().attestation_id == receipt.value().service_attestation->id);

    auto fetch = prepare_artifact_fetch(memory, artifact.value().id, "artifact-service", 11,
                                        "application/vnd.rbfsafe.atlas");
    CHECK(fetch);
    CHECK(valid_artifact_fetch_request(fetch.value()));
    auto unsigned_response = make_artifact_fetch_response(fetch.value(), payload, 102);
    CHECK(unsigned_response);
    auto response = authenticate_artifact_fetch_response(unsigned_response.value(), "service-key-1", key);
    CHECK(response);
    CHECK(valid_artifact_fetch_response(response.value()));
    auto verified_fetch =
        verify_artifact_fetch(memory, fetch.value(), response.value(), payload, "service-key-1", key);
    CHECK(verified_fetch);
    CHECK(verified_fetch.value().operation == ArtifactTransferOperation::Fetch);
    CHECK(verified_fetch.value().payload_digest == kPayloadDigest);
    auto missing_attestation = verify_artifact_fetch(memory, fetch.value(), unsigned_response.value(),
                                                     payload, "service-key-1", key);
    CHECK(!missing_attestation);
    CHECK(missing_attestation.error().code == StatusCode::IdentityMismatch);

    auto tampered_authentication = response.value();
    tampered_authentication.service_attestation->authentication_tag[0] =
        tampered_authentication.service_attestation->authentication_tag[0] == '0' ? '1' : '0';
    auto tampered =
        verify_artifact_fetch(memory, fetch.value(), tampered_authentication, payload, "service-key-1", key);
    CHECK(!tampered);
    CHECK(tampered.error().code == StatusCode::IdentityMismatch);

    auto replay_request = prepare_artifact_fetch(memory, artifact.value().id, "artifact-service", 16,
                                                 "application/vnd.rbfsafe.atlas");
    CHECK(replay_request);
    auto replay_response = make_artifact_fetch_response(replay_request.value(), payload, 102);
    CHECK(replay_response);
    replay_response.value().service_attestation = response.value().service_attestation;
    auto replayed = verify_artifact_fetch(memory, replay_request.value(), replay_response.value(), payload,
                                          "service-key-1", key);
    CHECK(!replayed);
    CHECK(replayed.error().code == StatusCode::IdentityMismatch);

    ArtifactTransferJournal journal;
    CHECK(journal.valid());
    const auto empty_identity = journal.identity();
    CHECK(empty_identity.size() == 64);
    auto first = journal.append(verified_publish.value(), "");
    CHECK(first);
    CHECK(first.value().sequence == 1);
    CHECK(first.value().parent_id.empty());
    CHECK(!journal.append(verified_fetch.value(), ""));
    auto second = journal.append(verified_fetch.value(), journal.current_record_id());
    CHECK(second);
    CHECK(second.value().sequence == 2);
    CHECK(second.value().parent_id == first.value().id);
    CHECK(journal.identity() == second.value().id);
    CHECK(journal.valid());

    auto unauthenticated_publish =
        prepare_artifact_publish(memory, artifact.value().id, payload, "isolated-service", 12,
                                 "application/octet-stream", ArtifactTransferAuthentication::None);
    CHECK(unauthenticated_publish);
    auto unauthenticated_receipt = make_artifact_publish_receipt(unauthenticated_publish.value(), 1);
    CHECK(unauthenticated_receipt);
    auto unauthenticated = verify_artifact_publish(memory, unauthenticated_publish.value(),
                                                   unauthenticated_receipt.value(), payload);
    CHECK(unauthenticated);
    CHECK(unauthenticated.value().authentication == ArtifactTransferAuthentication::None);
    CHECK(unauthenticated.value().attestation_id.empty());
    CHECK(!verify_artifact_publish(memory, unauthenticated_publish.value(), unauthenticated_receipt.value(),
                                   payload, "unexpected-key", key));

    CHECK(!prepare_artifact_publish(memory, artifact.value().id, altered, "artifact-service", 13,
                                    "application/octet-stream"));
    RemoteArtifactOptions one_byte;
    one_byte.maximum_payload_bytes = 1;
    auto limited = prepare_artifact_publish(memory, artifact.value().id, payload, "artifact-service", 14,
                                            "application/octet-stream",
                                            ArtifactTransferAuthentication::HmacSha256, one_byte);
    CHECK(!limited);
    CHECK(limited.error().code == StatusCode::ResourceLimit);
    RemoteArtifactOptions cancelled;
    cancelled.cancellation.cancel();
    auto cancelled_fetch = prepare_artifact_fetch(memory, artifact.value().id, "artifact-service", 15,
                                                  "application/octet-stream",
                                                  ArtifactTransferAuthentication::HmacSha256, cancelled);
    CHECK(!cancelled_fetch);
    CHECK(cancelled_fetch.error().code == StatusCode::Cancelled);

    auto wrong_authentication =
        verify_artifact_fetch(memory, fetch.value(), response.value(), payload, "service-key-1", wrong_key);
    CHECK(!wrong_authentication);
    CHECK(wrong_authentication.error().code == StatusCode::IdentityMismatch);
    auto wrong_payload =
        verify_artifact_fetch(memory, fetch.value(), response.value(), altered, "service-key-1", key);
    CHECK(!wrong_payload);
    CHECK(wrong_payload.error().code == StatusCode::IdentityMismatch);

    auto wrong_attestation_response = response.value();
    wrong_attestation_response.service_attestation = receipt.value().service_attestation;
    auto wrong_service = verify_artifact_fetch(memory, fetch.value(), wrong_attestation_response, payload,
                                               "service-key-1", key);
    CHECK(!wrong_service);
    CHECK(wrong_service.error().code == StatusCode::IdentityMismatch);

    auto malformed_response = response.value();
    malformed_response.payload_bytes += 1;
    auto malformed =
        verify_artifact_fetch(memory, fetch.value(), malformed_response, payload, "service-key-1", key);
    CHECK(!malformed);
    CHECK(malformed.error().code == StatusCode::CorruptData);

    const auto temporary = std::filesystem::temp_directory_path() /
                           ("rbfsafe-remote-test-" +
                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto journal_path = temporary / "journal";
    CHECK(journal.save(journal_path));
    CHECK(!journal.save(journal_path));
    auto loaded = ArtifactTransferJournal::load(journal_path);
    CHECK(loaded);
    CHECK(loaded.value().identity() == journal.identity());
    CHECK(loaded.value().records().size() == 2);
    CHECK(loaded.value().records()[1].transfer.id == verified_fetch.value().id);

    auto fixed = ArtifactTransferJournal::load(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) /
                                               "artifact_transfer_journal_schema1");
    CHECK(fixed);
    CHECK(fixed.value().identity() == "28a05f88667cf3c46fe2748cc9aff0e4a7aaebd3d267d649d708077536ac74bd");
    CHECK(fixed.value().records().size() == 1);
    CHECK(fixed.value().records()[0].transfer.id ==
          "c060045892043c6bc9d10a9ddc1a8b223c568fc9a67f00e26715dc2427068718");

    ArtifactTransferJournalLoadOptions one_record;
    one_record.maximum_records = 1;
    auto record_limited = ArtifactTransferJournal::load(journal_path, one_record);
    CHECK(!record_limited);
    CHECK(record_limited.error().code == StatusCode::ResourceLimit);
    ArtifactTransferJournalLoadOptions one_metadata_byte;
    one_metadata_byte.maximum_payload_bytes = 1;
    auto byte_limited = ArtifactTransferJournal::load(journal_path, one_metadata_byte);
    CHECK(!byte_limited);
    CHECK(byte_limited.error().code == StatusCode::ResourceLimit);

    const auto records_path = journal_path / "records.json";
    const auto saved_records = read_text(records_path);
    auto corrupted_records = saved_records;
    const auto digest_position = corrupted_records.find(verified_fetch.value().payload_digest);
    CHECK(digest_position != std::string::npos);
    corrupted_records[digest_position] = corrupted_records[digest_position] == '0' ? '1' : '0';
    write_text(records_path, corrupted_records);
    auto checksum_failure = ArtifactTransferJournal::load(journal_path);
    CHECK(!checksum_failure);
    CHECK(checksum_failure.error().code == StatusCode::CorruptData);
    write_text(records_path, saved_records);

    const auto manifest_path = journal_path / "manifest.json";
    const auto saved_manifest = read_text(manifest_path);
    auto unknown_schema = saved_manifest;
    const auto schema_position = unknown_schema.find("\"schema\": 1");
    CHECK(schema_position != std::string::npos);
    unknown_schema.replace(schema_position, std::string("\"schema\": 1").size(), "\"schema\": 99");
    write_text(manifest_path, unknown_schema);
    auto incompatible = ArtifactTransferJournal::load(journal_path);
    CHECK(!incompatible);
    CHECK(incompatible.error().code == StatusCode::IncompatibleFormat);
    write_text(manifest_path, saved_manifest);

    auto stale = memory.transition(artifact.value().id, artifact.value().generation,
                                   MemoryArtifactState::Stale, "scene changed");
    CHECK(stale);
    auto lifecycle_mismatch =
        verify_artifact_fetch(memory, fetch.value(), response.value(), payload, "service-key-1", key);
    CHECK(!lifecycle_mismatch);
    CHECK(lifecycle_mismatch.error().code == StatusCode::IdentityMismatch);

    CHECK(artifact_transfer_operation_name(ArtifactTransferOperation::Fetch) == "fetch");
    CHECK(artifact_transfer_authentication_name(ArtifactTransferAuthentication::HmacSha256) == "hmac_sha256");
    std::filesystem::remove_all(temporary);
    return EXIT_SUCCESS;
}
