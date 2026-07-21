#include <rbfsafe/atlas.h>
#include <rbfsafe/version.h>

#include "internal/binary.h"
#include "internal/json.h"
#include "internal/lect_codec.h"
#include "internal/sha256.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <set>
#include <string_view>

namespace rbfsafe {

Result<void> save_atlas_directory(const SafeAtlas& atlas, const std::filesystem::path& directory,
                                  const SaveOptions& options);
Result<SafeAtlas> load_atlas_directory(const std::filesystem::path& directory);

namespace {

constexpr std::string_view kRegionMagic = "RBFREGN1";
constexpr std::string_view kGraphMagic = "RBFGRPH1";
constexpr std::uint64_t kMaximumRegions = 10'000'000;
constexpr std::uint64_t kMaximumEdges = 200'000'000;
constexpr std::uint32_t kMaximumDimension = 64;

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

Result<std::uint64_t> json_unsigned(const internal::Json& object, std::string_view key,
                                    std::uint64_t maximum) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number()) ||
        value->as_number() < 0.0 || std::floor(value->as_number()) != value->as_number() ||
        value->as_number() > static_cast<double>(maximum)) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData, "invalid unsigned manifest field",
                                              std::string(key));
    }
    return static_cast<std::uint64_t>(value->as_number());
}

Result<std::string> json_string(const internal::Json& object, std::string_view key) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string()) {
        return Result<std::string>::failure(StatusCode::CorruptData, "invalid string field",
                                            std::string(key));
    }
    return value->as_string();
}

internal::BinaryWriter encode_regions(const SafeAtlas& atlas) {
    internal::BinaryWriter writer;
    writer.text(kRegionMagic);
    writer.u32(kAtlasSchemaVersion);
    writer.u32(static_cast<std::uint32_t>(atlas.dimension()));
    writer.u64(static_cast<std::uint64_t>(atlas.regions().size()));
    for (const auto& region : atlas.regions()) {
        writer.u64(region.id);
        writer.u64(static_cast<std::uint64_t>(region.certificate_index));
        writer.u64(region.component);
        writer.string(region.source_node.path());
        for (const auto& axis : region.bounds.axes()) {
            writer.f64(axis.lower);
            writer.f64(axis.upper);
        }
    }
    return writer;
}

Result<std::vector<SafeRegion>> decode_regions(std::span<const std::byte> bytes,
                                               std::size_t expected_dimension,
                                               std::size_t certificate_count) {
    internal::BinaryReader reader(bytes);
    auto magic = reader.fixed_text(kRegionMagic.size());
    if (!magic)
        return magic.error();
    if (magic.value() != kRegionMagic)
        return Result<std::vector<SafeRegion>>::failure(StatusCode::IncompatibleFormat,
                                                        "invalid region payload magic");
    auto schema = reader.u32();
    if (!schema)
        return schema.error();
    if (schema.value() != kAtlasSchemaVersion)
        return Result<std::vector<SafeRegion>>::failure(StatusCode::IncompatibleFormat,
                                                        "unsupported region payload schema");
    auto dimension = reader.u32();
    if (!dimension)
        return dimension.error();
    if (dimension.value() != expected_dimension)
        return Result<std::vector<SafeRegion>>::failure(StatusCode::CorruptData,
                                                        "region dimension does not match manifest");
    auto count = reader.u64();
    if (!count)
        return count.error();
    if (count.value() > kMaximumRegions)
        return Result<std::vector<SafeRegion>>::failure(StatusCode::ResourceLimit,
                                                        "region count exceeds limit");
    std::vector<SafeRegion> regions;
    regions.reserve(static_cast<std::size_t>(count.value()));
    std::set<RegionId> identifiers;
    for (std::uint64_t index = 0; index < count.value(); ++index) {
        auto id = reader.u64();
        if (!id)
            return id.error();
        auto certificate = reader.u64();
        if (!certificate)
            return certificate.error();
        auto component = reader.u64();
        if (!component)
            return component.error();
        auto path = reader.string(4096);
        if (!path)
            return path.error();
        if (id.value() == 0 || !identifiers.insert(id.value()).second) {
            return Result<std::vector<SafeRegion>>::failure(StatusCode::CorruptData,
                                                            "duplicate or zero region ID");
        }
        if (certificate.value() >= certificate_count) {
            return Result<std::vector<SafeRegion>>::failure(StatusCode::CorruptData,
                                                            "region certificate index is out of range");
        }
        LectNodeKey key(path.value());
        if (!key.valid())
            return Result<std::vector<SafeRegion>>::failure(StatusCode::CorruptData,
                                                            "invalid source LECT key");
        std::vector<Interval> axes;
        axes.reserve(expected_dimension);
        for (std::size_t dimension_index = 0; dimension_index < expected_dimension; ++dimension_index) {
            auto lower = reader.f64();
            if (!lower)
                return lower.error();
            auto upper = reader.f64();
            if (!upper)
                return upper.error();
            axes.emplace_back(lower.value(), upper.value());
        }
        CspaceAabb bounds(std::move(axes));
        if (!bounds.valid())
            return Result<std::vector<SafeRegion>>::failure(StatusCode::CorruptData,
                                                            "stored region bounds are invalid");
        regions.push_back({id.value(), std::move(bounds), static_cast<std::size_t>(certificate.value()),
                           component.value(), std::move(key)});
    }
    if (!reader.finished())
        return Result<std::vector<SafeRegion>>::failure(StatusCode::CorruptData,
                                                        "region payload has trailing bytes");
    return regions;
}

internal::BinaryWriter encode_graph(const SafeAtlas& atlas) {
    internal::BinaryWriter writer;
    writer.text(kGraphMagic);
    writer.u32(kAtlasSchemaVersion);
    writer.u64(static_cast<std::uint64_t>(atlas.adjacency().size()));
    std::uint64_t total = 0;
    writer.u64(0);
    for (const auto& neighbors : atlas.adjacency()) {
        total += static_cast<std::uint64_t>(neighbors.size());
        writer.u64(total);
    }
    writer.u64(total);
    for (const auto& neighbors : atlas.adjacency()) {
        for (const auto neighbor : neighbors)
            writer.u64(static_cast<std::uint64_t>(neighbor));
    }
    return writer;
}

Result<std::vector<std::vector<std::size_t>>> decode_graph(std::span<const std::byte> bytes,
                                                           std::size_t expected_vertices) {
    internal::BinaryReader reader(bytes);
    auto magic = reader.fixed_text(kGraphMagic.size());
    if (!magic)
        return magic.error();
    if (magic.value() != kGraphMagic)
        return Result<std::vector<std::vector<std::size_t>>>::failure(StatusCode::IncompatibleFormat,
                                                                      "invalid graph payload magic");
    auto schema = reader.u32();
    if (!schema)
        return schema.error();
    if (schema.value() != kAtlasSchemaVersion)
        return Result<std::vector<std::vector<std::size_t>>>::failure(StatusCode::IncompatibleFormat,
                                                                      "unsupported graph payload schema");
    auto vertices = reader.u64();
    if (!vertices)
        return vertices.error();
    if (vertices.value() != expected_vertices || vertices.value() > kMaximumRegions) {
        return Result<std::vector<std::vector<std::size_t>>>::failure(
            StatusCode::CorruptData, "graph vertex count does not match regions");
    }
    std::vector<std::uint64_t> offsets(static_cast<std::size_t>(vertices.value()) + 1u);
    for (auto& offset : offsets) {
        auto value = reader.u64();
        if (!value)
            return value.error();
        offset = value.value();
    }
    auto total = reader.u64();
    if (!total)
        return total.error();
    if (total.value() > kMaximumEdges || offsets.back() != total.value()) {
        return Result<std::vector<std::vector<std::size_t>>>::failure(StatusCode::CorruptData,
                                                                      "invalid graph edge count");
    }
    for (std::size_t index = 1; index < offsets.size(); ++index) {
        if (offsets[index] < offsets[index - 1])
            return Result<std::vector<std::vector<std::size_t>>>::failure(StatusCode::CorruptData,
                                                                          "graph offsets are not monotonic");
    }
    std::vector<std::vector<std::size_t>> graph(expected_vertices);
    for (std::size_t vertex = 0; vertex < expected_vertices; ++vertex) {
        graph[vertex].reserve(static_cast<std::size_t>(offsets[vertex + 1] - offsets[vertex]));
        std::size_t previous = 0;
        bool has_previous = false;
        for (std::uint64_t edge = offsets[vertex]; edge < offsets[vertex + 1]; ++edge) {
            auto neighbor = reader.u64();
            if (!neighbor)
                return neighbor.error();
            if (neighbor.value() >= expected_vertices || neighbor.value() == vertex ||
                (has_previous && neighbor.value() <= previous)) {
                return Result<std::vector<std::vector<std::size_t>>>::failure(StatusCode::CorruptData,
                                                                              "invalid graph neighbor list");
            }
            previous = static_cast<std::size_t>(neighbor.value());
            has_previous = true;
            graph[vertex].push_back(previous);
        }
    }
    if (!reader.finished())
        return Result<std::vector<std::vector<std::size_t>>>::failure(StatusCode::CorruptData,
                                                                      "graph payload has trailing bytes");
    for (std::size_t vertex = 0; vertex < graph.size(); ++vertex) {
        for (const auto neighbor : graph[vertex]) {
            if (!std::binary_search(graph[neighbor].begin(), graph[neighbor].end(), vertex)) {
                return Result<std::vector<std::vector<std::size_t>>>::failure(
                    StatusCode::CorruptData, "graph adjacency is not symmetric");
            }
        }
    }
    return graph;
}

internal::Json certificate_json(const Certificate& certificate) {
    return internal::Json::Object{
        {"clearance_lower_bound", certificate.clearance_lower_bound},
        {"id", certificate.id},
        {"level", evidence_level_name(certificate.level)},
        {"policy", internal::Json::Object{{"algorithm", certificate.policy.algorithm},
                                          {"algorithm_version", certificate.policy.algorithm_version},
                                          {"obstacle_padding", certificate.policy.obstacle_padding}}},
        {"robot_digest", certificate.robot_digest},
        {"scene_digest", certificate.scene_digest},
    };
}

std::string certificate_identity(const Certificate& certificate) {
    return internal::sha256(internal::Json(internal::Json::Object{
                                               {"algorithm", certificate.policy.algorithm},
                                               {"algorithm_version", certificate.policy.algorithm_version},
                                               {"clearance_lower_bound", certificate.clearance_lower_bound},
                                               {"level", evidence_level_name(certificate.level)},
                                               {"obstacle_padding", certificate.policy.obstacle_padding},
                                               {"robot_digest", certificate.robot_digest},
                                               {"scene_digest", certificate.scene_digest},
                                           })
                                .dump(false));
}

internal::Json encode_certificates(const SafeAtlas& atlas) {
    internal::Json::Array records;
    for (const auto& certificate : atlas.certificates())
        records.emplace_back(certificate_json(certificate));
    return internal::Json::Object{
        {"certificates", std::move(records)},
        {"format", "rbfsafe-certificates"},
        {"schema", static_cast<double>(kAtlasSchemaVersion)},
    };
}

Result<std::vector<Certificate>> decode_certificates(const internal::Json& root,
                                                     const std::string& robot_digest,
                                                     const std::string& scene_digest) {
    if (!root.is_object())
        return Result<std::vector<Certificate>>::failure(StatusCode::CorruptData,
                                                         "certificate document must be object");
    auto format = json_string(root, "format");
    if (!format)
        return format.error();
    auto schema = json_unsigned(root, "schema", 1000);
    if (!schema)
        return schema.error();
    if (format.value() != "rbfsafe-certificates" || schema.value() != kAtlasSchemaVersion) {
        return Result<std::vector<Certificate>>::failure(StatusCode::IncompatibleFormat,
                                                         "unsupported certificate document");
    }
    const auto* records = root.find("certificates");
    if (records == nullptr || !records->is_array() || records->as_array().size() > kMaximumRegions) {
        return Result<std::vector<Certificate>>::failure(StatusCode::CorruptData,
                                                         "invalid certificate array");
    }
    std::vector<Certificate> certificates;
    certificates.reserve(records->as_array().size());
    std::set<std::string> ids;
    for (const auto& record : records->as_array()) {
        if (!record.is_object())
            return Result<std::vector<Certificate>>::failure(StatusCode::CorruptData,
                                                             "certificate must be object");
        auto id = json_string(record, "id");
        if (!id)
            return id.error();
        auto level = json_string(record, "level");
        if (!level)
            return level.error();
        auto robot = json_string(record, "robot_digest");
        if (!robot)
            return robot.error();
        auto scene = json_string(record, "scene_digest");
        if (!scene)
            return scene.error();
        const auto* clearance_json = record.find("clearance_lower_bound");
        const auto* policy_json = record.find("policy");
        if (clearance_json == nullptr || !clearance_json->is_number() ||
            !std::isfinite(clearance_json->as_number()) || clearance_json->as_number() < 0.0 ||
            policy_json == nullptr || !policy_json->is_object()) {
            return Result<std::vector<Certificate>>::failure(StatusCode::CorruptData,
                                                             "invalid certificate values", id.value());
        }
        auto algorithm = json_string(*policy_json, "algorithm");
        if (!algorithm)
            return algorithm.error();
        auto algorithm_version = json_string(*policy_json, "algorithm_version");
        if (!algorithm_version)
            return algorithm_version.error();
        const auto* padding_json = policy_json->find("obstacle_padding");
        if (padding_json == nullptr || !padding_json->is_number() ||
            !std::isfinite(padding_json->as_number()) || padding_json->as_number() < 0.0) {
            return Result<std::vector<Certificate>>::failure(StatusCode::CorruptData,
                                                             "invalid certificate padding", id.value());
        }
        if (level.value() != "certified_region" || robot.value() != robot_digest ||
            scene.value() != scene_digest || id.value().size() != 64 || !ids.insert(id.value()).second) {
            return Result<std::vector<Certificate>>::failure(StatusCode::CorruptData,
                                                             "certificate identity is invalid", id.value());
        }
        Certificate certificate{id.value(),
                                EvidenceLevel::CertifiedRegion,
                                robot.value(),
                                scene.value(),
                                {algorithm.value(), algorithm_version.value(), padding_json->as_number()},
                                clearance_json->as_number()};
        if (certificate_identity(certificate) != certificate.id) {
            return Result<std::vector<Certificate>>::failure(
                StatusCode::CorruptData, "certificate digest does not match content", certificate.id);
        }
        certificates.push_back(std::move(certificate));
    }
    return certificates;
}

Result<void> validate_atlas(const SafeAtlas& atlas) {
    if (atlas.dimension() == 0 || atlas.dimension() > kMaximumDimension || !atlas.lect().valid() ||
        atlas.lect().root_domain().dimension() != atlas.dimension() ||
        atlas.regions().size() != atlas.certificates().size() ||
        atlas.adjacency().size() != atlas.regions().size()) {
        return Result<void>::failure(StatusCode::InternalError, "atlas state is inconsistent");
    }
    std::set<RegionId> ids;
    for (std::size_t index = 0; index < atlas.regions().size(); ++index) {
        const auto& region = atlas.regions()[index];
        if (region.id == 0 || !ids.insert(region.id).second || !region.bounds.valid() ||
            region.bounds.dimension() != atlas.dimension() ||
            region.certificate_index >= atlas.certificates().size() || region.component == 0 ||
            !region.source_node.valid()) {
            return Result<void>::failure(StatusCode::InternalError, "atlas region is inconsistent",
                                         std::to_string(index));
        }
        const auto& certificate = atlas.certificates()[region.certificate_index];
        if (certificate.robot_digest != atlas.robot_digest() ||
            certificate.scene_digest != atlas.scene_digest() ||
            certificate.level != EvidenceLevel::CertifiedRegion ||
            certificate_identity(certificate) != certificate.id) {
            return Result<void>::failure(StatusCode::InternalError, "atlas certificate is inconsistent",
                                         std::to_string(index));
        }
    }
    return Result<void>::success();
}

Result<void> verify_payload_checksum(const internal::Json& payloads, const std::filesystem::path& directory,
                                     std::string_view relative_path) {
    if (!payloads.is_object())
        return Result<void>::failure(StatusCode::CorruptData, "manifest payloads must be object");
    const auto* expected = payloads.find(relative_path);
    if (expected == nullptr || !expected->is_string() || expected->as_string().size() != 64) {
        return Result<void>::failure(StatusCode::CorruptData, "missing payload checksum",
                                     std::string(relative_path));
    }
    auto actual = internal::sha256_file(directory / std::filesystem::path(relative_path));
    if (!actual)
        return actual.error();
    if (actual.value() != expected->as_string()) {
        return Result<void>::failure(StatusCode::CorruptData, "payload checksum mismatch",
                                     std::string(relative_path));
    }
    return Result<void>::success();
}

} // namespace

Result<void> save_atlas_directory(const SafeAtlas& atlas, const std::filesystem::path& directory,
                                  const SaveOptions& options) {
    auto state = validate_atlas(atlas);
    if (!state)
        return state;
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(directory, error);
    if (error)
        return Result<void>::failure(StatusCode::IoError, "failed to inspect atlas destination",
                                     directory.string());
    if (destination_exists && !options.overwrite) {
        return Result<void>::failure(StatusCode::IoError, "atlas destination already exists",
                                     directory.string());
    }
    if (!directory.parent_path().empty()) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (error)
            return Result<void>::failure(StatusCode::IoError, "failed to create atlas parent directory",
                                         directory.parent_path().string());
    }
    const auto temporary = unique_sibling(directory, ".tmp-");
    std::filesystem::create_directories(temporary / "lect", error);
    if (error)
        return Result<void>::failure(StatusCode::IoError, "failed to create atlas temporary directory",
                                     temporary.string());
    auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    };

    auto lect_tree =
        LectTree::restore(atlas.lect().root_domain(), atlas.lect().policy(), atlas.lect().all_nodes());
    if (!lect_tree) {
        cleanup();
        return lect_tree.error();
    }
    auto lect_payload = internal::encode_lect_tree(lect_tree.value());
    auto regions_payload = encode_regions(atlas);
    auto graph_payload = encode_graph(atlas);
    auto write = lect_payload.save(temporary / "lect" / "nodes.bin");
    if (!write) {
        cleanup();
        return write;
    }
    write = regions_payload.save(temporary / "regions.bin");
    if (!write) {
        cleanup();
        return write;
    }
    write = graph_payload.save(temporary / "graph.bin");
    if (!write) {
        cleanup();
        return write;
    }
    const auto certificates_text = encode_certificates(atlas).dump(true) + "\n";
    write = internal::write_text_file(temporary / "certificates.json", certificates_text);
    if (!write) {
        cleanup();
        return write;
    }

    internal::Json::Object payloads;
    for (const auto& relative : {std::string("lect/nodes.bin"), std::string("regions.bin"),
                                 std::string("graph.bin"), std::string("certificates.json")}) {
        auto checksum = internal::sha256_file(temporary / std::filesystem::path(relative));
        if (!checksum) {
            cleanup();
            return checksum.error();
        }
        payloads.emplace(relative, checksum.value());
    }
    internal::Json manifest(internal::Json::Object{
        {"certificates", static_cast<double>(atlas.certificates().size())},
        {"dimension", static_cast<double>(atlas.dimension())},
        {"format", "rbfsafe-atlas"},
        {"lect_nodes", static_cast<double>(atlas.lect().size())},
        {"library_version", kVersion},
        {"payloads", std::move(payloads)},
        {"regions", static_cast<double>(atlas.regions().size())},
        {"robot_digest", atlas.robot_digest()},
        {"scene_digest", atlas.scene_digest()},
        {"schema", static_cast<double>(kAtlasSchemaVersion)},
    });
    write = internal::write_text_file(temporary / "manifest.json", manifest.dump(true) + "\n");
    if (!write) {
        cleanup();
        return write;
    }
    auto self_check = load_atlas_directory(temporary);
    if (!self_check) {
        cleanup();
        return self_check.error();
    }

    std::filesystem::path backup;
    if (destination_exists) {
        backup = unique_sibling(directory, ".backup-");
        std::filesystem::rename(directory, backup, error);
        if (error) {
            cleanup();
            return Result<void>::failure(
                StatusCode::IoError, "failed to stage existing atlas for replacement", directory.string());
        }
    }
    std::filesystem::rename(temporary, directory, error);
    if (error) {
        if (destination_exists) {
            std::error_code restore_error;
            std::filesystem::rename(backup, directory, restore_error);
        }
        cleanup();
        return Result<void>::failure(StatusCode::IoError, "failed to publish atlas directory",
                                     directory.string());
    }
    if (destination_exists)
        std::filesystem::remove_all(backup, error);
    return Result<void>::success();
}

Result<SafeAtlas> load_atlas_directory(const std::filesystem::path& directory) {
    auto manifest = internal::read_json_file(directory / "manifest.json");
    if (!manifest)
        return manifest.error();
    if (!manifest.value().is_object())
        return Result<SafeAtlas>::failure(StatusCode::CorruptData, "atlas manifest must be object");
    auto format = json_string(manifest.value(), "format");
    if (!format)
        return format.error();
    auto schema = json_unsigned(manifest.value(), "schema", 1000);
    if (!schema)
        return schema.error();
    if (format.value() != "rbfsafe-atlas" || schema.value() != kAtlasSchemaVersion) {
        return Result<SafeAtlas>::failure(StatusCode::IncompatibleFormat,
                                          "unsupported atlas format or schema");
    }
    auto dimension = json_unsigned(manifest.value(), "dimension", kMaximumDimension);
    if (!dimension)
        return dimension.error();
    if (dimension.value() == 0)
        return Result<SafeAtlas>::failure(StatusCode::CorruptData, "atlas dimension is zero");
    auto region_count = json_unsigned(manifest.value(), "regions", kMaximumRegions);
    if (!region_count)
        return region_count.error();
    auto certificate_count = json_unsigned(manifest.value(), "certificates", kMaximumRegions);
    if (!certificate_count)
        return certificate_count.error();
    auto lect_count = json_unsigned(manifest.value(), "lect_nodes", 10'000'000);
    if (!lect_count)
        return lect_count.error();
    auto robot_digest = json_string(manifest.value(), "robot_digest");
    if (!robot_digest)
        return robot_digest.error();
    auto scene_digest = json_string(manifest.value(), "scene_digest");
    if (!scene_digest)
        return scene_digest.error();
    if (robot_digest.value().size() != 64 || scene_digest.value().size() != 64) {
        return Result<SafeAtlas>::failure(StatusCode::CorruptData, "atlas identity digest length is invalid");
    }
    const auto* payloads = manifest.value().find("payloads");
    if (payloads == nullptr)
        return Result<SafeAtlas>::failure(StatusCode::CorruptData, "atlas manifest has no payload map");
    for (const auto relative : {"lect/nodes.bin", "regions.bin", "graph.bin", "certificates.json"}) {
        auto checksum = verify_payload_checksum(*payloads, directory, relative);
        if (!checksum)
            return checksum.error();
    }

    auto lect_bytes = internal::read_binary_file(directory / "lect" / "nodes.bin");
    if (!lect_bytes)
        return lect_bytes.error();
    auto tree = internal::decode_lect_tree(lect_bytes.value());
    if (!tree)
        return tree.error();
    if (tree.value().root_domain().dimension() != dimension.value() ||
        tree.value().size() != lect_count.value()) {
        return Result<SafeAtlas>::failure(StatusCode::CorruptData,
                                          "LECT payload does not match atlas manifest");
    }
    auto certificates_json = internal::read_json_file(directory / "certificates.json");
    if (!certificates_json)
        return certificates_json.error();
    auto certificates =
        decode_certificates(certificates_json.value(), robot_digest.value(), scene_digest.value());
    if (!certificates)
        return certificates.error();
    if (certificates.value().size() != certificate_count.value()) {
        return Result<SafeAtlas>::failure(StatusCode::CorruptData,
                                          "certificate count does not match manifest");
    }
    auto region_bytes = internal::read_binary_file(directory / "regions.bin");
    if (!region_bytes)
        return region_bytes.error();
    auto regions = decode_regions(region_bytes.value(), static_cast<std::size_t>(dimension.value()),
                                  certificates.value().size());
    if (!regions)
        return regions.error();
    if (regions.value().size() != region_count.value()) {
        return Result<SafeAtlas>::failure(StatusCode::CorruptData, "region count does not match manifest");
    }
    for (const auto& region : regions.value()) {
        bool inside_root = true;
        for (std::size_t axis = 0; axis < region.bounds.dimension(); ++axis) {
            const auto& root_axis = tree.value().root_domain().axes()[axis];
            const auto& region_axis = region.bounds.axes()[axis];
            if (region_axis.lower < root_axis.lower || region_axis.upper > root_axis.upper)
                inside_root = false;
        }
        if (!inside_root || region.component == 0 || !tree.value().node(region.source_node)) {
            return Result<SafeAtlas>::failure(StatusCode::CorruptData,
                                              "region lies outside LECT root or has no component");
        }
    }
    auto graph_bytes = internal::read_binary_file(directory / "graph.bin");
    if (!graph_bytes)
        return graph_bytes.error();
    auto graph = decode_graph(graph_bytes.value(), regions.value().size());
    if (!graph)
        return graph.error();

    SafeAtlas atlas;
    atlas.dimension_ = static_cast<std::size_t>(dimension.value());
    atlas.robot_digest_ = std::move(robot_digest).value();
    atlas.scene_digest_ = std::move(scene_digest).value();
    atlas.lect_ = LectSnapshot::from_tree(std::move(tree).value());
    atlas.regions_ = std::move(regions).value();
    atlas.certificates_ = std::move(certificates).value();
    atlas.adjacency_ = std::move(graph).value();
    auto valid = validate_atlas(atlas);
    if (!valid)
        return Result<SafeAtlas>::failure(StatusCode::CorruptData, valid.error().message,
                                          valid.error().context);
    atlas.rebuild_query_index();
    return atlas;
}

} // namespace rbfsafe
