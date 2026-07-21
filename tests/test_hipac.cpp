#include "test_support.h"

#include "internal/sha256.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

} // namespace

int main() {
    using namespace rbfsafe;

    const auto robot = planar_robot();
    const SceneSnapshot empty({}, "hipac-empty-v1");
    const std::vector<Configuration> path{{-1.0, -1.0}, {0.0, 0.0}, {1.0, 1.0}};
    HipacOptions options;
    options.minimum_lateral_half_width = 0.01;
    options.maximum_lateral_half_width = 0.05;
    options.growth_iterations = 8;
    auto built = HipacCorridorBuilder{}.build(robot, empty, path, options);
    CHECK(built);
    CHECK(built.value().status == HipacBuildStatus::Certified);
    CHECK(close(built.value().coverage_ratio, 1.0));
    CHECK(built.value().uncovered_intervals.empty());
    CHECK(built.value().corridor.regions().size() == 2);
    CHECK(built.value().corridor.portals().size() == 1);
    CHECK(built.value().stats.certified_cells == 2);
    CHECK(built.value().stats.portals == 1);
    CHECK(built.value().corridor.regions()[0].certificate.level == EvidenceLevel::CertifiedRegion);
    CHECK(!built.value().corridor.regions()[0].certificate.subject_digest.empty());
    CHECK(built.value().corridor.portals()[0].certificate.level == EvidenceLevel::CertifiedConnectivity);
    CHECK(built.value().corridor.contains(Configuration{-0.5, -0.5}));
    auto connected = built.value().corridor.connected(Configuration{-0.5, -0.5}, Configuration{0.5, 0.5});
    CHECK(connected);
    CHECK(connected.value());

    auto route = built.value().corridor.route(Configuration{-0.5, -0.5}, Configuration{0.5, 0.5});
    CHECK(route);
    CHECK(route.value().has_value());
    CHECK(route.value()->waypoints.size() == 3);
    CHECK(route.value()->region_sequence.size() == 2);
    CHECK(route.value()->portal_sequence.size() == 1);
    CHECK(route.value()->certificate.level == EvidenceLevel::CertifiedConnectivity);
    CHECK(route.value()->certificate.policy.algorithm == "hipac-convex-cell-chain");
    for (std::size_t segment = 0; segment + 1 < route.value()->waypoints.size(); ++segment) {
        const auto& region = built.value().corridor.regions()[segment];
        for (int sample = 0; sample <= 100; ++sample) {
            const double fraction = static_cast<double>(sample) / 100.0;
            Configuration point(2);
            for (std::size_t axis = 0; axis < point.size(); ++axis) {
                point[axis] = route.value()->waypoints[segment][axis] +
                              fraction * (route.value()->waypoints[segment + 1][axis] -
                                          route.value()->waypoints[segment][axis]);
            }
            CHECK(region.bounds.contains(point, 1e-12));
        }
    }
    auto disconnected = built.value().corridor.connected(Configuration{-0.5, -0.5}, Configuration{1.4, -1.4});
    CHECK(disconnected);
    CHECK(!disconnected.value());
    CHECK(built.value().corridor.verify_compatible(robot, empty));
    const SceneSnapshot other_scene({}, "hipac-empty-v2");
    auto incompatible = built.value().corridor.verify_compatible(robot, other_scene);
    CHECK(!incompatible);
    CHECK(incompatible.error().code == StatusCode::IdentityMismatch);

    auto repeated = HipacCorridorBuilder{}.build(robot, empty, path, options);
    CHECK(repeated);
    CHECK(repeated.value().corridor.regions().size() == built.value().corridor.regions().size());
    for (std::size_t index = 0; index < built.value().corridor.regions().size(); ++index) {
        CHECK(repeated.value().corridor.regions()[index].id == built.value().corridor.regions()[index].id);
        CHECK(repeated.value().corridor.regions()[index].certificate.id ==
              built.value().corridor.regions()[index].certificate.id);
    }

    std::mt19937_64 engine(7);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    for (const auto& region : built.value().corridor.regions()) {
        for (int sample = 0; sample < 1000; ++sample) {
            Configuration point = region.bounds.center();
            for (std::size_t axis = 0; axis < region.bounds.dimension(); ++axis) {
                const double local = unit(engine) * region.bounds.half_widths()[axis];
                for (std::size_t coordinate = 0; coordinate < point.size(); ++coordinate) {
                    point[coordinate] += local * region.bounds.basis()[axis * point.size() + coordinate];
                }
            }
            CHECK(region.bounds.contains(point, 1e-12));
            auto collision_free = configuration_is_collision_free(robot, empty, point);
            CHECK(collision_free);
            CHECK(collision_free.value());
        }
    }

    SerialRobotModel prismatic("hipac-prismatic", {{0.0, 0.0, 0.0, 0.0, JointType::Prismatic}}, {{0.0, 2.0}},
                               {0.05});
    SceneSnapshot split_scene({{"high-block", {{-0.1, -0.1, 1.1}, {0.1, 0.1, 1.2}}}}, "hipac-split-v1");
    HipacOptions split_options;
    split_options.minimum_lateral_half_width = 0.0;
    split_options.maximum_lateral_half_width = 0.0;
    split_options.maximum_subdivision_depth = 5;
    auto partial = HipacCorridorBuilder{}.build(prismatic, split_scene,
                                                std::vector<Configuration>{{0.25}, {1.5}}, split_options);
    CHECK(partial);
    CHECK(partial.value().status == HipacBuildStatus::Partial);
    CHECK(partial.value().coverage_ratio > 0.0);
    CHECK(partial.value().coverage_ratio < 1.0);
    CHECK(!partial.value().uncovered_intervals.empty());
    auto invalid = HipacCorridorBuilder{}.build(prismatic, split_scene,
                                                std::vector<Configuration>{{1.3}, {1.5}}, split_options);
    CHECK(invalid);
    CHECK(invalid.value().status == HipacBuildStatus::Invalid);

    HipacOptions budget_options = options;
    budget_options.maximum_validations = 1;
    auto budget = HipacCorridorBuilder{}.build(robot, empty, path, budget_options);
    CHECK(!budget);
    CHECK(budget.error().code == StatusCode::ResourceLimit);
    HipacOptions cancelled_options = options;
    cancelled_options.cancellation.cancel();
    auto cancelled = HipacCorridorBuilder{}.build(robot, empty, path, cancelled_options);
    CHECK(!cancelled);
    CHECK(cancelled.error().code == StatusCode::Cancelled);
    auto wrong_dimension =
        HipacCorridorBuilder{}.build(robot, empty, std::vector<Configuration>{{0.0}, {1.0}}, options);
    CHECK(!wrong_dimension);
    CHECK(wrong_dimension.error().code == StatusCode::DimensionMismatch);

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root =
        std::filesystem::temp_directory_path() / ("rbfsafe-corridor-test-" + std::to_string(nonce));
    const auto directory = root / "corridor";
    CHECK(built.value().corridor.save(directory));
    CHECK(!built.value().corridor.save(directory));
    auto loaded = HipacCorridor::load(directory);
    CHECK(loaded);
    CHECK(loaded.value().regions().size() == built.value().corridor.regions().size());
    CHECK(loaded.value().portals().size() == built.value().corridor.portals().size());
    auto loaded_connected = loaded.value().connected(Configuration{-0.5, -0.5}, Configuration{0.5, 0.5});
    CHECK(loaded_connected);
    CHECK(loaded_connected.value());
    CHECK(built.value().corridor.save(directory, SaveOptions{true}));
    const auto repeated_directory = root / "repeated";
    CHECK(repeated.value().corridor.save(repeated_directory));
    CHECK(read_text(directory / "manifest.json") == read_text(repeated_directory / "manifest.json"));
    CHECK(read_text(directory / "corridor.json") == read_text(repeated_directory / "corridor.json"));

    const auto unknown_schema = root / "unknown-schema";
    std::filesystem::copy(directory, unknown_schema, std::filesystem::copy_options::recursive);
    auto manifest = read_text(unknown_schema / "manifest.json");
    const auto schema_position = manifest.find("\"schema\": 1");
    CHECK(schema_position != std::string::npos);
    manifest.replace(schema_position, std::string("\"schema\": 1").size(), "\"schema\": 99");
    write_text(unknown_schema / "manifest.json", manifest);
    auto unknown = HipacCorridor::load(unknown_schema);
    CHECK(!unknown);
    CHECK(unknown.error().code == StatusCode::IncompatibleFormat);

    const auto bad_certificate = root / "bad-certificate";
    std::filesystem::copy(directory, bad_certificate, std::filesystem::copy_options::recursive);
    auto bad_payload = read_text(bad_certificate / "corridor.json");
    const auto subject_label = bad_payload.find("\"subject_digest\": \"");
    CHECK(subject_label != std::string::npos);
    const auto subject_digit = subject_label + std::string("\"subject_digest\": \"").size();
    bad_payload[subject_digit] = bad_payload[subject_digit] == '0' ? '1' : '0';
    write_text(bad_certificate / "corridor.json", bad_payload);
    auto bad_manifest = read_text(bad_certificate / "manifest.json");
    auto old_checksum = internal::sha256_file(directory / "corridor.json");
    auto new_checksum = internal::sha256_file(bad_certificate / "corridor.json");
    CHECK(old_checksum);
    CHECK(new_checksum);
    const auto checksum_position = bad_manifest.find(old_checksum.value());
    CHECK(checksum_position != std::string::npos);
    bad_manifest.replace(checksum_position, old_checksum.value().size(), new_checksum.value());
    write_text(bad_certificate / "manifest.json", bad_manifest);
    auto invalid_certificate = HipacCorridor::load(bad_certificate);
    CHECK(!invalid_certificate);
    CHECK(invalid_certificate.error().code == StatusCode::CorruptData);

    auto payload = read_text(directory / "corridor.json");
    CHECK(!payload.empty());
    payload[payload.size() / 2] = payload[payload.size() / 2] == '0' ? '1' : '0';
    write_text(directory / "corridor.json", payload);
    auto corrupted = HipacCorridor::load(directory);
    CHECK(!corrupted);
    CHECK(corrupted.error().code == StatusCode::CorruptData);

    std::error_code error;
    std::filesystem::remove_all(root, error);
    CHECK(!error);
    return EXIT_SUCCESS;
}
