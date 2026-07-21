#include "internal/region_index.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

#define CHECK(condition)                                                                                     \
    do {                                                                                                     \
        if (!(condition)) {                                                                                  \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition "\n";                  \
            return EXIT_FAILURE;                                                                             \
        }                                                                                                    \
    } while (false)

int main() {
    using namespace rbfsafe;

    std::vector<SafeRegion> regions;
    regions.push_back({20, CspaceAabb({{0.0, 1.0}, {0.0, 1.0}}), 0, 1, LectNodeKey::root()});
    regions.push_back({10, CspaceAabb({{2.0, 3.0}, {0.0, 1.0}}), 0, 1, LectNodeKey::root()});
    regions.push_back({30, CspaceAabb({{0.5, 2.5}, {0.25, 0.75}}), 0, 1, LectNodeKey::root()});
    for (std::size_t index = 0; index < 64; ++index) {
        const double offset = 10.0 + static_cast<double>(index);
        regions.push_back(
            {100 + index, CspaceAabb({{offset, offset + 0.5}, {2.0, 2.5}}), 0, 1, LectNodeKey::root()});
    }

    auto query_index = detail::RegionQueryIndex::build(regions, 2);
    auto containing = query_index->containing(Configuration{0.75, 0.5}, regions);
    CHECK(containing.size() == 2);
    CHECK(containing[0] == 0);
    CHECK(containing[1] == 2);

    auto nearest_tie = query_index->nearest(Configuration{1.5, -1.0}, regions);
    CHECK(nearest_tie.has_value());
    CHECK(*nearest_tie == 1);
    auto nearest_far = query_index->nearest(Configuration{42.2, 2.2}, regions);
    CHECK(nearest_far.has_value());
    CHECK(regions[*nearest_far].bounds.contains(Configuration{42.2, 2.2}));

    std::mt19937_64 engine(42);
    std::uniform_real_distribution<double> x_distribution(-5.0, 80.0);
    std::uniform_real_distribution<double> y_distribution(-3.0, 4.0);
    for (int query = 0; query < 1000; ++query) {
        const Configuration point{x_distribution(engine), y_distribution(engine)};
        std::vector<std::size_t> expected_containing;
        std::size_t expected_nearest = 0;
        double expected_distance = std::numeric_limits<double>::infinity();
        for (std::size_t index = 0; index < regions.size(); ++index) {
            if (regions[index].bounds.contains(point, 1e-12))
                expected_containing.push_back(index);
            double squared_distance = 0.0;
            for (std::size_t axis = 0; axis < point.size(); ++axis) {
                double difference = 0.0;
                if (point[axis] < regions[index].bounds.axes()[axis].lower)
                    difference = regions[index].bounds.axes()[axis].lower - point[axis];
                else if (point[axis] > regions[index].bounds.axes()[axis].upper)
                    difference = point[axis] - regions[index].bounds.axes()[axis].upper;
                squared_distance += difference * difference;
            }
            if (squared_distance < expected_distance ||
                (squared_distance == expected_distance && regions[index].id < regions[expected_nearest].id)) {
                expected_nearest = index;
                expected_distance = squared_distance;
            }
        }
        CHECK(query_index->containing(point, regions) == expected_containing);
        const auto actual_nearest = query_index->nearest(point, regions);
        CHECK(actual_nearest.has_value());
        CHECK(*actual_nearest == expected_nearest);
    }

    auto empty_index = detail::RegionQueryIndex::build({}, 2);
    CHECK(empty_index->containing(Configuration{0.0, 0.0}, {}).empty());
    CHECK(!empty_index->nearest(Configuration{0.0, 0.0}, {}).has_value());
    return EXIT_SUCCESS;
}
