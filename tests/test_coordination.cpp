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
