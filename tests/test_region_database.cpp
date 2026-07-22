#include "test_support.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

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

    const double inverse_sqrt_two = 1.0 / std::sqrt(2.0);
    auto first = CspaceObb::create(
        {0.0, 0.0}, {inverse_sqrt_two, inverse_sqrt_two, -inverse_sqrt_two, inverse_sqrt_two}, {0.8, 0.2});
    auto second = CspaceObb::create({0.4, 0.0}, {1.0, 0.0, 0.0, 1.0}, {0.4, 0.4});
    CHECK(first);
    CHECK(second);
    auto intersection = CspacePortal::intersect(first.value(), second.value());
    CHECK(intersection);
    CHECK(intersection.value().has_value());
    CHECK(intersection.value()->valid());
    CHECK(first.value().contains(intersection.value()->witness(), 1e-10));
    CHECK(second.value().contains(intersection.value()->witness(), 1e-10));
    CHECK(intersection.value()->contains(intersection.value()->witness(), 1e-10));

    auto disjoint = CspaceObb::create({3.0, 3.0}, {1.0, 0.0, 0.0, 1.0}, {0.1, 0.1});
    CHECK(disjoint);
    auto no_intersection = CspacePortal::intersect(first.value(), disjoint.value());
    CHECK(no_intersection);
    CHECK(!no_intersection.value().has_value());
    PortalIntersectionOptions cancelled_intersection;
    cancelled_intersection.cancellation.cancel();
    auto cancelled_portal = CspacePortal::intersect(first.value(), second.value(), cancelled_intersection);
    CHECK(!cancelled_portal);
    CHECK(cancelled_portal.error().code == StatusCode::Cancelled);

    const auto robot = planar_robot();
    const SceneSnapshot empty({}, "region-database-empty-v1");
    ObbAtlasBuildOptions options;
    options.initial_half_width = 0.01;
    options.maximum_half_width = 0.08;
    options.nearest_bridge_neighbors = 2;
    const std::vector<Configuration> samples{{-0.6, -0.2}, {0.0, 0.0}, {0.6, 0.2}, {0.0, 0.0}};
    auto built = ObbAtlasBuilder{}.build(robot, empty, samples, options);
    CHECK(built);
    CHECK(built.value().stats.unique_samples == 3);
    CHECK(built.value().stats.point_regions >= 1);
    CHECK(built.value().stats.bridge_regions >= 1);
    CHECK(!built.value().database.records().empty());
    CHECK(built.value().database.verify_compatible(robot, empty));
    CHECK(built.value().database.contains(Configuration{0.0, 0.0}));
    auto regions = built.value().database.regions_at(Configuration{0.0, 0.0});
    CHECK(regions);
    CHECK(!regions.value().empty());
    CHECK(std::all_of(regions.value().begin(), regions.value().end(),
                      [](const auto& region) { return region_type(region.geometry) == RegionType::Obb; }));
    RegionQueryOptions with_portals;
    with_portals.include_portals = true;
    auto all_regions = built.value().database.regions_at(Configuration{0.0, 0.0}, with_portals);
    CHECK(all_regions);
    CHECK(all_regions.value().size() >= regions.value().size());
    auto nearest = built.value().database.nearest_region(Configuration{1.4, 1.4});
    CHECK(nearest);
    CHECK(nearest.value().has_value());
    auto connected = built.value().database.connected(Configuration{-0.6, -0.2}, Configuration{0.6, 0.2});
    CHECK(connected);
    CHECK(connected.value());

    std::set<RegionId> ids;
    for (const auto& record : built.value().database.records()) {
        CHECK(record.id != 0);
        CHECK(ids.insert(record.id).second);
        CHECK(record.certificate_index < built.value().database.certificates().size());
        const auto& certificate = built.value().database.certificates()[record.certificate_index];
        auto found = built.value().database.certificate(certificate.id);
        CHECK(found);
        CHECK(found.value().has_value());
        CHECK(found.value()->id == certificate.id);
    }
    auto repeated = ObbAtlasBuilder{}.build(robot, empty, samples, options);
    CHECK(repeated);
    CHECK(repeated.value().database.records().size() == built.value().database.records().size());
    for (std::size_t index = 0; index < built.value().database.records().size(); ++index) {
        CHECK(repeated.value().database.records()[index].id == built.value().database.records()[index].id);
    }

    HipacOptions corridor_options;
    corridor_options.minimum_lateral_half_width = 0.01;
    corridor_options.maximum_lateral_half_width = 0.04;
    const std::vector<Configuration> path{{-0.8, -0.4}, {0.0, 0.0}, {0.8, 0.4}};
    auto corridor = HipacCorridorBuilder{}.build(robot, empty, path, corridor_options);
    CHECK(corridor);
    CHECK(corridor.value().status == HipacBuildStatus::Certified);
    auto imported = RegionDatabase::from_corridor(corridor.value().corridor, empty.version());
    CHECK(imported);
    CHECK(std::count_if(imported.value().records().begin(), imported.value().records().end(),
                        [](const auto& record) { return region_type(record.geometry) == RegionType::Obb; }) ==
          2);
    CHECK(std::count_if(
              imported.value().records().begin(), imported.value().records().end(),
              [](const auto& record) { return region_type(record.geometry) == RegionType::Portal; }) >= 1);
    CHECK(std::count_if(imported.value().records().begin(), imported.value().records().end(),
                        [](const auto& record) {
                            return region_type(record.geometry) == RegionType::TrajectoryTube;
                        }) == 1);
    RegionQueryOptions include_tubes;
    include_tubes.include_trajectory_tubes = true;
    auto tube_query = imported.value().regions_at(Configuration{0.0, 0.0}, include_tubes);
    CHECK(tube_query);
    CHECK(std::any_of(tube_query.value().begin(), tube_query.value().end(), [](const auto& record) {
        return region_type(record.geometry) == RegionType::TrajectoryTube;
    }));

    auto atlas = AtlasBuilder{}.build(robot, empty, {{0.0, 0.0}});
    CHECK(atlas);
    auto atlas_database = RegionDatabase::from_atlas(atlas.value().atlas, empty.version());
    CHECK(atlas_database);
    CHECK(atlas_database.value().records().size() == 1);
    CHECK(region_type(atlas_database.value().records()[0].geometry) == RegionType::Aabb);

    auto zonotope = CspaceZonotope::create({0.0, 0.0}, 1, {0.15, -0.15});
    CHECK(zonotope);
    HigherOrderRegionValidator higher_order_validator;
    auto zonotope_validation = higher_order_validator.validate(robot, empty, zonotope.value());
    CHECK(zonotope_validation);
    CHECK(zonotope_validation.value().disposition == ValidationDisposition::CertifiedFree);
    auto zonotope_certificate = make_higher_order_region_certificate(
        robot, empty, zonotope.value(), higher_order_validator, zonotope_validation.value());
    CHECK(zonotope_certificate);
    auto taylor = CspaceTaylorRegion::create({0.4, -0.4}, 1, {0.1, -0.1}, {0.01, 0.01});
    CHECK(taylor);
    auto taylor_validation = higher_order_validator.validate(robot, empty, taylor.value());
    CHECK(taylor_validation);
    CHECK(taylor_validation.value().disposition == ValidationDisposition::CertifiedFree);
    auto taylor_certificate = make_higher_order_region_certificate(
        robot, empty, taylor.value(), higher_order_validator, taylor_validation.value());
    CHECK(taylor_certificate);
    std::vector<CertifiedRegionInput> higher_order_inputs;
    higher_order_inputs.push_back({zonotope.value(), zonotope_certificate.value(),
                                   zonotope_validation.value().envelope, "correlated-seed"});
    higher_order_inputs.push_back(
        {taylor.value(), taylor_certificate.value(), taylor_validation.value().envelope, "taylor-seed"});
    auto higher_order_database = RegionDatabase::create(robot, empty, std::move(higher_order_inputs));
    CHECK(higher_order_database);
    CHECK(higher_order_database.value().records().size() == 2);
    CHECK(std::any_of(
        higher_order_database.value().records().begin(), higher_order_database.value().records().end(),
        [](const auto& record) { return region_type(record.geometry) == RegionType::Zonotope; }));
    CHECK(std::any_of(higher_order_database.value().records().begin(),
                      higher_order_database.value().records().end(),
                      [](const auto& record) { return region_type(record.geometry) == RegionType::Taylor; }));
    CHECK(higher_order_database.value().contains(Configuration{0.1, -0.1}));
    CHECK(!higher_order_database.value().contains(Configuration{0.1, 0.1}));

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root =
        std::filesystem::temp_directory_path() / ("rbfsafe-region-database-test-" + std::to_string(nonce));
    const auto directory = root / "database";
    CHECK(imported.value().save(directory));
    CHECK(!imported.value().save(directory));
    auto loaded = RegionDatabase::load(directory);
    CHECK(loaded);
    CHECK(loaded.value().records().size() == imported.value().records().size());
    CHECK(loaded.value().certificates().size() == imported.value().certificates().size());
    CHECK(loaded.value().connected(Configuration{-0.6, -0.3}, Configuration{0.6, 0.3}).value());
    CHECK(imported.value().save(directory, SaveOptions{true}));
    const auto repeated_directory = root / "repeated";
    CHECK(imported.value().save(repeated_directory));
    CHECK(read_text(directory / "manifest.json") == read_text(repeated_directory / "manifest.json"));
    CHECK(read_text(directory / "regions.json") == read_text(repeated_directory / "regions.json"));

    const auto higher_order_directory = root / "higher-order";
    CHECK(higher_order_database.value().save(higher_order_directory));
    auto loaded_higher_order = RegionDatabase::load(higher_order_directory);
    CHECK(loaded_higher_order);
    CHECK(loaded_higher_order.value().records().size() == 2);
    CHECK(std::any_of(loaded_higher_order.value().records().begin(),
                      loaded_higher_order.value().records().end(),
                      [](const auto& record) { return region_type(record.geometry) == RegionType::Taylor; }));
    CHECK(loaded_higher_order.value().contains(Configuration{0.1, -0.1}));

    const auto aabb_directory = root / "aabb";
    CHECK(atlas_database.value().save(aabb_directory));
    auto loaded_aabb = RegionDatabase::load(aabb_directory);
    CHECK(loaded_aabb);
    CHECK(region_type(loaded_aabb.value().records()[0].geometry) == RegionType::Aabb);

    const auto unknown_schema = root / "unknown-schema";
    std::filesystem::copy(directory, unknown_schema, std::filesystem::copy_options::recursive);
    auto manifest = read_text(unknown_schema / "manifest.json");
    const auto schema_position = manifest.find("\"schema\": 1");
    CHECK(schema_position != std::string::npos);
    manifest.replace(schema_position, std::string("\"schema\": 1").size(), "\"schema\": 99");
    write_text(unknown_schema / "manifest.json", manifest);
    auto unknown = RegionDatabase::load(unknown_schema);
    CHECK(!unknown);
    CHECK(unknown.error().code == StatusCode::IncompatibleFormat);

    auto corrupted_payload = read_text(directory / "regions.json");
    CHECK(!corrupted_payload.empty());
    corrupted_payload[corrupted_payload.size() / 2] =
        corrupted_payload[corrupted_payload.size() / 2] == '0' ? '1' : '0';
    write_text(directory / "regions.json", corrupted_payload);
    auto corrupted = RegionDatabase::load(directory);
    CHECK(!corrupted);
    CHECK(corrupted.error().code == StatusCode::CorruptData);
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    CHECK(!cleanup_error);

    ObbAtlasBuildOptions limited = options;
    limited.maximum_samples = 1;
    auto over_budget = ObbAtlasBuilder{}.build(robot, empty, samples, limited);
    CHECK(!over_budget);
    CHECK(over_budget.error().code == StatusCode::ResourceLimit);
    ObbAtlasBuildOptions cancelled = options;
    cancelled.cancellation.cancel();
    auto cancelled_build = ObbAtlasBuilder{}.build(robot, empty, samples, cancelled);
    CHECK(!cancelled_build);
    CHECK(cancelled_build.error().code == StatusCode::Cancelled);
    auto wrong_dimension = ObbAtlasBuilder{}.build(robot, empty, {{0.0}});
    CHECK(!wrong_dimension);
    CHECK(wrong_dimension.error().code == StatusCode::DimensionMismatch);
    auto invalid_query = built.value().database.regions_at(Configuration{0.0});
    CHECK(!invalid_query);
    CHECK(invalid_query.error().code == StatusCode::DimensionMismatch);
    return EXIT_SUCCESS;
}
