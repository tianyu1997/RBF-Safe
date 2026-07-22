#include <rbfsafe/dynamic.h>
#include <rbfsafe/version.h>

#include "internal/atlas_identity.h"
#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string_view>

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumStoredVersions = 1'000'000;

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

Result<std::string> string_field(const internal::Json& object, std::string_view key) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string()) {
        return Result<std::string>::failure(StatusCode::CorruptData, "invalid version-store string",
                                            std::string(key));
    }
    return value->as_string();
}

Result<std::uint64_t> decimal_u64(const internal::Json& object, std::string_view key) {
    auto text = string_field(object, key);
    if (!text)
        return text.error();
    if (text.value().empty()) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData, "empty version-store sequence");
    }
    std::uint64_t value = 0;
    for (const char digit : text.value()) {
        if (digit < '0' || digit > '9' ||
            value >
                (std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(digit - '0')) / 10u) {
            return Result<std::uint64_t>::failure(StatusCode::CorruptData, "invalid version-store sequence");
        }
        value = value * 10u + static_cast<std::uint64_t>(digit - '0');
    }
    return value;
}

internal::Json version_json(const AtlasVersionInfo& version) {
    return internal::Json::Object{
        {"id", version.id},
        {"parent_id", version.parent_id},
        {"scene_digest", version.scene_digest},
        {"scene_version", version.scene_version},
        {"sequence", std::to_string(version.sequence)},
        {"transition_digest", version.transition_digest},
    };
}

internal::Json manifest_payload(const std::string& current, const std::vector<AtlasVersionInfo>& versions) {
    internal::Json::Array records;
    for (const auto& version : versions)
        records.emplace_back(version_json(version));
    return internal::Json::Object{
        {"current_version_id", current},
        {"format", "rbfsafe-atlas-version-store"},
        {"schema", 1},
        {"versions", std::move(records)},
    };
}

internal::Json manifest_document(const std::string& current, const std::vector<AtlasVersionInfo>& versions) {
    auto payload = manifest_payload(current, versions);
    auto object = payload.as_object();
    object.emplace("identity", internal::sha256(payload.dump(false)));
    object.emplace("library_version", kVersion);
    return object;
}

Result<void> validate_version_record(const AtlasVersionInfo& version) {
    const bool initial =
        version.sequence == 0 && version.parent_id.empty() && version.transition_digest.empty();
    const bool derived = version.sequence > 0 && internal::valid_sha256(version.parent_id) &&
                         internal::valid_sha256(version.transition_digest);
    if (!internal::valid_sha256(version.id) || !internal::valid_sha256(version.scene_digest) ||
        version.scene_version.empty() || (!initial && !derived)) {
        return Result<void>::failure(StatusCode::CorruptData, "Atlas version-store record is invalid",
                                     version.id);
    }
    return Result<void>::success();
}

Result<void> write_manifest_atomic(const std::filesystem::path& directory, const std::string& current,
                                   const std::vector<AtlasVersionInfo>& versions) {
    const auto manifest = directory / "store.json";
    const auto temporary = unique_sibling(manifest, ".tmp-");
    auto write = internal::write_text_file(temporary, manifest_document(current, versions).dump(true) + "\n");
    if (!write)
        return write;
    std::error_code error;
    std::filesystem::path backup;
    const bool exists = std::filesystem::exists(manifest, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return Result<void>::failure(StatusCode::IoError, "failed to inspect Atlas version-store manifest");
    }
    if (exists) {
        backup = unique_sibling(manifest, ".backup-");
        std::filesystem::rename(manifest, backup, error);
        if (error) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return Result<void>::failure(StatusCode::IoError, "failed to stage Atlas version-store manifest");
        }
    }
    std::filesystem::rename(temporary, manifest, error);
    if (error) {
        if (exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, manifest, ignored);
        }
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Result<void>::failure(StatusCode::IoError, "failed to publish Atlas version-store manifest");
    }
    if (exists)
        std::filesystem::remove(backup, error);
    return Result<void>::success();
}

bool same_version(const AtlasVersionInfo& left, const AtlasVersionInfo& right) {
    return left.sequence == right.sequence && left.id == right.id && left.parent_id == right.parent_id &&
           left.scene_version == right.scene_version && left.scene_digest == right.scene_digest &&
           left.transition_digest == right.transition_digest;
}

bool same_dependency(const RegionDependency& left, const RegionDependency& right) {
    if (left.region_id != right.region_id || left.envelope.links.size() != right.envelope.links.size())
        return false;
    for (std::size_t index = 0; index < left.envelope.links.size(); ++index) {
        if (left.envelope.links[index].lower != right.envelope.links[index].lower ||
            left.envelope.links[index].upper != right.envelope.links[index].upper)
            return false;
    }
    return true;
}

Result<void> validate_transition_chain(const SafeAtlas& child, const SafeAtlas& parent) {
    if (!child.transition() || child.version_info().parent_id != parent.version_info().id ||
        child.transition()->from_digest != parent.scene_digest() ||
        child.transition()->to_digest != child.scene_digest() ||
        child.transition()->to_version != child.version_info().scene_version ||
        child.transition()->digest != child.version_info().transition_digest) {
        return Result<void>::failure(StatusCode::CorruptData,
                                     "Atlas version transition does not match its parent");
    }
    for (std::size_t index = 0; index < child.regions().size(); ++index) {
        const auto& region = child.regions()[index];
        const auto& certificate = child.certificates()[region.certificate_index];
        if (certificate.parent_certificate_id.empty())
            continue;
        auto parent_region = std::find_if(parent.regions().begin(), parent.regions().end(),
                                          [&](const auto& candidate) { return candidate.id == region.id; });
        if (parent_region == parent.regions().end() ||
            parent_region->certificate_index >= parent.certificates().size()) {
            return Result<void>::failure(StatusCode::CorruptData,
                                         "inherited certificate has no parent region",
                                         std::to_string(region.id));
        }
        const auto parent_index =
            static_cast<std::size_t>(std::distance(parent.regions().begin(), parent_region));
        if (index >= child.dependencies().size() || parent_index >= parent.dependencies().size() ||
            certificate.parent_certificate_id != parent.certificates()[parent_region->certificate_index].id ||
            certificate.subject_digest !=
                parent.certificates()[parent_region->certificate_index].subject_digest ||
            !same_dependency(child.dependencies()[index], parent.dependencies()[parent_index])) {
            return Result<void>::failure(StatusCode::CorruptData,
                                         "inherited certificate parent evidence is inconsistent",
                                         std::to_string(region.id));
        }
        double clearance = parent.certificates()[parent_region->certificate_index].clearance_lower_bound;
        for (const auto& change : child.transition()->changes) {
            if (change.kind == SceneChangeKind::Removed)
                continue;
            if (!change.after) {
                return Result<void>::failure(StatusCode::CorruptData,
                                             "scene transition has no new obstacle bounds");
            }
            for (const auto& link : child.dependencies()[index].envelope.links) {
                if (link.overlaps(*change.after)) {
                    return Result<void>::failure(StatusCode::CorruptData,
                                                 "inherited region intersects a changed obstacle",
                                                 std::to_string(region.id));
                }
                clearance = std::min(clearance, link.distance_lower_bound(*change.after));
            }
        }
        if (certificate.clearance_lower_bound != clearance) {
            return Result<void>::failure(StatusCode::CorruptData,
                                         "inherited certificate clearance is inconsistent",
                                         std::to_string(region.id));
        }
    }
    return Result<void>::success();
}

} // namespace

Result<AtlasVersionStore> AtlasVersionStore::create(const std::filesystem::path& directory,
                                                    const SafeAtlas& initial_atlas) {
    if (directory.empty() || initial_atlas.storage_schema() < 2 ||
        initial_atlas.version_info().sequence != 0 || initial_atlas.transition() ||
        internal::atlas_version_identity(initial_atlas) != initial_atlas.version_info().id) {
        return Result<AtlasVersionStore>::failure(StatusCode::InvalidArgument,
                                                  "invalid initial Atlas version");
    }
    auto record_status = validate_version_record(initial_atlas.version_info());
    if (!record_status)
        return record_status.error();
    std::error_code error;
    if (std::filesystem::exists(directory, error)) {
        return Result<AtlasVersionStore>::failure(
            StatusCode::IoError, "Atlas version-store destination exists", directory.string());
    }
    if (error) {
        return Result<AtlasVersionStore>::failure(
            StatusCode::IoError, "failed to inspect version-store destination", directory.string());
    }
    if (!directory.parent_path().empty()) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (error) {
            return Result<AtlasVersionStore>::failure(StatusCode::IoError,
                                                      "failed to create version-store parent",
                                                      directory.parent_path().string());
        }
    }
    const auto temporary = unique_sibling(directory, ".tmp-");
    std::filesystem::create_directories(temporary / "versions", error);
    if (error) {
        return Result<AtlasVersionStore>::failure(StatusCode::IoError,
                                                  "failed to create temporary version store");
    }
    auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    };
    auto save = initial_atlas.save(temporary / "versions" / initial_atlas.version_info().id);
    if (!save) {
        cleanup();
        return save.error();
    }
    const std::vector<AtlasVersionInfo> versions{initial_atlas.version_info()};
    auto manifest = internal::write_text_file(
        temporary / "store.json",
        manifest_document(initial_atlas.version_info().id, versions).dump(true) + "\n");
    if (!manifest) {
        cleanup();
        return manifest.error();
    }
    std::filesystem::rename(temporary, directory, error);
    if (error) {
        cleanup();
        return Result<AtlasVersionStore>::failure(StatusCode::IoError,
                                                  "failed to publish Atlas version store");
    }
    AtlasVersionStore store;
    store.directory_ = directory;
    store.current_version_id_ = initial_atlas.version_info().id;
    store.versions_ = versions;
    return store;
}

Result<AtlasVersionStore> AtlasVersionStore::open(const std::filesystem::path& directory) {
    auto document = internal::read_json_file(directory / "store.json");
    if (!document)
        return document.error();
    if (!document.value().is_object()) {
        return Result<AtlasVersionStore>::failure(StatusCode::CorruptData,
                                                  "version-store manifest must be an object");
    }
    auto format = string_field(document.value(), "format");
    if (!format)
        return format.error();
    const auto* schema = document.value().find("schema");
    if (format.value() != "rbfsafe-atlas-version-store" || schema == nullptr || !schema->is_number() ||
        schema->as_number() != 1.0) {
        return Result<AtlasVersionStore>::failure(StatusCode::IncompatibleFormat,
                                                  "unsupported Atlas version-store format");
    }
    auto current = string_field(document.value(), "current_version_id");
    if (!current)
        return current.error();
    auto identity = string_field(document.value(), "identity");
    if (!identity)
        return identity.error();
    const auto* records = document.value().find("versions");
    if (records == nullptr || !records->is_array() || records->as_array().empty() ||
        records->as_array().size() > kMaximumStoredVersions) {
        return Result<AtlasVersionStore>::failure(StatusCode::CorruptData,
                                                  "invalid Atlas version-store records");
    }
    std::vector<AtlasVersionInfo> versions;
    versions.reserve(records->as_array().size());
    std::map<std::string, std::size_t, std::less<>> version_indices;
    for (const auto& record : records->as_array()) {
        if (!record.is_object()) {
            return Result<AtlasVersionStore>::failure(StatusCode::CorruptData,
                                                      "Atlas version record must be object");
        }
        auto sequence = decimal_u64(record, "sequence");
        auto id = string_field(record, "id");
        auto parent = string_field(record, "parent_id");
        auto scene_version = string_field(record, "scene_version");
        auto scene_digest = string_field(record, "scene_digest");
        auto transition = string_field(record, "transition_digest");
        if (!sequence)
            return sequence.error();
        if (!id)
            return id.error();
        if (!parent)
            return parent.error();
        if (!scene_version)
            return scene_version.error();
        if (!scene_digest)
            return scene_digest.error();
        if (!transition)
            return transition.error();
        AtlasVersionInfo version{sequence.value(),
                                 std::move(id).value(),
                                 std::move(parent).value(),
                                 std::move(scene_version).value(),
                                 std::move(scene_digest).value(),
                                 std::move(transition).value()};
        auto valid = validate_version_record(version);
        if (!valid)
            return valid.error();
        if (!version_indices.emplace(version.id, versions.size()).second) {
            return Result<AtlasVersionStore>::failure(StatusCode::CorruptData, "duplicate Atlas version ID",
                                                      version.id);
        }
        versions.push_back(std::move(version));
    }
    if (!version_indices.contains(current.value())) {
        return Result<AtlasVersionStore>::failure(StatusCode::CorruptData,
                                                  "current Atlas version is unknown");
    }
    const auto root_count = std::count_if(versions.begin(), versions.end(), [](const auto& version) {
        return version.sequence == 0 && version.parent_id.empty();
    });
    if (root_count != 1) {
        return Result<AtlasVersionStore>::failure(StatusCode::CorruptData,
                                                  "Atlas version store must have exactly one root");
    }
    for (const auto& version : versions) {
        if (!version.parent_id.empty()) {
            const auto parent_index = version_indices.find(version.parent_id);
            if (parent_index == version_indices.end() ||
                versions[parent_index->second].sequence == std::numeric_limits<std::uint64_t>::max() ||
                versions[parent_index->second].sequence + 1 != version.sequence) {
                return Result<AtlasVersionStore>::failure(
                    StatusCode::CorruptData, "Atlas version parent chain is invalid", version.id);
            }
        }
    }
    if (!internal::valid_sha256(identity.value()) ||
        identity.value() != internal::sha256(manifest_payload(current.value(), versions).dump(false))) {
        return Result<AtlasVersionStore>::failure(StatusCode::CorruptData,
                                                  "Atlas version-store identity mismatch");
    }
    AtlasVersionStore store;
    store.directory_ = directory;
    store.current_version_id_ = std::move(current).value();
    store.versions_ = std::move(versions);
    auto loaded = store.load_current();
    if (!loaded)
        return loaded.error();
    return store;
}

Result<SafeAtlas> AtlasVersionStore::load_current() const { return load_version(current_version_id_); }

Result<SafeAtlas> AtlasVersionStore::load_version(const std::string& version_id) const {
    if (!internal::valid_sha256(version_id)) {
        return Result<SafeAtlas>::failure(StatusCode::InvalidArgument, "invalid Atlas version ID");
    }
    std::map<std::string_view, const AtlasVersionInfo*, std::less<>> records;
    for (const auto& version : versions_)
        records.emplace(version.id, &version);
    const auto record = records.find(version_id);
    if (record == records.end()) {
        return Result<SafeAtlas>::failure(StatusCode::InvalidArgument, "Atlas version is not registered",
                                          version_id);
    }
    auto loaded = SafeAtlas::load(directory_ / "versions" / version_id);
    if (!loaded)
        return loaded.error();
    if (loaded.value().storage_schema() < 2 ||
        !same_version(loaded.value().version_info(), *record->second)) {
        return Result<SafeAtlas>::failure(StatusCode::CorruptData,
                                          "stored Atlas does not match version record", version_id);
    }
    SafeAtlas atlas = std::move(loaded).value();
    const SafeAtlas* child = &atlas;
    std::optional<SafeAtlas> ancestor;
    const AtlasVersionInfo* child_record = record->second;
    while (!child_record->parent_id.empty()) {
        const auto parent_record = records.find(child_record->parent_id);
        if (parent_record == records.end()) {
            return Result<SafeAtlas>::failure(StatusCode::CorruptData,
                                              "Atlas parent version is not registered", version_id);
        }
        auto parent = SafeAtlas::load(directory_ / "versions" / parent_record->second->id);
        if (!parent)
            return parent.error();
        if (parent.value().storage_schema() < 2 ||
            !same_version(parent.value().version_info(), *parent_record->second)) {
            return Result<SafeAtlas>::failure(StatusCode::CorruptData,
                                              "stored parent Atlas does not match version record",
                                              parent_record->second->id);
        }
        auto chain = validate_transition_chain(*child, parent.value());
        if (!chain)
            return chain.error();
        ancestor = std::move(parent).value();
        child = &*ancestor;
        child_record = parent_record->second;
    }
    return atlas;
}

Result<void> AtlasVersionStore::publish(const SafeAtlas& atlas) {
    if (atlas.storage_schema() < 2 || internal::atlas_version_identity(atlas) != atlas.version_info().id) {
        return Result<void>::failure(StatusCode::InvalidArgument, "cannot publish an invalid Atlas version");
    }
    auto current = std::find_if(versions_.begin(), versions_.end(),
                                [&](const auto& version) { return version.id == current_version_id_; });
    if (current == versions_.end() || atlas.version_info().parent_id != current_version_id_ ||
        current->sequence == std::numeric_limits<std::uint64_t>::max() ||
        atlas.version_info().sequence != current->sequence + 1) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "Atlas version does not extend the current store head");
    }
    auto parent_atlas = load_current();
    if (!parent_atlas)
        return parent_atlas.error();
    auto chain = validate_transition_chain(atlas, parent_atlas.value());
    if (!chain)
        return Result<void>::failure(StatusCode::IdentityMismatch, chain.error().message,
                                     chain.error().context);
    auto valid = validate_version_record(atlas.version_info());
    if (!valid)
        return Result<void>::failure(StatusCode::InvalidArgument, valid.error().message,
                                     valid.error().context);
    if (std::any_of(versions_.begin(), versions_.end(),
                    [&](const auto& version) { return version.id == atlas.version_info().id; })) {
        return Result<void>::failure(StatusCode::IoError, "Atlas version is already published",
                                     atlas.version_info().id);
    }
    const auto version_directory = directory_ / "versions" / atlas.version_info().id;
    auto save = atlas.save(version_directory);
    if (!save)
        return save;
    auto next_versions = versions_;
    next_versions.push_back(atlas.version_info());
    auto manifest = write_manifest_atomic(directory_, atlas.version_info().id, next_versions);
    if (!manifest) {
        std::error_code ignored;
        std::filesystem::remove_all(version_directory, ignored);
        return manifest;
    }
    versions_ = std::move(next_versions);
    current_version_id_ = atlas.version_info().id;
    return Result<void>::success();
}

Result<void> AtlasVersionStore::rollback(const std::string& version_id) {
    auto loaded = load_version(version_id);
    if (!loaded)
        return loaded.error();
    auto manifest = write_manifest_atomic(directory_, version_id, versions_);
    if (!manifest)
        return manifest;
    current_version_id_ = version_id;
    return Result<void>::success();
}

} // namespace rbfsafe
