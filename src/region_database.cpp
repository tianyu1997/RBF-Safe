#include <rbfsafe/region_database.h>

#include "internal/certificate_utils.h"
#include "internal/region_identity.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace rbfsafe {
namespace {

bool primary_region(RegionType type) {
    return type == RegionType::Aabb || type == RegionType::Obb || type == RegionType::Zonotope ||
           type == RegionType::Taylor;
}

Result<bool> geometry_contains(const RegionGeometry& geometry, std::span<const double> configuration,
                               double tolerance) {
    switch (region_type(geometry)) {
    case RegionType::Aabb:
        return std::get<CspaceAabb>(geometry).contains(configuration, tolerance);
    case RegionType::Obb:
        return std::get<CspaceObb>(geometry).contains(configuration, tolerance);
    case RegionType::Portal:
        return std::get<PortalGeometry>(geometry).intersection.contains(configuration, tolerance);
    case RegionType::Zonotope:
        return std::get<CspaceZonotope>(geometry).contains(configuration, tolerance);
    case RegionType::Taylor:
        return std::get<CspaceTaylorRegion>(geometry).contains(configuration, tolerance);
    case RegionType::TrajectoryTube:
        break;
    }
    return false;
}

CspaceAabb geometry_enclosure(const RegionGeometry& geometry) {
    switch (region_type(geometry)) {
    case RegionType::Aabb:
        return std::get<CspaceAabb>(geometry);
    case RegionType::Obb:
        return std::get<CspaceObb>(geometry).enclosing_aabb();
    case RegionType::Portal:
        return std::get<PortalGeometry>(geometry).intersection.enclosing_aabb();
    case RegionType::Zonotope:
        return std::get<CspaceZonotope>(geometry).enclosing_aabb();
    case RegionType::Taylor:
        return std::get<CspaceTaylorRegion>(geometry).enclosing_aabb();
    case RegionType::TrajectoryTube:
        break;
    }
    return {};
}

Result<CspaceObb> portal_cell_obb(const RegionGeometry& geometry) {
    if (const auto* box = std::get_if<CspaceObb>(&geometry))
        return *box;
    const auto* aabb = std::get_if<CspaceAabb>(&geometry);
    if (aabb == nullptr || !aabb->valid()) {
        return Result<CspaceObb>::failure(StatusCode::InvalidArgument,
                                          "region type cannot participate in OBB portal discovery");
    }
    Configuration center = aabb->center();
    Configuration half_widths;
    std::vector<double> basis(aabb->dimension() * aabb->dimension(), 0.0);
    half_widths.reserve(aabb->dimension());
    for (std::size_t axis = 0; axis < aabb->dimension(); ++axis) {
        basis[axis * aabb->dimension() + axis] = 1.0;
        half_widths.push_back(0.5 * aabb->axes()[axis].width());
    }
    return CspaceObb::create(std::move(center), std::move(basis), std::move(half_widths));
}

Result<RegionId> allocate_id(const std::string& identity, std::set<RegionId>& used) {
    RegionId candidate = internal::digest_prefix_id(identity);
    for (std::size_t attempt = 0; attempt <= used.size(); ++attempt) {
        if (candidate != 0 && used.insert(candidate).second)
            return candidate;
        ++candidate;
        if (candidate == 0)
            candidate = 1;
    }
    return Result<RegionId>::failure(StatusCode::ResourceLimit, "unable to allocate a unique region ID");
}

Result<void> validate_portal_options(const PortalDiscoveryOptions& options) {
    if (options.maximum_candidate_pairs == 0 || options.maximum_portals == 0 ||
        options.maximum_iterations == 0 || options.maximum_iterations > 1'000'000 ||
        !std::isfinite(options.feasibility_tolerance) || options.feasibility_tolerance < 0.0 ||
        options.feasibility_tolerance > 1e-3) {
        return Result<void>::failure(StatusCode::InvalidArgument, "invalid portal discovery options");
    }
    return Result<void>::success();
}

struct PortalAssembly {
    PortalDiscoveryStats stats;
    std::map<std::pair<RegionId, RegionId>, RegionId> portal_by_pair;
};

Result<PortalAssembly> discover_portals(const std::string& robot_digest, const std::string& scene_digest,
                                        std::vector<RegionRecord>& records,
                                        std::vector<Certificate>& certificates, std::set<RegionId>& used_ids,
                                        const PortalDiscoveryOptions& options) {
    auto options_status = validate_portal_options(options);
    if (!options_status)
        return options_status.error();
    std::vector<std::size_t> cells;
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto type = region_type(records[index].geometry);
        if (type == RegionType::Aabb || type == RegionType::Obb)
            cells.push_back(index);
    }
    std::sort(cells.begin(), cells.end(),
              [&](std::size_t left, std::size_t right) { return records[left].id < records[right].id; });
    PortalAssembly output;
    for (std::size_t left_position = 0; left_position < cells.size(); ++left_position) {
        for (std::size_t right_position = left_position + 1; right_position < cells.size();
             ++right_position) {
            if (options.cancellation.cancelled()) {
                return Result<PortalAssembly>::failure(StatusCode::Cancelled,
                                                       "portal discovery was cancelled");
            }
            if (output.stats.candidate_pairs == options.maximum_candidate_pairs) {
                return Result<PortalAssembly>::failure(StatusCode::ResourceLimit,
                                                       "portal discovery reached candidate-pair budget",
                                                       std::to_string(options.maximum_candidate_pairs));
            }
            ++output.stats.candidate_pairs;
            const auto left_index = cells[left_position];
            const auto right_index = cells[right_position];
            const auto left_enclosure = geometry_enclosure(records[left_index].geometry);
            const auto right_enclosure = geometry_enclosure(records[right_index].geometry);
            if (!left_enclosure.overlaps(right_enclosure, options.feasibility_tolerance)) {
                ++output.stats.aabb_rejections;
                continue;
            }
            auto left_obb = portal_cell_obb(records[left_index].geometry);
            auto right_obb = portal_cell_obb(records[right_index].geometry);
            if (!left_obb)
                return left_obb.error();
            if (!right_obb)
                return right_obb.error();
            ++output.stats.feasibility_tests;
            auto intersection = CspacePortal::intersect(
                left_obb.value(), right_obb.value(),
                PortalIntersectionOptions{options.maximum_iterations, options.feasibility_tolerance,
                                          options.cancellation});
            if (!intersection)
                return intersection.error();
            if (!intersection.value())
                continue;
            if (output.stats.portals_created == options.maximum_portals) {
                return Result<PortalAssembly>::failure(StatusCode::ResourceLimit,
                                                       "portal discovery reached portal budget",
                                                       std::to_string(options.maximum_portals));
            }
            if (records[left_index].certificate_index >= certificates.size() ||
                records[right_index].certificate_index >= certificates.size()) {
                return Result<PortalAssembly>::failure(StatusCode::InternalError,
                                                       "portal parent certificate index is invalid");
            }
            const auto& left_certificate = certificates[records[left_index].certificate_index];
            const auto& right_certificate = certificates[records[right_index].certificate_index];
            if (left_certificate.level != EvidenceLevel::CertifiedRegion ||
                right_certificate.level != EvidenceLevel::CertifiedRegion ||
                left_certificate.robot_digest != robot_digest ||
                right_certificate.robot_digest != robot_digest ||
                left_certificate.scene_digest != scene_digest ||
                right_certificate.scene_digest != scene_digest ||
                internal::certificate_identity(left_certificate) != left_certificate.id ||
                internal::certificate_identity(right_certificate) != right_certificate.id) {
                return Result<PortalAssembly>::failure(StatusCode::InternalError,
                                                       "portal parent certificate is inconsistent");
            }
            const RegionId left_id = records[left_index].id;
            const RegionId right_id = records[right_index].id;
            const std::string subject =
                internal::portal_subject_digest(left_id, right_id, *intersection.value());
            const double padding =
                std::max(left_certificate.policy.obstacle_padding, right_certificate.policy.obstacle_padding);
            auto certificate = internal::make_subject_certificate(
                EvidenceLevel::CertifiedConnectivity, robot_digest, scene_digest,
                {"convex-obb-intersection-portal", "1", padding}, subject,
                std::min(left_certificate.clearance_lower_bound, right_certificate.clearance_lower_bound));
            if (!certificate)
                return certificate.error();
            auto id = allocate_id(
                internal::region_record_identity(robot_digest, scene_digest, RegionType::Portal, subject),
                used_ids);
            if (!id)
                return id.error();
            const auto certificate_index = certificates.size();
            certificates.push_back(std::move(certificate).value());
            records.push_back({id.value(),
                               PortalGeometry{left_id, right_id, std::move(*intersection.value())},
                               certificate_index,
                               0,
                               {},
                               "arbitrary-obb-intersection"});
            output.portal_by_pair.emplace(
                std::pair<RegionId, RegionId>{std::min(left_id, right_id), std::max(left_id, right_id)},
                id.value());
            ++output.stats.portals_created;
        }
    }
    return output;
}

void rebuild_graph_and_components(std::vector<RegionRecord>& records,
                                  std::vector<std::vector<std::size_t>>& adjacency) {
    adjacency.assign(records.size(), {});
    std::unordered_map<RegionId, std::size_t> index_by_id;
    index_by_id.reserve(records.size());
    for (std::size_t index = 0; index < records.size(); ++index)
        index_by_id.emplace(records[index].id, index);

    for (std::size_t index = 0; index < records.size(); ++index) {
        if (const auto* portal = std::get_if<PortalGeometry>(&records[index].geometry)) {
            const auto left = index_by_id.find(portal->left_region);
            const auto right = index_by_id.find(portal->right_region);
            if (left == index_by_id.end() || right == index_by_id.end())
                continue;
            adjacency[index].push_back(left->second);
            adjacency[index].push_back(right->second);
            adjacency[left->second].push_back(index);
            adjacency[right->second].push_back(index);
            adjacency[left->second].push_back(right->second);
            adjacency[right->second].push_back(left->second);
        }
    }
    for (auto& neighbors : adjacency) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }

    ComponentId component = 0;
    std::vector<bool> visited(records.size(), false);
    for (std::size_t start = 0; start < records.size(); ++start) {
        if (!primary_region(region_type(records[start].geometry)) || visited[start])
            continue;
        ++component;
        std::deque<std::size_t> frontier{start};
        visited[start] = true;
        while (!frontier.empty()) {
            const auto current = frontier.front();
            frontier.pop_front();
            records[current].component = component;
            for (const auto neighbor : adjacency[current]) {
                if (!primary_region(region_type(records[neighbor].geometry)) || visited[neighbor])
                    continue;
                visited[neighbor] = true;
                frontier.push_back(neighbor);
            }
        }
    }
    for (auto& record : records) {
        if (const auto* portal = std::get_if<PortalGeometry>(&record.geometry)) {
            const auto left = index_by_id.find(portal->left_region);
            const auto right = index_by_id.find(portal->right_region);
            if (left != index_by_id.end() && right != index_by_id.end() &&
                records[left->second].component == records[right->second].component) {
                record.component = records[left->second].component;
            }
        }
    }
}

double squared_distance_to_aabb(std::span<const double> configuration, const CspaceAabb& box) {
    double result = 0.0;
    for (std::size_t axis = 0; axis < box.dimension(); ++axis) {
        double separation = 0.0;
        if (configuration[axis] < box.axes()[axis].lower)
            separation = box.axes()[axis].lower - configuration[axis];
        else if (configuration[axis] > box.axes()[axis].upper)
            separation = configuration[axis] - box.axes()[axis].upper;
        result += separation * separation;
    }
    return result;
}

Result<void> validate_build_options(const ObbAtlasBuildOptions& options) {
    if (!std::isfinite(options.initial_half_width) || !std::isfinite(options.maximum_half_width) ||
        !std::isfinite(options.bridge_longitudinal_margin) || !std::isfinite(options.obstacle_padding) ||
        options.initial_half_width < 0.0 || options.maximum_half_width < options.initial_half_width ||
        options.bridge_longitudinal_margin < 0.0 || options.obstacle_padding < 0.0 ||
        options.nearest_bridge_neighbors > 1024 || options.growth_iterations > 128 ||
        options.maximum_samples == 0 || options.maximum_pair_evaluations == 0 ||
        options.maximum_validations == 0) {
        return Result<void>::failure(StatusCode::InvalidArgument, "invalid OBB Atlas build options");
    }
    return validate_portal_options(options.portal);
}

} // namespace

Result<std::optional<RegionRecord>> RegionDatabase::region(RegionId id) const {
    if (id == 0)
        return Result<std::optional<RegionRecord>>::failure(StatusCode::InvalidArgument,
                                                            "region ID must be nonzero");
    const auto found =
        std::find_if(records_.begin(), records_.end(), [id](const auto& record) { return record.id == id; });
    if (found == records_.end())
        return std::optional<RegionRecord>{};
    return std::optional<RegionRecord>{*found};
}

Result<std::optional<Certificate>> RegionDatabase::certificate(const std::string& certificate_id) const {
    if (!internal::valid_sha256(certificate_id)) {
        return Result<std::optional<Certificate>>::failure(StatusCode::InvalidArgument,
                                                           "certificate ID is not a SHA-256 digest");
    }
    const auto found = std::find_if(certificates_.begin(), certificates_.end(),
                                    [&](const auto& value) { return value.id == certificate_id; });
    if (found == certificates_.end())
        return std::optional<Certificate>{};
    return std::optional<Certificate>{*found};
}

Result<std::vector<RegionRecord>> RegionDatabase::regions_at(std::span<const double> configuration,
                                                             const RegionQueryOptions& options) const {
    auto query = validate_configuration(configuration, dimension_, "region database query");
    if (!query)
        return query.error();
    if (!std::isfinite(options.tolerance) || options.tolerance < 0.0) {
        return Result<std::vector<RegionRecord>>::failure(StatusCode::InvalidArgument,
                                                          "query tolerance is invalid");
    }
    std::vector<RegionRecord> result;
    for (const auto& record : records_) {
        const auto type = region_type(record.geometry);
        if (type == RegionType::TrajectoryTube) {
            if (!options.include_trajectory_tubes)
                continue;
            const auto& tube = std::get<TrajectoryTubeGeometry>(record.geometry);
            bool inside = false;
            for (const auto cell_id : tube.cell_ids) {
                const auto cell = std::find_if(records_.begin(), records_.end(),
                                               [&](const auto& value) { return value.id == cell_id; });
                if (cell == records_.end())
                    continue;
                auto membership = geometry_contains(cell->geometry, configuration, options.tolerance);
                if (!membership)
                    return membership.error();
                inside = inside || membership.value();
            }
            if (inside)
                result.push_back(record);
            continue;
        }
        if (type == RegionType::Portal && !options.include_portals)
            continue;
        auto membership = geometry_contains(record.geometry, configuration, options.tolerance);
        if (!membership)
            return membership.error();
        if (membership.value())
            result.push_back(record);
    }
    std::sort(result.begin(), result.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    return result;
}

bool RegionDatabase::contains(std::span<const double> configuration,
                              const RegionQueryOptions& options) const {
    auto result = regions_at(configuration, options);
    return result && !result.value().empty();
}

Result<std::optional<RegionRecord>> RegionDatabase::nearest_region(std::span<const double> configuration,
                                                                   const RegionQueryOptions& options) const {
    auto query = validate_configuration(configuration, dimension_, "nearest region query");
    if (!query)
        return query.error();
    if (!std::isfinite(options.tolerance) || options.tolerance < 0.0) {
        return Result<std::optional<RegionRecord>>::failure(StatusCode::InvalidArgument,
                                                            "query tolerance is invalid");
    }
    const RegionRecord* best = nullptr;
    double best_distance = std::numeric_limits<double>::infinity();
    for (const auto& record : records_) {
        const auto type = region_type(record.geometry);
        if (type == RegionType::TrajectoryTube || (type == RegionType::Portal && !options.include_portals)) {
            continue;
        }
        const auto enclosure = geometry_enclosure(record.geometry);
        if (!enclosure.valid())
            continue;
        const double distance = squared_distance_to_aabb(configuration, enclosure);
        if (best == nullptr || distance < best_distance ||
            (distance == best_distance && record.id < best->id)) {
            best = &record;
            best_distance = distance;
        }
    }
    if (best == nullptr)
        return std::optional<RegionRecord>{};
    return std::optional<RegionRecord>{*best};
}

Result<bool> RegionDatabase::connected(std::span<const double> first, std::span<const double> second) const {
    auto first_regions = regions_at(first);
    if (!first_regions)
        return first_regions.error();
    auto second_regions = regions_at(second);
    if (!second_regions)
        return second_regions.error();
    for (const auto& left : first_regions.value()) {
        for (const auto& right : second_regions.value()) {
            if (left.component != 0 && left.component == right.component)
                return true;
        }
    }
    return false;
}

Result<void> RegionDatabase::verify_compatible(const SerialRobotModel& robot,
                                               const SceneSnapshot& scene) const {
    if (robot.digest() != robot_digest_)
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "robot identity does not match region database");
    if (scene.digest() != scene_digest_)
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "scene identity does not match region database");
    return Result<void>::success();
}

Result<RegionDatabase> RegionDatabase::from_atlas(const SafeAtlas& atlas, std::string scene_version,
                                                  const PortalDiscoveryOptions& options) {
    if (atlas.dimension() == 0 || atlas.regions().size() != atlas.certificates().size()) {
        return Result<RegionDatabase>::failure(StatusCode::InvalidArgument, "source Atlas is inconsistent");
    }
    if (scene_version.empty())
        scene_version = atlas.version_info().scene_version;
    if (scene_version.empty()) {
        return Result<RegionDatabase>::failure(StatusCode::InvalidArgument,
                                               "source Atlas has no scene version");
    }
    RegionDatabase database;
    database.dimension_ = atlas.dimension();
    database.robot_digest_ = atlas.robot_digest();
    database.scene_digest_ = atlas.scene_digest();
    database.scene_version_ = std::move(scene_version);
    database.certificates_ = atlas.certificates();
    database.records_.reserve(atlas.regions().size());
    std::set<RegionId> used_ids;
    for (std::size_t index = 0; index < atlas.regions().size(); ++index) {
        const auto& region = atlas.regions()[index];
        used_ids.insert(region.id);
        LinkEnvelope dependency;
        if (index < atlas.dependencies().size())
            dependency = atlas.dependencies()[index].envelope;
        database.records_.push_back({region.id, region.bounds, region.certificate_index, 0,
                                     std::move(dependency), "atlas:" + region.source_node.path()});
    }
    auto portals = discover_portals(database.robot_digest_, database.scene_digest_, database.records_,
                                    database.certificates_, used_ids, options);
    if (!portals)
        return portals.error();
    rebuild_graph_and_components(database.records_, database.adjacency_);
    return database;
}

Result<RegionDatabase> RegionDatabase::from_corridor(const HipacCorridor& corridor, std::string scene_version,
                                                     const PortalDiscoveryOptions& options) {
    if (corridor.dimension() == 0 || scene_version.empty()) {
        return Result<RegionDatabase>::failure(StatusCode::InvalidArgument,
                                               "corridor import metadata is invalid");
    }
    RegionDatabase database;
    database.dimension_ = corridor.dimension();
    database.robot_digest_ = corridor.robot_digest();
    database.scene_digest_ = corridor.scene_digest();
    database.scene_version_ = std::move(scene_version);
    std::set<RegionId> used_ids;
    for (const auto& region : corridor.regions()) {
        if (!used_ids.insert(region.id).second) {
            return Result<RegionDatabase>::failure(StatusCode::InvalidArgument,
                                                   "corridor has duplicate region IDs");
        }
        const auto certificate_index = database.certificates_.size();
        database.certificates_.push_back(region.certificate);
        database.records_.push_back({region.id,
                                     region.bounds,
                                     certificate_index,
                                     0,
                                     {},
                                     "corridor:" + std::to_string(region.segment_index) + ":" +
                                         std::to_string(region.start_fraction)});
    }
    auto portals = discover_portals(database.robot_digest_, database.scene_digest_, database.records_,
                                    database.certificates_, used_ids, options);
    if (!portals)
        return portals.error();
    rebuild_graph_and_components(database.records_, database.adjacency_);

    auto append_tube = [&](std::size_t begin, std::size_t end,
                           const std::vector<RegionId>& portal_ids) -> Result<void> {
        if (begin >= end)
            return Result<void>::success();
        TrajectoryTubeGeometry tube;
        tube.portal_ids = portal_ids;
        tube.centerline.push_back(corridor.regions()[begin].entry);
        double clearance = std::numeric_limits<double>::infinity();
        double padding = 0.0;
        for (std::size_t index = begin; index < end; ++index) {
            const auto& source = corridor.regions()[index];
            tube.cell_ids.push_back(source.id);
            tube.centerline.push_back(source.exit);
            clearance = std::min(clearance, source.certificate.clearance_lower_bound);
            padding = std::max(padding, source.certificate.policy.obstacle_padding);
        }
        if (!tube.valid(database.dimension_)) {
            return Result<void>::failure(StatusCode::InternalError, "trajectory tube chain is inconsistent");
        }
        const std::string subject = internal::trajectory_tube_subject_digest(tube);
        auto certificate = internal::make_subject_certificate(
            EvidenceLevel::CertifiedConnectivity, database.robot_digest_, database.scene_digest_,
            {"trajectory-tube-convex-chain", "1", padding}, subject,
            std::isfinite(clearance) ? clearance : 0.0);
        if (!certificate)
            return certificate.error();
        auto id = allocate_id(internal::region_record_identity(
                                  database.robot_digest_, database.scene_digest_, RegionType::TrajectoryTube,
                                  subject, std::to_string(begin) + ":" + std::to_string(end)),
                              used_ids);
        if (!id)
            return id.error();
        const auto certificate_index = database.certificates_.size();
        database.certificates_.push_back(std::move(certificate).value());
        const ComponentId component = database.records_[begin].component;
        database.records_.push_back(
            {id.value(), std::move(tube), certificate_index, component, {}, "corridor-trajectory-tube"});
        return Result<void>::success();
    };

    std::size_t chain_begin = 0;
    std::vector<RegionId> chain_portals;
    for (std::size_t index = 1; index < corridor.regions().size(); ++index) {
        const auto pair = std::pair<RegionId, RegionId>{
            std::min(corridor.regions()[index - 1].id, corridor.regions()[index].id),
            std::max(corridor.regions()[index - 1].id, corridor.regions()[index].id)};
        const auto portal = portals.value().portal_by_pair.find(pair);
        if (portal != portals.value().portal_by_pair.end()) {
            chain_portals.push_back(portal->second);
            continue;
        }
        auto appended = append_tube(chain_begin, index, chain_portals);
        if (!appended)
            return appended.error();
        chain_begin = index;
        chain_portals.clear();
    }
    auto appended = append_tube(chain_begin, corridor.regions().size(), chain_portals);
    if (!appended)
        return appended.error();
    rebuild_graph_and_components(database.records_, database.adjacency_);
    for (auto& record : database.records_) {
        if (const auto* tube = std::get_if<TrajectoryTubeGeometry>(&record.geometry)) {
            const auto cell =
                std::find_if(database.records_.begin(), database.records_.end(),
                             [&](const auto& value) { return value.id == tube->cell_ids.front(); });
            if (cell != database.records_.end())
                record.component = cell->component;
        }
    }
    return database;
}

Result<RegionDatabase> RegionDatabase::create(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                              std::vector<CertifiedRegionInput> regions,
                                              const PortalDiscoveryOptions& options) {
    auto robot_status = robot.validate();
    if (!robot_status)
        return robot_status.error();
    auto scene_status = scene.validate();
    if (!scene_status)
        return scene_status.error();
    auto portal_status = validate_portal_options(options);
    if (!portal_status)
        return portal_status.error();
    if (regions.empty()) {
        return Result<RegionDatabase>::failure(StatusCode::InvalidArgument, "region database input is empty");
    }

    struct PreparedInput {
        CertifiedRegionInput input;
        std::string subject;
    };
    std::vector<PreparedInput> prepared;
    prepared.reserve(regions.size());
    for (auto& input : regions) {
        auto subject = internal::primary_region_subject_digest(input.geometry);
        if (!subject)
            return subject.error();
        const auto enclosure = geometry_enclosure(input.geometry);
        if (!enclosure.valid() || enclosure.dimension() != robot.dimension()) {
            return Result<RegionDatabase>::failure(StatusCode::DimensionMismatch,
                                                   "primary region geometry does not match robot dimension");
        }
        const auto& domain = robot.configuration_domain();
        for (std::size_t axis = 0; axis < robot.dimension(); ++axis) {
            if (enclosure.axes()[axis].lower < domain.axes()[axis].lower - 1e-12 ||
                enclosure.axes()[axis].upper > domain.axes()[axis].upper + 1e-12) {
                return Result<RegionDatabase>::failure(StatusCode::InvalidArgument,
                                                       "primary region exceeds robot joint limits");
            }
        }
        const auto& certificate = input.certificate;
        if (certificate.level != EvidenceLevel::CertifiedRegion ||
            certificate.robot_digest != robot.digest() || certificate.scene_digest != scene.digest() ||
            certificate.subject_digest != subject.value() ||
            internal::certificate_identity(certificate) != certificate.id ||
            input.dependency.links.size() != robot.link_count() ||
            !std::all_of(input.dependency.links.begin(), input.dependency.links.end(),
                         [](const auto& link) { return link.valid(); })) {
            return Result<RegionDatabase>::failure(
                StatusCode::InvalidArgument,
                "primary region certificate or workspace dependency is inconsistent");
        }
        prepared.push_back({std::move(input), std::move(subject).value()});
    }
    std::sort(prepared.begin(), prepared.end(), [](const auto& left, const auto& right) {
        if (left.subject != right.subject)
            return left.subject < right.subject;
        return left.input.source < right.input.source;
    });
    for (std::size_t index = 1; index < prepared.size(); ++index) {
        if (prepared[index - 1].subject == prepared[index].subject) {
            return Result<RegionDatabase>::failure(StatusCode::InvalidArgument,
                                                   "duplicate primary region geometry");
        }
    }

    RegionDatabase database;
    database.dimension_ = robot.dimension();
    database.robot_digest_ = robot.digest();
    database.scene_digest_ = scene.digest();
    database.scene_version_ = scene.version();
    std::set<RegionId> used_ids;
    for (auto& item : prepared) {
        const auto type = region_type(item.input.geometry);
        auto id = allocate_id(internal::region_record_identity(database.robot_digest_, database.scene_digest_,
                                                               type, item.subject, item.input.source),
                              used_ids);
        if (!id)
            return id.error();
        const auto certificate_index = database.certificates_.size();
        database.certificates_.push_back(std::move(item.input.certificate));
        database.records_.push_back({id.value(), std::move(item.input.geometry), certificate_index, 0,
                                     std::move(item.input.dependency), std::move(item.input.source)});
    }
    auto portals = discover_portals(database.robot_digest_, database.scene_digest_, database.records_,
                                    database.certificates_, used_ids, options);
    if (!portals)
        return portals.error();
    rebuild_graph_and_components(database.records_, database.adjacency_);
    return database;
}

Result<ObbAtlasBuildResult> ObbAtlasBuilder::build(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                                   std::vector<Configuration> samples,
                                                   const ObbAtlasBuildOptions& options) const {
    auto options_status = validate_build_options(options);
    if (!options_status)
        return options_status.error();
    auto robot_status = robot.validate();
    if (!robot_status)
        return robot_status.error();
    auto scene_status = scene.validate();
    if (!scene_status)
        return scene_status.error();
    if (samples.empty()) {
        return Result<ObbAtlasBuildResult>::failure(StatusCode::InvalidArgument,
                                                    "OBB Atlas requires at least one sample");
    }
    for (std::size_t index = 0; index < samples.size(); ++index) {
        auto status = validate_configuration(samples[index], robot.dimension(),
                                             "OBB Atlas sample " + std::to_string(index));
        if (!status)
            return status.error();
        if (!robot.configuration_domain().contains(samples[index], 1e-12)) {
            return Result<ObbAtlasBuildResult>::failure(
                StatusCode::InvalidArgument, "OBB Atlas sample exceeds joint limits", std::to_string(index));
        }
    }
    std::sort(samples.begin(), samples.end());
    samples.erase(std::unique(samples.begin(), samples.end()), samples.end());
    if (samples.size() > options.maximum_samples) {
        return Result<ObbAtlasBuildResult>::failure(StatusCode::ResourceLimit,
                                                    "OBB Atlas sample count exceeds limit");
    }

    struct Candidate {
        CspaceObb region;
        ObbValidation validation;
        std::string source;
        bool bridge = false;
    };
    ObbAtlasBuildResult output;
    output.stats.unique_samples = samples.size();
    std::vector<Candidate> candidates;
    std::set<std::string> subjects;
    std::size_t remaining_validations = options.maximum_validations;

    auto grow = [&](std::span<const double> first, std::span<const double> second, std::string source,
                    bool bridge) -> Result<void> {
        if (options.cancellation.cancelled())
            return Result<void>::failure(StatusCode::Cancelled, "OBB Atlas build was cancelled");
        if (remaining_validations == 0) {
            return Result<void>::failure(StatusCode::ResourceLimit, "OBB Atlas validation budget exhausted");
        }
        ObbGrowthOptions growth;
        growth.initial_lateral_half_width = options.initial_half_width;
        growth.maximum_lateral_half_width = options.maximum_half_width;
        growth.longitudinal_margin = bridge ? options.bridge_longitudinal_margin : 0.0;
        growth.maximum_iterations = options.growth_iterations;
        growth.maximum_validations = remaining_validations;
        growth.obstacle_padding = options.obstacle_padding;
        growth.cancellation = options.cancellation;
        auto result = ObbGrower{}.grow(robot, scene, first, second, growth);
        if (!result)
            return result.error();
        remaining_validations -= result.value().validations;
        output.stats.validations += result.value().validations;
        output.stats.growth_attempts += result.value().growth_attempts;
        if (!result.value().certified) {
            ++output.stats.rejected_candidates;
            return Result<void>::success();
        }
        const std::string subject = internal::obb_subject_digest(result.value().region);
        if (!subjects.insert(subject).second)
            return Result<void>::success();
        auto growth_result = std::move(result).value();
        candidates.push_back({std::move(growth_result.region), std::move(growth_result.validation),
                              std::move(source), bridge});
        if (bridge)
            ++output.stats.bridge_regions;
        else
            ++output.stats.point_regions;
        return Result<void>::success();
    };

    for (std::size_t index = 0; index < samples.size(); ++index) {
        auto result = grow(samples[index], samples[index], "sample:" + std::to_string(index), false);
        if (!result)
            return result.error();
    }

    std::set<std::pair<std::size_t, std::size_t>> bridge_pairs;
    std::size_t pair_evaluations = 0;
    for (std::size_t left = 0; left < samples.size(); ++left) {
        std::vector<std::pair<double, std::size_t>> neighbors;
        neighbors.reserve(samples.size() - 1);
        for (std::size_t right = 0; right < samples.size(); ++right) {
            if (left == right)
                continue;
            if (pair_evaluations == options.maximum_pair_evaluations) {
                return Result<ObbAtlasBuildResult>::failure(StatusCode::ResourceLimit,
                                                            "OBB Atlas nearest-neighbor budget exhausted");
            }
            ++pair_evaluations;
            double distance = 0.0;
            for (std::size_t axis = 0; axis < robot.dimension(); ++axis) {
                const double difference = samples[left][axis] - samples[right][axis];
                distance += difference * difference;
            }
            neighbors.emplace_back(distance, right);
        }
        std::sort(neighbors.begin(), neighbors.end(), [](const auto& first, const auto& second) {
            if (first.first != second.first)
                return first.first < second.first;
            return first.second < second.second;
        });
        const std::size_t count = std::min(options.nearest_bridge_neighbors, neighbors.size());
        for (std::size_t index = 0; index < count; ++index)
            bridge_pairs.emplace(std::min(left, neighbors[index].second),
                                 std::max(left, neighbors[index].second));
    }
    for (const auto& [left, right] : bridge_pairs) {
        auto result = grow(samples[left], samples[right],
                           "bridge:" + std::to_string(left) + ":" + std::to_string(right), true);
        if (!result)
            return result.error();
    }

    RegionDatabase database;
    database.dimension_ = robot.dimension();
    database.robot_digest_ = robot.digest();
    database.scene_digest_ = scene.digest();
    database.scene_version_ = scene.version();
    std::set<RegionId> used_ids;
    struct CertifiedCandidate {
        RegionId id = 0;
        Candidate candidate;
        Certificate certificate;
    };
    std::vector<CertifiedCandidate> certified;
    certified.reserve(candidates.size());
    for (auto& candidate : candidates) {
        const std::string subject = internal::obb_subject_digest(candidate.region);
        auto certificate = internal::make_subject_certificate(
            EvidenceLevel::CertifiedRegion, database.robot_digest_, database.scene_digest_,
            {"ifk-aa-link-iaabb-obb-enclosure", "1", options.obstacle_padding}, subject,
            candidate.validation.clearance_lower_bound);
        if (!certificate)
            return certificate.error();
        auto id = allocate_id(internal::region_record_identity(database.robot_digest_, database.scene_digest_,
                                                               RegionType::Obb, subject, candidate.source),
                              used_ids);
        if (!id)
            return id.error();
        certified.push_back({id.value(), std::move(candidate), std::move(certificate).value()});
    }
    std::sort(certified.begin(), certified.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    for (auto& candidate : certified) {
        const auto certificate_index = database.certificates_.size();
        database.certificates_.push_back(std::move(candidate.certificate));
        database.records_.push_back({candidate.id, std::move(candidate.candidate.region), certificate_index,
                                     0, std::move(candidate.candidate.validation.envelope),
                                     std::move(candidate.candidate.source)});
    }
    auto portal_options = options.portal;
    portal_options.cancellation = options.cancellation;
    auto portals = discover_portals(database.robot_digest_, database.scene_digest_, database.records_,
                                    database.certificates_, used_ids, portal_options);
    if (!portals)
        return portals.error();
    output.stats.portal = portals.value().stats;
    rebuild_graph_and_components(database.records_, database.adjacency_);
    output.database = std::move(database);
    return output;
}

} // namespace rbfsafe
