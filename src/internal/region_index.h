#pragma once

#include <rbfsafe/atlas.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace rbfsafe::detail {

class RegionQueryIndex {
  public:
    static std::shared_ptr<const RegionQueryIndex> build(const std::vector<SafeRegion>& regions,
                                                         std::size_t dimension);

    std::vector<std::size_t> containing(std::span<const double> configuration,
                                        const std::vector<SafeRegion>& regions) const;
    std::optional<std::size_t> nearest(std::span<const double> configuration,
                                       const std::vector<SafeRegion>& regions) const;

  private:
    struct Node {
        CspaceAabb bounds;
        std::size_t begin = 0;
        std::size_t end = 0;
        std::size_t left = 0;
        std::size_t right = 0;
        bool leaf = true;
    };

    std::size_t build_node(const std::vector<SafeRegion>& regions, std::size_t begin, std::size_t end);
    void collect(std::size_t node_index, std::span<const double> configuration,
                 const std::vector<SafeRegion>& regions, std::vector<std::size_t>& result) const;
    void search_nearest(std::size_t node_index, std::span<const double> configuration,
                        const std::vector<SafeRegion>& regions, std::size_t& selected,
                        double& best_distance) const;

    std::size_t dimension_ = 0;
    std::vector<double> root_widths_;
    std::vector<std::size_t> indices_;
    std::vector<Node> nodes_;
};

} // namespace rbfsafe::detail
