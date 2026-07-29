#include "test_support.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::filesystem::path temporary_directory() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path =
        std::filesystem::temp_directory_path() / ("rbfsafe-coordination-test-" + std::to_string(nonce));
    std::filesystem::create_directories(path);
    return path;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace

int main() {
    using namespace rbfsafe;

    const auto occupancy_path = std::filesystem::path(RBFSAFE_TEST_DATA_DIR) /
                                "continuous_fleet_occupancy_schema2" / "occupancy.json";
    const auto legacy_occupancy_path = std::filesystem::path(RBFSAFE_TEST_DATA_DIR) /
                                       "continuous_fleet_occupancy_schema1" / "occupancy.json";
    std::array<std::byte, kEd25519SeedBytes> seed{};
    for (std::size_t index = 0; index < seed.size(); ++index)
        seed[index] = static_cast<std::byte>(index + 1);
    auto key_pair = ed25519_key_pair_from_seed(seed);
    CHECK(key_pair);
    auto key = make_service_public_key("fixture-occupancy-publisher", key_pair.value().public_key, 1, 0,
                                       ServiceKeyState::Active, false, true, false);
    CHECK(key);
    auto trust_bundle = ServiceTrustBundle::create(1, "", {key.value()});
    CHECK(trust_bundle);

    const auto fixture_directory =
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "occupancy_publication_schema1";
    auto fixture_publication = OccupancyPublication::load(fixture_directory / "publication.json");
    CHECK(fixture_publication);
    CHECK(fixture_publication.value().id ==
          "90f3620a182c6f34088cfc1b4cc15a676eeed9d69ea37222b4a04a0ddc494251");
    auto fixture_trust_bundle = ServiceTrustBundle::load(fixture_directory / "trust-bundle.json");
    CHECK(fixture_trust_bundle);
    CHECK(fixture_trust_bundle.value().id() ==
          "89e2700e95a4558f0e238a1b505f92ecbccf5435c3a263c485a086a6daf8661d");
    auto fixture_verification = verify_continuous_fleet_occupancy_publication(
        occupancy_path, fixture_publication.value(), fixture_trust_bundle.value(),
        "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher", fixture_trust_bundle.value().id(),
        "", 16);
    CHECK(fixture_verification);
    CHECK(fixture_verification.value().id ==
          "9910e71348c6b69609bb2026e7a7f926d27bca243bc2140a0259b7ece9d8fe09");

    const auto history_fixture_directory =
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "occupancy_publication_history_schema1";
    auto history_fixture = OccupancyPublicationHistory::open(
        history_fixture_directory, "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher",
        fixture_trust_bundle.value().id(), fixture_publication.value().id,
        "83a6952083ac661aacff43168473c1938e29adfe738275d2230458dd6074dfb9");
    CHECK(history_fixture);
    CHECK(history_fixture.value().records().size() == 2);
    CHECK(history_fixture.value().records().front().id ==
          "d61cfb66d7473dd731559758e6f6b29327f9e326d3346817dd05c9db95b4cedb");
    CHECK(history_fixture.value().records().back().id ==
          "34fad28d5893818a1cfd79e3195e9ff757fff1764fed10fae326e7f4ef12fcf9");
    CHECK(history_fixture.value().verify(history_fixture.value().current_publication_id(), 31));

    auto root = sign_continuous_fleet_occupancy_publication(
        occupancy_path, trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", key.value().id, key_pair.value().secret_key, 1, "", 0, 32);
    CHECK(root);
    CHECK(root.value().valid());
    CHECK(root.value().publisher_sequence == 1);
    CHECK(root.value().parent_publication_id.empty());
    CHECK(root.value().occupancy_bundle_id ==
          "6030e3574db5634f60b6cf04ffc325077f944ef23d256ef7cc937fe857dce8d0");
    CHECK(root.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!root.value().authorizes_execution());

    auto verified = verify_continuous_fleet_occupancy_publication(
        occupancy_path, root.value(), trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", trust_bundle.value().id(), "", 16);
    CHECK(verified);
    CHECK(verified.value().valid());
    CHECK(verified.value().publication_id == root.value().id);
    CHECK(verified.value().evaluation_tick == 16);
    CHECK(verified.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!verified.value().authorizes_execution());

    const auto temporary = temporary_directory();
    const auto publication_path = temporary / "publication.json";
    CHECK(root.value().save(publication_path));
    CHECK(!root.value().save(publication_path));
    auto loaded = OccupancyPublication::load(publication_path);
    CHECK(loaded);
    CHECK(loaded.value().id == root.value().id);
    SaveOptions overwrite;
    overwrite.overwrite = true;
    CHECK(root.value().save(publication_path, overwrite));
    CHECK(!OccupancyPublication::load(publication_path, 16));
    const auto publication_text = read_text(publication_path);
    auto checksum_tamper = publication_text;
    const auto checksum_position = checksum_tamper.find("\"checksum\": \"");
    CHECK(checksum_position != std::string::npos);
    const auto digest_position = checksum_position + 13;
    checksum_tamper[digest_position] = checksum_tamper[digest_position] == 'a' ? 'b' : 'a';
    const auto tamper_path = temporary / "tamper.json";
    write_text(tamper_path, checksum_tamper);
    auto tamper_rejected = OccupancyPublication::load(tamper_path);
    CHECK(!tamper_rejected);
    CHECK(tamper_rejected.error().code == StatusCode::CorruptData);

    auto unknown_schema = publication_text;
    const auto schema_position = unknown_schema.rfind("\"schema\": 1");
    CHECK(schema_position != std::string::npos);
    unknown_schema.replace(schema_position, 11, "\"schema\": 2");
    const auto schema_path = temporary / "schema.json";
    write_text(schema_path, unknown_schema);
    auto schema_rejected = OccupancyPublication::load(schema_path);
    CHECK(!schema_rejected);
    CHECK(schema_rejected.error().code == StatusCode::IncompatibleFormat);

    const auto truncated_path = temporary / "truncated.json";
    write_text(truncated_path, publication_text.substr(0, publication_text.size() / 2));
    CHECK(!OccupancyPublication::load(truncated_path));

    const auto publication_symlink = temporary / "publication-link.json";
    std::error_code symlink_error;
    std::filesystem::create_symlink(publication_path, publication_symlink, symlink_error);
    if (!symlink_error) {
        auto indirect = OccupancyPublication::load(publication_symlink);
        CHECK(!indirect);
        CHECK(indirect.error().code == StatusCode::CorruptData);
    }

    const auto payload_text = read_text(occupancy_path);
    auto loaded_from_bytes = load_continuous_fleet_occupancy_bundle(
        std::as_bytes(std::span(payload_text.data(), payload_text.size())),
        ContinuousFleetOccupancyBundleLoadOptions{});
    CHECK(loaded_from_bytes);
    CHECK(loaded_from_bytes.value().id() == root.value().occupancy_bundle_id);

    auto successor = sign_continuous_fleet_occupancy_publication(
        occupancy_path, trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", key.value().id, key_pair.value().secret_key, 2, root.value().id, 1,
        31);
    CHECK(successor);
    CHECK(verify_occupancy_publication_successor(root.value(), successor.value()));
    CHECK(verify_continuous_fleet_occupancy_publication(
        occupancy_path, successor.value(), trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", trust_bundle.value().id(), root.value().id, 31));

    const auto history_directory = temporary / "publication-history";
    auto history = OccupancyPublicationHistory::create(
        history_directory, root.value(), occupancy_path, trust_bundle.value(),
        "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher", trust_bundle.value().id(),
        root.value().id);
    CHECK(history);
    CHECK(history.value().valid());
    CHECK(history.value().records().size() == 1);
    CHECK(history.value().records().front().valid());
    CHECK(history.value().current_publication_id() == root.value().id);
    CHECK(history.value().timeline_id() == root.value().timeline_id);
    CHECK(history.value().workspace_frame_id() == root.value().workspace_frame_id);
    CHECK(history.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!history.value().authorizes_execution());
    auto history_trust = history.value().trust_bundle();
    CHECK(history_trust);
    CHECK(history_trust.value().id() == trust_bundle.value().id());
    auto history_root = history.value().current_publication();
    CHECK(history_root);
    CHECK(history_root.value().id == root.value().id);
    CHECK(history.value().publication(root.value().id));
    CHECK(!history.value().publication(std::string(64, '0')));
    auto history_verification = history.value().verify(root.value().id, 16);
    CHECK(history_verification);
    CHECK(history_verification.value().publication_id == root.value().id);
    CHECK(!history.value().verify(root.value().id, 33));

    auto stale_open = OccupancyPublicationHistory::open(
        history_directory, "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher",
        trust_bundle.value().id(), root.value().id, std::string(64, '0'));
    CHECK(!stale_open);
    CHECK(stale_open.error().code == StatusCode::IdentityMismatch);
    CHECK(!OccupancyPublicationHistory::open(history_directory, "wrong-stream", "fixture-occupancy-publisher",
                                             trust_bundle.value().id(), root.value().id, root.value().id));

    const auto root_only_directory = temporary / "root-only-history";
    auto root_only = OccupancyPublicationHistory::create(
        root_only_directory, root.value(), occupancy_path, trust_bundle.value(),
        "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher", trust_bundle.value().id(),
        root.value().id);
    CHECK(root_only);
    auto appended = history.value().publish(successor.value(), occupancy_path, root.value().id);
    CHECK(appended);
    CHECK(appended.value().valid());
    CHECK(appended.value().sequence == 2);
    CHECK(history.value().records().size() == 2);
    CHECK(history.value().current_publication_id() == successor.value().id);
    CHECK(history.value().verify(successor.value().id, 31));
    CHECK(!history.value().publish(successor.value(), occupancy_path, root.value().id));

    auto reopened = OccupancyPublicationHistory::open(
        history_directory, "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher",
        trust_bundle.value().id(), root.value().id, successor.value().id);
    CHECK(reopened);
    CHECK(reopened.value().records().size() == 2);
    auto identical = audit_occupancy_publication_histories(history.value(), reopened.value());
    CHECK(identical);
    CHECK(identical.value().valid());
    CHECK(identical.value().relation == OccupancyPublicationHistoryRelation::Identical);
    CHECK(!identical.value().fork_detected());
    CHECK(identical.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!identical.value().authorizes_execution());
    auto extension = audit_occupancy_publication_histories(history.value(), root_only.value());
    CHECK(extension);
    CHECK(extension.value().relation == OccupancyPublicationHistoryRelation::FirstExtendsSecond);
    CHECK(extension.value().common_prefix_count == 1);
    auto reverse_extension = audit_occupancy_publication_histories(root_only.value(), history.value());
    CHECK(reverse_extension);
    CHECK(reverse_extension.value().relation == OccupancyPublicationHistoryRelation::SecondExtendsFirst);
    CHECK(std::string(occupancy_publication_history_relation_name(
              OccupancyPublicationHistoryRelation::Forked)) == "forked");

    const auto fork_directory = temporary / "fork-history";
    auto fork_history = OccupancyPublicationHistory::create(
        fork_directory, root.value(), occupancy_path, trust_bundle.value(),
        "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher", trust_bundle.value().id(),
        root.value().id);
    CHECK(fork_history);
    auto fork_successor = sign_continuous_fleet_occupancy_publication(
        occupancy_path, trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", key.value().id, key_pair.value().secret_key, 2, root.value().id, 2,
        30);
    CHECK(fork_successor);
    CHECK(fork_successor.value().id != successor.value().id);
    CHECK(fork_history.value().publish(fork_successor.value(), occupancy_path, root.value().id));
    auto fork_audit = audit_occupancy_publication_histories(history.value(), fork_history.value());
    CHECK(fork_audit);
    CHECK(fork_audit.value().relation == OccupancyPublicationHistoryRelation::Forked);
    CHECK(fork_audit.value().fork_detected());
    CHECK(fork_audit.value().common_prefix_count == 1);

    OccupancyPublicationHistoryLoadOptions one_publication;
    one_publication.maximum_publications = 1;
    auto publication_limit = OccupancyPublicationHistory::open(
        history_directory, "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher",
        trust_bundle.value().id(), root.value().id, successor.value().id, one_publication);
    CHECK(!publication_limit);
    CHECK(publication_limit.error().code == StatusCode::ResourceLimit);
    OccupancyPublicationHistoryLoadOptions byte_limit;
    byte_limit.maximum_total_payload_bytes = root.value().payload_bytes;
    auto total_limit = OccupancyPublicationHistory::open(
        history_directory, "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher",
        trust_bundle.value().id(), root.value().id, successor.value().id, byte_limit);
    CHECK(!total_limit);
    CHECK(total_limit.error().code == StatusCode::ResourceLimit);
    OccupancyPublicationHistoryLoadOptions cancelled_history;
    cancelled_history.occupancy.cancellation.cancel();
    auto cancelled_open = OccupancyPublicationHistory::open(
        history_directory, "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher",
        trust_bundle.value().id(), root.value().id, successor.value().id, cancelled_history);
    CHECK(!cancelled_open);
    CHECK(cancelled_open.error().code == StatusCode::Cancelled);

    const auto corrupt_history = temporary / "corrupt-history";
    std::filesystem::copy(history_directory, corrupt_history, std::filesystem::copy_options::recursive);
    auto corrupt_payload_path =
        corrupt_history / "payloads" / ("00000000000000000002-" + successor.value().payload_digest + ".bin");
    auto corrupt_payload = read_text(corrupt_payload_path);
    CHECK(!corrupt_payload.empty());
    corrupt_payload.front() = corrupt_payload.front() == '{' ? '[' : '{';
    write_text(corrupt_payload_path, corrupt_payload);
    auto corrupt_rejected = OccupancyPublicationHistory::open(
        corrupt_history, "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher",
        trust_bundle.value().id(), root.value().id, successor.value().id);
    CHECK(!corrupt_rejected);

    const auto record_tamper_history = temporary / "record-tamper-history";
    std::filesystem::copy(history_directory, record_tamper_history, std::filesystem::copy_options::recursive);
    const auto record_tamper_path =
        record_tamper_history / "records" / ("00000000000000000002-" + appended.value().id + ".json");
    auto record_tamper = read_text(record_tamper_path);
    const auto record_checksum_position = record_tamper.find("\"checksum\": \"");
    CHECK(record_checksum_position != std::string::npos);
    record_tamper[record_checksum_position + 13] =
        record_tamper[record_checksum_position + 13] == 'a' ? 'b' : 'a';
    write_text(record_tamper_path, record_tamper);
    CHECK(!OccupancyPublicationHistory::open(record_tamper_history, "fixture-cell-occupancy-stream-v1",
                                             "fixture-occupancy-publisher", trust_bundle.value().id(),
                                             root.value().id, successor.value().id));

    const auto schema_history = temporary / "unknown-schema-history";
    std::filesystem::copy(history_directory, schema_history, std::filesystem::copy_options::recursive);
    const auto history_manifest_path = schema_history / "manifest.json";
    auto history_manifest = read_text(history_manifest_path);
    const auto history_schema_position = history_manifest.rfind("\"schema\": 1");
    CHECK(history_schema_position != std::string::npos);
    history_manifest.replace(history_schema_position, 11, "\"schema\": 2");
    write_text(history_manifest_path, history_manifest);
    CHECK(!OccupancyPublicationHistory::open(schema_history, "fixture-cell-occupancy-stream-v1",
                                             "fixture-occupancy-publisher", trust_bundle.value().id(),
                                             root.value().id, successor.value().id));

    const auto history_symlink = temporary / "history-link";
    symlink_error.clear();
    std::filesystem::create_directory_symlink(history_directory, history_symlink, symlink_error);
    if (!symlink_error) {
        auto indirect_history = OccupancyPublicationHistory::open(
            history_symlink, "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher",
            trust_bundle.value().id(), root.value().id, successor.value().id);
        CHECK(!indirect_history);
        CHECK(indirect_history.error().code == StatusCode::CorruptData);
    }

    auto third = sign_continuous_fleet_occupancy_publication(
        occupancy_path, trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", key.value().id, key_pair.value().secret_key, 3, successor.value().id,
        3, 29);
    CHECK(third);
    std::filesystem::create_directory(history_directory / ".writer-lock");
    auto locked = history.value().publish(third.value(), occupancy_path, successor.value().id);
    CHECK(!locked);
    CHECK(locked.error().code == StatusCode::ResourceLimit);
    std::filesystem::remove(history_directory / ".writer-lock");
    CHECK(history.value().publish(third.value(), occupancy_path, successor.value().id));

    CHECK(!verify_continuous_fleet_occupancy_publication(occupancy_path, root.value(), trust_bundle.value(),
                                                         "wrong-stream", "fixture-occupancy-publisher",
                                                         trust_bundle.value().id(), "", 16));
    CHECK(!verify_continuous_fleet_occupancy_publication(
        occupancy_path, root.value(), trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", trust_bundle.value().id(), "", 33));
    CHECK(!verify_continuous_fleet_occupancy_publication(
        legacy_occupancy_path, root.value(), trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", trust_bundle.value().id(), "", 16));

    auto altered_signature = root.value();
    altered_signature.authentication_tag[0] = altered_signature.authentication_tag[0] == '0' ? '1' : '0';
    CHECK(altered_signature.valid());
    CHECK(!verify_continuous_fleet_occupancy_publication(
        occupancy_path, altered_signature, trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", trust_bundle.value().id(), "", 16));

    auto altered_identity = root.value();
    altered_identity.stream_id = "altered-stream";
    CHECK(!altered_identity.valid());
    CHECK(!verify_occupancy_publication_successor(root.value(), root.value()));

    CHECK(!sign_continuous_fleet_occupancy_publication(
        occupancy_path, trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", key.value().id, key_pair.value().secret_key, 2, "", 0, 32));
    CHECK(!sign_continuous_fleet_occupancy_publication(
        occupancy_path, trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", key.value().id, key_pair.value().secret_key, 1, "", 0, 33));
    auto wrong_seed = seed;
    wrong_seed[0] ^= std::byte{1};
    auto wrong_key_pair = ed25519_key_pair_from_seed(wrong_seed);
    CHECK(wrong_key_pair);
    CHECK(!sign_continuous_fleet_occupancy_publication(
        occupancy_path, trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", key.value().id, wrong_key_pair.value().secret_key, 1, "", 0, 32));
    auto fetch_only_key = make_service_public_key("fixture-occupancy-publisher", key_pair.value().public_key,
                                                  1, 0, ServiceKeyState::Active, true, false, false);
    CHECK(fetch_only_key);
    auto fetch_only_bundle = ServiceTrustBundle::create(1, "", {fetch_only_key.value()});
    CHECK(fetch_only_bundle);
    CHECK(!sign_continuous_fleet_occupancy_publication(
        occupancy_path, fetch_only_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", fetch_only_key.value().id, key_pair.value().secret_key, 1, "", 0, 32));

    const auto occupancy_symlink = temporary / "occupancy-link.json";
    symlink_error.clear();
    std::filesystem::create_symlink(occupancy_path, occupancy_symlink, symlink_error);
    if (!symlink_error) {
        auto indirect_payload = verify_continuous_fleet_occupancy_publication(
            occupancy_symlink, root.value(), trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
            "fixture-occupancy-publisher", trust_bundle.value().id(), "", 16);
        CHECK(!indirect_payload);
        CHECK(indirect_payload.error().code == StatusCode::CorruptData);
    }

    std::array<std::byte, kEd25519SeedBytes> rotation_seed{};
    std::array<std::byte, kEd25519SeedBytes> successor_seed{};
    std::array<std::byte, kEd25519SeedBytes> alternate_seed{};
    for (std::size_t index = 0; index < rotation_seed.size(); ++index) {
        rotation_seed[index] = static_cast<std::byte>(index + 41);
        successor_seed[index] = static_cast<std::byte>(index + 81);
        alternate_seed[index] = static_cast<std::byte>(index + 121);
    }
    auto rotation_pair = ed25519_key_pair_from_seed(rotation_seed);
    auto successor_pair = ed25519_key_pair_from_seed(successor_seed);
    auto alternate_pair = ed25519_key_pair_from_seed(alternate_seed);
    CHECK(rotation_pair);
    CHECK(successor_pair);
    CHECK(alternate_pair);
    auto rotation_key =
        make_service_public_key("rotating-occupancy-publisher", rotation_pair.value().public_key, 1, 0,
                                ServiceKeyState::Active, false, true, true);
    auto successor_key =
        make_service_public_key("rotating-occupancy-publisher", successor_pair.value().public_key, 2, 0,
                                ServiceKeyState::Active, false, true, true);
    auto alternate_key =
        make_service_public_key("rotating-occupancy-publisher", alternate_pair.value().public_key, 2, 0,
                                ServiceKeyState::Active, false, true, true);
    CHECK(rotation_key);
    CHECK(successor_key);
    CHECK(alternate_key);
    auto rotation_root_bundle = ServiceTrustBundle::create(1, "", {rotation_key.value()});
    CHECK(rotation_root_bundle);
    const auto source_trust_directory = temporary / "rotating-source-trust";
    auto source_trust = ServiceTrustHistory::create(source_trust_directory, rotation_root_bundle.value(),
                                                    rotation_root_bundle.value().id());
    CHECK(source_trust);
    auto rotating_root = sign_continuous_fleet_occupancy_publication(
        occupancy_path, rotation_root_bundle.value(), "rotating-cell-stream-v1",
        "rotating-occupancy-publisher", rotation_key.value().id, rotation_pair.value().secret_key, 1, "", 0,
        32);
    CHECK(rotating_root);
    const auto rotating_directory = temporary / "rotating-occupancy-history";
    auto rotating = RotatingOccupancyPublicationHistory::create(
        rotating_directory, rotating_root.value(), occupancy_path, source_trust.value(),
        "rotating-cell-stream-v1", "rotating-occupancy-publisher", rotation_root_bundle.value().id(),
        rotation_root_bundle.value().id(), rotating_root.value().id);
    CHECK(rotating);
    CHECK(rotating.value().valid());
    CHECK(rotating.value().storage_schema() == 1);
    CHECK(rotating.value().trust_root_bundle_id() == rotation_root_bundle.value().id());
    CHECK(rotating.value().current_trust_bundle_id() == rotation_root_bundle.value().id());
    CHECK(rotating.value().current_publication_id() == rotating_root.value().id);
    CHECK(rotating.value().records().size() == 1);
    CHECK(rotating.value().trust_history());
    CHECK(rotating.value().current_trust_bundle());
    CHECK(rotating.value().current_publication());
    CHECK(rotating.value().publication(rotating_root.value().id));
    CHECK(rotating.value().verify(rotating_root.value().id, 16));
    CHECK(rotating.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!rotating.value().authorizes_execution());

    auto retired_rotation_key = rotation_key.value();
    retired_rotation_key.state = ServiceKeyState::Retired;
    retired_rotation_key.valid_through_sequence = 1;
    auto rotation_successor_bundle = rotate_service_trust_bundle(
        rotation_root_bundle.value(), {retired_rotation_key, successor_key.value()});
    CHECK(rotation_successor_bundle);
    auto rotation_authorization = authorize_service_trust_bundle_successor(
        rotation_root_bundle.value(), rotation_successor_bundle.value(), rotation_key.value().service_id,
        rotation_key.value().id, rotation_pair.value().secret_key);
    CHECK(rotation_authorization);
    auto rotated_record = rotating.value().rotate_trust(
        rotation_successor_bundle.value(), rotation_authorization.value(), rotation_root_bundle.value().id());
    CHECK(rotated_record);
    CHECK(rotated_record.value().bundle_id == rotation_successor_bundle.value().id());
    CHECK(rotating.value().current_trust_bundle_id() == rotation_successor_bundle.value().id());
    CHECK(rotating.value().records().size() == 1);
    auto rotated_publication = sign_continuous_fleet_occupancy_publication(
        occupancy_path, rotation_successor_bundle.value(), "rotating-cell-stream-v1",
        "rotating-occupancy-publisher", successor_key.value().id, successor_pair.value().secret_key, 2,
        rotating_root.value().id, 1, 31);
    CHECK(rotated_publication);
    auto rotated_publication_record =
        rotating.value().publish(rotated_publication.value(), occupancy_path, rotating_root.value().id,
                                 rotation_successor_bundle.value().id());
    CHECK(rotated_publication_record);
    CHECK(rotated_publication_record.value().sequence == 2);
    CHECK(rotating.value().records().size() == 2);
    CHECK(rotating.value().current_publication_id() == rotated_publication.value().id);
    CHECK(rotating.value().verify(rotating_root.value().id, 16));
    CHECK(rotating.value().verify(rotated_publication.value().id, 31));

    auto rotating_reopened = RotatingOccupancyPublicationHistory::open(
        rotating_directory, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
        rotation_root_bundle.value().id(), rotation_successor_bundle.value().id(), rotating_root.value().id,
        rotated_publication.value().id);
    CHECK(rotating_reopened);
    CHECK(rotating_reopened.value().records().size() == 2);
    auto rotating_embedded_trust = rotating.value().trust_history();
    CHECK(rotating_embedded_trust);
    auto rotating_checkpoint_signature =
        sign_service_trust_checkpoint(rotating_embedded_trust.value(), successor_key.value().service_id,
                                      successor_key.value().id, successor_pair.value().secret_key);
    CHECK(rotating_checkpoint_signature);
    auto rotating_checkpoint = assemble_service_trust_checkpoint(rotating_embedded_trust.value(),
                                                                 {rotating_checkpoint_signature.value()});
    CHECK(rotating_checkpoint);
    auto checkpoint_reopened = RotatingOccupancyPublicationHistory::open(
        rotating_directory, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
        rotation_root_bundle.value().id(), rotating_checkpoint.value(), rotating_checkpoint.value().id,
        rotating_root.value().id, rotated_publication.value().id);
    CHECK(checkpoint_reopened);
    CHECK(!RotatingOccupancyPublicationHistory::open(
        rotating_directory, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
        rotation_root_bundle.value().id(), rotating_checkpoint.value(), std::string(64, '0'),
        rotating_root.value().id, rotated_publication.value().id));
    CHECK(!RotatingOccupancyPublicationHistory::open(
        rotating_directory, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
        rotation_root_bundle.value().id(), rotation_root_bundle.value().id(), rotating_root.value().id,
        rotated_publication.value().id));
    CHECK(!RotatingOccupancyPublicationHistory::open(
        rotating_directory, "wrong-stream", "rotating-occupancy-publisher", rotation_root_bundle.value().id(),
        rotation_successor_bundle.value().id(), rotating_root.value().id, rotated_publication.value().id));
    auto old_trust_publication = sign_continuous_fleet_occupancy_publication(
        occupancy_path, rotation_root_bundle.value(), "rotating-cell-stream-v1",
        "rotating-occupancy-publisher", rotation_key.value().id, rotation_pair.value().secret_key, 3,
        rotated_publication.value().id, 2, 30);
    CHECK(old_trust_publication);
    CHECK(!rotating.value().publish(old_trust_publication.value(), occupancy_path,
                                    rotated_publication.value().id, rotation_successor_bundle.value().id()));
    CHECK(!rotating.value().rotate_trust(rotation_successor_bundle.value(), rotation_authorization.value(),
                                         rotation_root_bundle.value().id()));

    const auto rotating_root_only_directory =
        temporary / ("rotating-occupancy-root-only-" + std::string(32, 'r'));
    auto rotating_root_only = RotatingOccupancyPublicationHistory::create(
        rotating_root_only_directory, rotating_root.value(), occupancy_path, source_trust.value(),
        "rotating-cell-stream-v1", "rotating-occupancy-publisher", rotation_root_bundle.value().id(),
        rotation_root_bundle.value().id(), rotating_root.value().id);
    CHECK(rotating_root_only);
    auto rotating_extension =
        audit_rotating_occupancy_publication_histories(rotating.value(), rotating_root_only.value());
    CHECK(rotating_extension);
    CHECK(rotating_extension.value().valid());
    CHECK(rotating_extension.value().trust_relation ==
          OccupancyPublicationHistoryRelation::FirstExtendsSecond);
    CHECK(rotating_extension.value().publication_relation ==
          OccupancyPublicationHistoryRelation::FirstExtendsSecond);
    CHECK(!rotating_extension.value().fork_detected());
    CHECK(rotating_extension.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!rotating_extension.value().authorizes_execution());

    auto alternate_successor_bundle = rotate_service_trust_bundle(
        rotation_root_bundle.value(), {retired_rotation_key, alternate_key.value()});
    CHECK(alternate_successor_bundle);
    auto alternate_authorization = authorize_service_trust_bundle_successor(
        rotation_root_bundle.value(), alternate_successor_bundle.value(), rotation_key.value().service_id,
        rotation_key.value().id, rotation_pair.value().secret_key);
    CHECK(alternate_authorization);
    CHECK(rotating_root_only.value().rotate_trust(alternate_successor_bundle.value(),
                                                  alternate_authorization.value(),
                                                  rotation_root_bundle.value().id()));
    auto trust_fork =
        audit_rotating_occupancy_publication_histories(rotating.value(), rotating_root_only.value());
    CHECK(trust_fork);
    CHECK(trust_fork.value().trust_relation == OccupancyPublicationHistoryRelation::Forked);
    CHECK(trust_fork.value().publication_relation == OccupancyPublicationHistoryRelation::FirstExtendsSecond);
    CHECK(trust_fork.value().fork_detected());
    CHECK(trust_fork.value().common_trust_prefix_count == 1);

    const auto rotating_publication_fork_directory =
        temporary / ("rotating-occupancy-publication-fork-" + std::string(24, 'p'));
    auto rotating_publication_fork = RotatingOccupancyPublicationHistory::create(
        rotating_publication_fork_directory, rotating_root.value(), occupancy_path, source_trust.value(),
        "rotating-cell-stream-v1", "rotating-occupancy-publisher", rotation_root_bundle.value().id(),
        rotation_root_bundle.value().id(), rotating_root.value().id);
    CHECK(rotating_publication_fork);
    CHECK(rotating_publication_fork.value().rotate_trust(rotation_successor_bundle.value(),
                                                         rotation_authorization.value(),
                                                         rotation_root_bundle.value().id()));
    auto alternate_publication = sign_continuous_fleet_occupancy_publication(
        occupancy_path, rotation_successor_bundle.value(), "rotating-cell-stream-v1",
        "rotating-occupancy-publisher", successor_key.value().id, successor_pair.value().secret_key, 2,
        rotating_root.value().id, 2, 30);
    CHECK(alternate_publication);
    CHECK(rotating_publication_fork.value().publish(alternate_publication.value(), occupancy_path,
                                                    rotating_root.value().id,
                                                    rotation_successor_bundle.value().id()));
    auto publication_fork =
        audit_rotating_occupancy_publication_histories(rotating.value(), rotating_publication_fork.value());
    CHECK(publication_fork);
    CHECK(publication_fork.value().trust_relation == OccupancyPublicationHistoryRelation::Identical);
    CHECK(publication_fork.value().publication_relation == OccupancyPublicationHistoryRelation::Forked);
    CHECK(publication_fork.value().fork_detected());
    CHECK(publication_fork.value().common_publication_prefix_count == 1);

    RotatingOccupancyPublicationHistoryLoadOptions rotating_one_publication;
    rotating_one_publication.maximum_publications = 1;
    auto rotating_publication_limit = RotatingOccupancyPublicationHistory::open(
        rotating_directory, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
        rotation_root_bundle.value().id(), rotation_successor_bundle.value().id(), rotating_root.value().id,
        rotated_publication.value().id, rotating_one_publication);
    CHECK(!rotating_publication_limit);
    CHECK(rotating_publication_limit.error().code == StatusCode::ResourceLimit);
    RotatingOccupancyPublicationHistoryLoadOptions rotating_one_trust_bundle;
    rotating_one_trust_bundle.trust.maximum_bundles = 1;
    auto rotating_trust_limit = RotatingOccupancyPublicationHistory::open(
        rotating_directory, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
        rotation_root_bundle.value().id(), rotation_successor_bundle.value().id(), rotating_root.value().id,
        rotated_publication.value().id, rotating_one_trust_bundle);
    CHECK(!rotating_trust_limit);
    CHECK(rotating_trust_limit.error().code == StatusCode::ResourceLimit);
    RotatingOccupancyPublicationHistoryLoadOptions rotating_byte_limit;
    rotating_byte_limit.maximum_total_payload_bytes = rotating_root.value().payload_bytes;
    auto rotating_total_limit = RotatingOccupancyPublicationHistory::open(
        rotating_directory, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
        rotation_root_bundle.value().id(), rotation_successor_bundle.value().id(), rotating_root.value().id,
        rotated_publication.value().id, rotating_byte_limit);
    CHECK(!rotating_total_limit);
    CHECK(rotating_total_limit.error().code == StatusCode::ResourceLimit);
    RotatingOccupancyPublicationHistoryLoadOptions rotating_cancelled;
    rotating_cancelled.trust.cancellation.cancel();
    auto rotating_cancelled_open = RotatingOccupancyPublicationHistory::open(
        rotating_directory, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
        rotation_root_bundle.value().id(), rotation_successor_bundle.value().id(), rotating_root.value().id,
        rotated_publication.value().id, rotating_cancelled);
    CHECK(!rotating_cancelled_open);
    CHECK(rotating_cancelled_open.error().code == StatusCode::Cancelled);
    RotatingOccupancyPublicationHistoryLoadOptions rotating_occupancy_cancelled;
    rotating_occupancy_cancelled.occupancy.cancellation.cancel();
    auto rotating_occupancy_cancelled_open = RotatingOccupancyPublicationHistory::open(
        rotating_directory, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
        rotation_root_bundle.value().id(), rotation_successor_bundle.value().id(), rotating_root.value().id,
        rotated_publication.value().id, rotating_occupancy_cancelled);
    CHECK(!rotating_occupancy_cancelled_open);
    CHECK(rotating_occupancy_cancelled_open.error().code == StatusCode::Cancelled);

    std::filesystem::create_directory(rotating_directory / ".writer-lock");
    auto rotating_locked =
        rotating.value().publish(old_trust_publication.value(), occupancy_path,
                                 rotated_publication.value().id, rotation_successor_bundle.value().id());
    CHECK(!rotating_locked);
    CHECK(rotating_locked.error().code == StatusCode::ResourceLimit);
    std::filesystem::remove(rotating_directory / ".writer-lock");

    const auto rotating_corrupt_directory = temporary / "rotating-occupancy-corrupt";
    std::filesystem::copy(rotating_directory, rotating_corrupt_directory,
                          std::filesystem::copy_options::recursive);
    const auto rotating_payload_path =
        rotating_corrupt_directory / "payloads" /
        ("00000000000000000002-" + rotated_publication.value().payload_digest + ".bin");
    auto rotating_payload = read_text(rotating_payload_path);
    CHECK(!rotating_payload.empty());
    rotating_payload.front() = rotating_payload.front() == '{' ? '[' : '{';
    write_text(rotating_payload_path, rotating_payload);
    CHECK(!RotatingOccupancyPublicationHistory::open(
        rotating_corrupt_directory, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
        rotation_root_bundle.value().id(), rotation_successor_bundle.value().id(), rotating_root.value().id,
        rotated_publication.value().id));

    const auto rotating_schema_directory = temporary / "rotating-occupancy-unknown-schema";
    std::filesystem::copy(rotating_directory, rotating_schema_directory,
                          std::filesystem::copy_options::recursive);
    const auto rotating_manifest_path = rotating_schema_directory / "manifest.json";
    auto rotating_manifest = read_text(rotating_manifest_path);
    const auto rotating_schema_position = rotating_manifest.rfind("\"schema\": 1");
    CHECK(rotating_schema_position != std::string::npos);
    rotating_manifest.replace(rotating_schema_position, 11, "\"schema\": 2");
    write_text(rotating_manifest_path, rotating_manifest);
    CHECK(!RotatingOccupancyPublicationHistory::open(
        rotating_schema_directory, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
        rotation_root_bundle.value().id(), rotation_successor_bundle.value().id(), rotating_root.value().id,
        rotated_publication.value().id));

    const auto rotating_unexpected_directory = temporary / "rotating-occupancy-unexpected";
    std::filesystem::copy(rotating_directory, rotating_unexpected_directory,
                          std::filesystem::copy_options::recursive);
    write_text(rotating_unexpected_directory / "records" / "unexpected", "unexpected");
    auto rotating_unexpected = RotatingOccupancyPublicationHistory::open(
        rotating_unexpected_directory, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
        rotation_root_bundle.value().id(), rotation_successor_bundle.value().id(), rotating_root.value().id,
        rotated_publication.value().id);
    CHECK(!rotating_unexpected);
    CHECK(rotating_unexpected.error().code == StatusCode::CorruptData);

    const auto rotating_history_symlink = temporary / "rotating-occupancy-link";
    symlink_error.clear();
    std::filesystem::create_directory_symlink(rotating_directory, rotating_history_symlink, symlink_error);
    if (!symlink_error) {
        auto rotating_indirect = RotatingOccupancyPublicationHistory::open(
            rotating_history_symlink, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
            rotation_root_bundle.value().id(), rotation_successor_bundle.value().id(),
            rotating_root.value().id, rotated_publication.value().id);
        CHECK(!rotating_indirect);
        CHECK(rotating_indirect.error().code == StatusCode::CorruptData);
    }
    const auto rotating_trust_symlink_directory = temporary / "rotating-trust-history-link";
    std::filesystem::copy(rotating_directory, rotating_trust_symlink_directory,
                          std::filesystem::copy_options::recursive);
    std::filesystem::remove_all(rotating_trust_symlink_directory / "trust-history");
    symlink_error.clear();
    std::filesystem::create_directory_symlink(rotating_directory / "trust-history",
                                              rotating_trust_symlink_directory / "trust-history",
                                              symlink_error);
    if (!symlink_error) {
        auto rotating_indirect_trust = RotatingOccupancyPublicationHistory::open(
            rotating_trust_symlink_directory, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
            rotation_root_bundle.value().id(), rotation_successor_bundle.value().id(),
            rotating_root.value().id, rotated_publication.value().id);
        CHECK(!rotating_indirect_trust);
        CHECK(rotating_indirect_trust.error().code == StatusCode::CorruptData);
    }

    auto quorum_publisher_key =
        make_service_public_key("quorum-occupancy-publisher", rotation_pair.value().public_key, 1, 0,
                                ServiceKeyState::Active, false, true, true);
    auto quorum_witness_key =
        make_service_public_key("quorum-rotation-witness", alternate_pair.value().public_key, 1, 0,
                                ServiceKeyState::Active, false, false, true);
    CHECK(quorum_publisher_key);
    CHECK(quorum_witness_key);
    ServiceTrustRotationPolicy rotating_quorum_policy;
    rotating_quorum_policy.minimum_signatures = 2;
    rotating_quorum_policy.require_distinct_services = true;
    auto rotating_quorum_root = ServiceTrustBundle::create_with_rotation_policy(
        1, "", {quorum_witness_key.value(), quorum_publisher_key.value()}, rotating_quorum_policy);
    CHECK(rotating_quorum_root);
    const auto rotating_quorum_source_directory = temporary / "rotating-quorum-source";
    auto rotating_quorum_source = ServiceTrustHistory::create(
        rotating_quorum_source_directory, rotating_quorum_root.value(), rotating_quorum_root.value().id());
    CHECK(rotating_quorum_source);
    auto rotating_quorum_root_publication = sign_continuous_fleet_occupancy_publication(
        occupancy_path, rotating_quorum_root.value(), "rotating-quorum-stream-v1",
        "quorum-occupancy-publisher", quorum_publisher_key.value().id, rotation_pair.value().secret_key, 1,
        "", 0, 32);
    CHECK(rotating_quorum_root_publication);
    const auto rotating_quorum_directory = temporary / "rotating-quorum-history";
    auto rotating_quorum = RotatingOccupancyPublicationHistory::create(
        rotating_quorum_directory, rotating_quorum_root_publication.value(), occupancy_path,
        rotating_quorum_source.value(), "rotating-quorum-stream-v1", "quorum-occupancy-publisher",
        rotating_quorum_root.value().id(), rotating_quorum_root.value().id(),
        rotating_quorum_root_publication.value().id);
    CHECK(rotating_quorum);
    auto rotating_quorum_successor =
        rotate_service_trust_bundle(rotating_quorum_root.value(), rotating_quorum_root.value().keys());
    CHECK(rotating_quorum_successor);
    auto rotating_quorum_authorization_a = authorize_service_trust_bundle_successor(
        rotating_quorum_root.value(), rotating_quorum_successor.value(),
        quorum_publisher_key.value().service_id, quorum_publisher_key.value().id,
        rotation_pair.value().secret_key);
    auto rotating_quorum_authorization_b = authorize_service_trust_bundle_successor(
        rotating_quorum_root.value(), rotating_quorum_successor.value(),
        quorum_witness_key.value().service_id, quorum_witness_key.value().id,
        alternate_pair.value().secret_key);
    CHECK(rotating_quorum_authorization_a);
    CHECK(rotating_quorum_authorization_b);
    auto rotating_quorum_authorizations = assemble_service_trust_bundle_authorizations(
        rotating_quorum_root.value(), rotating_quorum_successor.value(),
        {rotating_quorum_authorization_b.value(), rotating_quorum_authorization_a.value()});
    CHECK(rotating_quorum_authorizations);
    CHECK(rotating_quorum.value().rotate_trust(rotating_quorum_successor.value(),
                                               rotating_quorum_authorizations.value(),
                                               rotating_quorum_root.value().id()));
    auto rotating_quorum_successor_publication = sign_continuous_fleet_occupancy_publication(
        occupancy_path, rotating_quorum_successor.value(), "rotating-quorum-stream-v1",
        "quorum-occupancy-publisher", quorum_publisher_key.value().id, rotation_pair.value().secret_key, 2,
        rotating_quorum_root_publication.value().id, 1, 31);
    CHECK(rotating_quorum_successor_publication);
    CHECK(rotating_quorum.value().publish(rotating_quorum_successor_publication.value(), occupancy_path,
                                          rotating_quorum_root_publication.value().id,
                                          rotating_quorum_successor.value().id()));

    ContinuousFleetOccupancyBundleLoadOptions cancelled;
    cancelled.cancellation.cancel();
    CHECK(!sign_continuous_fleet_occupancy_publication(
        occupancy_path, trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", key.value().id, key_pair.value().secret_key, 1, "", 0, 32, cancelled));

    std::filesystem::remove_all(temporary);
    return EXIT_SUCCESS;
}
