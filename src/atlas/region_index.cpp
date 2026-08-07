#include "internal/region_index.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

namespace rbfsafe::detail {
namespace {

constexpr std::size_t leaf_capacity = 8;
constexpr double query_tolerance = 1e-12;

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

CspaceAabb enclosing_box(const std::vector<SafeRegion>& regions, const std::vector<std::size_t>& indices,
                         std::size_t begin, std::size_t end, std::size_t dimension) {
    std::vector<Interval> axes(
        dimension, {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()});
    for (std::size_t offset = begin; offset < end; ++offset) {
        const auto& box = regions[indices[offset]].bounds;
        for (std::size_t axis = 0; axis < dimension; ++axis) {
            axes[axis].lower = std::min(axes[axis].lower, box.axes()[axis].lower);
            axes[axis].upper = std::max(axes[axis].upper, box.axes()[axis].upper);
        }
    }
    return CspaceAabb(std::move(axes));
}

} // namespace

std::shared_ptr<const RegionQueryIndex> RegionQueryIndex::build(const std::vector<SafeRegion>& regions,
                                                                std::size_t dimension) {
    auto index = std::shared_ptr<RegionQueryIndex>(new RegionQueryIndex());
    index->dimension_ = dimension;
    index->indices_.resize(regions.size());
    std::iota(index->indices_.begin(), index->indices_.end(), 0);
    if (regions.empty() || dimension == 0)
        return index;

    const auto root = enclosing_box(regions, index->indices_, 0, regions.size(), dimension);
    index->root_widths_.reserve(dimension);
    for (const auto& axis : root.axes())
        index->root_widths_.push_back(axis.width());
    index->nodes_.reserve(regions.size() * 2);
    index->build_node(regions, 0, regions.size());
    return index;
}

std::size_t RegionQueryIndex::build_node(const std::vector<SafeRegion>& regions, std::size_t begin,
                                         std::size_t end) {
    const std::size_t node_index = nodes_.size();
    nodes_.push_back({enclosing_box(regions, indices_, begin, end, dimension_), begin, end, 0, 0,
                      end - begin <= leaf_capacity});
    if (nodes_[node_index].leaf)
        return node_index;

    std::size_t split_axis = 0;
    double best_spread = -1.0;
    for (std::size_t axis = 0; axis < dimension_; ++axis) {
        double minimum_center = std::numeric_limits<double>::infinity();
        double maximum_center = -std::numeric_limits<double>::infinity();
        for (std::size_t offset = begin; offset < end; ++offset) {
            const double center = regions[indices_[offset]].bounds.axes()[axis].center();
            minimum_center = std::min(minimum_center, center);
            maximum_center = std::max(maximum_center, center);
        }
        const double normalizer = root_widths_[axis] > 0.0 ? root_widths_[axis] : 1.0;
        const double spread = (maximum_center - minimum_center) / normalizer;
        if (spread > best_spread) {
            best_spread = spread;
            split_axis = axis;
        }
    }

    std::stable_sort(indices_.begin() + static_cast<std::ptrdiff_t>(begin),
                     indices_.begin() + static_cast<std::ptrdiff_t>(end),
                     [&](std::size_t left, std::size_t right) {
                         const double left_center = regions[left].bounds.axes()[split_axis].center();
                         const double right_center = regions[right].bounds.axes()[split_axis].center();
                         if (left_center != right_center)
                             return left_center < right_center;
                         return regions[left].id < regions[right].id;
                     });
    const std::size_t middle = begin + (end - begin) / 2;
    const std::size_t left = build_node(regions, begin, middle);
    const std::size_t right = build_node(regions, middle, end);
    nodes_[node_index].left = left;
    nodes_[node_index].right = right;
    return node_index;
}

std::vector<std::size_t> RegionQueryIndex::containing(std::span<const double> configuration,
                                                      const std::vector<SafeRegion>& regions) const {
    std::vector<std::size_t> result;
    if (!nodes_.empty())
        collect(0, configuration, regions, result);
    std::sort(result.begin(), result.end());
    return result;
}

void RegionQueryIndex::collect(std::size_t node_index, std::span<const double> configuration,
                               const std::vector<SafeRegion>& regions,
                               std::vector<std::size_t>& result) const {
    const auto& node = nodes_[node_index];
    if (!node.bounds.contains(configuration, query_tolerance))
        return;
    if (!node.leaf) {
        collect(node.left, configuration, regions, result);
        collect(node.right, configuration, regions, result);
        return;
    }
    for (std::size_t offset = node.begin; offset < node.end; ++offset) {
        const auto region_index = indices_[offset];
        if (regions[region_index].bounds.contains(configuration, query_tolerance))
            result.push_back(region_index);
    }
}

std::optional<std::size_t> RegionQueryIndex::nearest(std::span<const double> configuration,
                                                     const std::vector<SafeRegion>& regions) const {
    if (nodes_.empty())
        return std::nullopt;
    std::size_t selected = indices_.front();
    double best_distance = point_box_distance_squared(configuration, regions[selected].bounds);
    search_nearest(0, configuration, regions, selected, best_distance);
    return selected;
}

void RegionQueryIndex::search_nearest(std::size_t node_index, std::span<const double> configuration,
                                      const std::vector<SafeRegion>& regions, std::size_t& selected,
                                      double& best_distance) const {
    const auto& node = nodes_[node_index];
    if (point_box_distance_squared(configuration, node.bounds) > best_distance)
        return;
    if (node.leaf) {
        for (std::size_t offset = node.begin; offset < node.end; ++offset) {
            const auto candidate = indices_[offset];
            const double distance = point_box_distance_squared(configuration, regions[candidate].bounds);
            if (distance < best_distance ||
                (distance == best_distance && regions[candidate].id < regions[selected].id)) {
                selected = candidate;
                best_distance = distance;
            }
        }
        return;
    }

    const double left_distance = point_box_distance_squared(configuration, nodes_[node.left].bounds);
    const double right_distance = point_box_distance_squared(configuration, nodes_[node.right].bounds);
    if (left_distance <= right_distance) {
        search_nearest(node.left, configuration, regions, selected, best_distance);
        search_nearest(node.right, configuration, regions, selected, best_distance);
    } else {
        search_nearest(node.right, configuration, regions, selected, best_distance);
        search_nearest(node.left, configuration, regions, selected, best_distance);
    }
}

} // namespace rbfsafe::detail
