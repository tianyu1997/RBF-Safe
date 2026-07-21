#include <rbfsafe/atlas.h>

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
            pending.push_back({node.value().box, node.value().key, validation.value().clearance_lower_bound});
            ++stats.certified_nodes;
            continue;
        }
        if (!current.may_split || current.samples.empty() || current.key.depth() >= options.maximum_depth) {
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
                pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(right));
                ++stats.merged_regions;
                changed = true;
                break;
            }
        }
    }

    SafeAtlas atlas;
    atlas.dimension_ = robot.dimension();
    atlas.robot_digest_ = robot.digest();
    atlas.scene_digest_ = scene.digest();
    atlas.lect_ = LectSnapshot::from_tree(std::move(tree));
    atlas.regions_.reserve(pending.size());
    atlas.certificates_.reserve(pending.size());
    std::set<RegionId> ids;
    for (const auto& region : pending) {
        RegionValidation validation;
        validation.disposition = ValidationDisposition::CertifiedFree;
        validation.clearance_lower_bound = region.clearance;
        auto certificate =
            make_region_certificate(robot, scene, *validator, validation, options.obstacle_padding);
        if (!certificate)
            return certificate.error();
        const auto certificate_index = atlas.certificates_.size();
        atlas.certificates_.push_back(std::move(certificate).value());
        RegionId id = stable_region_id(atlas.robot_digest_, atlas.scene_digest_, region);
        while (!ids.insert(id).second)
            ++id;
        atlas.regions_.push_back({id, region.bounds, certificate_index, 0, region.source});
    }
    std::sort(atlas.regions_.begin(), atlas.regions_.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    // Reorder certificates to remain aligned with the sorted regions.
    std::vector<Certificate> sorted_certificates;
    sorted_certificates.reserve(atlas.certificates_.size());
    for (auto& region : atlas.regions_) {
        sorted_certificates.push_back(atlas.certificates_[region.certificate_index]);
        region.certificate_index = sorted_certificates.size() - 1;
    }
    atlas.certificates_ = std::move(sorted_certificates);

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
    return AtlasBuildResult{std::move(atlas), stats};
}

Result<std::vector<SafeRegion>> SafeAtlas::regions_at(std::span<const double> configuration) const {
    auto status = validate_configuration(configuration, dimension_);
    if (!status)
        return status.error();
    std::vector<SafeRegion> result;
    for (const auto& region : regions_)
        if (region.bounds.contains(configuration, 1e-12))
            result.push_back(region);
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
    double best = point_box_distance_squared(configuration, regions_[0].bounds);
    for (std::size_t index = 1; index < regions_.size(); ++index) {
        const double distance = point_box_distance_squared(configuration, regions_[index].bounds);
        if (distance < best || (distance == best && regions_[index].id < regions_[selected].id)) {
            selected = index;
            best = distance;
        }
    }
    return std::optional<SafeRegion>{regions_[selected]};
}

Result<bool> SafeAtlas::connected(std::span<const double> first, std::span<const double> second) const {
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
