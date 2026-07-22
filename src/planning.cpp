#include <rbfsafe/planning.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <utility>

namespace rbfsafe {
namespace {

Result<void> validate_sampler_options(const CertifiedSamplerOptions& options) {
    if (options.maximum_attempts == 0) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "certified sampler attempt budget must be positive", "planning");
    }
    if (options.policy != CertifiedSamplingPolicy::UniformRegions &&
        options.policy != CertifiedSamplingPolicy::VolumeWeighted) {
        return Result<void>::failure(StatusCode::InvalidArgument, "certified sampling policy is unsupported",
                                     "planning");
    }
    return Result<void>::success();
}

Result<void> validate_atlas_for_planning(const SafeAtlas& atlas) {
    if (atlas.dimension() == 0 || !atlas.lect().valid() || atlas.regions().empty()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "planning requires a non-empty valid SafeAtlas", "planning");
    }
    if (atlas.adjacency().size() != atlas.regions().size()) {
        return Result<void>::failure(StatusCode::CorruptData,
                                     "Atlas adjacency count does not match region count", "planning");
    }
    for (std::size_t index = 0; index < atlas.regions().size(); ++index) {
        const auto& region = atlas.regions()[index];
        if (region.id == 0 || !region.bounds.valid() || region.bounds.dimension() != atlas.dimension() ||
            region.certificate_index >= atlas.certificates().size()) {
            return Result<void>::failure(StatusCode::CorruptData,
                                         "Atlas contains an invalid certified region", "planning");
        }
        const auto& certificate = atlas.certificates()[region.certificate_index];
        if (certificate.level != EvidenceLevel::CertifiedRegion ||
            certificate.robot_digest != atlas.robot_digest() ||
            certificate.scene_digest != atlas.scene_digest()) {
            return Result<void>::failure(StatusCode::CorruptData, "Atlas region certificate is inconsistent",
                                         "planning");
        }
    }
    return Result<void>::success();
}

double point_box_distance_squared(std::span<const double> point, const CspaceAabb& box) {
    double squared = 0.0;
    for (std::size_t axis = 0; axis < box.dimension(); ++axis) {
        double difference = 0.0;
        if (point[axis] < box.axes()[axis].lower)
            difference = box.axes()[axis].lower - point[axis];
        else if (point[axis] > box.axes()[axis].upper)
            difference = point[axis] - box.axes()[axis].upper;
        squared += difference * difference;
    }
    return squared;
}

double distance_squared(std::span<const double> first, std::span<const double> second) {
    double squared = 0.0;
    for (std::size_t axis = 0; axis < first.size(); ++axis) {
        const double difference = first[axis] - second[axis];
        squared += difference * difference;
    }
    return squared;
}

std::optional<Configuration> exact_intersection_witness(const CspaceAabb& first, const CspaceAabb& second) {
    if (first.dimension() != second.dimension())
        return std::nullopt;
    Configuration witness(first.dimension());
    for (std::size_t axis = 0; axis < first.dimension(); ++axis) {
        const double lower = std::max(first.axes()[axis].lower, second.axes()[axis].lower);
        const double upper = std::min(first.axes()[axis].upper, second.axes()[axis].upper);
        if (lower > upper)
            return std::nullopt;
        witness[axis] = 0.5 * (lower + upper);
    }
    return witness;
}

Result<void> validate_roadmap_options(const CertifiedRoadmapOptions& options) {
    if (options.maximum_nodes == 0 || options.maximum_edges == 0) {
        return Result<void>::failure(StatusCode::InvalidArgument, "roadmap resource limits must be positive",
                                     "planning");
    }
    return Result<void>::success();
}

} // namespace

Result<CertifiedRegionSampler> CertifiedRegionSampler::create(std::shared_ptr<const SafeAtlas> atlas,
                                                              const CertifiedSamplerOptions& options) {
    auto option_status = validate_sampler_options(options);
    if (!option_status)
        return option_status.error();
    if (!atlas) {
        return Result<CertifiedRegionSampler>::failure(StatusCode::InvalidArgument,
                                                       "certified sampler Atlas is null", "planning");
    }
    auto atlas_status = validate_atlas_for_planning(*atlas);
    if (!atlas_status)
        return atlas_status.error();

    CertifiedRegionSampler sampler;
    sampler.atlas_ = std::move(atlas);
    sampler.options_ = options;
    sampler.engine_.seed(options.seed);
    sampler.weights_.resize(sampler.atlas_->regions().size(), 1.0);
    if (options.policy == CertifiedSamplingPolicy::VolumeWeighted) {
        double total = 0.0;
        const auto& root = sampler.atlas_->lect().root_domain();
        for (std::size_t index = 0; index < sampler.atlas_->regions().size(); ++index) {
            double weight = 1.0;
            for (std::size_t axis = 0; axis < sampler.atlas_->dimension(); ++axis) {
                const double root_width = root.axes()[axis].width();
                if (!(root_width > 0.0)) {
                    weight = 0.0;
                    break;
                }
                weight *= std::clamp(
                    sampler.atlas_->regions()[index].bounds.axes()[axis].width() / root_width, 0.0, 1.0);
            }
            sampler.weights_[index] = std::isfinite(weight) ? weight : 0.0;
            total += sampler.weights_[index];
        }
        if (!(total > 0.0))
            std::fill(sampler.weights_.begin(), sampler.weights_.end(), 1.0);
    }
    return sampler;
}

std::size_t CertifiedRegionSampler::choose_region(std::span<const std::size_t> candidates) {
    double total = 0.0;
    for (const auto index : candidates)
        total += weights_[index];
    if (!(total > 0.0)) {
        std::uniform_int_distribution<std::size_t> distribution(0, candidates.size() - 1);
        return candidates[distribution(engine_)];
    }
    std::uniform_real_distribution<double> distribution(0.0, total);
    double draw = distribution(engine_);
    for (const auto index : candidates) {
        draw -= weights_[index];
        if (draw <= 0.0)
            return index;
    }
    return candidates.back();
}

Configuration CertifiedRegionSampler::sample_box(const CspaceAabb& box) {
    Configuration result(box.dimension());
    for (std::size_t axis = 0; axis < box.dimension(); ++axis) {
        std::uniform_real_distribution<double> distribution(box.axes()[axis].lower, box.axes()[axis].upper);
        result[axis] = distribution(engine_);
    }
    return result;
}

Result<Configuration> CertifiedRegionSampler::sample() {
    if (!valid()) {
        return Result<Configuration>::failure(StatusCode::InvalidArgument,
                                              "certified sampler is not initialized", "planning");
    }
    ++stats_.samples_requested;
    std::vector<std::size_t> candidates(atlas_->regions().size());
    std::iota(candidates.begin(), candidates.end(), 0);
    const auto index = choose_region(candidates);
    ++stats_.samples_returned;
    return sample_box(atlas_->regions()[index].bounds);
}

Result<Configuration> CertifiedRegionSampler::sample_near(std::span<const double> reference,
                                                          double maximum_distance) {
    if (!valid()) {
        return Result<Configuration>::failure(StatusCode::InvalidArgument,
                                              "certified sampler is not initialized", "planning");
    }
    auto configuration_status =
        validate_configuration(reference, atlas_->dimension(), "planning near-sample reference");
    if (!configuration_status)
        return configuration_status.error();
    if (!std::isfinite(maximum_distance) || maximum_distance < 0.0) {
        return Result<Configuration>::failure(
            StatusCode::InvalidArgument, "near-sample distance must be finite and non-negative", "planning");
    }
    ++stats_.samples_requested;
    ++stats_.near_samples_requested;
    const double maximum_squared = maximum_distance * maximum_distance;
    std::vector<std::size_t> candidates;
    for (std::size_t index = 0; index < atlas_->regions().size(); ++index) {
        if (point_box_distance_squared(reference, atlas_->regions()[index].bounds) <= maximum_squared)
            candidates.push_back(index);
    }
    if (candidates.empty()) {
        return Result<Configuration>::failure(StatusCode::InvalidArgument,
                                              "no certified region intersects the requested near ball",
                                              "planning");
    }

    for (std::size_t attempt = 0; attempt < options_.maximum_attempts; ++attempt) {
        const auto region_index = choose_region(candidates);
        const auto& box = atlas_->regions()[region_index].bounds;
        Configuration sample(box.dimension());
        for (std::size_t axis = 0; axis < box.dimension(); ++axis) {
            const double lower = std::max(box.axes()[axis].lower, reference[axis] - maximum_distance);
            const double upper = std::min(box.axes()[axis].upper, reference[axis] + maximum_distance);
            std::uniform_real_distribution<double> distribution(lower, upper);
            sample[axis] = distribution(engine_);
        }
        if (distance_squared(reference, sample) <= maximum_squared) {
            ++stats_.samples_returned;
            return sample;
        }
        ++stats_.rejected_attempts;
    }

    Configuration closest(reference.begin(), reference.end());
    double closest_distance = std::numeric_limits<double>::infinity();
    for (const auto region_index : candidates) {
        Configuration candidate(reference.begin(), reference.end());
        const auto& box = atlas_->regions()[region_index].bounds;
        for (std::size_t axis = 0; axis < box.dimension(); ++axis) {
            candidate[axis] = std::clamp(candidate[axis], box.axes()[axis].lower, box.axes()[axis].upper);
        }
        const double squared = distance_squared(reference, candidate);
        if (squared < closest_distance) {
            closest_distance = squared;
            closest = std::move(candidate);
        }
    }
    if (closest_distance <= maximum_squared) {
        ++stats_.samples_returned;
        return closest;
    }
    return Result<Configuration>::failure(StatusCode::InternalError,
                                          "near-sample feasibility search was inconsistent", "planning");
}

bool CertifiedRoadmap::valid() const noexcept {
    if (dimension_ == 0 || robot_digest_.empty() || scene_digest_.empty() ||
        adjacency_.size() != nodes_.size())
        return false;
    for (std::size_t index = 0; index < nodes_.size(); ++index) {
        const auto& node = nodes_[index];
        if (node.id != index + 1 || node.configuration.size() != dimension_ || node.support_regions.empty())
            return false;
        for (const double value : node.configuration) {
            if (!std::isfinite(value))
                return false;
        }
    }
    for (std::size_t edge_index = 0; edge_index < edges_.size(); ++edge_index) {
        const auto& edge = edges_[edge_index];
        if (edge.first == 0 || edge.second == 0 || edge.first == edge.second || edge.first > nodes_.size() ||
            edge.second > nodes_.size() || edge.covering_region == 0)
            return false;
    }
    return true;
}

Result<std::optional<CertifiedRoadmapNode>>
CertifiedRoadmap::nearest_node(std::span<const double> configuration) const {
    if (!valid()) {
        return Result<std::optional<CertifiedRoadmapNode>>::failure(
            StatusCode::InvalidArgument, "certified roadmap is invalid", "planning");
    }
    auto status = validate_configuration(configuration, dimension_, "roadmap nearest query");
    if (!status)
        return status.error();
    if (nodes_.empty())
        return std::optional<CertifiedRoadmapNode>{};
    std::size_t best = 0;
    double best_distance = distance_squared(configuration, nodes_.front().configuration);
    for (std::size_t index = 1; index < nodes_.size(); ++index) {
        const double candidate = distance_squared(configuration, nodes_[index].configuration);
        if (candidate < best_distance) {
            best = index;
            best_distance = candidate;
        }
    }
    return std::optional<CertifiedRoadmapNode>(nodes_[best]);
}

Result<void> CertifiedRoadmap::verify_compatible(const SerialRobotModel& robot,
                                                 const SceneSnapshot& scene) const {
    if (!valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument, "certified roadmap is invalid", "planning");
    }
    if (robot.dimension() != dimension_) {
        return Result<void>::failure(StatusCode::DimensionMismatch, "roadmap and robot dimensions differ",
                                     "planning");
    }
    if (robot.digest() != robot_digest_ || scene.digest() != scene_digest_) {
        return Result<void>::failure(StatusCode::IdentityMismatch, "roadmap robot or scene identity differs",
                                     "planning");
    }
    return Result<void>::success();
}

Result<CertifiedRoadmapBuildResult>
CertifiedRoadmapBuilder::build(const SafeAtlas& atlas, const CertifiedRoadmapOptions& options) const {
    auto option_status = validate_roadmap_options(options);
    if (!option_status)
        return option_status.error();
    auto atlas_status = validate_atlas_for_planning(atlas);
    if (!atlas_status)
        return atlas_status.error();

    CertifiedRoadmapBuildResult result;
    result.roadmap.dimension_ = atlas.dimension();
    result.roadmap.robot_digest_ = atlas.robot_digest();
    result.roadmap.scene_digest_ = atlas.scene_digest();

    std::vector<std::size_t> region_order(atlas.regions().size());
    std::iota(region_order.begin(), region_order.end(), 0);
    std::sort(region_order.begin(), region_order.end(), [&](std::size_t left, std::size_t right) {
        return atlas.regions()[left].id < atlas.regions()[right].id;
    });
    std::vector<RoadmapNodeId> center_ids(atlas.regions().size(), 0);
    for (const auto region_index : region_order) {
        if (options.cancellation.cancelled()) {
            return Result<CertifiedRoadmapBuildResult>::failure(
                StatusCode::Cancelled, "certified roadmap build was cancelled", "planning");
        }
        if (result.roadmap.nodes_.size() == options.maximum_nodes) {
            return Result<CertifiedRoadmapBuildResult>::failure(
                StatusCode::ResourceLimit, "certified roadmap reached node budget", "planning");
        }
        const auto node_id = static_cast<RoadmapNodeId>(result.roadmap.nodes_.size() + 1);
        center_ids[region_index] = node_id;
        result.roadmap.nodes_.push_back({node_id,
                                         RoadmapNodeKind::RegionCenter,
                                         atlas.regions()[region_index].bounds.center(),
                                         {atlas.regions()[region_index].id}});
        ++result.stats.region_nodes;
    }

    std::set<std::pair<std::size_t, std::size_t>> pairs;
    for (std::size_t left = 0; left < atlas.adjacency().size(); ++left) {
        for (const auto right : atlas.adjacency()[left]) {
            if (right >= atlas.regions().size()) {
                return Result<CertifiedRoadmapBuildResult>::failure(
                    StatusCode::CorruptData, "Atlas adjacency index is out of range", "planning");
            }
            if (left != right)
                pairs.emplace(std::min(left, right), std::max(left, right));
        }
    }
    std::vector<std::pair<std::size_t, std::size_t>> ordered_pairs(pairs.begin(), pairs.end());
    std::sort(ordered_pairs.begin(), ordered_pairs.end(), [&](const auto& first, const auto& second) {
        const auto first_ids = std::minmax(atlas.regions()[first.first].id, atlas.regions()[first.second].id);
        const auto second_ids =
            std::minmax(atlas.regions()[second.first].id, atlas.regions()[second.second].id);
        return first_ids < second_ids;
    });

    for (const auto& [left, right] : ordered_pairs) {
        if (options.cancellation.cancelled()) {
            return Result<CertifiedRoadmapBuildResult>::failure(
                StatusCode::Cancelled, "certified roadmap build was cancelled", "planning");
        }
        auto witness =
            exact_intersection_witness(atlas.regions()[left].bounds, atlas.regions()[right].bounds);
        if (!witness) {
            ++result.stats.nonintersecting_adjacencies;
            continue;
        }
        if (result.roadmap.nodes_.size() == options.maximum_nodes) {
            return Result<CertifiedRoadmapBuildResult>::failure(
                StatusCode::ResourceLimit, "certified roadmap reached node budget", "planning");
        }
        if (options.maximum_edges - result.roadmap.edges_.size() < 2) {
            return Result<CertifiedRoadmapBuildResult>::failure(
                StatusCode::ResourceLimit, "certified roadmap reached edge budget", "planning");
        }
        const auto portal_id = static_cast<RoadmapNodeId>(result.roadmap.nodes_.size() + 1);
        RegionId left_region_id = atlas.regions()[left].id;
        RegionId right_region_id = atlas.regions()[right].id;
        std::vector<RegionId> supports{left_region_id, right_region_id};
        std::sort(supports.begin(), supports.end());
        result.roadmap.nodes_.push_back(
            {portal_id, RoadmapNodeKind::PortalWitness, std::move(*witness), std::move(supports)});
        result.roadmap.edges_.push_back({center_ids[left], portal_id, left_region_id});
        result.roadmap.edges_.push_back({portal_id, center_ids[right], right_region_id});
        ++result.stats.portal_nodes;
        result.stats.edges += 2;
    }

    result.roadmap.adjacency_.assign(result.roadmap.nodes_.size(), {});
    for (std::size_t edge_index = 0; edge_index < result.roadmap.edges_.size(); ++edge_index) {
        const auto& edge = result.roadmap.edges_[edge_index];
        result.roadmap.adjacency_[edge.first - 1].push_back(edge_index);
        result.roadmap.adjacency_[edge.second - 1].push_back(edge_index);
    }
    for (auto& adjacency : result.roadmap.adjacency_)
        std::sort(adjacency.begin(), adjacency.end());
    if (!result.roadmap.valid()) {
        return Result<CertifiedRoadmapBuildResult>::failure(
            StatusCode::InternalError, "constructed certified roadmap is invalid", "planning");
    }
    return result;
}

} // namespace rbfsafe
