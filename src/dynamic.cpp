#include <rbfsafe/dynamic.h>
#include <rbfsafe/version.h>

#include "internal/atlas_identity.h"
#include "internal/certificate_utils.h"
#include "internal/sha256.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <numeric>
#include <set>
#include <utility>

namespace rbfsafe {
namespace {

bool contains_box(const CspaceAabb& outer, const CspaceAabb& inner) {
    if (outer.dimension() != inner.dimension())
        return false;
    for (std::size_t axis = 0; axis < outer.dimension(); ++axis) {
        if (outer.axes()[axis].lower > inner.axes()[axis].lower ||
            outer.axes()[axis].upper < inner.axes()[axis].upper)
            return false;
    }
    return true;
}

bool dependency_valid(const RegionDependency& dependency, const SafeRegion& region,
                      const SerialRobotModel& robot) {
    if (dependency.region_id != region.id || dependency.envelope.links.size() != robot.link_count())
        return false;
    return std::all_of(dependency.envelope.links.begin(), dependency.envelope.links.end(),
                       [](const auto& link) { return link.valid(); });
}

struct InheritanceCheck {
    bool safe = false;
    double clearance = 0.0;
};

InheritanceCheck can_inherit(const RegionDependency& dependency, const SceneDelta& delta,
                             double previous_clearance) {
    double clearance = previous_clearance;
    for (const auto& change : delta.changes) {
        if (change.kind == SceneChangeKind::Removed)
            continue;
        if (!change.after)
            return {};
        for (const auto& link : dependency.envelope.links) {
            if (link.overlaps(*change.after))
                return {};
            clearance = std::min(clearance, link.distance_lower_bound(*change.after));
        }
    }
    return {true, clearance};
}

RegionId repaired_region_id(const std::string& robot_digest, const std::string& scene_digest,
                            RegionId parent_id, const LectNodeKey& local_key, const CspaceAabb& bounds) {
    const std::string digest =
        internal::sha256(robot_digest + "|" + scene_digest + "|repair|" + std::to_string(parent_id) + "|" +
                         local_key.path() + "|" + internal::cspace_aabb_subject_digest(bounds));
    RegionId result = 0;
    for (std::size_t index = 0; index < 16; ++index) {
        const char digit = digest[index];
        const unsigned value = digit >= '0' && digit <= '9' ? static_cast<unsigned>(digit - '0')
                                                            : static_cast<unsigned>(digit - 'a' + 10);
        result = (result << 4u) | value;
    }
    return result == 0 ? 1 : result;
}

void rebuild_graph(std::vector<SafeRegion>& regions, std::vector<std::vector<std::size_t>>& adjacency,
                   double tolerance) {
    adjacency.assign(regions.size(), {});
    for (std::size_t left = 0; left < regions.size(); ++left) {
        for (std::size_t right = left + 1; right < regions.size(); ++right) {
            if (regions[left].bounds.overlaps(regions[right].bounds, tolerance)) {
                adjacency[left].push_back(right);
                adjacency[right].push_back(left);
            }
        }
    }
    ComponentId component = 0;
    std::vector<bool> visited(regions.size(), false);
    for (std::size_t start = 0; start < regions.size(); ++start) {
        if (visited[start])
            continue;
        ++component;
        std::deque<std::size_t> frontier{start};
        visited[start] = true;
        while (!frontier.empty()) {
            const auto current = frontier.front();
            frontier.pop_front();
            regions[current].component = component;
            for (const auto neighbor : adjacency[current]) {
                if (!visited[neighbor]) {
                    visited[neighbor] = true;
                    frontier.push_back(neighbor);
                }
            }
        }
    }
}

Result<void> consume_validation_budget(AtlasUpdateStats& stats, const AtlasUpdateOptions& options) {
    if (stats.validations >= options.maximum_validations) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "Atlas update reached maximum validation count",
                                     std::to_string(options.maximum_validations));
    }
    ++stats.validations;
    return Result<void>::success();
}

struct CandidateRegion {
    SafeRegion region;
    Certificate certificate;
    RegionDependency dependency;
};

} // namespace

AtlasUpdater::AtlasUpdater() = default;

AtlasUpdater::AtlasUpdater(std::shared_ptr<const RegionValidator> validator)
    : validator_(std::move(validator)) {}

Result<AtlasUpdateResult>
AtlasUpdater::update(const SerialRobotModel& robot, const SceneSnapshot& previous_scene,
                     const SceneSnapshot& next_scene, const SafeAtlas& previous_atlas,
                     std::vector<Configuration> repair_samples, const AtlasUpdateOptions& options) const {
    auto compatible = previous_atlas.verify_compatible(robot, previous_scene);
    if (!compatible)
        return compatible.error();
    if (previous_scene.digest() == next_scene.digest()) {
        return Result<AtlasUpdateResult>::failure(StatusCode::InvalidArgument,
                                                  "Atlas update target scene is unchanged");
    }
    if (options.maximum_repair_depth == 0 || options.maximum_repair_depth > 4096 ||
        options.maximum_repair_nodes == 0 || options.maximum_validations == 0 ||
        !std::isfinite(options.minimum_normalized_width) || options.minimum_normalized_width < 0.0 ||
        options.minimum_normalized_width >= 1.0 || !std::isfinite(options.adjacency_tolerance) ||
        options.adjacency_tolerance < 0.0 || !std::isfinite(options.obstacle_padding) ||
        options.obstacle_padding < 0.0) {
        return Result<AtlasUpdateResult>::failure(StatusCode::InvalidArgument,
                                                  "invalid Atlas update options");
    }
    for (std::size_t index = 0; index < repair_samples.size(); ++index) {
        auto sample_status = validate_configuration(repair_samples[index], robot.dimension(),
                                                    "repair sample " + std::to_string(index));
        if (!sample_status)
            return sample_status.error();
        if (!robot.configuration_domain().contains(repair_samples[index], 1e-12)) {
            return Result<AtlasUpdateResult>::failure(StatusCode::InvalidArgument,
                                                      "repair sample lies outside joint limits",
                                                      std::to_string(index));
        }
    }
    std::sort(repair_samples.begin(), repair_samples.end());
    repair_samples.erase(std::unique(repair_samples.begin(), repair_samples.end()), repair_samples.end());

    auto delta_result = compare_scenes(previous_scene, next_scene);
    if (!delta_result)
        return delta_result.error();
    SceneDelta delta = std::move(delta_result).value();

    std::shared_ptr<const RegionValidator> default_validator;
    const RegionValidator* validator = validator_.get();
    if (validator == nullptr) {
        default_validator =
            std::make_shared<IfkAaLinkAabbValidator>(EnvelopeOptions{options.obstacle_padding});
        validator = default_validator.get();
    }

    AtlasUpdateResult output;
    output.delta = delta;
    std::vector<CandidateRegion> candidates;
    candidates.reserve(previous_atlas.regions().size());
    std::vector<SafeRegion> invalidated;

    for (std::size_t index = 0; index < previous_atlas.regions().size(); ++index) {
        if (options.cancellation.cancelled())
            return Result<AtlasUpdateResult>::failure(StatusCode::Cancelled, "Atlas update was cancelled");
        ++output.stats.regions_examined;
        const auto& region = previous_atlas.regions()[index];
        if (region.certificate_index >= previous_atlas.certificates().size()) {
            return Result<AtlasUpdateResult>::failure(StatusCode::CorruptData,
                                                      "previous Atlas certificate index is invalid");
        }
        const auto& parent = previous_atlas.certificates()[region.certificate_index];
        const bool have_dependency = index < previous_atlas.dependencies().size() &&
                                     dependency_valid(previous_atlas.dependencies()[index], region, robot);
        const std::string subject = internal::cspace_aabb_subject_digest(region.bounds);
        const bool policy_matches = parent.policy.algorithm == validator->algorithm_name() &&
                                    parent.policy.algorithm_version == validator->algorithm_version() &&
                                    parent.policy.obstacle_padding == options.obstacle_padding;
        const bool parent_valid = parent.level == EvidenceLevel::CertifiedRegion &&
                                  parent.robot_digest == previous_atlas.robot_digest() &&
                                  parent.scene_digest == previous_atlas.scene_digest() &&
                                  parent.subject_digest == subject &&
                                  internal::certificate_identity(parent) == parent.id;

        if (have_dependency && policy_matches && parent_valid) {
            const auto inheritance =
                can_inherit(previous_atlas.dependencies()[index], delta, parent.clearance_lower_bound);
            if (inheritance.safe) {
                auto certificate = internal::make_transition_certificate(parent, next_scene.digest(), subject,
                                                                         delta.digest, inheritance.clearance);
                if (!certificate)
                    return certificate.error();
                candidates.push_back(
                    {region, std::move(certificate).value(), previous_atlas.dependencies()[index]});
                candidates.back().dependency.region_id = region.id;
                output.retained_region_ids.push_back(region.id);
                ++output.stats.certificates_inherited;
                continue;
            }
        }

        auto budget = consume_validation_budget(output.stats, options);
        if (!budget)
            return budget.error();
        auto validation = validator->validate(robot, next_scene, region.bounds);
        if (!validation)
            return validation.error();
        ++output.stats.regions_revalidated;
        if (validation.value().disposition == ValidationDisposition::CertifiedFree) {
            auto certificate = make_region_certificate(robot, next_scene, region.bounds, *validator,
                                                       validation.value(), options.obstacle_padding);
            if (!certificate)
                return certificate.error();
            candidates.push_back(
                {region, std::move(certificate).value(), {region.id, validation.value().envelope}});
            output.retained_region_ids.push_back(region.id);
        } else {
            invalidated.push_back(region);
            output.invalidated_region_ids.push_back(region.id);
            ++output.stats.regions_invalidated;
        }
    }

    std::set<RegionId> used_ids;
    for (const auto& candidate : candidates)
        used_ids.insert(candidate.region.id);

    const bool may_recover_space =
        std::any_of(delta.changes.begin(), delta.changes.end(), [](const auto& change) {
            return change.kind == SceneChangeKind::Removed || change.kind == SceneChangeKind::Modified;
        });
    std::vector<AtlasRepairDomain> domains_to_repair;
    std::vector<AtlasRepairDomain> remaining_domains;
    if (may_recover_space)
        domains_to_repair = previous_atlas.repair_domains();
    else
        remaining_domains = previous_atlas.repair_domains();
    for (const auto& region : invalidated)
        domains_to_repair.push_back({region.id, region.bounds, region.source_node});
    std::sort(domains_to_repair.begin(), domains_to_repair.end(), [](const auto& left, const auto& right) {
        const auto left_subject = internal::cspace_aabb_subject_digest(left.bounds);
        const auto right_subject = internal::cspace_aabb_subject_digest(right.bounds);
        if (left_subject != right_subject)
            return left_subject < right_subject;
        return left.id < right.id;
    });
    domains_to_repair.erase(std::unique(domains_to_repair.begin(), domains_to_repair.end(),
                                        [](const auto& left, const auto& right) {
                                            return internal::cspace_aabb_subject_digest(left.bounds) ==
                                                   internal::cspace_aabb_subject_digest(right.bounds);
                                        }),
                            domains_to_repair.end());

    if (options.repair_invalidated_regions) {
        for (const auto& repair_domain : domains_to_repair) {
            std::vector<Configuration> local_samples;
            local_samples.push_back(repair_domain.bounds.center());
            for (const auto& sample : repair_samples) {
                if (repair_domain.bounds.contains(sample, 1e-12))
                    local_samples.push_back(sample);
            }
            std::sort(local_samples.begin(), local_samples.end());
            local_samples.erase(std::unique(local_samples.begin(), local_samples.end()), local_samples.end());

            auto tree_result =
                LectTree::create(repair_domain.bounds, SplitPolicy{SplitStrategy::NormalizedLongestAxis,
                                                                   options.minimum_normalized_width});
            if (!tree_result)
                return tree_result.error();
            LectTree tree = std::move(tree_result).value();
            struct WorkItem {
                LectNodeKey key;
                std::vector<std::size_t> samples;
                bool may_split = true;
            };
            std::vector<std::size_t> sample_indices(local_samples.size());
            std::iota(sample_indices.begin(), sample_indices.end(), 0);
            std::deque<WorkItem> work;
            work.push_back({tree.root_key(), std::move(sample_indices), true});
            bool has_unresolved_nodes = false;

            while (!work.empty()) {
                if (options.cancellation.cancelled())
                    return Result<AtlasUpdateResult>::failure(StatusCode::Cancelled,
                                                              "Atlas repair was cancelled");
                WorkItem item = std::move(work.front());
                work.pop_front();
                if (output.stats.repair_nodes_visited >= options.maximum_repair_nodes) {
                    return Result<AtlasUpdateResult>::failure(StatusCode::ResourceLimit,
                                                              "Atlas repair reached maximum node count",
                                                              std::to_string(options.maximum_repair_nodes));
                }
                auto node = tree.node(item.key);
                if (!node)
                    return node.error();
                ++output.stats.repair_nodes_visited;
                auto budget = consume_validation_budget(output.stats, options);
                if (!budget)
                    return budget.error();
                auto validation = validator->validate(robot, next_scene, node.value().box);
                if (!validation)
                    return validation.error();
                if (validation.value().disposition == ValidationDisposition::CertifiedFree) {
                    const bool covered_by_existing =
                        std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
                            return contains_box(candidate.region.bounds, node.value().box);
                        });
                    if (covered_by_existing)
                        continue;
                    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                                    [&](const auto& candidate) {
                                                        return contains_box(node.value().box,
                                                                            candidate.region.bounds);
                                                    }),
                                     candidates.end());
                    auto certificate =
                        make_region_certificate(robot, next_scene, node.value().box, *validator,
                                                validation.value(), options.obstacle_padding);
                    if (!certificate)
                        return certificate.error();
                    RegionId id = item.key.depth() == 0 ? repair_domain.id : 0;
                    if (id == 0 || used_ids.contains(id)) {
                        id = repaired_region_id(robot.digest(), next_scene.digest(), repair_domain.id,
                                                item.key, node.value().box);
                    }
                    while (!used_ids.insert(id).second)
                        ++id;
                    SafeRegion repaired{id, node.value().box, 0, 0, repair_domain.source_node};
                    candidates.push_back({std::move(repaired),
                                          std::move(certificate).value(),
                                          {id, validation.value().envelope}});
                    output.repaired_region_ids.push_back(id);
                    ++output.stats.repaired_regions;
                    continue;
                }
                if (!item.may_split || item.samples.empty() ||
                    item.key.depth() >= options.maximum_repair_depth) {
                    has_unresolved_nodes = true;
                    ++output.stats.unresolved_repair_nodes;
                    continue;
                }
                if (output.stats.repair_nodes_visited + work.size() + 2 > options.maximum_repair_nodes) {
                    return Result<AtlasUpdateResult>::failure(StatusCode::ResourceLimit,
                                                              "Atlas repair reached maximum node count",
                                                              std::to_string(options.maximum_repair_nodes));
                }
                auto children = tree.split(item.key);
                if (!children) {
                    if (children.error().code == StatusCode::ResourceLimit) {
                        has_unresolved_nodes = true;
                        ++output.stats.unresolved_repair_nodes;
                        continue;
                    }
                    return children.error();
                }
                auto parent = tree.node(item.key);
                if (!parent)
                    return parent.error();
                const std::size_t split_dimension = parent.value().split_dimension;
                const double midpoint = parent.value().box.axes()[split_dimension].center();
                std::vector<std::size_t> left_samples;
                std::vector<std::size_t> right_samples;
                for (const auto sample : item.samples) {
                    if (local_samples[sample][split_dimension] <= midpoint)
                        left_samples.push_back(sample);
                    else
                        right_samples.push_back(sample);
                }
                const bool split_left = !left_samples.empty();
                const bool split_right = !right_samples.empty();
                work.push_back({children.value().first, std::move(left_samples), split_left});
                work.push_back({children.value().second, std::move(right_samples), split_right});
            }
            if (has_unresolved_nodes)
                remaining_domains.push_back(repair_domain);
        }
    } else {
        remaining_domains.insert(remaining_domains.end(), domains_to_repair.begin(), domains_to_repair.end());
    }

    std::sort(remaining_domains.begin(), remaining_domains.end(), [](const auto& left, const auto& right) {
        const auto left_subject = internal::cspace_aabb_subject_digest(left.bounds);
        const auto right_subject = internal::cspace_aabb_subject_digest(right.bounds);
        if (left_subject != right_subject)
            return left_subject < right_subject;
        return left.id < right.id;
    });
    remaining_domains.erase(std::unique(remaining_domains.begin(), remaining_domains.end(),
                                        [](const auto& left, const auto& right) {
                                            return internal::cspace_aabb_subject_digest(left.bounds) ==
                                                   internal::cspace_aabb_subject_digest(right.bounds);
                                        }),
                            remaining_domains.end());
    std::sort(remaining_domains.begin(), remaining_domains.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& left, const auto& right) { return left.region.id < right.region.id; });
    SafeAtlas atlas;
    atlas.dimension_ = previous_atlas.dimension();
    atlas.storage_schema_ = kAtlasSchemaVersion;
    atlas.robot_digest_ = previous_atlas.robot_digest();
    atlas.scene_digest_ = next_scene.digest();
    atlas.lect_ = previous_atlas.lect();
    atlas.regions_.reserve(candidates.size());
    atlas.certificates_.reserve(candidates.size());
    atlas.dependencies_.reserve(candidates.size());
    for (auto& candidate : candidates) {
        candidate.region.certificate_index = atlas.certificates_.size();
        candidate.dependency.region_id = candidate.region.id;
        atlas.regions_.push_back(std::move(candidate.region));
        atlas.certificates_.push_back(std::move(candidate.certificate));
        atlas.dependencies_.push_back(std::move(candidate.dependency));
    }
    atlas.repair_domains_ = std::move(remaining_domains);
    rebuild_graph(atlas.regions_, atlas.adjacency_, options.adjacency_tolerance);
    if (previous_atlas.version_info().sequence == std::numeric_limits<std::uint64_t>::max()) {
        return Result<AtlasUpdateResult>::failure(StatusCode::ResourceLimit,
                                                  "Atlas version sequence is exhausted");
    }
    atlas.version_info_.sequence = previous_atlas.version_info().sequence + 1;
    atlas.version_info_.parent_id = previous_atlas.version_info().id;
    atlas.version_info_.scene_version = next_scene.version();
    atlas.version_info_.scene_digest = next_scene.digest();
    atlas.version_info_.transition_digest = delta.digest;
    atlas.transition_ = delta;
    atlas.version_info_.id = internal::atlas_version_identity(atlas);
    atlas.rebuild_query_index();
    std::set<RegionId> final_ids;
    for (const auto& region : atlas.regions())
        final_ids.insert(region.id);
    std::erase_if(output.retained_region_ids, [&](RegionId id) { return !final_ids.contains(id); });
    std::erase_if(output.repaired_region_ids, [&](RegionId id) { return !final_ids.contains(id); });
    output.atlas = std::move(atlas);
    return output;
}

} // namespace rbfsafe
