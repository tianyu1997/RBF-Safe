#include <rbfsafe/atlas.h>
#include <rbfsafe/version.h>

#include "internal/atlas_identity.h"
#include "internal/binary.h"
#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/lect_codec.h"
#include "internal/scene_delta_utils.h"
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
constexpr std::string_view kDependencyMagic = "RBFDEPS1";
constexpr std::uint64_t kMaximumRegions = 10'000'000;
constexpr std::uint64_t kMaximumEdges = 200'000'000;
constexpr std::uint64_t kMaximumDependencyLinks = 100'000'000;
constexpr std::uint64_t kMaximumLinksPerRegion = 4096;
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

Result<bool> json_bool(const internal::Json& object, std::string_view key) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_bool()) {
        return Result<bool>::failure(StatusCode::CorruptData, "invalid boolean field", std::string(key));
    }
    return value->as_bool();
}

Result<std::uint64_t> json_u64_string(const internal::Json& object, std::string_view key) {
    auto text = json_string(object, key);
    if (!text)
        return text.error();
    if (text.value().empty()) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData, "empty unsigned integer field",
                                              std::string(key));
    }
    std::uint64_t value = 0;
    for (const char digit : text.value()) {
        if (digit < '0' || digit > '9' ||
            value >
                (std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(digit - '0')) / 10u) {
            return Result<std::uint64_t>::failure(StatusCode::CorruptData, "invalid unsigned integer field",
                                                  std::string(key));
        }
        value = value * 10u + static_cast<std::uint64_t>(digit - '0');
    }
    return value;
}

internal::BinaryWriter encode_regions(const SafeAtlas& atlas) {
    internal::BinaryWriter writer;
    writer.text(kRegionMagic);
    writer.u32(atlas.storage_schema());
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
                                               std::size_t expected_dimension, std::size_t certificate_count,
                                               std::uint32_t expected_schema) {
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
    if (schema.value() != expected_schema)
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
    writer.u32(atlas.storage_schema());
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

Result<std::vector<std::vector<std::size_t>>>
decode_graph(std::span<const std::byte> bytes, std::size_t expected_vertices, std::uint32_t expected_schema) {
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
    if (schema.value() != expected_schema)
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

internal::BinaryWriter encode_dependencies(const SafeAtlas& atlas) {
    internal::BinaryWriter writer;
    writer.text(kDependencyMagic);
    writer.u32(atlas.storage_schema());
    writer.u64(static_cast<std::uint64_t>(atlas.dependencies().size()));
    for (const auto& dependency : atlas.dependencies()) {
        writer.u64(dependency.region_id);
        writer.u64(static_cast<std::uint64_t>(dependency.envelope.links.size()));
        for (const auto& link : dependency.envelope.links) {
            for (const auto value : link.lower)
                writer.f64(value);
            for (const auto value : link.upper)
                writer.f64(value);
        }
    }
    writer.u64(static_cast<std::uint64_t>(atlas.repair_domains().size()));
    for (const auto& domain : atlas.repair_domains()) {
        writer.u64(domain.id);
        writer.string(domain.source_node.path());
        for (const auto& axis : domain.bounds.axes()) {
            writer.f64(axis.lower);
            writer.f64(axis.upper);
        }
    }
    return writer;
}

struct DecodedDependencies {
    std::vector<RegionDependency> regions;
    std::vector<AtlasRepairDomain> repair_domains;
};

Result<DecodedDependencies> decode_dependencies(std::span<const std::byte> bytes,
                                                const std::vector<SafeRegion>& regions, std::size_t dimension,
                                                std::uint32_t expected_schema) {
    internal::BinaryReader reader(bytes);
    auto magic = reader.fixed_text(kDependencyMagic.size());
    if (!magic)
        return magic.error();
    if (magic.value() != kDependencyMagic) {
        return Result<DecodedDependencies>::failure(StatusCode::IncompatibleFormat,
                                                    "invalid dependency payload magic");
    }
    auto schema = reader.u32();
    if (!schema)
        return schema.error();
    if (schema.value() != expected_schema) {
        return Result<DecodedDependencies>::failure(StatusCode::IncompatibleFormat,
                                                    "unsupported dependency payload schema");
    }
    auto count = reader.u64();
    if (!count)
        return count.error();
    if (count.value() != regions.size()) {
        return Result<DecodedDependencies>::failure(StatusCode::CorruptData,
                                                    "dependency count does not match regions");
    }
    std::uint64_t total_links = 0;
    DecodedDependencies output;
    output.regions.reserve(regions.size());
    for (std::size_t index = 0; index < regions.size(); ++index) {
        auto region_id = reader.u64();
        if (!region_id)
            return region_id.error();
        auto link_count = reader.u64();
        if (!link_count)
            return link_count.error();
        if (region_id.value() != regions[index].id || link_count.value() > kMaximumLinksPerRegion ||
            total_links > kMaximumDependencyLinks - link_count.value()) {
            return Result<DecodedDependencies>::failure(StatusCode::ResourceLimit,
                                                        "invalid or excessive dependency record");
        }
        total_links += link_count.value();
        RegionDependency dependency;
        dependency.region_id = region_id.value();
        dependency.envelope.links.reserve(static_cast<std::size_t>(link_count.value()));
        for (std::uint64_t link_index = 0; link_index < link_count.value(); ++link_index) {
            WorkspaceAabb link;
            for (auto& value : link.lower) {
                auto decoded = reader.f64();
                if (!decoded)
                    return decoded.error();
                value = decoded.value();
            }
            for (auto& value : link.upper) {
                auto decoded = reader.f64();
                if (!decoded)
                    return decoded.error();
                value = decoded.value();
            }
            if (!link.valid()) {
                return Result<DecodedDependencies>::failure(StatusCode::CorruptData,
                                                            "stored dependency envelope is invalid");
            }
            dependency.envelope.links.push_back(link);
        }
        output.regions.push_back(std::move(dependency));
    }
    auto repair_count = reader.u64();
    if (!repair_count)
        return repair_count.error();
    if (repair_count.value() > kMaximumRegions) {
        return Result<DecodedDependencies>::failure(StatusCode::ResourceLimit,
                                                    "repair-domain count exceeds limit");
    }
    output.repair_domains.reserve(static_cast<std::size_t>(repair_count.value()));
    std::set<RegionId> repair_ids;
    for (std::uint64_t index = 0; index < repair_count.value(); ++index) {
        auto id = reader.u64();
        if (!id)
            return id.error();
        auto source = reader.string(4096);
        if (!source)
            return source.error();
        LectNodeKey source_node(source.value());
        if (id.value() == 0 || !repair_ids.insert(id.value()).second || !source_node.valid()) {
            return Result<DecodedDependencies>::failure(StatusCode::CorruptData,
                                                        "repair-domain identity is invalid");
        }
        std::vector<Interval> axes;
        axes.reserve(dimension);
        for (std::size_t axis = 0; axis < dimension; ++axis) {
            auto lower = reader.f64();
            if (!lower)
                return lower.error();
            auto upper = reader.f64();
            if (!upper)
                return upper.error();
            axes.emplace_back(lower.value(), upper.value());
        }
        CspaceAabb bounds(std::move(axes));
        if (!bounds.valid()) {
            return Result<DecodedDependencies>::failure(StatusCode::CorruptData,
                                                        "repair-domain bounds are invalid");
        }
        output.repair_domains.push_back({id.value(), std::move(bounds), std::move(source_node)});
    }
    if (!reader.finished()) {
        return Result<DecodedDependencies>::failure(StatusCode::CorruptData,
                                                    "dependency payload has trailing bytes");
    }
    return output;
}

internal::Json certificate_json(const Certificate& certificate, std::uint32_t schema) {
    internal::Json::Object record{
        {"clearance_lower_bound", certificate.clearance_lower_bound},
        {"id", certificate.id},
        {"level", evidence_level_name(certificate.level)},
        {"policy", internal::Json::Object{{"algorithm", certificate.policy.algorithm},
                                          {"algorithm_version", certificate.policy.algorithm_version},
                                          {"obstacle_padding", certificate.policy.obstacle_padding}}},
        {"robot_digest", certificate.robot_digest},
        {"scene_digest", certificate.scene_digest},
    };
    if (schema >= 2) {
        record.emplace("parent_certificate_id", certificate.parent_certificate_id);
        record.emplace("subject_digest", certificate.subject_digest);
        record.emplace("transition_digest", certificate.transition_digest);
    }
    return record;
}

internal::Json encode_certificates(const SafeAtlas& atlas) {
    internal::Json::Array records;
    for (const auto& certificate : atlas.certificates())
        records.emplace_back(certificate_json(certificate, atlas.storage_schema()));
    return internal::Json::Object{
        {"certificates", std::move(records)},
        {"format", "rbfsafe-certificates"},
        {"schema", static_cast<double>(atlas.storage_schema())},
    };
}

Result<std::vector<Certificate>> decode_certificates(const internal::Json& root,
                                                     const std::string& robot_digest,
                                                     const std::string& scene_digest,
                                                     std::uint32_t expected_schema) {
    if (!root.is_object())
        return Result<std::vector<Certificate>>::failure(StatusCode::CorruptData,
                                                         "certificate document must be object");
    auto format = json_string(root, "format");
    if (!format)
        return format.error();
    auto schema = json_unsigned(root, "schema", 1000);
    if (!schema)
        return schema.error();
    if (format.value() != "rbfsafe-certificates" || schema.value() != expected_schema) {
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
        Certificate certificate;
        certificate.id = id.value();
        certificate.level = EvidenceLevel::CertifiedRegion;
        certificate.robot_digest = robot.value();
        certificate.scene_digest = scene.value();
        certificate.policy = {algorithm.value(), algorithm_version.value(), padding_json->as_number()};
        certificate.clearance_lower_bound = clearance_json->as_number();
        if (expected_schema >= 2) {
            auto subject = json_string(record, "subject_digest");
            if (!subject)
                return subject.error();
            auto parent = json_string(record, "parent_certificate_id");
            if (!parent)
                return parent.error();
            auto transition = json_string(record, "transition_digest");
            if (!transition)
                return transition.error();
            certificate.subject_digest = std::move(subject).value();
            certificate.parent_certificate_id = std::move(parent).value();
            certificate.transition_digest = std::move(transition).value();
            const bool direct =
                certificate.parent_certificate_id.empty() && certificate.transition_digest.empty();
            const bool inherited = internal::valid_sha256(certificate.parent_certificate_id) &&
                                   internal::valid_sha256(certificate.transition_digest);
            if (!internal::valid_sha256(certificate.subject_digest) || (!direct && !inherited)) {
                return Result<std::vector<Certificate>>::failure(
                    StatusCode::CorruptData, "certificate lineage is invalid", certificate.id);
            }
        }
        if (internal::certificate_identity(certificate) != certificate.id) {
            return Result<std::vector<Certificate>>::failure(
                StatusCode::CorruptData, "certificate digest does not match content", certificate.id);
        }
        certificates.push_back(std::move(certificate));
    }
    return certificates;
}

Result<void> validate_atlas(const SafeAtlas& atlas) {
    if ((atlas.storage_schema() != 1 && atlas.storage_schema() != kAtlasSchemaVersion) ||
        atlas.dimension() == 0 || atlas.dimension() > kMaximumDimension || !atlas.lect().valid() ||
        atlas.lect().root_domain().dimension() != atlas.dimension() ||
        atlas.regions().size() != atlas.certificates().size() ||
        atlas.adjacency().size() != atlas.regions().size() ||
        (atlas.storage_schema() >= 2 && atlas.dependencies().size() != atlas.regions().size())) {
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
            internal::certificate_identity(certificate) != certificate.id) {
            return Result<void>::failure(StatusCode::InternalError, "atlas certificate is inconsistent",
                                         std::to_string(index));
        }
        if (atlas.storage_schema() >= 2) {
            const auto& dependency = atlas.dependencies()[index];
            const bool direct =
                certificate.parent_certificate_id.empty() && certificate.transition_digest.empty();
            const bool inherited = internal::valid_sha256(certificate.parent_certificate_id) &&
                                   certificate.transition_digest == atlas.version_info().transition_digest;
            if (dependency.region_id != region.id || dependency.envelope.links.size() != atlas.dimension() ||
                certificate.subject_digest != internal::cspace_aabb_subject_digest(region.bounds) ||
                (!direct && !inherited) ||
                !std::all_of(dependency.envelope.links.begin(), dependency.envelope.links.end(),
                             [](const auto& link) { return link.valid(); })) {
                return Result<void>::failure(StatusCode::InternalError, "atlas dependency is inconsistent",
                                             std::to_string(index));
            }
        }
    }
    if (atlas.storage_schema() >= 2) {
        std::set<RegionId> repair_ids;
        for (const auto& domain : atlas.repair_domains()) {
            bool inside_root = domain.bounds.valid() && domain.bounds.dimension() == atlas.dimension();
            for (std::size_t axis = 0; inside_root && axis < atlas.dimension(); ++axis) {
                const auto& root = atlas.lect().root_domain().axes()[axis];
                const auto& value = domain.bounds.axes()[axis];
                inside_root = value.lower >= root.lower && value.upper <= root.upper;
            }
            if (domain.id == 0 || !repair_ids.insert(domain.id).second || !inside_root ||
                !domain.source_node.valid() || !atlas.lect().node(domain.source_node)) {
                return Result<void>::failure(StatusCode::InternalError,
                                             "atlas repair domain is inconsistent");
            }
        }
        const auto& version = atlas.version_info();
        const bool initial =
            version.sequence == 0 && version.parent_id.empty() && version.transition_digest.empty();
        const bool derived = version.sequence > 0 && internal::valid_sha256(version.parent_id) &&
                             internal::valid_sha256(version.transition_digest);
        if (version.scene_version.empty() || version.scene_digest != atlas.scene_digest() ||
            !internal::valid_sha256(version.id) || (!initial && !derived) ||
            internal::atlas_version_identity(atlas) != version.id) {
            return Result<void>::failure(StatusCode::InternalError, "atlas version identity is inconsistent");
        }
        if ((initial && atlas.transition().has_value()) || (derived && !atlas.transition())) {
            return Result<void>::failure(StatusCode::InternalError,
                                         "atlas transition presence is inconsistent");
        }
        if (atlas.transition()) {
            auto transition_status = internal::validate_scene_delta(*atlas.transition());
            if (!transition_status || atlas.transition()->digest != version.transition_digest ||
                atlas.transition()->to_digest != atlas.scene_digest() ||
                atlas.transition()->to_version != version.scene_version) {
                return Result<void>::failure(StatusCode::InternalError,
                                             "atlas scene transition is inconsistent");
            }
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
    auto dependencies_payload = encode_dependencies(atlas);
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
    if (atlas.storage_schema() >= 2) {
        write = dependencies_payload.save(temporary / "dependencies.bin");
        if (!write) {
            cleanup();
            return write;
        }
    }
    const auto certificates_text = encode_certificates(atlas).dump(true) + "\n";
    write = internal::write_text_file(temporary / "certificates.json", certificates_text);
    if (!write) {
        cleanup();
        return write;
    }
    if (atlas.storage_schema() >= 2 && atlas.transition()) {
        write =
            internal::write_text_file(temporary / "transition.json",
                                      internal::encode_scene_delta(*atlas.transition()).dump(true) + "\n");
        if (!write) {
            cleanup();
            return write;
        }
    }

    internal::Json::Object payloads;
    std::vector<std::string> relative_payloads{"lect/nodes.bin", "regions.bin", "graph.bin",
                                               "certificates.json"};
    if (atlas.storage_schema() >= 2)
        relative_payloads.emplace_back("dependencies.bin");
    if (atlas.storage_schema() >= 2 && atlas.transition())
        relative_payloads.emplace_back("transition.json");
    for (const auto& relative : relative_payloads) {
        auto checksum = internal::sha256_file(temporary / std::filesystem::path(relative));
        if (!checksum) {
            cleanup();
            return checksum.error();
        }
        payloads.emplace(relative, checksum.value());
    }
    internal::Json::Object manifest_fields{
        {"certificates", static_cast<double>(atlas.certificates().size())},
        {"dimension", static_cast<double>(atlas.dimension())},
        {"format", "rbfsafe-atlas"},
        {"lect_nodes", static_cast<double>(atlas.lect().size())},
        {"library_version", kVersion},
        {"payloads", std::move(payloads)},
        {"regions", static_cast<double>(atlas.regions().size())},
        {"robot_digest", atlas.robot_digest()},
        {"scene_digest", atlas.scene_digest()},
        {"schema", static_cast<double>(atlas.storage_schema())},
    };
    if (atlas.storage_schema() >= 2) {
        manifest_fields.emplace("atlas_version",
                                internal::Json::Object{
                                    {"id", atlas.version_info().id},
                                    {"parent_id", atlas.version_info().parent_id},
                                    {"scene_version", atlas.version_info().scene_version},
                                    {"scene_digest", atlas.version_info().scene_digest},
                                    {"sequence", std::to_string(atlas.version_info().sequence)},
                                    {"transition_digest", atlas.version_info().transition_digest},
                                });
        manifest_fields.emplace("dependencies", static_cast<double>(atlas.dependencies().size()));
        manifest_fields.emplace("repair_domains", static_cast<double>(atlas.repair_domains().size()));
        manifest_fields.emplace("transition", atlas.transition().has_value());
    }
    internal::Json manifest(std::move(manifest_fields));
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
    if (format.value() != "rbfsafe-atlas" || (schema.value() != 1 && schema.value() != kAtlasSchemaVersion)) {
        return Result<SafeAtlas>::failure(StatusCode::IncompatibleFormat,
                                          "unsupported atlas format or schema");
    }
    const auto atlas_schema = static_cast<std::uint32_t>(schema.value());
    bool has_transition = false;
    if (atlas_schema >= 2) {
        auto transition = json_bool(manifest.value(), "transition");
        if (!transition)
            return transition.error();
        has_transition = transition.value();
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
    std::vector<std::string_view> required_payloads{"lect/nodes.bin", "regions.bin", "graph.bin",
                                                    "certificates.json"};
    if (atlas_schema >= 2)
        required_payloads.emplace_back("dependencies.bin");
    if (has_transition)
        required_payloads.emplace_back("transition.json");
    for (const auto relative : required_payloads) {
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
    auto certificates = decode_certificates(certificates_json.value(), robot_digest.value(),
                                            scene_digest.value(), atlas_schema);
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
                                  certificates.value().size(), atlas_schema);
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
    auto graph = decode_graph(graph_bytes.value(), regions.value().size(), atlas_schema);
    if (!graph)
        return graph.error();

    std::vector<RegionDependency> dependencies;
    std::vector<AtlasRepairDomain> repair_domains;
    std::optional<SceneDelta> transition;
    if (atlas_schema >= 2) {
        auto dependency_count = json_unsigned(manifest.value(), "dependencies", kMaximumRegions);
        if (!dependency_count)
            return dependency_count.error();
        if (dependency_count.value() != regions.value().size()) {
            return Result<SafeAtlas>::failure(StatusCode::CorruptData,
                                              "dependency count does not match manifest regions");
        }
        auto dependency_bytes = internal::read_binary_file(directory / "dependencies.bin");
        if (!dependency_bytes)
            return dependency_bytes.error();
        auto repair_count = json_unsigned(manifest.value(), "repair_domains", kMaximumRegions);
        if (!repair_count)
            return repair_count.error();
        auto decoded = decode_dependencies(dependency_bytes.value(), regions.value(),
                                           static_cast<std::size_t>(dimension.value()), atlas_schema);
        if (!decoded)
            return decoded.error();
        if (decoded.value().repair_domains.size() != repair_count.value()) {
            return Result<SafeAtlas>::failure(StatusCode::CorruptData,
                                              "repair-domain count does not match manifest");
        }
        dependencies = std::move(decoded.value().regions);
        repair_domains = std::move(decoded.value().repair_domains);
    } else {
        dependencies.reserve(regions.value().size());
        for (const auto& region : regions.value())
            dependencies.push_back({region.id, {}});
    }
    if (has_transition) {
        auto transition_json = internal::read_json_file(directory / "transition.json");
        if (!transition_json)
            return transition_json.error();
        auto decoded_transition = internal::decode_scene_delta(transition_json.value());
        if (!decoded_transition)
            return decoded_transition.error();
        transition = std::move(decoded_transition).value();
    }

    SafeAtlas atlas;
    atlas.storage_schema_ = atlas_schema;
    atlas.dimension_ = static_cast<std::size_t>(dimension.value());
    atlas.robot_digest_ = std::move(robot_digest).value();
    atlas.scene_digest_ = std::move(scene_digest).value();
    atlas.lect_ = LectSnapshot::from_tree(std::move(tree).value());
    atlas.regions_ = std::move(regions).value();
    atlas.certificates_ = std::move(certificates).value();
    atlas.dependencies_ = std::move(dependencies);
    atlas.repair_domains_ = std::move(repair_domains);
    atlas.transition_ = std::move(transition);
    atlas.adjacency_ = std::move(graph).value();
    if (atlas_schema >= 2) {
        const auto* version = manifest.value().find("atlas_version");
        if (version == nullptr || !version->is_object()) {
            return Result<SafeAtlas>::failure(StatusCode::CorruptData,
                                              "atlas manifest has no version record");
        }
        auto id = json_string(*version, "id");
        if (!id)
            return id.error();
        auto parent = json_string(*version, "parent_id");
        if (!parent)
            return parent.error();
        auto scene_version = json_string(*version, "scene_version");
        if (!scene_version)
            return scene_version.error();
        auto sequence = json_u64_string(*version, "sequence");
        if (!sequence)
            return sequence.error();
        auto transition_digest = json_string(*version, "transition_digest");
        if (!transition_digest)
            return transition_digest.error();
        auto version_scene_digest = json_string(*version, "scene_digest");
        if (!version_scene_digest)
            return version_scene_digest.error();
        atlas.version_info_ = {sequence.value(),
                               std::move(id).value(),
                               std::move(parent).value(),
                               std::move(scene_version).value(),
                               std::move(version_scene_digest).value(),
                               std::move(transition_digest).value()};
    } else {
        atlas.version_info_.sequence = 0;
        atlas.version_info_.scene_digest = atlas.scene_digest_;
        atlas.version_info_.id = internal::atlas_version_identity(atlas);
    }
    auto valid = validate_atlas(atlas);
    if (!valid)
        return Result<SafeAtlas>::failure(StatusCode::CorruptData, valid.error().message,
                                          valid.error().context);
    atlas.rebuild_query_index();
    return atlas;
}

} // namespace rbfsafe
