#pragma once

#include <rbfsafe/model.h>
#include <rbfsafe/result.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rbfsafe {

enum class SceneChangeKind : std::uint8_t { Added = 0, Removed = 1, Modified = 2 };

struct SceneObstacleChange {
    SceneChangeKind kind = SceneChangeKind::Modified;
    std::string obstacle_id;
    std::optional<WorkspaceAabb> before;
    std::optional<WorkspaceAabb> after;
};

struct SceneDelta {
    std::string from_version;
    std::string to_version;
    std::string from_digest;
    std::string to_digest;
    std::vector<SceneObstacleChange> changes;
    std::string digest;

    bool geometry_changed() const noexcept { return !changes.empty(); }
};

Result<SceneDelta> compare_scenes(const SceneSnapshot& before, const SceneSnapshot& after);
std::string scene_change_kind_name(SceneChangeKind kind);

} // namespace rbfsafe
