#include <rbfsafe/corridor.h>
#include <rbfsafe/version.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rbfsafe {
namespace {

constexpr std::size_t maximum_dimension = 128;
constexpr std::size_t maximum_regions = 10'000'000;
constexpr std::size_t maximum_portals = 20'000'000;
constexpr std::uintmax_t maximum_payload_bytes = 1'073'741'824;
constexpr std::uint64_t corridor_schema = 1;

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

internal::Json configuration_json(std::span<const double> configuration) {
    internal::Json::Array values;
    values.reserve(configuration.size());
    for (const double value : configuration)
        values.emplace_back(value);
    return values;
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
        {"subject_digest", certificate.subject_digest},
    };
}

std::string obb_subject_digest(const CspaceObb& obb) {
    internal::Json::Array basis;
    basis.reserve(obb.basis().size());
    for (const double value : obb.basis())
        basis.emplace_back(value);
    return internal::sha256(internal::Json(internal::Json::Object{
                                               {"basis", std::move(basis)},
                                               {"center", configuration_json(obb.center())},
                                               {"half_widths", configuration_json(obb.half_widths())},
                                               {"type", "cspace-obb"},
                                           })
                                .dump(false));
}

std::string portal_subject_digest(RegionId left, RegionId right, std::span<const double> witness) {
    return internal::sha256(internal::Json(internal::Json::Object{
                                               {"left_region", std::to_string(left)},
                                               {"right_region", std::to_string(right)},
                                               {"type", "witness-portal"},
                                               {"witness", configuration_json(witness)},
                                           })
                                .dump(false));
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 std::string context = "corridor") {
    if (!object.is_object())
        return Result<std::string>::failure(StatusCode::CorruptData, "expected JSON object", context);
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string())
        return Result<std::string>::failure(StatusCode::CorruptData, "missing or invalid string field",
                                            std::string(key));
    return value->as_string();
}

Result<double> number_field(const internal::Json& object, std::string_view key,
                            std::string context = "corridor") {
    if (!object.is_object())
        return Result<double>::failure(StatusCode::CorruptData, "expected JSON object", context);
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number()))
        return Result<double>::failure(StatusCode::CorruptData, "missing or invalid numeric field",
                                       std::string(key));
    return value->as_number();
}

Result<std::size_t> size_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    auto number = number_field(object, key);
    if (!number)
        return number.error();
    if (number.value() < 0.0 || std::floor(number.value()) != number.value() ||
        number.value() > static_cast<double>(maximum)) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "numeric field exceeds resource limit",
                                            std::string(key));
    }
    return static_cast<std::size_t>(number.value());
}

Result<std::uint64_t> decimal_id(const internal::Json& object, std::string_view key) {
    auto text = string_field(object, key);
    if (!text)
        return text.error();
    std::uint64_t value = 0;
    const auto* first = text.value().data();
    const auto* last = first + text.value().size();
    const auto parsed = std::from_chars(first, last, value);
    if (first == last || parsed.ec != std::errc{} || parsed.ptr != last || value == 0) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData, "invalid decimal identifier",
                                              std::string(key));
    }
    return value;
}

Result<Configuration> configuration_field(const internal::Json& object, std::string_view key,
                                          std::size_t dimension) {
    if (!object.is_object())
        return Result<Configuration>::failure(StatusCode::CorruptData, "expected JSON object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_array() || value->as_array().size() != dimension) {
        return Result<Configuration>::failure(StatusCode::CorruptData, "configuration dimension is invalid",
                                              std::string(key));
    }
    Configuration result;
    result.reserve(dimension);
    for (const auto& coordinate : value->as_array()) {
        if (!coordinate.is_number() || !std::isfinite(coordinate.as_number())) {
            return Result<Configuration>::failure(
                StatusCode::CorruptData, "configuration contains a non-finite value", std::string(key));
        }
        result.push_back(coordinate.as_number());
    }
    return result;
}

Result<std::vector<double>> basis_field(const internal::Json& object, std::size_t dimension) {
    const auto* value = object.find("basis");
    if (value == nullptr || !value->is_array() || value->as_array().size() != dimension * dimension) {
        return Result<std::vector<double>>::failure(StatusCode::CorruptData,
                                                    "OBB basis dimension is invalid");
    }
    std::vector<double> result;
    result.reserve(value->as_array().size());
    for (const auto& coefficient : value->as_array()) {
        if (!coefficient.is_number() || !std::isfinite(coefficient.as_number())) {
            return Result<std::vector<double>>::failure(StatusCode::CorruptData,
                                                        "OBB basis contains a non-finite value");
        }
        result.push_back(coefficient.as_number());
    }
    return result;
}

Result<Certificate> decode_certificate(const internal::Json& object, EvidenceLevel expected_level,
                                       const std::string& robot_digest, const std::string& scene_digest,
                                       const std::string& expected_subject) {
    auto id = string_field(object, "id");
    auto level = string_field(object, "level");
    auto robot = string_field(object, "robot_digest");
    auto scene = string_field(object, "scene_digest");
    auto subject = string_field(object, "subject_digest");
    auto clearance = number_field(object, "clearance_lower_bound");
    if (!id)
        return id.error();
    if (!level)
        return level.error();
    if (!robot)
        return robot.error();
    if (!scene)
        return scene.error();
    if (!subject)
        return subject.error();
    if (!clearance)
        return clearance.error();
    const auto* policy = object.find("policy");
    if (policy == nullptr || !policy->is_object())
        return Result<Certificate>::failure(StatusCode::CorruptData,
                                            "corridor certificate policy is invalid");
    auto algorithm = string_field(*policy, "algorithm");
    auto algorithm_version = string_field(*policy, "algorithm_version");
    auto padding = number_field(*policy, "obstacle_padding");
    if (!algorithm)
        return algorithm.error();
    if (!algorithm_version)
        return algorithm_version.error();
    if (!padding)
        return padding.error();
    if (clearance.value() < 0.0 || padding.value() < 0.0 || robot.value() != robot_digest ||
        scene.value() != scene_digest || subject.value() != expected_subject ||
        level.value() != evidence_level_name(expected_level)) {
        return Result<Certificate>::failure(StatusCode::CorruptData,
                                            "corridor certificate identity fields do not match");
    }
    auto certificate = internal::make_subject_certificate(
        expected_level, robot.value(), scene.value(),
        {algorithm.value(), algorithm_version.value(), padding.value()}, subject.value(), clearance.value());
    if (!certificate)
        return Result<Certificate>::failure(StatusCode::CorruptData,
                                            "corridor certificate parameters are invalid");
    if (certificate.value().id != id.value()) {
        return Result<Certificate>::failure(StatusCode::CorruptData,
                                            "corridor certificate digest does not match content");
    }
    return certificate;
}

Result<void> validate_corridor(const HipacCorridor& corridor) {
    if (corridor.dimension() == 0 || corridor.dimension() > maximum_dimension ||
        corridor.robot_digest().size() != 64 || corridor.scene_digest().size() != 64 ||
        corridor.regions().size() > maximum_regions || corridor.portals().size() > maximum_portals) {
        return Result<void>::failure(StatusCode::InternalError, "corridor metadata is inconsistent");
    }
    std::set<RegionId> region_ids;
    std::unordered_map<RegionId, std::size_t> index_by_id;
    double corridor_padding = 0.0;
    for (std::size_t index = 0; index < corridor.regions().size(); ++index) {
        const auto& region = corridor.regions()[index];
        const std::string subject = obb_subject_digest(region.bounds);
        if (region.id == 0 || !region_ids.insert(region.id).second || !region.bounds.valid() ||
            region.bounds.dimension() != corridor.dimension() || region.component == 0 ||
            region.entry.size() != corridor.dimension() || region.exit.size() != corridor.dimension() ||
            !region.bounds.contains(region.entry, 1e-12) || !region.bounds.contains(region.exit, 1e-12) ||
            !std::isfinite(region.start_fraction) || !std::isfinite(region.end_fraction) ||
            region.start_fraction < 0.0 || region.end_fraction > 1.0 ||
            region.start_fraction > region.end_fraction ||
            region.certificate.level != EvidenceLevel::CertifiedRegion ||
            region.certificate.subject_digest != subject ||
            region.certificate.robot_digest != corridor.robot_digest() ||
            region.certificate.scene_digest != corridor.scene_digest() ||
            region.certificate.policy.algorithm != "ifk-aa-link-iaabb-obb-enclosure" ||
            region.certificate.policy.algorithm_version != "1") {
            return Result<void>::failure(StatusCode::InternalError, "corridor region is inconsistent",
                                         std::to_string(index));
        }
        auto rebuilt = internal::make_subject_certificate(
            region.certificate.level, region.certificate.robot_digest, region.certificate.scene_digest,
            region.certificate.policy, region.certificate.subject_digest,
            region.certificate.clearance_lower_bound);
        if (!rebuilt || rebuilt.value().id != region.certificate.id)
            return Result<void>::failure(StatusCode::InternalError,
                                         "corridor region certificate is inconsistent",
                                         std::to_string(index));
        if (index == 0)
            corridor_padding = region.certificate.policy.obstacle_padding;
        else if (region.certificate.policy.obstacle_padding != corridor_padding)
            return Result<void>::failure(StatusCode::InternalError,
                                         "corridor region padding policies differ");
        if (index > 0) {
            const auto& previous = corridor.regions()[index - 1];
            if (region.segment_index < previous.segment_index ||
                (region.segment_index == previous.segment_index &&
                 region.start_fraction < previous.end_fraction - 1e-15)) {
                return Result<void>::failure(StatusCode::InternalError,
                                             "corridor regions are not in path order");
            }
        }
        index_by_id.emplace(region.id, index);
    }

    std::set<PortalId> portal_ids;
    std::vector<std::vector<std::size_t>> adjacency(corridor.regions().size());
    for (std::size_t index = 0; index < corridor.portals().size(); ++index) {
        const auto& portal = corridor.portals()[index];
        const auto left = index_by_id.find(portal.left_region);
        const auto right = index_by_id.find(portal.right_region);
        const std::string subject =
            portal_subject_digest(portal.left_region, portal.right_region, portal.witness);
        if (portal.id == 0 || !portal_ids.insert(portal.id).second || left == index_by_id.end() ||
            right == index_by_id.end() || left->second == right->second ||
            portal.witness.size() != corridor.dimension() ||
            !corridor.regions()[left->second].bounds.contains(portal.witness, 1e-12) ||
            !corridor.regions()[right->second].bounds.contains(portal.witness, 1e-12) ||
            portal.certificate.level != EvidenceLevel::CertifiedConnectivity ||
            portal.certificate.subject_digest != subject ||
            portal.certificate.robot_digest != corridor.robot_digest() ||
            portal.certificate.scene_digest != corridor.scene_digest() ||
            portal.certificate.policy.algorithm != "hipac-witness-portal" ||
            portal.certificate.policy.algorithm_version != "1" ||
            portal.certificate.policy.obstacle_padding !=
                corridor.regions()[left->second].certificate.policy.obstacle_padding ||
            portal.certificate.policy.obstacle_padding !=
                corridor.regions()[right->second].certificate.policy.obstacle_padding ||
            portal.certificate.clearance_lower_bound !=
                std::min(corridor.regions()[left->second].certificate.clearance_lower_bound,
                         corridor.regions()[right->second].certificate.clearance_lower_bound)) {
            return Result<void>::failure(StatusCode::InternalError, "corridor portal is inconsistent",
                                         std::to_string(index));
        }
        auto rebuilt = internal::make_subject_certificate(
            portal.certificate.level, portal.certificate.robot_digest, portal.certificate.scene_digest,
            portal.certificate.policy, portal.certificate.subject_digest,
            portal.certificate.clearance_lower_bound);
        if (!rebuilt || rebuilt.value().id != portal.certificate.id)
            return Result<void>::failure(StatusCode::InternalError,
                                         "corridor portal certificate is inconsistent",
                                         std::to_string(index));
        adjacency[left->second].push_back(right->second);
        adjacency[right->second].push_back(left->second);
    }

    ComponentId component = 0;
    std::vector<bool> visited(corridor.regions().size(), false);
    for (std::size_t start = 0; start < corridor.regions().size(); ++start) {
        if (visited[start])
            continue;
        ++component;
        std::deque<std::size_t> frontier{start};
        visited[start] = true;
        while (!frontier.empty()) {
            const std::size_t current = frontier.front();
            frontier.pop_front();
            if (corridor.regions()[current].component != component) {
                return Result<void>::failure(StatusCode::InternalError,
                                             "corridor component labels are inconsistent");
            }
            for (const auto neighbor : adjacency[current]) {
                if (!visited[neighbor]) {
                    visited[neighbor] = true;
                    frontier.push_back(neighbor);
                }
            }
        }
    }
    return Result<void>::success();
}

internal::Json encode_corridor(const HipacCorridor& corridor) {
    internal::Json::Array regions;
    regions.reserve(corridor.regions().size());
    for (const auto& region : corridor.regions()) {
        internal::Json::Array basis;
        basis.reserve(region.bounds.basis().size());
        for (const double value : region.bounds.basis())
            basis.emplace_back(value);
        regions.emplace_back(internal::Json::Object{
            {"basis", std::move(basis)},
            {"center", configuration_json(region.bounds.center())},
            {"certificate", certificate_json(region.certificate)},
            {"component", std::to_string(region.component)},
            {"end_fraction", region.end_fraction},
            {"entry", configuration_json(region.entry)},
            {"exit", configuration_json(region.exit)},
            {"half_widths", configuration_json(region.bounds.half_widths())},
            {"id", std::to_string(region.id)},
            {"segment_index", static_cast<double>(region.segment_index)},
            {"start_fraction", region.start_fraction},
        });
    }
    internal::Json::Array portals;
    portals.reserve(corridor.portals().size());
    for (const auto& portal : corridor.portals()) {
        portals.emplace_back(internal::Json::Object{
            {"certificate", certificate_json(portal.certificate)},
            {"id", std::to_string(portal.id)},
            {"left_region", std::to_string(portal.left_region)},
            {"right_region", std::to_string(portal.right_region)},
            {"witness", configuration_json(portal.witness)},
        });
    }
    return internal::Json::Object{
        {"format", "rbfsafe-corridor-records"},
        {"portals", std::move(portals)},
        {"regions", std::move(regions)},
        {"schema", static_cast<double>(corridor_schema)},
    };
}

struct DecodedCorridorData {
    std::size_t dimension = 0;
    std::string robot_digest;
    std::string scene_digest;
    std::vector<CertifiedObbRegion> regions;
    std::vector<PortalRegion> portals;
};

Result<DecodedCorridorData> decode_corridor(const internal::Json& root, std::size_t dimension,
                                            const std::string& robot_digest, const std::string& scene_digest,
                                            std::size_t expected_regions, std::size_t expected_portals) {
    auto format = string_field(root, "format");
    auto schema = size_field(root, "schema", 1000);
    if (!format)
        return format.error();
    if (!schema)
        return schema.error();
    if (format.value() != "rbfsafe-corridor-records" || schema.value() != corridor_schema) {
        return Result<DecodedCorridorData>::failure(StatusCode::IncompatibleFormat,
                                                    "unsupported corridor payload schema");
    }
    const auto* regions_json = root.find("regions");
    const auto* portals_json = root.find("portals");
    if (regions_json == nullptr || !regions_json->is_array() ||
        regions_json->as_array().size() != expected_regions || portals_json == nullptr ||
        !portals_json->is_array() || portals_json->as_array().size() != expected_portals) {
        return Result<DecodedCorridorData>::failure(StatusCode::CorruptData,
                                                    "corridor record counts do not match manifest");
    }

    DecodedCorridorData corridor;
    corridor.dimension = dimension;
    corridor.robot_digest = robot_digest;
    corridor.scene_digest = scene_digest;
    corridor.regions.reserve(expected_regions);
    for (const auto& record : regions_json->as_array()) {
        auto id = decimal_id(record, "id");
        auto component = decimal_id(record, "component");
        auto segment = size_field(record, "segment_index", maximum_regions);
        auto start = number_field(record, "start_fraction");
        auto end = number_field(record, "end_fraction");
        auto center = configuration_field(record, "center", dimension);
        auto half_widths = configuration_field(record, "half_widths", dimension);
        auto basis = basis_field(record, dimension);
        auto entry = configuration_field(record, "entry", dimension);
        auto exit = configuration_field(record, "exit", dimension);
        if (!id)
            return id.error();
        if (!component)
            return component.error();
        if (!segment)
            return segment.error();
        if (!start)
            return start.error();
        if (!end)
            return end.error();
        if (!center)
            return center.error();
        if (!half_widths)
            return half_widths.error();
        if (!basis)
            return basis.error();
        if (!entry)
            return entry.error();
        if (!exit)
            return exit.error();
        auto obb = CspaceObb::create(std::move(center).value(), std::move(basis).value(),
                                     std::move(half_widths).value());
        if (!obb)
            return Result<DecodedCorridorData>::failure(StatusCode::CorruptData, "corridor OBB is invalid");
        const auto* certificate_json_value = record.find("certificate");
        if (certificate_json_value == nullptr)
            return Result<DecodedCorridorData>::failure(StatusCode::CorruptData,
                                                        "corridor region certificate is missing");
        auto certificate = decode_certificate(*certificate_json_value, EvidenceLevel::CertifiedRegion,
                                              robot_digest, scene_digest, obb_subject_digest(obb.value()));
        if (!certificate)
            return certificate.error();
        corridor.regions.push_back({id.value(), std::move(obb).value(), std::move(certificate).value(),
                                    component.value(), segment.value(), start.value(), end.value(),
                                    std::move(entry).value(), std::move(exit).value()});
    }

    corridor.portals.reserve(expected_portals);
    for (const auto& record : portals_json->as_array()) {
        auto id = decimal_id(record, "id");
        auto left = decimal_id(record, "left_region");
        auto right = decimal_id(record, "right_region");
        auto witness = configuration_field(record, "witness", dimension);
        if (!id)
            return id.error();
        if (!left)
            return left.error();
        if (!right)
            return right.error();
        if (!witness)
            return witness.error();
        const auto* certificate_json_value = record.find("certificate");
        if (certificate_json_value == nullptr)
            return Result<DecodedCorridorData>::failure(StatusCode::CorruptData,
                                                        "corridor portal certificate is missing");
        auto certificate = decode_certificate(
            *certificate_json_value, EvidenceLevel::CertifiedConnectivity, robot_digest, scene_digest,
            portal_subject_digest(left.value(), right.value(), witness.value()));
        if (!certificate)
            return certificate.error();
        corridor.portals.push_back({id.value(), left.value(), right.value(), std::move(witness).value(),
                                    std::move(certificate).value()});
    }
    return corridor;
}

} // namespace

Result<void> save_corridor_directory(const HipacCorridor& corridor, const std::filesystem::path& directory,
                                     const SaveOptions& options) {
    auto valid = validate_corridor(corridor);
    if (!valid)
        return valid;
    if (directory.empty() || directory == directory.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "corridor destination must be a specific directory");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(directory, error);
    if (error)
        return Result<void>::failure(StatusCode::IoError, "failed to inspect corridor destination",
                                     directory.string());
    if (destination_exists && !options.overwrite)
        return Result<void>::failure(StatusCode::IoError, "corridor destination already exists",
                                     directory.string());
    if (!directory.parent_path().empty()) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (error)
            return Result<void>::failure(StatusCode::IoError, "failed to create corridor parent directory",
                                         directory.parent_path().string());
    }

    const auto temporary = unique_sibling(directory, ".tmp-");
    std::filesystem::create_directories(temporary, error);
    if (error)
        return Result<void>::failure(StatusCode::IoError, "failed to create corridor temporary directory",
                                     temporary.string());
    auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    };

    const std::string payload = encode_corridor(corridor).dump(true) + "\n";
    auto write = internal::write_text_file(temporary / "corridor.json", payload);
    if (!write) {
        cleanup();
        return write;
    }
    const std::string payload_digest = internal::sha256(payload);
    internal::Json manifest(internal::Json::Object{
        {"dimension", static_cast<double>(corridor.dimension())},
        {"format", "rbfsafe-corridor"},
        {"library_version", kVersion},
        {"payload_sha256", payload_digest},
        {"portals", static_cast<double>(corridor.portals().size())},
        {"regions", static_cast<double>(corridor.regions().size())},
        {"robot_digest", corridor.robot_digest()},
        {"scene_digest", corridor.scene_digest()},
        {"schema", static_cast<double>(corridor_schema)},
    });
    write = internal::write_text_file(temporary / "manifest.json", manifest.dump(true) + "\n");
    if (!write) {
        cleanup();
        return write;
    }

    std::filesystem::path backup;
    if (destination_exists) {
        backup = unique_sibling(directory, ".backup-");
        std::filesystem::rename(directory, backup, error);
        if (error) {
            cleanup();
            return Result<void>::failure(
                StatusCode::IoError, "failed to stage existing corridor for overwrite", directory.string());
        }
    }
    std::filesystem::rename(temporary, directory, error);
    if (error) {
        if (destination_exists) {
            std::error_code restore_error;
            std::filesystem::rename(backup, directory, restore_error);
        }
        cleanup();
        return Result<void>::failure(StatusCode::IoError, "failed to publish corridor directory",
                                     directory.string());
    }
    if (destination_exists) {
        std::error_code ignored;
        std::filesystem::remove_all(backup, ignored);
    }
    return Result<void>::success();
}

Result<HipacCorridor> load_corridor_directory(const std::filesystem::path& directory) {
    auto manifest = internal::read_json_file(directory / "manifest.json");
    if (!manifest)
        return manifest.error();
    auto format = string_field(manifest.value(), "format");
    auto schema = size_field(manifest.value(), "schema", 1000);
    auto dimension = size_field(manifest.value(), "dimension", maximum_dimension);
    auto region_count = size_field(manifest.value(), "regions", maximum_regions);
    auto portal_count = size_field(manifest.value(), "portals", maximum_portals);
    auto robot_digest = string_field(manifest.value(), "robot_digest");
    auto scene_digest = string_field(manifest.value(), "scene_digest");
    auto expected_checksum = string_field(manifest.value(), "payload_sha256");
    if (!format)
        return format.error();
    if (!schema)
        return schema.error();
    if (!dimension)
        return dimension.error();
    if (!region_count)
        return region_count.error();
    if (!portal_count)
        return portal_count.error();
    if (!robot_digest)
        return robot_digest.error();
    if (!scene_digest)
        return scene_digest.error();
    if (!expected_checksum)
        return expected_checksum.error();
    if (format.value() != "rbfsafe-corridor" || schema.value() != corridor_schema) {
        return Result<HipacCorridor>::failure(StatusCode::IncompatibleFormat,
                                              "unsupported corridor manifest schema");
    }
    if (dimension.value() == 0 || robot_digest.value().size() != 64 || scene_digest.value().size() != 64 ||
        expected_checksum.value().size() != 64) {
        return Result<HipacCorridor>::failure(StatusCode::CorruptData,
                                              "corridor manifest metadata is invalid");
    }
    std::error_code size_error;
    const auto payload_size = std::filesystem::file_size(directory / "corridor.json", size_error);
    if (size_error)
        return Result<HipacCorridor>::failure(StatusCode::IoError, "failed to inspect corridor payload size");
    if (payload_size > maximum_payload_bytes)
        return Result<HipacCorridor>::failure(StatusCode::ResourceLimit,
                                              "corridor payload exceeds size limit");
    auto actual_checksum = internal::sha256_file(directory / "corridor.json");
    if (!actual_checksum)
        return actual_checksum.error();
    if (actual_checksum.value() != expected_checksum.value()) {
        return Result<HipacCorridor>::failure(StatusCode::CorruptData, "corridor payload checksum mismatch");
    }
    auto payload = internal::read_json_file(directory / "corridor.json");
    if (!payload)
        return payload.error();
    auto decoded = decode_corridor(payload.value(), dimension.value(), robot_digest.value(),
                                   scene_digest.value(), region_count.value(), portal_count.value());
    if (!decoded)
        return decoded.error();
    HipacCorridor corridor;
    corridor.dimension_ = decoded.value().dimension;
    corridor.robot_digest_ = std::move(decoded.value().robot_digest);
    corridor.scene_digest_ = std::move(decoded.value().scene_digest);
    corridor.regions_ = std::move(decoded.value().regions);
    corridor.portals_ = std::move(decoded.value().portals);
    auto valid = validate_corridor(corridor);
    if (!valid)
        return Result<HipacCorridor>::failure(StatusCode::CorruptData, valid.error().message,
                                              valid.error().context);
    return corridor;
}

} // namespace rbfsafe
