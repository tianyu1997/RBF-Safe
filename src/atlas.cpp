#include <rbfsafe/atlas.h>
#include <rbfsafe/version.h>

#include "internal/atlas_identity.h"
#include "internal/certificate_utils.h"
#include "internal/region_index.h"

#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <future>
#include <limits>
#include <numeric>
#include <set>
#include <unordered_set>

namespace rbfsafe {

Result<void> save_atlas_directory(const SafeAtlas& atlas, const std::filesystem::path& directory,
                                  const SaveOptions& options);
Result<SafeAtlas> load_atlas_directory(const std::filesystem::path& directory);

namespace {

struct PendingRegion {
    CspaceAabb bounds;
    LectNodeKey source;
    double clearance = 0.0;
    LinkEnvelope envelope;
};

std::string box_identity(const CspaceAabb& box) {
    internal::Json::Array axes;
    for (const auto& axis : box.axes())
        axes.emplace_back(internal::Json::Array{axis.lower, axis.upper});
    return internal::Json(std::move(axes)).dump(false);
}

RegionId stable_region_id(const std::string& robot_digest, const std::string& scene_digest,
                          const PendingRegion& region) {
    const auto digest = internal::sha256(robot_digest + "|" + scene_digest + "|" +
                                         box_identity(region.bounds) + "|" + region.source.path());
    RegionId result = 0;
    for (std::size_t index = 0; index < 16; ++index) {
        const char digit = digest[index];
        const unsigned value = digit >= '0' && digit <= '9' ? static_cast<unsigned>(digit - '0')
                                                            : static_cast<unsigned>(digit - 'a' + 10);
        result = (result << 4u) | value;
    }
    return result == 0 ? 1 : result;
}

RegionId stable_repair_domain_id(const std::string& robot_digest, const std::string& scene_digest,
                                 const CspaceAabb& bounds, const LectNodeKey& source) {
    const auto digest = internal::sha256(robot_digest + "|" + scene_digest + "|repair-domain|" +
                                         box_identity(bounds) + "|" + source.path());
    RegionId result = 0;
    for (std::size_t index = 0; index < 16; ++index) {
        const char digit = digest[index];
        const unsigned value = digit >= '0' && digit <= '9' ? static_cast<unsigned>(digit - '0')
                                                            : static_cast<unsigned>(digit - 'a' + 10);
        result = (result << 4u) | value;
    }
    return result == 0 ? 1 : result;
}

std::optional<CspaceAabb> rectangular_union(const CspaceAabb& left, const CspaceAabb& right,
                                            double tolerance) {
    if (left.dimension() != right.dimension())
        return std::nullopt;
    std::vector<Interval> axes;
    axes.reserve(left.dimension());
    std::size_t differing_axes = 0;
    for (std::size_t dimension = 0; dimension < left.dimension(); ++dimension) {
        const auto& a = left.axes()[dimension];
        const auto& b = right.axes()[dimension];
        const bool equal =
            std::abs(a.lower - b.lower) <= tolerance && std::abs(a.upper - b.upper) <= tolerance;
        if (!equal) {
            ++differing_axes;
            if (differing_axes > 1 || !a.overlaps(b, tolerance))
                return std::nullopt;
        }
        axes.emplace_back(std::min(a.lower, b.lower), std::max(a.upper, b.upper));
    }
    if (differing_axes == 0)
        return CspaceAabb(std::move(axes));
    return CspaceAabb(std::move(axes));
}

double point_box_distance_squared(std::span<const double> point, const CspaceAabb& box) {
    double squared = 0.0;
    for (std::size_t dimension = 0; dimension < box.dimension(); ++dimension) {
        double delta = 0.0;
        if (point[dimension] < box.axes()[dimension].lower)
            delta = box.axes()[dimension].lower - point[dimension];
        else if (point[dimension] > box.axes()[dimension].upper)
            delta = point[dimension] - box.axes()[dimension].upper;
        squared += delta * delta;
    }
    return squared;
}

internal::Json configuration_json(std::span<const double> configuration) {
    internal::Json::Array values;
    values.reserve(configuration.size());
    for (const double value : configuration)
        values.emplace_back(value);
    return values;
}

std::optional<Configuration> intersection_witness(const CspaceAabb& left, const CspaceAabb& right) {
    if (left.dimension() != right.dimension())
        return std::nullopt;
    Configuration witness;
    witness.reserve(left.dimension());
    for (std::size_t axis = 0; axis < left.dimension(); ++axis) {
        const double lower = std::max(left.axes()[axis].lower, right.axes()[axis].lower);
        const double upper = std::min(left.axes()[axis].upper, right.axes()[axis].upper);
        if (lower > upper)
            return std::nullopt;
        witness.push_back(0.5 * (lower + upper));
    }
    return witness;
}

} // namespace

AtlasBuilder::AtlasBuilder() = default;

AtlasBuilder::AtlasBuilder(std::shared_ptr<const RegionValidator> validator)
    : validator_(std::move(validator)) {}

Result<AtlasBuildResult> AtlasBuilder::build(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                             std::vector<Configuration> samples,
                                             const BuildOptions& options) const {
    std::shared_ptr<const RegionValidator> default_validator;
    const RegionValidator* validator = validator_.get();
    if (validator == nullptr) {
        default_validator =
            std::make_shared<IfkAaLinkAabbValidator>(EnvelopeOptions{options.obstacle_padding});
        validator = default_validator.get();
    }
    auto robot_status = robot.validate();
    if (!robot_status)
        return robot_status.error();
    auto scene_status = scene.validate();
    if (!scene_status)
        return scene_status.error();
    if (samples.empty())
        return Result<AtlasBuildResult>::failure(StatusCode::InvalidArgument,
                                                 "AtlasBuilder requires at least one sample");
    if (options.maximum_depth == 0 || options.maximum_depth > 4096 || options.maximum_nodes < 1 ||
        !std::isfinite(options.minimum_normalized_width) || options.minimum_normalized_width < 0.0 ||
        options.minimum_normalized_width >= 1.0 || !std::isfinite(options.adjacency_tolerance) ||
        options.adjacency_tolerance < 0.0 || !std::isfinite(options.obstacle_padding) ||
        options.obstacle_padding < 0.0 || options.threads < 1) {
        return Result<AtlasBuildResult>::failure(StatusCode::InvalidArgument, "invalid AtlasBuilder options");
    }

    BuildStats stats;
    stats.input_samples = samples.size();
    for (std::size_t index = 0; index < samples.size(); ++index) {
        auto status =
            validate_configuration(samples[index], robot.dimension(), "sample " + std::to_string(index));
        if (!status)
            return status.error();
        if (!robot.configuration_domain().contains(samples[index], 1e-12)) {
            return Result<AtlasBuildResult>::failure(
                StatusCode::InvalidArgument, "sample lies outside robot joint limits", std::to_string(index));
        }
    }
    std::sort(samples.begin(), samples.end());
    samples.erase(std::unique(samples.begin(), samples.end()), samples.end());
    stats.unique_samples = samples.size();

    auto tree_result =
        LectTree::create(robot.configuration_domain(),
                         SplitPolicy{SplitStrategy::NormalizedLongestAxis, options.minimum_normalized_width});
    if (!tree_result)
        return tree_result.error();
    LectTree tree = std::move(tree_result).value();

    struct WorkItem {
        LectNodeKey key;
        std::vector<std::size_t> samples;
        bool may_split = true;
        std::optional<RegionValidation> cached_validation;
    };
    std::deque<WorkItem> work;
    std::vector<std::size_t> all_samples(samples.size());
    std::iota(all_samples.begin(), all_samples.end(), 0);
    work.push_back({tree.root_key(), std::move(all_samples), true, std::nullopt});
    std::vector<PendingRegion> pending;
    std::vector<AtlasRepairDomain> unresolved_domains;

    while (!work.empty()) {
        if (options.cancellation.cancelled()) {
            return Result<AtlasBuildResult>::failure(StatusCode::Cancelled, "Atlas build was cancelled");
        }
        WorkItem current = std::move(work.front());
        work.pop_front();
        auto node = tree.node(current.key);
        if (!node)
            return node.error();
        ++stats.nodes_visited;
        Result<RegionValidation> validation =
            current.cached_validation ? Result<RegionValidation>(std::move(*current.cached_validation))
                                      : validator->validate(robot, scene, node.value().box);
        if (!validation)
            return validation.error();
        if (validation.value().disposition == ValidationDisposition::CertifiedFree) {
            pending.push_back({node.value().box, node.value().key, validation.value().clearance_lower_bound,
                               std::move(validation).value().envelope});
            ++stats.certified_nodes;
            continue;
        }
        if (!current.may_split || current.samples.empty() || current.key.depth() >= options.maximum_depth) {
            unresolved_domains.push_back({0, node.value().box, node.value().key});
            ++stats.unresolved_nodes;
            continue;
        }
        if (tree.size() + 2 > options.maximum_nodes) {
            return Result<AtlasBuildResult>::failure(StatusCode::ResourceLimit,
                                                     "Atlas build reached maximum node count",
                                                     std::to_string(options.maximum_nodes));
        }
        auto children = tree.split(current.key);
        if (!children) {
            if (children.error().code == StatusCode::ResourceLimit) {
                unresolved_domains.push_back({0, node.value().box, node.value().key});
                ++stats.unresolved_nodes;
                continue;
            }
            return children.error();
        }
        auto parent = tree.node(current.key);
        if (!parent)
            return parent.error();
        const std::size_t split_dimension = parent.value().split_dimension;
        const double midpoint = parent.value().box.axes()[split_dimension].center();
        std::vector<std::size_t> left_samples;
        std::vector<std::size_t> right_samples;
        for (const auto sample_index : current.samples) {
            if (samples[sample_index][split_dimension] <= midpoint)
                left_samples.push_back(sample_index);
            else
                right_samples.push_back(sample_index);
        }
        const bool left_may_split = !left_samples.empty();
        const bool right_may_split = !right_samples.empty();
        std::optional<RegionValidation> left_validation;
        std::optional<RegionValidation> right_validation;
        if (options.threads > 1) {
            auto left_node = tree.node(children.value().first);
            if (!left_node)
                return left_node.error();
            auto right_node = tree.node(children.value().second);
            if (!right_node)
                return right_node.error();
            auto left_future =
                std::async(std::launch::async, [&robot, &scene, validator, box = left_node.value().box]() {
                    return validator->validate(robot, scene, box);
                });
            auto right_result = validator->validate(robot, scene, right_node.value().box);
            auto left_result = left_future.get();
            if (!left_result)
                return left_result.error();
            if (!right_result)
                return right_result.error();
            left_validation = std::move(left_result).value();
            right_validation = std::move(right_result).value();
        }
        work.push_back(
            {children.value().first, std::move(left_samples), left_may_split, std::move(left_validation)});
        work.push_back({children.value().second, std::move(right_samples), right_may_split,
                        std::move(right_validation)});
    }

    std::sort(pending.begin(), pending.end(),
              [](const auto& left, const auto& right) { return left.source < right.source; });
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t left = 0; left < pending.size() && !changed; ++left) {
            for (std::size_t right = left + 1; right < pending.size(); ++right) {
                auto merged_box = rectangular_union(pending[left].bounds, pending[right].bounds,
                                                    options.adjacency_tolerance);
                if (!merged_box)
                    continue;
                auto validation = validator->validate(robot, scene, *merged_box);
                if (!validation)
                    return validation.error();
                if (validation.value().disposition != ValidationDisposition::CertifiedFree)
                    continue;
                pending[left].bounds = std::move(*merged_box);
                pending[left].source = std::min(pending[left].source, pending[right].source);
                pending[left].clearance = validation.value().clearance_lower_bound;
                pending[left].envelope = std::move(validation).value().envelope;
                pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(right));
                ++stats.merged_regions;
                changed = true;
                break;
            }
        }
    }

    SafeAtlas atlas;
    atlas.dimension_ = robot.dimension();
    atlas.storage_schema_ = kAtlasSchemaVersion;
    atlas.robot_digest_ = robot.digest();
    atlas.scene_digest_ = scene.digest();
    atlas.lect_ = LectSnapshot::from_tree(std::move(tree));
    atlas.regions_.reserve(pending.size());
    atlas.certificates_.reserve(pending.size());
    atlas.dependencies_.reserve(pending.size());
    std::set<RegionId> ids;
    for (const auto& region : pending) {
        RegionValidation validation;
        validation.disposition = ValidationDisposition::CertifiedFree;
        validation.clearance_lower_bound = region.clearance;
        validation.envelope = region.envelope;
        auto certificate = make_region_certificate(robot, scene, region.bounds, *validator, validation,
                                                   options.obstacle_padding);
        if (!certificate)
            return certificate.error();
        const auto certificate_index = atlas.certificates_.size();
        atlas.certificates_.push_back(std::move(certificate).value());
        RegionId id = stable_region_id(atlas.robot_digest_, atlas.scene_digest_, region);
        while (!ids.insert(id).second)
            ++id;
        atlas.regions_.push_back({id, region.bounds, certificate_index, 0, region.source});
        atlas.dependencies_.push_back({id, region.envelope});
    }
    std::sort(atlas.regions_.begin(), atlas.regions_.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    // Reorder certificates to remain aligned with the sorted regions.
    std::vector<Certificate> sorted_certificates;
    std::vector<RegionDependency> sorted_dependencies;
    sorted_certificates.reserve(atlas.certificates_.size());
    sorted_dependencies.reserve(atlas.dependencies_.size());
    for (auto& region : atlas.regions_) {
        sorted_certificates.push_back(atlas.certificates_[region.certificate_index]);
        sorted_dependencies.push_back(atlas.dependencies_[region.certificate_index]);
        sorted_dependencies.back().region_id = region.id;
        region.certificate_index = sorted_certificates.size() - 1;
    }
    atlas.certificates_ = std::move(sorted_certificates);
    atlas.dependencies_ = std::move(sorted_dependencies);
    std::set<RegionId> repair_ids;
    for (auto& domain : unresolved_domains) {
        domain.id = stable_repair_domain_id(atlas.robot_digest_, atlas.scene_digest_, domain.bounds,
                                            domain.source_node);
        while (!repair_ids.insert(domain.id).second)
            ++domain.id;
    }
    std::sort(unresolved_domains.begin(), unresolved_domains.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    atlas.repair_domains_ = std::move(unresolved_domains);

    atlas.adjacency_.assign(atlas.regions_.size(), {});
    for (std::size_t left = 0; left < atlas.regions_.size(); ++left) {
        for (std::size_t right = left + 1; right < atlas.regions_.size(); ++right) {
            if (atlas.regions_[left].bounds.overlaps(atlas.regions_[right].bounds,
                                                     options.adjacency_tolerance)) {
                atlas.adjacency_[left].push_back(right);
                atlas.adjacency_[right].push_back(left);
            }
        }
    }
    ComponentId component = 0;
    std::vector<bool> visited(atlas.regions_.size(), false);
    for (std::size_t start = 0; start < atlas.regions_.size(); ++start) {
        if (visited[start])
            continue;
        ++component;
        std::deque<std::size_t> frontier{start};
        visited[start] = true;
        while (!frontier.empty()) {
            const auto index = frontier.front();
            frontier.pop_front();
            atlas.regions_[index].component = component;
            for (const auto neighbor : atlas.adjacency_[index]) {
                if (!visited[neighbor]) {
                    visited[neighbor] = true;
                    frontier.push_back(neighbor);
                }
            }
        }
    }
    atlas.version_info_.sequence = 0;
    atlas.version_info_.scene_version = scene.version();
    atlas.version_info_.scene_digest = scene.digest();
    atlas.version_info_.id = internal::atlas_version_identity(atlas);
    atlas.rebuild_query_index();
    return AtlasBuildResult{std::move(atlas), stats};
}

void SafeAtlas::rebuild_query_index() {
    query_index_ = detail::RegionQueryIndex::build(regions_, dimension_);
}

Result<std::vector<SafeRegion>> SafeAtlas::regions_at(std::span<const double> configuration) const {
    auto status = validate_configuration(configuration, dimension_);
    if (!status)
        return status.error();
    std::vector<SafeRegion> result;
    const auto candidates =
        query_index_ ? query_index_->containing(configuration, regions_) : std::vector<std::size_t>{};
    if (query_index_) {
        result.reserve(candidates.size());
        for (const auto index : candidates)
            result.push_back(regions_[index]);
    } else {
        for (const auto& region : regions_)
            if (region.bounds.contains(configuration, 1e-12))
                result.push_back(region);
    }
    return result;
}

bool SafeAtlas::contains(std::span<const double> configuration) const {
    auto result = regions_at(configuration);
    return result && !result.value().empty();
}

Result<std::optional<SafeRegion>> SafeAtlas::nearest_region(std::span<const double> configuration) const {
    auto status = validate_configuration(configuration, dimension_);
    if (!status)
        return status.error();
    if (regions_.empty())
        return std::optional<SafeRegion>{};
    std::size_t selected = 0;
    if (query_index_) {
        const auto nearest = query_index_->nearest(configuration, regions_);
        if (!nearest)
            return std::optional<SafeRegion>{};
        selected = *nearest;
    } else {
        double best = point_box_distance_squared(configuration, regions_[0].bounds);
        for (std::size_t index = 1; index < regions_.size(); ++index) {
            const double distance = point_box_distance_squared(configuration, regions_[index].bounds);
            if (distance < best || (distance == best && regions_[index].id < regions_[selected].id)) {
                selected = index;
                best = distance;
            }
        }
    }
    return std::optional<SafeRegion>{regions_[selected]};
}

Result<bool> SafeAtlas::connected(std::span<const double> first, std::span<const double> second) const {
    auto result = route(first, second);
    if (!result)
        return result.error();
    return result.value().has_value();
}

Result<std::optional<AtlasRoute>> SafeAtlas::route(std::span<const double> first,
                                                   std::span<const double> second) const {
    auto first_status = validate_configuration(first, dimension_, "Atlas route start");
    if (!first_status)
        return first_status.error();
    auto second_status = validate_configuration(second, dimension_, "Atlas route goal");
    if (!second_status)
        return second_status.error();
    if (adjacency_.size() != regions_.size()) {
        return Result<std::optional<AtlasRoute>>::failure(StatusCode::InternalError,
                                                          "Atlas adjacency size does not match regions");
    }

    std::vector<std::size_t> starts;
    std::vector<std::size_t> goals;
    for (std::size_t index = 0; index < regions_.size(); ++index) {
        if (regions_[index].bounds.contains(first))
            starts.push_back(index);
        if (regions_[index].bounds.contains(second))
            goals.push_back(index);
    }
    if (starts.empty() || goals.empty())
        return std::optional<AtlasRoute>{};
    auto by_id = [&](std::size_t left, std::size_t right) { return regions_[left].id < regions_[right].id; };
    std::sort(starts.begin(), starts.end(), by_id);
    std::sort(goals.begin(), goals.end(), by_id);
    std::vector<bool> is_goal(regions_.size(), false);
    for (const auto goal : goals)
        is_goal[goal] = true;

    std::vector<std::vector<std::size_t>> certified_adjacency(regions_.size());
    for (std::size_t index = 0; index < adjacency_.size(); ++index) {
        for (const auto neighbor : adjacency_[index]) {
            if (neighbor >= regions_.size()) {
                return Result<std::optional<AtlasRoute>>::failure(
                    StatusCode::InternalError, "Atlas adjacency references an unknown region");
            }
            if (intersection_witness(regions_[index].bounds, regions_[neighbor].bounds))
                certified_adjacency[index].push_back(neighbor);
        }
        std::sort(certified_adjacency[index].begin(), certified_adjacency[index].end(), by_id);
        certified_adjacency[index].erase(
            std::unique(certified_adjacency[index].begin(), certified_adjacency[index].end()),
            certified_adjacency[index].end());
    }

    const std::size_t none = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> predecessor(regions_.size(), none);
    std::deque<std::size_t> frontier;
    for (const auto start : starts) {
        predecessor[start] = start;
        frontier.push_back(start);
    }
    std::size_t selected_goal = none;
    while (!frontier.empty()) {
        const std::size_t current = frontier.front();
        frontier.pop_front();
        if (is_goal[current]) {
            selected_goal = current;
            break;
        }
        for (const auto neighbor : certified_adjacency[current]) {
            if (predecessor[neighbor] != none)
                continue;
            predecessor[neighbor] = current;
            frontier.push_back(neighbor);
        }
    }
    if (selected_goal == none)
        return std::optional<AtlasRoute>{};

    std::vector<std::size_t> route_indices;
    for (std::size_t current = selected_goal;; current = predecessor[current]) {
        route_indices.push_back(current);
        if (predecessor[current] == current)
            break;
    }
    std::reverse(route_indices.begin(), route_indices.end());

    AtlasRoute route;
    route.waypoints.emplace_back(first.begin(), first.end());
    for (const auto index : route_indices)
        route.region_sequence.push_back(regions_[index].id);
    for (std::size_t index = 1; index < route_indices.size(); ++index) {
        auto witness = intersection_witness(regions_[route_indices[index - 1]].bounds,
                                            regions_[route_indices[index]].bounds);
        if (!witness) {
            return Result<std::optional<AtlasRoute>>::failure(StatusCode::InternalError,
                                                              "Atlas route lost its intersection witness");
        }
        route.waypoints.push_back(std::move(*witness));
    }
    route.waypoints.emplace_back(second.begin(), second.end());

    double clearance = std::numeric_limits<double>::infinity();
    double obstacle_padding = 0.0;
    bool have_padding = false;
    internal::Json::Array region_ids;
    internal::Json::Array region_certificates;
    internal::Json::Array region_bounds;
    for (const auto index : route_indices) {
        if (regions_[index].certificate_index >= certificates_.size()) {
            return Result<std::optional<AtlasRoute>>::failure(StatusCode::InternalError,
                                                              "Atlas route region has no certificate");
        }
        const auto& certificate = certificates_[regions_[index].certificate_index];
        if (certificate.level != EvidenceLevel::CertifiedRegion ||
            certificate.robot_digest != robot_digest_ || certificate.scene_digest != scene_digest_) {
            return Result<std::optional<AtlasRoute>>::failure(StatusCode::InternalError,
                                                              "Atlas route region certificate is invalid");
        }
        clearance = std::min(clearance, certificate.clearance_lower_bound);
        if (!have_padding) {
            obstacle_padding = certificate.policy.obstacle_padding;
            have_padding = true;
        } else if (certificate.policy.obstacle_padding != obstacle_padding) {
            return Result<std::optional<AtlasRoute>>::failure(
                StatusCode::InternalError, "Atlas route certificate padding policies differ");
        }
        region_ids.emplace_back(std::to_string(regions_[index].id));
        region_certificates.emplace_back(certificate.id);
        region_bounds.emplace_back(box_identity(regions_[index].bounds));
    }
    if (!std::isfinite(clearance))
        clearance = 0.0;
    internal::Json::Array waypoints;
    for (const auto& waypoint : route.waypoints)
        waypoints.emplace_back(configuration_json(waypoint));
    const std::string subject =
        internal::sha256(internal::Json(internal::Json::Object{
                                            {"region_sequence", std::move(region_ids)},
                                            {"region_certificates", std::move(region_certificates)},
                                            {"region_bounds", std::move(region_bounds)},
                                            {"type", "atlas-convex-aabb-chain"},
                                            {"waypoints", std::move(waypoints)},
                                        })
                             .dump(false));
    auto certificate = internal::make_subject_certificate(
        EvidenceLevel::CertifiedConnectivity, robot_digest_, scene_digest_,
        {"atlas-convex-aabb-chain", "1", obstacle_padding}, subject, clearance);
    if (!certificate)
        return certificate.error();
    route.certificate = std::move(certificate).value();
    return std::optional<AtlasRoute>{std::move(route)};
}

Result<void> SafeAtlas::verify_compatible(const SerialRobotModel& robot, const SceneSnapshot& scene) const {
    auto robot_status = robot.validate();
    if (!robot_status)
        return robot_status;
    auto scene_status = scene.validate();
    if (!scene_status)
        return scene_status;
    if (robot.digest() != robot_digest_) {
        return Result<void>::failure(StatusCode::IdentityMismatch, "atlas robot identity does not match");
    }
    if (scene.digest() != scene_digest_) {
        return Result<void>::failure(StatusCode::IdentityMismatch, "atlas scene identity does not match");
    }
    return Result<void>::success();
}

Result<void> SafeAtlas::save(const std::filesystem::path& directory, const SaveOptions& options) const {
    return save_atlas_directory(*this, directory, options);
}

Result<SafeAtlas> SafeAtlas::load(const std::filesystem::path& directory) {
    return load_atlas_directory(directory);
}

} // namespace rbfsafe
