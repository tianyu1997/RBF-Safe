#include <rbfsafe/region_database.h>
#include <rbfsafe/version.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/region_identity.h"
#include "internal/sha256.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <limits>
#include <set>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::uint64_t kSchema = 1;
constexpr std::size_t kMaximumDimension = 128;
constexpr std::size_t kMaximumRecords = 10'000'000;
constexpr std::size_t kMaximumCertificates = 20'000'000;
constexpr std::size_t kMaximumCoefficients = 100'000'000;
constexpr std::uintmax_t kMaximumPayloadBytes = 2'147'483'648ULL;

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

internal::Json number_array(std::span<const double> values) {
    internal::Json::Array result;
    result.reserve(values.size());
    for (const double value : values)
        result.emplace_back(value);
    return result;
}

internal::Json id_array(std::span<const RegionId> values) {
    internal::Json::Array result;
    result.reserve(values.size());
    for (const auto value : values)
        result.emplace_back(std::to_string(value));
    return result;
}

internal::Json aabb_json(const CspaceAabb& box) {
    internal::Json::Array axes;
    axes.reserve(box.dimension());
    for (const auto& axis : box.axes())
        axes.emplace_back(internal::Json::Array{axis.lower, axis.upper});
    return axes;
}

internal::Json envelope_json(const LinkEnvelope& envelope) {
    internal::Json::Array links;
    links.reserve(envelope.links.size());
    for (const auto& link : envelope.links) {
        links.emplace_back(
            internal::Json::Object{{"lower", number_array(link.lower)}, {"upper", number_array(link.upper)}});
    }
    return links;
}

internal::Json certificate_json(const Certificate& certificate) {
    return internal::Json::Object{
        {"clearance_lower_bound", certificate.clearance_lower_bound},
        {"id", certificate.id},
        {"level", evidence_level_name(certificate.level)},
        {"parent_certificate_id", certificate.parent_certificate_id},
        {"policy", internal::Json::Object{{"algorithm", certificate.policy.algorithm},
                                          {"algorithm_version", certificate.policy.algorithm_version},
                                          {"obstacle_padding", certificate.policy.obstacle_padding}}},
        {"robot_digest", certificate.robot_digest},
        {"scene_digest", certificate.scene_digest},
        {"subject_digest", certificate.subject_digest},
        {"transition_digest", certificate.transition_digest},
    };
}

internal::Json geometry_json(const RegionGeometry& geometry) {
    internal::Json::Object result{{"type", region_type_name(region_type(geometry))}};
    switch (region_type(geometry)) {
    case RegionType::Aabb:
        result.emplace("axes", aabb_json(std::get<CspaceAabb>(geometry)));
        break;
    case RegionType::Obb: {
        const auto& box = std::get<CspaceObb>(geometry);
        result.emplace("basis", number_array(box.basis()));
        result.emplace("center", number_array(box.center()));
        result.emplace("half_widths", number_array(box.half_widths()));
        break;
    }
    case RegionType::Portal: {
        const auto& portal = std::get<PortalGeometry>(geometry);
        result.emplace("enclosing_axes", aabb_json(portal.intersection.enclosing_aabb()));
        result.emplace("left_region", std::to_string(portal.left_region));
        result.emplace("normals", number_array(portal.intersection.normals()));
        result.emplace("offsets", number_array(portal.intersection.offsets()));
        result.emplace("right_region", std::to_string(portal.right_region));
        result.emplace("witness", number_array(portal.intersection.witness()));
        break;
    }
    case RegionType::TrajectoryTube: {
        const auto& tube = std::get<TrajectoryTubeGeometry>(geometry);
        internal::Json::Array centerline;
        centerline.reserve(tube.centerline.size());
        for (const auto& point : tube.centerline)
            centerline.emplace_back(number_array(point));
        result.emplace("cell_ids", id_array(tube.cell_ids));
        result.emplace("centerline", std::move(centerline));
        result.emplace("portal_ids", id_array(tube.portal_ids));
        break;
    }
    case RegionType::Zonotope: {
        const auto& region = std::get<CspaceZonotope>(geometry);
        result.emplace("center", number_array(region.center()));
        result.emplace("generator_count", std::to_string(region.generator_count()));
        result.emplace("generators", number_array(region.generators()));
        break;
    }
    case RegionType::Taylor: {
        const auto& region = std::get<CspaceTaylorRegion>(geometry);
        result.emplace("center", number_array(region.center()));
        result.emplace("linear", number_array(region.linear()));
        result.emplace("remainder_radii", number_array(region.remainder_radii()));
        result.emplace("variable_count", std::to_string(region.variable_count()));
        break;
    }
    }
    return result;
}

internal::Json payload_json(const RegionDatabase& database) {
    internal::Json::Array certificates;
    certificates.reserve(database.certificates().size());
    for (const auto& certificate : database.certificates())
        certificates.emplace_back(certificate_json(certificate));
    internal::Json::Array records;
    records.reserve(database.records().size());
    for (const auto& record : database.records()) {
        records.emplace_back(internal::Json::Object{
            {"certificate_index", static_cast<double>(record.certificate_index)},
            {"component", std::to_string(record.component)},
            {"dependency", envelope_json(record.dependency)},
            {"geometry", geometry_json(record.geometry)},
            {"id", std::to_string(record.id)},
            {"source", record.source},
        });
    }
    return internal::Json::Object{
        {"certificates", std::move(certificates)},
        {"format", "rbfsafe-region-database-records"},
        {"records", std::move(records)},
        {"schema", static_cast<double>(kSchema)},
    };
}

Result<std::string> string_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object())
        return Result<std::string>::failure(StatusCode::CorruptData, "expected JSON object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string()) {
        return Result<std::string>::failure(StatusCode::CorruptData, "missing or invalid string field",
                                            std::string(key));
    }
    return value->as_string();
}

Result<double> number_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object())
        return Result<double>::failure(StatusCode::CorruptData, "expected JSON object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number())) {
        return Result<double>::failure(StatusCode::CorruptData, "missing or invalid numeric field",
                                       std::string(key));
    }
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

Result<std::uint64_t> decimal(std::string_view text, std::string context, bool allow_zero = false) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        (!allow_zero && value == 0)) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData, "invalid decimal identifier",
                                              std::move(context));
    }
    return value;
}

Result<std::uint64_t> id_field(const internal::Json& object, std::string_view key, bool allow_zero = false) {
    auto text = string_field(object, key);
    if (!text)
        return text.error();
    return decimal(text.value(), std::string(key), allow_zero);
}

Result<std::vector<double>> number_array_field(const internal::Json& object, std::string_view key,
                                               std::size_t expected,
                                               std::size_t maximum = kMaximumCoefficients) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_array() || value->as_array().size() != expected ||
        expected > maximum) {
        return Result<std::vector<double>>::failure(StatusCode::CorruptData,
                                                    "numeric array has an invalid length", std::string(key));
    }
    std::vector<double> result;
    result.reserve(expected);
    for (const auto& coordinate : value->as_array()) {
        if (!coordinate.is_number() || !std::isfinite(coordinate.as_number())) {
            return Result<std::vector<double>>::failure(
                StatusCode::CorruptData, "numeric array contains a non-finite value", std::string(key));
        }
        result.push_back(coordinate.as_number());
    }
    return result;
}

Result<CspaceAabb> aabb_field(const internal::Json& object, std::string_view key, std::size_t dimension) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_array() || value->as_array().size() != dimension) {
        return Result<CspaceAabb>::failure(StatusCode::CorruptData, "AABB dimension is invalid",
                                           std::string(key));
    }
    std::vector<Interval> axes;
    axes.reserve(dimension);
    for (const auto& axis : value->as_array()) {
        if (!axis.is_array() || axis.as_array().size() != 2 || !axis.as_array()[0].is_number() ||
            !axis.as_array()[1].is_number()) {
            return Result<CspaceAabb>::failure(StatusCode::CorruptData, "AABB axis is invalid",
                                               std::string(key));
        }
        axes.emplace_back(axis.as_array()[0].as_number(), axis.as_array()[1].as_number());
    }
    CspaceAabb result(std::move(axes));
    if (!result.valid())
        return Result<CspaceAabb>::failure(StatusCode::CorruptData, "AABB is invalid");
    return result;
}

Result<std::vector<RegionId>> id_array_field(const internal::Json& object, std::string_view key,
                                             std::size_t maximum) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_array() || value->as_array().size() > maximum) {
        return Result<std::vector<RegionId>>::failure(StatusCode::CorruptData, "identifier array is invalid",
                                                      std::string(key));
    }
    std::vector<RegionId> result;
    result.reserve(value->as_array().size());
    for (const auto& item : value->as_array()) {
        if (!item.is_string())
            return Result<std::vector<RegionId>>::failure(
                StatusCode::CorruptData, "identifier array entry is invalid", std::string(key));
        auto id = decimal(item.as_string(), std::string(key));
        if (!id)
            return id.error();
        result.push_back(id.value());
    }
    return result;
}

Result<Certificate> decode_certificate(const internal::Json& object) {
    auto id = string_field(object, "id");
    auto level = string_field(object, "level");
    auto robot = string_field(object, "robot_digest");
    auto scene = string_field(object, "scene_digest");
    auto subject = string_field(object, "subject_digest");
    auto parent = string_field(object, "parent_certificate_id");
    auto transition = string_field(object, "transition_digest");
    auto clearance = number_field(object, "clearance_lower_bound");
    const auto* policy_json = object.find("policy");
    if (!id || !level || !robot || !scene || !subject || !parent || !transition || !clearance ||
        policy_json == nullptr || !policy_json->is_object()) {
        return Result<Certificate>::failure(StatusCode::CorruptData, "certificate record is incomplete");
    }
    auto algorithm = string_field(*policy_json, "algorithm");
    auto algorithm_version = string_field(*policy_json, "algorithm_version");
    auto padding = number_field(*policy_json, "obstacle_padding");
    if (!algorithm || !algorithm_version || !padding)
        return Result<Certificate>::failure(StatusCode::CorruptData, "certificate policy is incomplete");
    EvidenceLevel evidence = EvidenceLevel::Unknown;
    if (level.value() == "certified_region")
        evidence = EvidenceLevel::CertifiedRegion;
    else if (level.value() == "certified_connectivity")
        evidence = EvidenceLevel::CertifiedConnectivity;
    else {
        return Result<Certificate>::failure(StatusCode::CorruptData,
                                            "database certificate evidence level is invalid");
    }
    Certificate result{id.value(),
                       evidence,
                       robot.value(),
                       scene.value(),
                       {algorithm.value(), algorithm_version.value(), padding.value()},
                       clearance.value(),
                       subject.value(),
                       parent.value(),
                       transition.value()};
    if (!internal::valid_sha256(result.id) || !internal::valid_sha256(result.robot_digest) ||
        !internal::valid_sha256(result.scene_digest) || !internal::valid_sha256(result.subject_digest) ||
        result.policy.algorithm.empty() || result.policy.algorithm_version.empty() ||
        result.policy.obstacle_padding < 0.0 || result.clearance_lower_bound < 0.0 ||
        (result.parent_certificate_id.empty() != result.transition_digest.empty()) ||
        (!result.parent_certificate_id.empty() && (!internal::valid_sha256(result.parent_certificate_id) ||
                                                   !internal::valid_sha256(result.transition_digest))) ||
        internal::certificate_identity(result) != result.id) {
        return Result<Certificate>::failure(StatusCode::CorruptData, "certificate identity is invalid");
    }
    return result;
}

Result<LinkEnvelope> decode_envelope(const internal::Json& record) {
    const auto* links = record.find("dependency");
    if (links == nullptr || !links->is_array() || links->as_array().size() > kMaximumRecords) {
        return Result<LinkEnvelope>::failure(StatusCode::CorruptData, "workspace dependency is invalid");
    }
    LinkEnvelope result;
    result.links.reserve(links->as_array().size());
    for (const auto& link : links->as_array()) {
        auto lower = number_array_field(link, "lower", 3);
        auto upper = number_array_field(link, "upper", 3);
        if (!lower)
            return lower.error();
        if (!upper)
            return upper.error();
        WorkspaceAabb box;
        std::copy(lower.value().begin(), lower.value().end(), box.lower.begin());
        std::copy(upper.value().begin(), upper.value().end(), box.upper.begin());
        if (!box.valid())
            return Result<LinkEnvelope>::failure(StatusCode::CorruptData,
                                                 "workspace dependency box is invalid");
        result.links.push_back(box);
    }
    return result;
}

Result<RegionGeometry> decode_geometry(const internal::Json& object, std::size_t dimension) {
    auto type = string_field(object, "type");
    if (!type)
        return type.error();
    if (type.value() == "aabb") {
        auto box = aabb_field(object, "axes", dimension);
        if (!box)
            return box.error();
        return RegionGeometry(std::move(box).value());
    }
    if (type.value() == "obb") {
        auto center = number_array_field(object, "center", dimension);
        auto basis = number_array_field(object, "basis", dimension * dimension);
        auto half_widths = number_array_field(object, "half_widths", dimension);
        if (!center)
            return center.error();
        if (!basis)
            return basis.error();
        if (!half_widths)
            return half_widths.error();
        auto box = CspaceObb::create(std::move(center).value(), std::move(basis).value(),
                                     std::move(half_widths).value());
        if (!box)
            return Result<RegionGeometry>::failure(StatusCode::CorruptData, "OBB record is invalid");
        return RegionGeometry(std::move(box).value());
    }
    if (type.value() == "portal") {
        auto left = id_field(object, "left_region");
        auto right = id_field(object, "right_region");
        auto witness = number_array_field(object, "witness", dimension);
        auto offsets_value = object.find("offsets");
        if (!left || !right || !witness || offsets_value == nullptr || !offsets_value->is_array() ||
            offsets_value->as_array().empty() || offsets_value->as_array().size() > kMaximumCoefficients) {
            return Result<RegionGeometry>::failure(StatusCode::CorruptData, "portal record is incomplete");
        }
        const std::size_t constraints = offsets_value->as_array().size();
        auto normals = number_array_field(object, "normals", constraints * dimension);
        auto offsets = number_array_field(object, "offsets", constraints);
        auto enclosure = aabb_field(object, "enclosing_axes", dimension);
        if (!normals)
            return normals.error();
        if (!offsets)
            return offsets.error();
        if (!enclosure)
            return enclosure.error();
        auto portal = CspacePortal::create(std::move(normals).value(), std::move(offsets).value(),
                                           std::move(witness).value(), std::move(enclosure).value());
        if (!portal)
            return Result<RegionGeometry>::failure(StatusCode::CorruptData, "portal half-spaces are invalid");
        return RegionGeometry(PortalGeometry{left.value(), right.value(), std::move(portal).value()});
    }
    if (type.value() == "trajectory_tube") {
        auto cells = id_array_field(object, "cell_ids", kMaximumRecords);
        auto portals = id_array_field(object, "portal_ids", kMaximumRecords);
        const auto* centerline_json = object.find("centerline");
        if (!cells || !portals || centerline_json == nullptr || !centerline_json->is_array() ||
            centerline_json->as_array().size() > kMaximumRecords) {
            return Result<RegionGeometry>::failure(StatusCode::CorruptData,
                                                   "trajectory tube record is invalid");
        }
        TrajectoryTubeGeometry tube;
        tube.cell_ids = std::move(cells).value();
        tube.portal_ids = std::move(portals).value();
        tube.centerline.reserve(centerline_json->as_array().size());
        for (const auto& point : centerline_json->as_array()) {
            if (!point.is_array() || point.as_array().size() != dimension) {
                return Result<RegionGeometry>::failure(StatusCode::CorruptData,
                                                       "trajectory centerline is invalid");
            }
            Configuration configuration;
            configuration.reserve(dimension);
            for (const auto& coordinate : point.as_array()) {
                if (!coordinate.is_number() || !std::isfinite(coordinate.as_number())) {
                    return Result<RegionGeometry>::failure(StatusCode::CorruptData,
                                                           "trajectory centerline is non-finite");
                }
                configuration.push_back(coordinate.as_number());
            }
            tube.centerline.push_back(std::move(configuration));
        }
        if (!tube.valid(dimension))
            return Result<RegionGeometry>::failure(StatusCode::CorruptData,
                                                   "trajectory tube is inconsistent");
        return RegionGeometry(std::move(tube));
    }
    if (type.value() == "zonotope") {
        auto variables = id_field(object, "generator_count", true);
        auto center = number_array_field(object, "center", dimension);
        if (!variables || !center || variables.value() > kMaximumCoefficients ||
            variables.value() > kMaximumCoefficients / dimension) {
            return Result<RegionGeometry>::failure(StatusCode::ResourceLimit,
                                                   "zonotope coefficient count is invalid");
        }
        auto generators =
            number_array_field(object, "generators", static_cast<std::size_t>(variables.value()) * dimension);
        if (!generators)
            return generators.error();
        auto region =
            CspaceZonotope::create(std::move(center).value(), static_cast<std::size_t>(variables.value()),
                                   std::move(generators).value());
        if (!region)
            return Result<RegionGeometry>::failure(StatusCode::CorruptData, "zonotope record is invalid");
        return RegionGeometry(std::move(region).value());
    }
    if (type.value() == "taylor") {
        auto variables = id_field(object, "variable_count", true);
        auto center = number_array_field(object, "center", dimension);
        auto remainder = number_array_field(object, "remainder_radii", dimension);
        if (!variables || !center || !remainder || variables.value() > kMaximumCoefficients ||
            variables.value() > kMaximumCoefficients / dimension) {
            return Result<RegionGeometry>::failure(StatusCode::ResourceLimit,
                                                   "Taylor coefficient count is invalid");
        }
        auto linear =
            number_array_field(object, "linear", static_cast<std::size_t>(variables.value()) * dimension);
        if (!linear)
            return linear.error();
        auto region =
            CspaceTaylorRegion::create(std::move(center).value(), static_cast<std::size_t>(variables.value()),
                                       std::move(linear).value(), std::move(remainder).value());
        if (!region)
            return Result<RegionGeometry>::failure(StatusCode::CorruptData, "Taylor record is invalid");
        return RegionGeometry(std::move(region).value());
    }
    return Result<RegionGeometry>::failure(StatusCode::IncompatibleFormat, "unknown region geometry type",
                                           type.value());
}

bool primary(RegionType type) {
    return type == RegionType::Aabb || type == RegionType::Obb || type == RegionType::Zonotope ||
           type == RegionType::Taylor;
}

void rebuild_graph(std::vector<RegionRecord>& records, std::vector<std::vector<std::size_t>>& adjacency) {
    adjacency.assign(records.size(), {});
    std::unordered_map<RegionId, std::size_t> by_id;
    for (std::size_t index = 0; index < records.size(); ++index)
        by_id.emplace(records[index].id, index);
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto* portal = std::get_if<PortalGeometry>(&records[index].geometry);
        if (portal == nullptr)
            continue;
        const auto left = by_id.find(portal->left_region);
        const auto right = by_id.find(portal->right_region);
        if (left == by_id.end() || right == by_id.end())
            continue;
        adjacency[index] = {left->second, right->second};
        adjacency[left->second].push_back(index);
        adjacency[right->second].push_back(index);
        adjacency[left->second].push_back(right->second);
        adjacency[right->second].push_back(left->second);
    }
    for (auto& neighbors : adjacency) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
    ComponentId component = 0;
    std::vector<bool> visited(records.size(), false);
    for (std::size_t start = 0; start < records.size(); ++start) {
        if (!primary(region_type(records[start].geometry)) || visited[start])
            continue;
        ++component;
        std::deque<std::size_t> queue{start};
        visited[start] = true;
        while (!queue.empty()) {
            const auto current = queue.front();
            queue.pop_front();
            records[current].component = component;
            for (const auto neighbor : adjacency[current]) {
                if (primary(region_type(records[neighbor].geometry)) && !visited[neighbor]) {
                    visited[neighbor] = true;
                    queue.push_back(neighbor);
                }
            }
        }
    }
    for (auto& record : records) {
        if (const auto* portal = std::get_if<PortalGeometry>(&record.geometry)) {
            record.component = records[by_id.at(portal->left_region)].component;
        } else if (const auto* tube = std::get_if<TrajectoryTubeGeometry>(&record.geometry)) {
            record.component = records[by_id.at(tube->cell_ids.front())].component;
        }
    }
}

bool cell_contains(const RegionGeometry& geometry, std::span<const double> point) {
    if (const auto* box = std::get_if<CspaceAabb>(&geometry))
        return box->contains(point, 1e-9);
    if (const auto* box = std::get_if<CspaceObb>(&geometry))
        return box->contains(point, 1e-9);
    return false;
}

Result<void> validate_database(const RegionDatabase& database) {
    if (database.dimension() == 0 || database.dimension() > kMaximumDimension ||
        !internal::valid_sha256(database.robot_digest()) ||
        !internal::valid_sha256(database.scene_digest()) || database.scene_version().empty() ||
        database.records().empty() || database.records().size() > kMaximumRecords ||
        database.certificates().empty() || database.certificates().size() > kMaximumCertificates) {
        return Result<void>::failure(StatusCode::InternalError, "region database metadata is inconsistent");
    }
    std::set<std::string> certificate_ids;
    for (const auto& certificate : database.certificates()) {
        if (certificate.robot_digest != database.robot_digest() ||
            certificate.scene_digest != database.scene_digest() ||
            internal::certificate_identity(certificate) != certificate.id ||
            !certificate_ids.insert(certificate.id).second) {
            return Result<void>::failure(StatusCode::InternalError,
                                         "region database certificate is inconsistent");
        }
    }
    std::set<RegionId> record_ids;
    std::unordered_map<RegionId, const RegionRecord*> by_id;
    for (const auto& record : database.records()) {
        if (record.id == 0 || !record_ids.insert(record.id).second ||
            record.certificate_index >= database.certificates().size()) {
            return Result<void>::failure(StatusCode::InternalError,
                                         "region database record identity is inconsistent");
        }
        by_id.emplace(record.id, &record);
    }
    for (const auto& record : database.records()) {
        const auto& certificate = database.certificates()[record.certificate_index];
        std::string subject;
        if (primary(region_type(record.geometry))) {
            auto digest = internal::primary_region_subject_digest(record.geometry);
            if (!digest)
                return digest.error();
            subject = std::move(digest).value();
            if (certificate.level != EvidenceLevel::CertifiedRegion)
                return Result<void>::failure(StatusCode::InternalError,
                                             "primary record has wrong evidence level");
        } else if (const auto* portal = std::get_if<PortalGeometry>(&record.geometry)) {
            const auto left = by_id.find(portal->left_region);
            const auto right = by_id.find(portal->right_region);
            if (!portal->valid() || left == by_id.end() || right == by_id.end() ||
                !cell_contains(left->second->geometry, portal->intersection.witness()) ||
                !cell_contains(right->second->geometry, portal->intersection.witness())) {
                return Result<void>::failure(StatusCode::InternalError, "portal record is inconsistent");
            }
            subject = internal::portal_subject_digest(portal->left_region, portal->right_region,
                                                      portal->intersection);
            if (certificate.level != EvidenceLevel::CertifiedConnectivity)
                return Result<void>::failure(StatusCode::InternalError,
                                             "portal record has wrong evidence level");
        } else {
            const auto& tube = std::get<TrajectoryTubeGeometry>(record.geometry);
            if (!tube.valid(database.dimension()))
                return Result<void>::failure(StatusCode::InternalError, "trajectory tube record is invalid");
            for (const auto id : tube.cell_ids) {
                const auto cell = by_id.find(id);
                if (cell == by_id.end() || !primary(region_type(cell->second->geometry)))
                    return Result<void>::failure(StatusCode::InternalError,
                                                 "trajectory tube cell is missing");
            }
            for (const auto id : tube.portal_ids) {
                const auto portal_record = by_id.find(id);
                if (portal_record == by_id.end() ||
                    region_type(portal_record->second->geometry) != RegionType::Portal)
                    return Result<void>::failure(StatusCode::InternalError,
                                                 "trajectory tube portal is missing");
            }
            subject = internal::trajectory_tube_subject_digest(tube);
            if (certificate.level != EvidenceLevel::CertifiedConnectivity)
                return Result<void>::failure(StatusCode::InternalError,
                                             "trajectory tube has wrong evidence level");
        }
        if (certificate.subject_digest != subject)
            return Result<void>::failure(StatusCode::InternalError,
                                         "record certificate subject does not match geometry");
        if (!record.dependency.links.empty() &&
            !std::all_of(record.dependency.links.begin(), record.dependency.links.end(),
                         [](const auto& link) { return link.valid(); })) {
            return Result<void>::failure(StatusCode::InternalError, "record workspace dependency is invalid");
        }
    }
    return Result<void>::success();
}

} // namespace

Result<void> save_region_database_directory(const RegionDatabase& database,
                                            const std::filesystem::path& directory,
                                            const SaveOptions& options) {
    auto valid = validate_database(database);
    if (!valid)
        return valid;
    if (directory.empty() || directory == directory.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "database destination must be a specific directory");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(directory, error);
    if (error)
        return Result<void>::failure(StatusCode::IoError, "failed to inspect database destination");
    if (destination_exists && !options.overwrite)
        return Result<void>::failure(StatusCode::IoError, "database destination already exists");
    if (!directory.parent_path().empty()) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (error)
            return Result<void>::failure(StatusCode::IoError, "failed to create database parent directory");
    }
    const auto temporary = unique_sibling(directory, ".tmp-");
    std::filesystem::create_directories(temporary, error);
    if (error)
        return Result<void>::failure(StatusCode::IoError, "failed to create database temporary directory");
    auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    };
    const std::string payload = payload_json(database).dump(true) + "\n";
    auto written = internal::write_text_file(temporary / "regions.json", payload);
    if (!written) {
        cleanup();
        return written;
    }
    internal::Json manifest(internal::Json::Object{
        {"certificates", static_cast<double>(database.certificates().size())},
        {"dimension", static_cast<double>(database.dimension())},
        {"format", "rbfsafe-region-database"},
        {"library_version", kVersion},
        {"payload_sha256", internal::sha256(payload)},
        {"records", static_cast<double>(database.records().size())},
        {"robot_digest", database.robot_digest()},
        {"scene_digest", database.scene_digest()},
        {"scene_version", database.scene_version()},
        {"schema", static_cast<double>(kSchema)},
    });
    written = internal::write_text_file(temporary / "manifest.json", manifest.dump(true) + "\n");
    if (!written) {
        cleanup();
        return written;
    }
    std::filesystem::path backup;
    if (destination_exists) {
        backup = unique_sibling(directory, ".backup-");
        std::filesystem::rename(directory, backup, error);
        if (error) {
            cleanup();
            return Result<void>::failure(StatusCode::IoError, "failed to stage existing database");
        }
    }
    std::filesystem::rename(temporary, directory, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, directory, ignored);
        }
        cleanup();
        return Result<void>::failure(StatusCode::IoError, "failed to publish database directory");
    }
    if (destination_exists) {
        std::error_code ignored;
        std::filesystem::remove_all(backup, ignored);
    }
    return Result<void>::success();
}

Result<RegionDatabase> load_region_database_directory(const std::filesystem::path& directory) {
    auto manifest = internal::read_json_file(directory / "manifest.json");
    if (!manifest)
        return manifest.error();
    auto format = string_field(manifest.value(), "format");
    auto schema = size_field(manifest.value(), "schema", 1000);
    auto dimension = size_field(manifest.value(), "dimension", kMaximumDimension);
    auto records_count = size_field(manifest.value(), "records", kMaximumRecords);
    auto certificates_count = size_field(manifest.value(), "certificates", kMaximumCertificates);
    auto robot = string_field(manifest.value(), "robot_digest");
    auto scene = string_field(manifest.value(), "scene_digest");
    auto scene_version = string_field(manifest.value(), "scene_version");
    auto checksum = string_field(manifest.value(), "payload_sha256");
    if (!format || !schema || !dimension || !records_count || !certificates_count || !robot || !scene ||
        !scene_version || !checksum) {
        return Result<RegionDatabase>::failure(StatusCode::CorruptData, "database manifest is incomplete");
    }
    if (format.value() != "rbfsafe-region-database" || schema.value() != kSchema) {
        return Result<RegionDatabase>::failure(StatusCode::IncompatibleFormat,
                                               "unsupported database manifest schema");
    }
    if (dimension.value() == 0 || records_count.value() == 0 || certificates_count.value() == 0 ||
        !internal::valid_sha256(robot.value()) || !internal::valid_sha256(scene.value()) ||
        scene_version.value().empty() || !internal::valid_sha256(checksum.value())) {
        return Result<RegionDatabase>::failure(StatusCode::CorruptData,
                                               "database manifest metadata is invalid");
    }
    std::error_code error;
    const auto payload_size = std::filesystem::file_size(directory / "regions.json", error);
    if (error)
        return Result<RegionDatabase>::failure(StatusCode::IoError, "failed to inspect database payload");
    if (payload_size > kMaximumPayloadBytes)
        return Result<RegionDatabase>::failure(StatusCode::ResourceLimit,
                                               "database payload exceeds size limit");
    auto actual_checksum = internal::sha256_file(directory / "regions.json");
    if (!actual_checksum)
        return actual_checksum.error();
    if (actual_checksum.value() != checksum.value())
        return Result<RegionDatabase>::failure(StatusCode::CorruptData, "database payload checksum mismatch");
    auto payload = internal::read_json_file(directory / "regions.json");
    if (!payload)
        return payload.error();
    auto payload_format = string_field(payload.value(), "format");
    auto payload_schema = size_field(payload.value(), "schema", 1000);
    const auto* certificates_json = payload.value().find("certificates");
    const auto* records_json = payload.value().find("records");
    if (!payload_format || !payload_schema || payload_format.value() != "rbfsafe-region-database-records" ||
        payload_schema.value() != kSchema || certificates_json == nullptr || !certificates_json->is_array() ||
        records_json == nullptr || !records_json->is_array() ||
        certificates_json->as_array().size() != certificates_count.value() ||
        records_json->as_array().size() != records_count.value()) {
        return Result<RegionDatabase>::failure(StatusCode::CorruptData,
                                               "database payload metadata is inconsistent");
    }
    RegionDatabase database;
    database.dimension_ = dimension.value();
    database.robot_digest_ = std::move(robot).value();
    database.scene_digest_ = std::move(scene).value();
    database.scene_version_ = std::move(scene_version).value();
    database.certificates_.reserve(certificates_count.value());
    for (const auto& item : certificates_json->as_array()) {
        auto certificate = decode_certificate(item);
        if (!certificate)
            return certificate.error();
        database.certificates_.push_back(std::move(certificate).value());
    }
    database.records_.reserve(records_count.value());
    std::vector<ComponentId> serialized_components;
    serialized_components.reserve(records_count.value());
    for (const auto& item : records_json->as_array()) {
        auto id = id_field(item, "id");
        auto component = id_field(item, "component", true);
        auto certificate_index = size_field(item, "certificate_index", certificates_count.value() - 1);
        auto source = string_field(item, "source");
        auto dependency = decode_envelope(item);
        const auto* geometry_json_value = item.find("geometry");
        if (!id || !component || !certificate_index || !source || !dependency ||
            geometry_json_value == nullptr) {
            return Result<RegionDatabase>::failure(StatusCode::CorruptData, "database record is incomplete");
        }
        auto geometry = decode_geometry(*geometry_json_value, database.dimension_);
        if (!geometry)
            return geometry.error();
        serialized_components.push_back(component.value());
        database.records_.push_back({id.value(), std::move(geometry).value(), certificate_index.value(),
                                     component.value(), std::move(dependency).value(), source.value()});
    }
    auto valid = validate_database(database);
    if (!valid) {
        return Result<RegionDatabase>::failure(StatusCode::CorruptData, valid.error().message,
                                               valid.error().context);
    }
    rebuild_graph(database.records_, database.adjacency_);
    for (std::size_t index = 0; index < database.records_.size(); ++index) {
        if (database.records_[index].component != serialized_components[index]) {
            return Result<RegionDatabase>::failure(StatusCode::CorruptData,
                                                   "database component labels are inconsistent");
        }
    }
    return database;
}

Result<void> RegionDatabase::save(const std::filesystem::path& directory, const SaveOptions& options) const {
    return save_region_database_directory(*this, directory, options);
}

Result<RegionDatabase> RegionDatabase::load(const std::filesystem::path& directory) {
    return load_region_database_directory(directory);
}

} // namespace rbfsafe
