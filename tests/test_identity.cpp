#include "test_support.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

std::vector<std::byte> hex_bytes(const std::string& text) {
    auto nibble = [](char value) {
        if (value >= '0' && value <= '9')
            return value - '0';
        return value - 'a' + 10;
    };
    std::vector<std::byte> result(text.size() / 2);
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::byte>((nibble(text[index * 2]) << 4) | nibble(text[index * 2 + 1]));
    }
    return result;
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

    const auto seed = hex_bytes("9d61b19deffd5a60ba844af492ec2cc4"
                                "4449c5697b326919703bac031cae7f60");
    const auto expected_public_key = hex_bytes("d75a980182b10ab7d54bfed3c964073a"
                                               "0ee172f3daa62325af021a68f707511a");
    const auto expected_signature = hex_bytes("e5564300c360ac729086e2cc806e828a"
                                              "84877f1eb8e5d974d873e06522490155"
                                              "5fb8821590a33bacc61e39701cf9b46b"
                                              "d25bf5f0595bbe24655141438e7a100b");
    auto key_pair = ed25519_key_pair_from_seed(seed);
    CHECK(key_pair);
    CHECK(!ed25519_key_pair_from_seed(std::span<const std::byte>(seed).first(seed.size() - 1)));
    CHECK(std::equal(key_pair.value().public_key.begin(), key_pair.value().public_key.end(),
                     expected_public_key.begin()));
    auto vector_signature = ed25519_sign({}, key_pair.value().secret_key);
    CHECK(vector_signature);
    CHECK(std::equal(vector_signature.value().begin(), vector_signature.value().end(),
                     expected_signature.begin()));
    CHECK(ed25519_verify({}, expected_signature, expected_public_key));
    auto altered_signature = expected_signature;
    altered_signature[0] ^= std::byte{1};
    CHECK(!ed25519_verify({}, altered_signature, expected_public_key));
    CHECK(!ed25519_verify({},
                          std::span<const std::byte>(expected_signature).first(expected_signature.size() - 1),
                          expected_public_key));

    auto service_key = make_service_public_key("artifact-service", key_pair.value().public_key, 1, 0,
                                               ServiceKeyState::Active, true, true, true);
    CHECK(service_key);
    CHECK(valid_service_public_key(service_key.value()));
    CHECK(service_key.value().id.size() == 64);
    auto root = ServiceTrustBundle::create(1, "", {service_key.value()});
    CHECK(root);
    CHECK(root.value().valid());
    CHECK(root.value().storage_schema() == 2);
    CHECK(root.value().keys()[0].allow_rotate);
    auto no_rotation_key = service_key.value();
    no_rotation_key.allow_rotate = false;
    auto no_rotation_root = ServiceTrustBundle::create(1, "", {no_rotation_key});
    CHECK(no_rotation_root);
    auto no_rotation_successor = rotate_service_trust_bundle(no_rotation_root.value(), {no_rotation_key});
    CHECK(no_rotation_successor);
    CHECK(!authorize_service_trust_bundle_successor(no_rotation_root.value(), no_rotation_successor.value(),
                                                    no_rotation_key.service_id, no_rotation_key.id,
                                                    key_pair.value().secret_key));
    auto bounded_rotation_key = service_key.value();
    bounded_rotation_key.valid_through_sequence = 1;
    auto bounded_rotation_root = ServiceTrustBundle::create(1, "", {bounded_rotation_key});
    CHECK(bounded_rotation_root);
    auto bounded_rotation_successor =
        rotate_service_trust_bundle(bounded_rotation_root.value(), {bounded_rotation_key});
    CHECK(bounded_rotation_successor);
    CHECK(!authorize_service_trust_bundle_successor(
        bounded_rotation_root.value(), bounded_rotation_successor.value(), bounded_rotation_key.service_id,
        bounded_rotation_key.id, key_pair.value().secret_key));

    SafetyMemory memory;
    auto artifact = memory.register_artifact(artifact_input());
    CHECK(artifact);
    const std::string payload_text = "immutable atlas payload\n";
    const auto payload = std::as_bytes(std::span(payload_text.data(), payload_text.size()));

    auto publish =
        prepare_artifact_publish(memory, artifact.value().id, payload, "artifact-service", 21,
                                 "application/vnd.rbfsafe.atlas", ArtifactTransferAuthentication::Ed25519);
    CHECK(publish);
    auto unsigned_receipt = make_artifact_publish_receipt(publish.value(), 7);
    CHECK(unsigned_receipt);
    auto receipt = sign_artifact_publish_receipt(unsigned_receipt.value(), service_key.value().id,
                                                 key_pair.value().secret_key);
    CHECK(receipt);
    CHECK(receipt.value().service_attestation);
    CHECK(receipt.value().service_attestation->algorithm == ArtifactAuthenticationAlgorithm::Ed25519);
    CHECK(receipt.value().service_attestation->authentication_tag.size() == 128);
    auto verified_publish =
        verify_artifact_publish_offline(memory, publish.value(), receipt.value(), payload, root.value());
    CHECK(verified_publish);
    CHECK(valid_verified_artifact_transfer(verified_publish.value()));
    CHECK(verified_publish.value().authentication == ArtifactTransferAuthentication::Ed25519);
    CHECK(verified_publish.value().verification_key_id == service_key.value().id);
    CHECK(verified_publish.value().trust_bundle_id == root.value().id());
    CHECK(!verify_artifact_publish(memory, publish.value(), receipt.value(), payload));
    RemoteArtifactOptions cancelled_options;
    cancelled_options.cancellation.cancel();
    auto cancelled_publish = verify_artifact_publish_offline(memory, publish.value(), receipt.value(),
                                                             payload, root.value(), cancelled_options);
    CHECK(!cancelled_publish);
    CHECK(cancelled_publish.error().code == StatusCode::Cancelled);

    auto fetch_only_key = service_key.value();
    fetch_only_key.allow_publish = false;
    auto fetch_only_bundle = ServiceTrustBundle::create(1, "", {fetch_only_key});
    CHECK(fetch_only_bundle);
    CHECK(!verify_artifact_publish_offline(memory, publish.value(), receipt.value(), payload,
                                           fetch_only_bundle.value()));
    auto expired_key = service_key.value();
    expired_key.valid_through_sequence = 6;
    auto expired_bundle = ServiceTrustBundle::create(1, "", {expired_key});
    CHECK(expired_bundle);
    CHECK(!verify_artifact_publish_offline(memory, publish.value(), receipt.value(), payload,
                                           expired_bundle.value()));

    auto fetch =
        prepare_artifact_fetch(memory, artifact.value().id, "artifact-service", 22,
                               "application/vnd.rbfsafe.atlas", ArtifactTransferAuthentication::Ed25519);
    CHECK(fetch);
    auto unsigned_response = make_artifact_fetch_response(fetch.value(), payload, 7);
    CHECK(unsigned_response);
    auto response = sign_artifact_fetch_response(unsigned_response.value(), service_key.value().id,
                                                 key_pair.value().secret_key);
    CHECK(response);
    auto verified_fetch =
        verify_artifact_fetch_offline(memory, fetch.value(), response.value(), payload, root.value());
    CHECK(verified_fetch);
    CHECK(verified_fetch.value().operation == ArtifactTransferOperation::Fetch);

    auto tampered = response.value();
    tampered.service_attestation->authentication_tag[0] =
        tampered.service_attestation->authentication_tag[0] == '0' ? '1' : '0';
    auto tampered_result =
        verify_artifact_fetch_offline(memory, fetch.value(), tampered, payload, root.value());
    CHECK(!tampered_result);
    CHECK(tampered_result.error().code == StatusCode::IdentityMismatch);

    auto pending_key = service_key.value();
    pending_key.state = ServiceKeyState::Pending;
    auto pending_bundle = ServiceTrustBundle::create(1, "", {pending_key});
    CHECK(pending_bundle);
    CHECK(!verify_artifact_fetch_offline(memory, fetch.value(), response.value(), payload,
                                         pending_bundle.value()));

    std::array<std::byte, kEd25519SeedBytes> second_seed{};
    for (std::size_t index = 0; index < second_seed.size(); ++index)
        second_seed[index] = static_cast<std::byte>(index + 1);
    auto second_pair = ed25519_key_pair_from_seed(second_seed);
    CHECK(second_pair);
    CHECK(!sign_artifact_publish_receipt(unsigned_receipt.value(), service_key.value().id,
                                         second_pair.value().secret_key));
    auto inconsistent_secret = key_pair.value().secret_key;
    inconsistent_secret[0] ^= std::byte{1};
    CHECK(!sign_artifact_publish_receipt(unsigned_receipt.value(), service_key.value().id,
                                         inconsistent_secret));
    auto second_key = make_service_public_key("artifact-service", second_pair.value().public_key, 2, 0,
                                              ServiceKeyState::Active, true, true, true);
    CHECK(second_key);
    auto retired_key = service_key.value();
    retired_key.state = ServiceKeyState::Retired;
    retired_key.valid_through_sequence = 7;
    auto rotated = rotate_service_trust_bundle(root.value(), {retired_key, second_key.value()});
    CHECK(rotated);
    CHECK(rotated.value().sequence() == 2);
    CHECK(rotated.value().parent_id() == root.value().id());
    auto authorization = authorize_service_trust_bundle_successor(
        root.value(), rotated.value(), service_key.value().service_id, service_key.value().id,
        key_pair.value().secret_key);
    CHECK(authorization);
    CHECK(valid_service_trust_bundle_authorization(authorization.value()));
    CHECK(verify_service_trust_bundle_successor(root.value(), rotated.value(), authorization.value()));
    CHECK(!authorize_service_trust_bundle_successor(root.value(), rotated.value(),
                                                    service_key.value().service_id, service_key.value().id,
                                                    second_pair.value().secret_key));
    auto tampered_authorization = authorization.value();
    tampered_authorization.authentication_tag[0] =
        tampered_authorization.authentication_tag[0] == '0' ? '1' : '0';
    CHECK(!valid_service_trust_bundle_authorization(tampered_authorization));
    CHECK(!verify_service_trust_bundle_successor(root.value(), rotated.value(), tampered_authorization));
    CHECK(verify_artifact_fetch_offline(memory, fetch.value(), response.value(), payload, rotated.value()));
    auto expired_response = make_artifact_fetch_response(fetch.value(), payload, 8);
    CHECK(expired_response);
    auto expired_signature = sign_artifact_fetch_response(expired_response.value(), service_key.value().id,
                                                          key_pair.value().secret_key);
    CHECK(expired_signature);
    CHECK(!verify_artifact_fetch_offline(memory, fetch.value(), expired_signature.value(), payload,
                                         rotated.value()));

    auto revoked_key = retired_key;
    revoked_key.state = ServiceKeyState::Revoked;
    auto revoked = rotate_service_trust_bundle(root.value(), {revoked_key, second_key.value()});
    CHECK(revoked);
    CHECK(!verify_service_trust_bundle_successor(root.value(), revoked.value(), authorization.value()));
    CHECK(!verify_artifact_fetch_offline(memory, fetch.value(), response.value(), payload, revoked.value()));
    auto reactivated_key = retired_key;
    reactivated_key.state = ServiceKeyState::Active;
    CHECK(!rotate_service_trust_bundle(rotated.value(), {reactivated_key, second_key.value()}));
    CHECK(!rotate_service_trust_bundle(root.value(), {second_key.value()}));

    ArtifactTransferJournal journal;
    auto journal_record = journal.append(verified_publish.value(), "");
    CHECK(journal_record);
    CHECK(journal.valid());

    const auto temporary = std::filesystem::temp_directory_path() /
                           ("rbfsafe-identity-test-" +
                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temporary);

    auto rotation_key_a = make_service_public_key("rotation-a", key_pair.value().public_key, 1, 0,
                                                  ServiceKeyState::Active, false, false, true);
    auto rotation_key_b = make_service_public_key("rotation-b", second_pair.value().public_key, 1, 0,
                                                  ServiceKeyState::Active, false, false, true);
    CHECK(rotation_key_a);
    CHECK(rotation_key_b);
    ServiceTrustRotationPolicy quorum_policy;
    quorum_policy.minimum_signatures = 2;
    quorum_policy.require_distinct_services = true;
    CHECK(valid_service_trust_rotation_policy(quorum_policy));
    auto same_service_root = ServiceTrustBundle::create_with_rotation_policy(
        1, "", {service_key.value(), second_key.value()}, quorum_policy);
    CHECK(same_service_root);
    auto same_service_successor =
        rotate_service_trust_bundle(same_service_root.value(), same_service_root.value().keys());
    CHECK(same_service_successor);
    auto same_service_authorization_a = authorize_service_trust_bundle_successor(
        same_service_root.value(), same_service_successor.value(), service_key.value().service_id,
        service_key.value().id, key_pair.value().secret_key);
    auto same_service_authorization_b = authorize_service_trust_bundle_successor(
        same_service_root.value(), same_service_successor.value(), second_key.value().service_id,
        second_key.value().id, second_pair.value().secret_key);
    CHECK(same_service_authorization_a);
    CHECK(same_service_authorization_b);
    CHECK(!assemble_service_trust_bundle_authorizations(
        same_service_root.value(), same_service_successor.value(),
        {same_service_authorization_a.value(), same_service_authorization_b.value()}));
    auto quorum_root = ServiceTrustBundle::create_with_rotation_policy(
        1, "", {rotation_key_b.value(), rotation_key_a.value()}, quorum_policy);
    CHECK(quorum_root);
    CHECK(quorum_root.value().storage_schema() == 3);
    CHECK(quorum_root.value().rotation_policy().minimum_signatures == 2);
    CHECK(quorum_root.value().rotation_policy().require_distinct_services);
    CHECK(quorum_root.value().keys()[0].service_id == "rotation-a");
    auto quorum_successor = rotate_service_trust_bundle(quorum_root.value(), quorum_root.value().keys());
    CHECK(quorum_successor);
    CHECK(quorum_successor.value().storage_schema() == 3);
    auto quorum_authorization_a = authorize_service_trust_bundle_successor(
        quorum_root.value(), quorum_successor.value(), rotation_key_a.value().service_id,
        rotation_key_a.value().id, key_pair.value().secret_key);
    auto quorum_authorization_b = authorize_service_trust_bundle_successor(
        quorum_root.value(), quorum_successor.value(), rotation_key_b.value().service_id,
        rotation_key_b.value().id, second_pair.value().secret_key);
    CHECK(quorum_authorization_a);
    CHECK(quorum_authorization_b);
    CHECK(!verify_service_trust_bundle_successor(quorum_root.value(), quorum_successor.value(),
                                                 quorum_authorization_a.value()));
    CHECK(!assemble_service_trust_bundle_authorizations(quorum_root.value(), quorum_successor.value(),
                                                        {quorum_authorization_a.value()}));
    CHECK(!assemble_service_trust_bundle_authorizations(
        quorum_root.value(), quorum_successor.value(),
        {quorum_authorization_a.value(), quorum_authorization_a.value()}));
    auto quorum_authorizations = assemble_service_trust_bundle_authorizations(
        quorum_root.value(), quorum_successor.value(),
        {quorum_authorization_b.value(), quorum_authorization_a.value()});
    CHECK(quorum_authorizations);
    CHECK(valid_service_trust_bundle_authorization_set(quorum_authorizations.value()));
    CHECK(quorum_authorizations.value().authorizations[0].signer_service_id == "rotation-a");
    CHECK(verify_service_trust_bundle_successor(quorum_root.value(), quorum_successor.value(),
                                                quorum_authorizations.value()));
    auto altered_authorization_set = quorum_authorizations.value();
    altered_authorization_set.authorizations.pop_back();
    CHECK(!valid_service_trust_bundle_authorization_set(altered_authorization_set));

    const auto quorum_bundle_path = temporary / "quorum-trust-bundle.json";
    CHECK(quorum_root.value().save(quorum_bundle_path));
    auto loaded_quorum_bundle = ServiceTrustBundle::load(quorum_bundle_path);
    CHECK(loaded_quorum_bundle);
    CHECK(loaded_quorum_bundle.value().id() == quorum_root.value().id());
    CHECK(loaded_quorum_bundle.value().rotation_policy().minimum_signatures == 2);

    const auto quorum_history_path = temporary / "quorum-trust-history";
    auto quorum_history =
        ServiceTrustHistory::create(quorum_history_path, quorum_root.value(), quorum_root.value().id());
    CHECK(quorum_history);
    CHECK(quorum_history.value().storage_schema() == 2);
    auto root_checkpoint_signature_a =
        sign_service_trust_checkpoint(quorum_history.value(), rotation_key_a.value().service_id,
                                      rotation_key_a.value().id, key_pair.value().secret_key);
    auto root_checkpoint_signature_b =
        sign_service_trust_checkpoint(quorum_history.value(), rotation_key_b.value().service_id,
                                      rotation_key_b.value().id, second_pair.value().secret_key);
    CHECK(root_checkpoint_signature_a);
    CHECK(root_checkpoint_signature_b);
    CHECK(!assemble_service_trust_checkpoint(quorum_history.value(), {root_checkpoint_signature_a.value()}));
    auto root_checkpoint = assemble_service_trust_checkpoint(
        quorum_history.value(), {root_checkpoint_signature_b.value(), root_checkpoint_signature_a.value()});
    CHECK(root_checkpoint);
    CHECK(root_checkpoint.value().valid());
    auto quorum_rotation = quorum_history.value().publish(
        quorum_successor.value(), quorum_authorizations.value(), quorum_root.value().id());
    CHECK(quorum_rotation);
    CHECK(quorum_rotation.value().storage_schema == 2);
    CHECK(!quorum_rotation.value().authorization);
    CHECK(quorum_rotation.value().authorization_set);
    CHECK(quorum_rotation.value().authorization_set->authorizations.size() == 2);
    CHECK(!verify_service_trust_checkpoint(quorum_history.value(), root_checkpoint.value(),
                                           root_checkpoint.value().id));

    auto checkpoint_signature_a =
        sign_service_trust_checkpoint(quorum_history.value(), rotation_key_a.value().service_id,
                                      rotation_key_a.value().id, key_pair.value().secret_key);
    auto checkpoint_signature_b =
        sign_service_trust_checkpoint(quorum_history.value(), rotation_key_b.value().service_id,
                                      rotation_key_b.value().id, second_pair.value().secret_key);
    CHECK(checkpoint_signature_a);
    CHECK(checkpoint_signature_b);
    CHECK(!sign_service_trust_checkpoint(quorum_history.value(), rotation_key_a.value().service_id,
                                         rotation_key_a.value().id, second_pair.value().secret_key));
    auto checkpoint = assemble_service_trust_checkpoint(
        quorum_history.value(), {checkpoint_signature_b.value(), checkpoint_signature_a.value()});
    CHECK(checkpoint);
    CHECK(checkpoint.value().signatures[0].signer_service_id == "rotation-a");
    CHECK(verify_service_trust_checkpoint(quorum_history.value(), checkpoint.value(), checkpoint.value().id));
    CHECK(!verify_service_trust_checkpoint(quorum_history.value(), checkpoint.value(), digest('c')));
    const auto checkpoint_path = temporary / "trust-checkpoint.json";
    CHECK(checkpoint.value().save(checkpoint_path));
    CHECK(!checkpoint.value().save(checkpoint_path));
    auto loaded_checkpoint = ServiceTrustCheckpoint::load(checkpoint_path);
    CHECK(loaded_checkpoint);
    CHECK(loaded_checkpoint.value().id == checkpoint.value().id);
    ServiceTrustCheckpointLoadOptions one_signature;
    one_signature.maximum_signatures = 1;
    auto signature_limited_checkpoint = ServiceTrustCheckpoint::load(checkpoint_path, one_signature);
    CHECK(!signature_limited_checkpoint);
    CHECK(signature_limited_checkpoint.error().code == StatusCode::ResourceLimit);
    ServiceTrustCheckpointLoadOptions one_checkpoint_byte;
    one_checkpoint_byte.maximum_payload_bytes = 1;
    auto byte_limited_checkpoint = ServiceTrustCheckpoint::load(checkpoint_path, one_checkpoint_byte);
    CHECK(!byte_limited_checkpoint);
    CHECK(byte_limited_checkpoint.error().code == StatusCode::ResourceLimit);
    const auto saved_checkpoint = read_text(checkpoint_path);
    auto unknown_checkpoint_schema = saved_checkpoint;
    const auto checkpoint_schema_position = unknown_checkpoint_schema.find("\"schema\": 1");
    CHECK(checkpoint_schema_position != std::string::npos);
    unknown_checkpoint_schema.replace(checkpoint_schema_position, std::string("\"schema\": 1").size(),
                                      "\"schema\": 99");
    write_text(checkpoint_path, unknown_checkpoint_schema);
    auto incompatible_checkpoint = ServiceTrustCheckpoint::load(checkpoint_path);
    CHECK(!incompatible_checkpoint);
    CHECK(incompatible_checkpoint.error().code == StatusCode::IncompatibleFormat);
    auto corrupt_checkpoint = saved_checkpoint;
    auto checkpoint_id_position = corrupt_checkpoint.find("\"id\": \"");
    CHECK(checkpoint_id_position != std::string::npos);
    checkpoint_id_position += std::string("\"id\": \"").size();
    corrupt_checkpoint[checkpoint_id_position] =
        corrupt_checkpoint[checkpoint_id_position] == '0' ? '1' : '0';
    write_text(checkpoint_path, corrupt_checkpoint);
    auto corrupted_checkpoint_load = ServiceTrustCheckpoint::load(checkpoint_path);
    CHECK(!corrupted_checkpoint_load);
    CHECK(corrupted_checkpoint_load.error().code == StatusCode::CorruptData);
    write_text(checkpoint_path, saved_checkpoint.substr(0, saved_checkpoint.size() / 2));
    auto truncated_checkpoint = ServiceTrustCheckpoint::load(checkpoint_path);
    CHECK(!truncated_checkpoint);
    CHECK(truncated_checkpoint.error().code == StatusCode::CorruptData);
    write_text(checkpoint_path, saved_checkpoint);
    auto checkpoint_opened =
        ServiceTrustHistory::open(quorum_history_path, quorum_root.value().id(), loaded_checkpoint.value(),
                                  loaded_checkpoint.value().id);
    CHECK(checkpoint_opened);
    CHECK(checkpoint_opened.value().current_bundle_id() == quorum_successor.value().id());
    ServiceTrustHistoryLoadOptions one_rotation_signature;
    one_rotation_signature.maximum_signatures_per_rotation = 1;
    auto signature_limited_history = ServiceTrustHistory::open(
        quorum_history_path, quorum_root.value().id(), quorum_successor.value().id(), one_rotation_signature);
    CHECK(!signature_limited_history);
    CHECK(signature_limited_history.error().code == StatusCode::ResourceLimit);
    std::filesystem::path quorum_authorization_record_path;
    for (const auto& entry : std::filesystem::directory_iterator(quorum_history_path / "records")) {
        if (entry.path().filename().string().starts_with("00000000000000000002-"))
            quorum_authorization_record_path = entry.path();
    }
    CHECK(!quorum_authorization_record_path.empty());
    const auto saved_quorum_authorization_record = read_text(quorum_authorization_record_path);
    auto corrupt_quorum_authorization_record = saved_quorum_authorization_record;
    auto quorum_tag_position = corrupt_quorum_authorization_record.find("\"authentication_tag\": \"");
    CHECK(quorum_tag_position != std::string::npos);
    quorum_tag_position += std::string("\"authentication_tag\": \"").size();
    corrupt_quorum_authorization_record[quorum_tag_position] =
        corrupt_quorum_authorization_record[quorum_tag_position] == '0' ? '1' : '0';
    write_text(quorum_authorization_record_path, corrupt_quorum_authorization_record);
    auto corrupted_quorum_history = ServiceTrustHistory::open(quorum_history_path, quorum_root.value().id(),
                                                              quorum_successor.value().id());
    CHECK(!corrupted_quorum_history);
    CHECK(corrupted_quorum_history.error().code == StatusCode::CorruptData);
    write_text(quorum_authorization_record_path, saved_quorum_authorization_record);
    CHECK(!ServiceTrustHistory::open(quorum_history_path, quorum_root.value().id(), root_checkpoint.value(),
                                     root_checkpoint.value().id));
    auto tampered_checkpoint = loaded_checkpoint.value();
    tampered_checkpoint.signatures[0].authentication_tag[0] =
        tampered_checkpoint.signatures[0].authentication_tag[0] == '0' ? '1' : '0';
    CHECK(!tampered_checkpoint.valid());

    const auto history_path = temporary / "trust-history";
    CHECK(!ServiceTrustHistory::create(history_path, root.value(), digest('f')));
    auto history = ServiceTrustHistory::create(history_path, root.value(), root.value().id());
    CHECK(history);
    CHECK(history.value().valid());
    CHECK(history.value().current_bundle_id() == root.value().id());
    CHECK(history.value().root_bundle_id() == root.value().id());
    CHECK(history.value().records().size() == 1);
    CHECK(history.value().records()[0].type == ServiceTrustRotationEventType::RootPinned);
    CHECK(!ServiceTrustHistory::create(history_path, root.value(), root.value().id()));
    CHECK(!history.value().publish(rotated.value(), authorization.value(), digest('e')));
    auto first_rotation = history.value().publish(rotated.value(), authorization.value(), root.value().id());
    CHECK(first_rotation);
    CHECK(first_rotation.value().type == ServiceTrustRotationEventType::SuccessorAuthorized);
    CHECK(first_rotation.value().authorization);
    CHECK(history.value().current_bundle_id() == rotated.value().id());

    auto successor = rotate_service_trust_bundle(rotated.value(), rotated.value().keys());
    CHECK(successor);
    auto successor_authorization = authorize_service_trust_bundle_successor(
        rotated.value(), successor.value(), second_key.value().service_id, second_key.value().id,
        second_pair.value().secret_key);
    CHECK(successor_authorization);
    std::filesystem::create_directory(history_path / ".writer-lock");
    auto locked =
        history.value().publish(successor.value(), successor_authorization.value(), rotated.value().id());
    CHECK(!locked);
    CHECK(locked.error().code == StatusCode::ResourceLimit);
    std::filesystem::remove_all(history_path / ".writer-lock");
    auto second_rotation =
        history.value().publish(successor.value(), successor_authorization.value(), rotated.value().id());
    CHECK(second_rotation);
    CHECK(history.value().records().size() == 3);
    CHECK(history.value().current_bundle().value().id() == successor.value().id());
    CHECK(history.value().bundle(root.value().id()).value().id() == root.value().id());
    CHECK(!history.value().bundle(digest('d')));

    auto opened_history = ServiceTrustHistory::open(history_path, root.value().id(), successor.value().id());
    CHECK(opened_history);
    CHECK(opened_history.value().valid());
    CHECK(opened_history.value().records()[1].id == first_rotation.value().id);
    CHECK(!ServiceTrustHistory::open(history_path, digest('f'), successor.value().id()));
    auto rollback_detected = ServiceTrustHistory::open(history_path, root.value().id(), rotated.value().id());
    CHECK(!rollback_detected);
    CHECK(rollback_detected.error().code == StatusCode::IdentityMismatch);

    ServiceTrustHistoryLoadOptions bundle_limited;
    bundle_limited.maximum_bundles = 2;
    auto limited_history =
        ServiceTrustHistory::open(history_path, root.value().id(), successor.value().id(), bundle_limited);
    CHECK(!limited_history);
    CHECK(limited_history.error().code == StatusCode::ResourceLimit);
    ServiceTrustHistoryLoadOptions key_limited_history;
    key_limited_history.maximum_keys_per_bundle = 1;
    limited_history = ServiceTrustHistory::open(history_path, root.value().id(), successor.value().id(),
                                                key_limited_history);
    CHECK(!limited_history);
    CHECK(limited_history.error().code == StatusCode::ResourceLimit);
    ServiceTrustHistoryLoadOptions total_key_limited;
    total_key_limited.maximum_total_keys = 2;
    limited_history =
        ServiceTrustHistory::open(history_path, root.value().id(), successor.value().id(), total_key_limited);
    CHECK(!limited_history);
    CHECK(limited_history.error().code == StatusCode::ResourceLimit);
    ServiceTrustHistoryLoadOptions metadata_limited;
    metadata_limited.maximum_metadata_bytes = 1;
    limited_history =
        ServiceTrustHistory::open(history_path, root.value().id(), successor.value().id(), metadata_limited);
    CHECK(!limited_history);
    CHECK(limited_history.error().code == StatusCode::ResourceLimit);
    ServiceTrustHistoryLoadOptions bundle_bytes_limited;
    bundle_bytes_limited.maximum_bundle_bytes = 1;
    limited_history = ServiceTrustHistory::open(history_path, root.value().id(), successor.value().id(),
                                                bundle_bytes_limited);
    CHECK(!limited_history);
    CHECK(limited_history.error().code == StatusCode::ResourceLimit);
    ServiceTrustHistoryLoadOptions cancelled_history_options;
    cancelled_history_options.cancellation.cancel();
    auto cancelled_history = ServiceTrustHistory::open(history_path, root.value().id(),
                                                       successor.value().id(), cancelled_history_options);
    CHECK(!cancelled_history);
    CHECK(cancelled_history.error().code == StatusCode::Cancelled);

    const auto history_manifest_path = history_path / "manifest.json";
    const auto saved_history_manifest = read_text(history_manifest_path);
    auto incompatible_history_manifest = saved_history_manifest;
    const auto history_schema_position = incompatible_history_manifest.find("\"schema\": 1");
    CHECK(history_schema_position != std::string::npos);
    incompatible_history_manifest.replace(history_schema_position, std::string("\"schema\": 1").size(),
                                          "\"schema\": 99");
    write_text(history_manifest_path, incompatible_history_manifest);
    auto incompatible_history =
        ServiceTrustHistory::open(history_path, root.value().id(), successor.value().id());
    CHECK(!incompatible_history);
    CHECK(incompatible_history.error().code == StatusCode::IncompatibleFormat);
    write_text(history_manifest_path, saved_history_manifest);

    std::filesystem::path authorization_record_path;
    for (const auto& entry : std::filesystem::directory_iterator(history_path / "records")) {
        if (entry.path().filename().string().starts_with("00000000000000000002-"))
            authorization_record_path = entry.path();
    }
    CHECK(!authorization_record_path.empty());
    const auto saved_authorization_record = read_text(authorization_record_path);
    auto corrupted_authorization_record = saved_authorization_record;
    auto tag_position = corrupted_authorization_record.find("\"authentication_tag\": \"");
    CHECK(tag_position != std::string::npos);
    tag_position += std::string("\"authentication_tag\": \"").size();
    corrupted_authorization_record[tag_position] =
        corrupted_authorization_record[tag_position] == '0' ? '1' : '0';
    write_text(authorization_record_path, corrupted_authorization_record);
    auto corrupted_history =
        ServiceTrustHistory::open(history_path, root.value().id(), successor.value().id());
    CHECK(!corrupted_history);
    CHECK(corrupted_history.error().code == StatusCode::CorruptData);
    write_text(authorization_record_path, saved_authorization_record);

    std::filesystem::path newest_record_path;
    for (const auto& entry : std::filesystem::directory_iterator(history_path / "records")) {
        if (entry.path().filename().string().starts_with("00000000000000000003-"))
            newest_record_path = entry.path();
    }
    CHECK(!newest_record_path.empty());
    const auto saved_newest_record = read_text(newest_record_path);
    std::filesystem::remove(newest_record_path);
    auto directory_rollback =
        ServiceTrustHistory::open(history_path, root.value().id(), successor.value().id());
    CHECK(!directory_rollback);
    CHECK(directory_rollback.error().code == StatusCode::IdentityMismatch);
    write_text(newest_record_path, saved_newest_record);

    const auto unexpected_record_entry = history_path / "records" / "unexpected";
    write_text(unexpected_record_entry, "unexpected");
    corrupted_history = ServiceTrustHistory::open(history_path, root.value().id(), successor.value().id());
    CHECK(!corrupted_history);
    CHECK(corrupted_history.error().code == StatusCode::CorruptData);
    std::filesystem::remove(unexpected_record_entry);

    const auto bundle_path = temporary / "trust-bundle.json";
    CHECK(rotated.value().save(bundle_path));
    CHECK(!rotated.value().save(bundle_path));
    auto loaded = ServiceTrustBundle::load(bundle_path);
    CHECK(loaded);
    CHECK(loaded.value().id() == rotated.value().id());
    CHECK(loaded.value().keys().size() == 2);
    ServiceTrustBundleLoadOptions one_key;
    one_key.maximum_keys = 1;
    auto key_limited = ServiceTrustBundle::load(bundle_path, one_key);
    CHECK(!key_limited);
    CHECK(key_limited.error().code == StatusCode::ResourceLimit);
    ServiceTrustBundleLoadOptions one_byte;
    one_byte.maximum_payload_bytes = 1;
    auto byte_limited = ServiceTrustBundle::load(bundle_path, one_byte);
    CHECK(!byte_limited);
    CHECK(byte_limited.error().code == StatusCode::ResourceLimit);

    const auto saved_bundle = read_text(bundle_path);
    auto unknown_schema = saved_bundle;
    const auto schema_position = unknown_schema.find("\"schema\": 2");
    CHECK(schema_position != std::string::npos);
    unknown_schema.replace(schema_position, std::string("\"schema\": 2").size(), "\"schema\": 99");
    write_text(bundle_path, unknown_schema);
    auto incompatible = ServiceTrustBundle::load(bundle_path);
    CHECK(!incompatible);
    CHECK(incompatible.error().code == StatusCode::IncompatibleFormat);
    write_text(bundle_path, saved_bundle);

    auto fixed_bundle = ServiceTrustBundle::load(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) /
                                                 "service_trust_bundle_schema1" / "bundle.json");
    CHECK(fixed_bundle);
    CHECK(fixed_bundle.value().id() == "b6f6e30bc2245e64a519c8a02e61063bdf2fe1d8dc5ee35d980f46e1e4aa584d");
    CHECK(fixed_bundle.value().storage_schema() == 1);
    CHECK(fixed_bundle.value().keys().size() == 1);
    CHECK(fixed_bundle.value().keys()[0].id ==
          "deeb721ce417eb73af26ccc9a69a2a8c4343dada00e2e6f3864084c1e328e732");
    CHECK(fixed_bundle.value().keys()[0].state == ServiceKeyState::Active);
    CHECK(!fixed_bundle.value().keys()[0].allow_rotate);
    const auto legacy_roundtrip_path = temporary / "legacy-trust-bundle.json";
    CHECK(fixed_bundle.value().save(legacy_roundtrip_path));
    auto legacy_roundtrip = ServiceTrustBundle::load(legacy_roundtrip_path);
    CHECK(legacy_roundtrip);
    CHECK(legacy_roundtrip.value().storage_schema() == 1);
    CHECK(legacy_roundtrip.value().id() == fixed_bundle.value().id());
    CHECK(!ServiceTrustHistory::create(temporary / "legacy-history", fixed_bundle.value(),
                                       fixed_bundle.value().id()));
    auto fixed_schema2_bundle = ServiceTrustBundle::load(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) /
                                                         "service_trust_bundle_schema2" / "bundle.json");
    CHECK(fixed_schema2_bundle);
    CHECK(fixed_schema2_bundle.value().storage_schema() == 2);
    CHECK(fixed_schema2_bundle.value().id() ==
          "9e30c7b54a023db15fdb3751592e4291f6714f1751183860e1013d486c0357bd");
    CHECK(fixed_schema2_bundle.value().keys()[0].allow_rotate);
    auto fixed_history = ServiceTrustHistory::open(
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "service_trust_history_schema1",
        "9e30c7b54a023db15fdb3751592e4291f6714f1751183860e1013d486c0357bd",
        "4c119f290036039ce28f4c8b8d8db572a7950cdf28e907153ef4c02445afad3b");
    CHECK(fixed_history);
    CHECK(fixed_history.value().records().size() == 2);
    CHECK(fixed_history.value().records()[0].id ==
          "42f9b146a215bf9268a6ba4f2899df24d24a94e9a3ec8ae1b61a254e2ad2f0f5");
    CHECK(fixed_history.value().records()[1].id ==
          "07603ec3d963c68c102a8d0da980207363030d102e1be2ac21de2874db10b07a");
    CHECK(fixed_history.value().records()[1].authorization);
    CHECK(fixed_history.value().records()[1].authorization->id ==
          "68debbfc4156e5c829d641e5b9ed4aeab04174454d655d5558783f81fc8711d3");
    auto fixed_schema3_bundle = ServiceTrustBundle::load(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) /
                                                         "service_trust_bundle_schema3" / "bundle.json");
    CHECK(fixed_schema3_bundle);
    CHECK(fixed_schema3_bundle.value().storage_schema() == 3);
    CHECK(fixed_schema3_bundle.value().id() ==
          "e9126145264ac126845b17db5db782a43a4816d5d4ca3a9d4f9462d874e7b89b");
    CHECK(fixed_schema3_bundle.value().rotation_policy().minimum_signatures == 2);
    CHECK(fixed_schema3_bundle.value().rotation_policy().require_distinct_services);
    auto fixed_checkpoint =
        ServiceTrustCheckpoint::load(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) /
                                     "service_trust_checkpoint_schema1" / "checkpoint.json");
    CHECK(fixed_checkpoint);
    CHECK(fixed_checkpoint.value().id == "9cbf2bfad11354201ec0eb79dd1f11cf78925e4ea917cc3a7e5d15f6307a2e24");
    auto fixed_quorum_history = ServiceTrustHistory::open(
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "service_trust_history_schema2",
        fixed_schema3_bundle.value().id(), fixed_checkpoint.value(), fixed_checkpoint.value().id);
    CHECK(fixed_quorum_history);
    CHECK(fixed_quorum_history.value().storage_schema() == 2);
    CHECK(fixed_quorum_history.value().current_bundle_id() ==
          "d31c074a89a038167c34b1a65934186c0696aff196165dbdac67d0839cd34fb6");
    CHECK(fixed_quorum_history.value().records()[1].authorization_set);
    CHECK(fixed_quorum_history.value().records()[1].authorization_set->id ==
          "ee8d89a646c7b3acb4d94c7f321ad6dcdfc96a2347a62ff1d0c17e8b4df61870");

    auto fixed_journal = ArtifactTransferJournal::load(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) /
                                                       "artifact_transfer_journal_schema2");
    CHECK(fixed_journal);
    CHECK(fixed_journal.value().identity() ==
          "43197ea62038f190add715bfcb0d60d9d14c118f2713e540e2716fd9daa42800");
    CHECK(fixed_journal.value().records().size() == 1);
    CHECK(fixed_journal.value().records()[0].transfer.trust_bundle_id == fixed_bundle.value().id());
    CHECK(fixed_journal.value().records()[0].transfer.verification_key_id ==
          fixed_bundle.value().keys()[0].id);

    CHECK(artifact_authentication_algorithm_name(ArtifactAuthenticationAlgorithm::Ed25519) == "ed25519");
    CHECK(artifact_transfer_authentication_name(ArtifactTransferAuthentication::Ed25519) == "ed25519");
    CHECK(service_key_state_name(ServiceKeyState::Revoked) == "revoked");
    CHECK(service_trust_rotation_event_type_name(ServiceTrustRotationEventType::SuccessorAuthorized) ==
          "successor_authorized");

    std::filesystem::remove_all(temporary);
    return EXIT_SUCCESS;
}
