#include <rbfsafe/scene_delta.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/scene_delta_utils.h"
#include "internal/sha256.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace rbfsafe {
namespace {

bool same_bounds(const WorkspaceAabb& left, const WorkspaceAabb& right) {
    return left.lower == right.lower && left.upper == right.upper;
}

internal::Json workspace_box_json(const WorkspaceAabb& box) {
    internal::Json::Array lower;
    internal::Json::Array upper;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        lower.emplace_back(box.lower[axis]);
        upper.emplace_back(box.upper[axis]);
    }
    return internal::Json::Object{{"lower", std::move(lower)}, {"upper", std::move(upper)}};
}

internal::Json optional_box_json(const std::optional<WorkspaceAabb>& box) {
    return box ? workspace_box_json(*box) : internal::Json(nullptr);
}

internal::Json delta_payload(const SceneDelta& delta) {
    internal::Json::Array changes;
    for (const auto& change : delta.changes) {
        changes.emplace_back(internal::Json::Object{
            {"after", optional_box_json(change.after)},
            {"before", optional_box_json(change.before)},
            {"kind", scene_change_kind_name(change.kind)},
            {"obstacle_id", change.obstacle_id},
        });
    }
    return internal::Json::Object{
        {"changes", std::move(changes)},
        {"format", "rbfsafe-scene-delta"},
        {"from_digest", delta.from_digest},
        {"from_version", delta.from_version},
        {"schema", 1},
        {"to_digest", delta.to_digest},
        {"to_version", delta.to_version},
    };
}

std::string delta_identity(const SceneDelta& delta) {
    return internal::sha256(delta_payload(delta).dump(false));
}

Result<std::string> string_field(const internal::Json& object, std::string_view key) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string()) {
        return Result<std::string>::failure(StatusCode::CorruptData, "invalid scene-delta string",
                                            std::string(key));
    }
    return value->as_string();
}

Result<std::optional<WorkspaceAabb>> optional_box(const internal::Json& value) {
    if (value.is_null())
        return std::optional<WorkspaceAabb>{};
    if (!value.is_object()) {
        return Result<std::optional<WorkspaceAabb>>::failure(StatusCode::CorruptData,
                                                             "scene-delta bounds must be object or null");
    }
    const auto* lower = value.find("lower");
    const auto* upper = value.find("upper");
    if (lower == nullptr || upper == nullptr || !lower->is_array() || !upper->is_array() ||
        lower->as_array().size() != 3 || upper->as_array().size() != 3) {
        return Result<std::optional<WorkspaceAabb>>::failure(StatusCode::CorruptData,
                                                             "scene-delta bounds arrays are invalid");
    }
    WorkspaceAabb box;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!lower->as_array()[axis].is_number() || !upper->as_array()[axis].is_number()) {
            return Result<std::optional<WorkspaceAabb>>::failure(StatusCode::CorruptData,
                                                                 "scene-delta bounds are not numeric");
        }
        box.lower[axis] = lower->as_array()[axis].as_number();
        box.upper[axis] = upper->as_array()[axis].as_number();
    }
    if (!box.valid()) {
        return Result<std::optional<WorkspaceAabb>>::failure(StatusCode::CorruptData,
                                                             "scene-delta bounds are invalid");
    }
    return std::optional<WorkspaceAabb>{box};
}

} // namespace

std::string scene_change_kind_name(SceneChangeKind kind) {
    switch (kind) {
    case SceneChangeKind::Added:
        return "added";
    case SceneChangeKind::Removed:
        return "removed";
    case SceneChangeKind::Modified:
        return "modified";
    }
    return "modified";
}

Result<void> internal::validate_scene_delta(const SceneDelta& delta) {
    if (delta.from_version.empty() || delta.to_version.empty() || !valid_sha256(delta.from_digest) ||
        !valid_sha256(delta.to_digest) || delta.from_digest == delta.to_digest ||
        !valid_sha256(delta.digest)) {
        return Result<void>::failure(StatusCode::CorruptData, "scene-delta identity is invalid");
    }
    std::string previous_id;
    for (const auto& change : delta.changes) {
        const bool added = change.kind == SceneChangeKind::Added && !change.before && change.after;
        const bool removed = change.kind == SceneChangeKind::Removed && change.before && !change.after;
        const bool modified = change.kind == SceneChangeKind::Modified && change.before && change.after &&
                              !same_bounds(*change.before, *change.after);
        if (change.obstacle_id.empty() || (!previous_id.empty() && change.obstacle_id <= previous_id) ||
            (!added && !removed && !modified) || (change.before && !change.before->valid()) ||
            (change.after && !change.after->valid())) {
            return Result<void>::failure(StatusCode::CorruptData, "scene-delta change is invalid",
                                         change.obstacle_id);
        }
        previous_id = change.obstacle_id;
    }
    if (delta_identity(delta) != delta.digest) {
        return Result<void>::failure(StatusCode::CorruptData, "scene-delta digest does not match content");
    }
    return Result<void>::success();
}

internal::Json internal::encode_scene_delta(const SceneDelta& delta) {
    auto document = delta_payload(delta).as_object();
    document.emplace("digest", delta.digest);
    return document;
}

Result<SceneDelta> internal::decode_scene_delta(const internal::Json& document) {
    if (!document.is_object()) {
        return Result<SceneDelta>::failure(StatusCode::CorruptData, "scene-delta document must be object");
    }
    auto format = string_field(document, "format");
    const auto* schema = document.find("schema");
    if (!format)
        return format.error();
    if (format.value() != "rbfsafe-scene-delta" || schema == nullptr || !schema->is_number() ||
        schema->as_number() != 1.0) {
        return Result<SceneDelta>::failure(StatusCode::IncompatibleFormat,
                                           "unsupported scene-delta document");
    }
    auto from_version = string_field(document, "from_version");
    auto to_version = string_field(document, "to_version");
    auto from_digest = string_field(document, "from_digest");
    auto to_digest = string_field(document, "to_digest");
    auto digest = string_field(document, "digest");
    if (!from_version)
        return from_version.error();
    if (!to_version)
        return to_version.error();
    if (!from_digest)
        return from_digest.error();
    if (!to_digest)
        return to_digest.error();
    if (!digest)
        return digest.error();
    const auto* changes = document.find("changes");
    if (changes == nullptr || !changes->is_array() || changes->as_array().size() > 10'000'000) {
        return Result<SceneDelta>::failure(StatusCode::ResourceLimit, "scene-delta change count is invalid");
    }
    SceneDelta delta;
    delta.from_version = std::move(from_version).value();
    delta.to_version = std::move(to_version).value();
    delta.from_digest = std::move(from_digest).value();
    delta.to_digest = std::move(to_digest).value();
    delta.digest = std::move(digest).value();
    for (const auto& record : changes->as_array()) {
        if (!record.is_object()) {
            return Result<SceneDelta>::failure(StatusCode::CorruptData, "scene-delta change must be object");
        }
        auto kind = string_field(record, "kind");
        auto id = string_field(record, "obstacle_id");
        const auto* before_json = record.find("before");
        const auto* after_json = record.find("after");
        if (!kind)
            return kind.error();
        if (!id)
            return id.error();
        if (before_json == nullptr || after_json == nullptr) {
            return Result<SceneDelta>::failure(StatusCode::CorruptData,
                                               "scene-delta change bounds are missing");
        }
        auto before = optional_box(*before_json);
        auto after = optional_box(*after_json);
        if (!before)
            return before.error();
        if (!after)
            return after.error();
        SceneChangeKind change_kind;
        if (kind.value() == "added")
            change_kind = SceneChangeKind::Added;
        else if (kind.value() == "removed")
            change_kind = SceneChangeKind::Removed;
        else if (kind.value() == "modified")
            change_kind = SceneChangeKind::Modified;
        else
            return Result<SceneDelta>::failure(StatusCode::CorruptData, "unknown scene-delta change kind",
                                               kind.value());
        delta.changes.push_back(
            {change_kind, std::move(id).value(), std::move(before).value(), std::move(after).value()});
    }
    auto valid = validate_scene_delta(delta);
    if (!valid)
        return valid.error();
    return delta;
}

Result<SceneDelta> compare_scenes(const SceneSnapshot& before, const SceneSnapshot& after) {
    auto status = before.validate();
    if (!status)
        return status.error();
    status = after.validate();
    if (!status)
        return status.error();

    SceneDelta delta;
    delta.from_version = before.version();
    delta.to_version = after.version();
    delta.from_digest = before.digest();
    delta.to_digest = after.digest();
    std::map<std::string, WorkspaceAabb> old_obstacles;
    std::map<std::string, WorkspaceAabb> new_obstacles;
    for (const auto& obstacle : before.obstacles())
        old_obstacles.emplace(obstacle.id, obstacle.bounds);
    for (const auto& obstacle : after.obstacles())
        new_obstacles.emplace(obstacle.id, obstacle.bounds);

    auto old = old_obstacles.begin();
    auto next = new_obstacles.begin();
    while (old != old_obstacles.end() || next != new_obstacles.end()) {
        if (next == new_obstacles.end() || (old != old_obstacles.end() && old->first < next->first)) {
            delta.changes.push_back({SceneChangeKind::Removed, old->first, old->second, std::nullopt});
            ++old;
        } else if (old == old_obstacles.end() || next->first < old->first) {
            delta.changes.push_back({SceneChangeKind::Added, next->first, std::nullopt, next->second});
            ++next;
        } else {
            if (!same_bounds(old->second, next->second)) {
                delta.changes.push_back({SceneChangeKind::Modified, old->first, old->second, next->second});
            }
            ++old;
            ++next;
        }
    }
    delta.digest = delta_identity(delta);
    return delta;
}

} // namespace rbfsafe
