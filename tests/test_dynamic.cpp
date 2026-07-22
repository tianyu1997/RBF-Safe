#include "test_support.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

rbfsafe::SerialRobotModel prismatic_robot() {
    return rbfsafe::SerialRobotModel(
        "dynamic-prismatic-1d", {{0.0, 0.0, 0.0, 0.0, rbfsafe::JointType::Prismatic}}, {{0.0, 2.0}}, {0.05});
}

rbfsafe::SceneSnapshot near_scene(std::string version) {
    return rbfsafe::SceneSnapshot({{"block", {{-0.1, -0.1, 1.1}, {0.1, 0.1, 1.2}}}}, std::move(version));
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
    const auto robot = prismatic_robot();
    const SceneSnapshot empty({}, "empty-v1");
    const SceneSnapshot renamed({}, "empty-v2");
    const SceneSnapshot far({{"far", {{-0.1, -0.1, 3.0}, {0.1, 0.1, 3.1}}}}, "far-v1");
    const auto blocked = near_scene("blocked-v1");

    auto version_delta = compare_scenes(empty, renamed);
    CHECK(version_delta);
    CHECK(!version_delta.value().geometry_changed());
    CHECK(version_delta.value().from_digest == empty.digest());
    CHECK(version_delta.value().to_digest == renamed.digest());
    CHECK(version_delta.value().digest.size() == 64);
    auto added_delta = compare_scenes(empty, blocked);
    CHECK(added_delta);
    CHECK(added_delta.value().changes.size() == 1);
    CHECK(added_delta.value().changes[0].kind == SceneChangeKind::Added);
    auto removed_delta = compare_scenes(blocked, empty);
    CHECK(removed_delta);
    CHECK(removed_delta.value().changes[0].kind == SceneChangeKind::Removed);
    const SceneSnapshot moved({{"block", {{-0.1, -0.1, 1.3}, {0.1, 0.1, 1.4}}}}, "blocked-v2");
    auto moved_delta = compare_scenes(blocked, moved);
    CHECK(moved_delta);
    CHECK(moved_delta.value().changes[0].kind == SceneChangeKind::Modified);

    auto initial = AtlasBuilder{}.build(robot, empty, {{0.25}});
    CHECK(initial);
    CHECK(initial.value().atlas.storage_schema() == 2);
    CHECK(initial.value().atlas.regions().size() == 1);
    CHECK(initial.value().atlas.dependencies().size() == 1);
    CHECK(initial.value().atlas.dependencies()[0].envelope.links.size() == 1);
    CHECK(initial.value().atlas.certificates()[0].subject_digest.size() == 64);
    CHECK(initial.value().atlas.version_info().sequence == 0);
    CHECK(initial.value().atlas.version_info().scene_digest == empty.digest());
    const RegionId initial_region_id = initial.value().atlas.regions()[0].id;

    AtlasUpdater updater;
    auto version_only = updater.update(robot, empty, renamed, initial.value().atlas);
    CHECK(version_only);
    CHECK(version_only.value().stats.certificates_inherited == 1);
    CHECK(version_only.value().stats.validations == 0);
    CHECK(version_only.value().atlas.regions()[0].id == initial_region_id);
    CHECK(version_only.value().atlas.certificates()[0].parent_certificate_id ==
          initial.value().atlas.certificates()[0].id);
    CHECK(version_only.value().atlas.certificates()[0].transition_digest ==
          version_only.value().delta.digest);
    CHECK(version_only.value().atlas.version_info().sequence == 1);
    CHECK(version_only.value().atlas.version_info().parent_id == initial.value().atlas.version_info().id);

    auto far_update = updater.update(robot, empty, far, initial.value().atlas);
    CHECK(far_update);
    CHECK(far_update.value().stats.certificates_inherited == 1);
    CHECK(far_update.value().stats.validations == 0);
    CHECK(far_update.value().atlas.regions()[0].id == initial_region_id);

    auto restricted = updater.update(robot, empty, blocked, initial.value().atlas);
    CHECK(restricted);
    CHECK(restricted.value().stats.regions_invalidated == 1);
    CHECK(restricted.value().stats.repaired_regions == 1);
    CHECK(restricted.value().atlas.repair_domains().size() == 1);
    CHECK(restricted.value().atlas.contains(Configuration{0.25}));
    CHECK(!restricted.value().atlas.contains(Configuration{1.5}));
    CHECK(restricted.value().invalidated_region_ids == std::vector<RegionId>{initial_region_id});

    auto restricted_again = updater.update(robot, empty, blocked, initial.value().atlas);
    CHECK(restricted_again);
    CHECK(restricted_again.value().atlas.version_info().id == restricted.value().atlas.version_info().id);
    CHECK(restricted_again.value().atlas.regions()[0].id == restricted.value().atlas.regions()[0].id);

    const SceneSnapshot reopened({}, "empty-v3");
    auto recovered = updater.update(robot, blocked, reopened, restricted.value().atlas);
    CHECK(recovered);
    CHECK(recovered.value().atlas.repair_domains().empty());
    CHECK(recovered.value().atlas.regions().size() == 1);
    CHECK(recovered.value().atlas.regions()[0].id == initial_region_id);
    CHECK(recovered.value().atlas.contains(Configuration{1.5}));
    CHECK(recovered.value().stats.repaired_regions == 1);

    AtlasUpdateOptions cancelled_options;
    cancelled_options.cancellation.cancel();
    auto cancelled = updater.update(robot, empty, blocked, initial.value().atlas, {}, cancelled_options);
    CHECK(!cancelled);
    CHECK(cancelled.error().code == StatusCode::Cancelled);
    AtlasUpdateOptions limited;
    limited.maximum_validations = 1;
    auto exhausted = updater.update(robot, empty, blocked, initial.value().atlas, {}, limited);
    CHECK(!exhausted);
    CHECK(exhausted.error().code == StatusCode::ResourceLimit);
    auto wrong_previous = updater.update(robot, renamed, blocked, initial.value().atlas);
    CHECK(!wrong_previous);
    CHECK(wrong_previous.error().code == StatusCode::IdentityMismatch);

    auto legacy = SafeAtlas::load(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "atlas_schema1");
    CHECK(legacy);
    const SerialRobotModel legacy_robot(
        "planar-2r", {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}, {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
        {{-1.5, 1.5}, {-1.5, 1.5}}, {0.05, 0.05});
    const SceneSnapshot legacy_scene({}, "quickstart-v1");
    const SceneSnapshot migrated_scene({}, "quickstart-v2");
    auto migrated = updater.update(legacy_robot, legacy_scene, migrated_scene, legacy.value());
    CHECK(migrated);
    CHECK(migrated.value().atlas.storage_schema() == 2);
    CHECK(migrated.value().stats.certificates_inherited == 0);
    CHECK(migrated.value().stats.regions_revalidated == 1);
    CHECK(migrated.value().atlas.certificates()[0].subject_digest.size() == 64);

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root =
        std::filesystem::temp_directory_path() / ("rbfsafe-version-store-" + std::to_string(nonce));
    const auto standalone = root / "updated-atlas";
    CHECK(restricted.value().atlas.save(standalone));
    auto loaded_update = SafeAtlas::load(standalone);
    CHECK(loaded_update);
    CHECK(loaded_update.value().transition().has_value());
    CHECK(loaded_update.value().transition()->digest == restricted.value().delta.digest);
    auto transition_text = read_text(standalone / "transition.json");
    const auto transition_digest = transition_text.find("\"digest\": \"");
    CHECK(transition_digest != std::string::npos);
    const auto transition_digit = transition_digest + std::string("\"digest\": \"").size();
    transition_text[transition_digit] = transition_text[transition_digit] == '0' ? '1' : '0';
    write_text(standalone / "transition.json", transition_text);
    auto corrupted_transition = SafeAtlas::load(standalone);
    CHECK(!corrupted_transition);
    CHECK(corrupted_transition.error().code == StatusCode::CorruptData);

    const auto store_path = root / "store";
    auto store = AtlasVersionStore::create(store_path, initial.value().atlas);
    CHECK(store);
    CHECK(store.value().versions().size() == 1);
    auto current = store.value().load_current();
    CHECK(current);
    CHECK(current.value().version_info().id == initial.value().atlas.version_info().id);
    CHECK(store.value().publish(version_only.value().atlas));
    CHECK(store.value().versions().size() == 2);
    CHECK(store.value().current_version_id() == version_only.value().atlas.version_info().id);
    const SceneSnapshot chain_scene({}, "empty-v3-chain");
    auto chain_update = updater.update(robot, renamed, chain_scene, version_only.value().atlas);
    CHECK(chain_update);
    CHECK(store.value().publish(chain_update.value().atlas));
    CHECK(store.value().versions().size() == 3);
    CHECK(store.value().load_version(chain_update.value().atlas.version_info().id));
    CHECK(store.value().rollback(initial.value().atlas.version_info().id));
    CHECK(store.value().load_current().value().scene_digest() == empty.digest());
    auto reopened_store = AtlasVersionStore::open(store_path);
    CHECK(reopened_store);
    CHECK(reopened_store.value().current_version_id() == initial.value().atlas.version_info().id);
    CHECK(reopened_store.value().load_version(version_only.value().atlas.version_info().id));

    auto manifest = read_text(store_path / "store.json");
    const auto identity = manifest.find("\"identity\": \"");
    CHECK(identity != std::string::npos);
    const auto digit = identity + std::string("\"identity\": \"").size();
    manifest[digit] = manifest[digit] == '0' ? '1' : '0';
    write_text(store_path / "store.json", manifest);
    auto corrupted_store = AtlasVersionStore::open(store_path);
    CHECK(!corrupted_store);
    CHECK(corrupted_store.error().code == StatusCode::CorruptData);

    std::error_code error;
    std::filesystem::remove_all(root, error);
    CHECK(!error);
    return EXIT_SUCCESS;
}
