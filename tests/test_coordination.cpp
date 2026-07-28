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

    ContinuousFleetOccupancyBundleLoadOptions cancelled;
    cancelled.cancellation.cancel();
    CHECK(!sign_continuous_fleet_occupancy_publication(
        occupancy_path, trust_bundle.value(), "fixture-cell-occupancy-stream-v1",
        "fixture-occupancy-publisher", key.value().id, key_pair.value().secret_key, 1, "", 0, 32, cancelled));

    std::filesystem::remove_all(temporary);
    return EXIT_SUCCESS;
}
