#include "test_support.h"

#include <array>
#include <cmath>
#include <random>
#include <vector>

int main() {
    using namespace rbfsafe;

    const double inverse_sqrt_two = 1.0 / std::sqrt(2.0);
    auto rotated = CspaceObb::create(
        {0.0, 0.0}, {inverse_sqrt_two, inverse_sqrt_two, -inverse_sqrt_two, inverse_sqrt_two}, {1.0, 0.25});
    CHECK(rotated);
    CHECK(rotated.value().valid());
    CHECK(rotated.value().dimension() == 2);
    CHECK(close(rotated.value().volume(), 1.0));
    CHECK(rotated.value().contains(Configuration{0.0, 0.0}));
    CHECK(rotated.value().contains(Configuration{inverse_sqrt_two, inverse_sqrt_two}));
    CHECK(!rotated.value().contains(Configuration{1.0, -1.0}));

    const CspaceAabb enclosure = rotated.value().enclosing_aabb();
    CHECK(enclosure.valid());
    for (int first_sign : {-1, 1}) {
        for (int second_sign : {-1, 1}) {
            Configuration corner(2, 0.0);
            for (std::size_t coordinate = 0; coordinate < 2; ++coordinate) {
                corner[coordinate] = rotated.value().center()[coordinate] +
                                     static_cast<double>(first_sign) * rotated.value().half_widths()[0] *
                                         rotated.value().basis()[coordinate] +
                                     static_cast<double>(second_sign) * rotated.value().half_widths()[1] *
                                         rotated.value().basis()[2 + coordinate];
            }
            CHECK(rotated.value().contains(corner, 1e-12));
            CHECK(enclosure.contains(corner, 1e-12));
        }
    }

    CHECK(!CspaceObb::create({0.0, 0.0}, {1.0, 0.0, 0.5, 1.0}, {1.0, 1.0}));
    CHECK(!CspaceObb::create({0.0}, {1.0}, {-1.0}));
    CHECK(!CspaceObb::create({0.0}, {1.0, 0.0}, {1.0}));

    auto tube = ObbGenerator::segment_tube(Configuration{-1.0, -1.0}, Configuration{1.0, 1.0}, 0.1, 0.05);
    CHECK(tube);
    CHECK(tube.value().contains(Configuration{-1.0, -1.0}, 1e-12));
    CHECK(tube.value().contains(Configuration{1.0, 1.0}, 1e-12));
    CHECK(close(tube.value().half_widths()[0], std::sqrt(2.0) + 0.05));
    CHECK(!ObbGenerator::segment_tube(Configuration{0.0}, Configuration{0.0, 1.0}, 0.1));

    const auto robot = planar_robot();
    const SceneSnapshot empty({}, "obb-empty-v1");
    ObbRegionValidator validator;
    auto validation = validator.validate(robot, empty, rotated.value());
    CHECK(validation);
    CHECK(validation.value().disposition == ValidationDisposition::CertifiedFree);
    CHECK(validation.value().conservative_enclosure.axes() == enclosure.axes());

    ObbGrowthOptions growth_options;
    growth_options.initial_lateral_half_width = 0.01;
    growth_options.maximum_lateral_half_width = 0.2;
    auto grown =
        ObbGrower{}.grow(robot, empty, Configuration{-0.5, -0.5}, Configuration{0.5, 0.5}, growth_options);
    CHECK(grown);
    CHECK(grown.value().certified);
    CHECK(close(grown.value().achieved_lateral_half_width, 0.2));
    CHECK(grown.value().validations == 2);
    CHECK(grown.value().growth_attempts == 1);
    CHECK(grown.value().region.contains(Configuration{-0.5, -0.5}, 1e-12));

    ObbGrowthOptions bounded_growth = growth_options;
    bounded_growth.maximum_validations = 1;
    auto bounded =
        ObbGrower{}.grow(robot, empty, Configuration{-0.5, -0.5}, Configuration{0.5, 0.5}, bounded_growth);
    CHECK(bounded);
    CHECK(bounded.value().certified);
    CHECK(bounded.value().validations == 1);
    CHECK(close(bounded.value().achieved_lateral_half_width, 0.01));

    SceneSnapshot blocked({{"block", {{0.4, -0.2, -0.2}, {1.2, 0.2, 0.2}}}}, "obb-blocked-v1");
    auto blocked_validation = validator.validate(robot, blocked, rotated.value());
    CHECK(blocked_validation);
    CHECK(blocked_validation.value().disposition == ValidationDisposition::Undetermined);
    auto blocked_growth =
        ObbGrower{}.grow(robot, blocked, Configuration{-0.5, -0.5}, Configuration{0.5, 0.5}, growth_options);
    CHECK(blocked_growth);
    CHECK(!blocked_growth.value().certified);

    ObbGrowthOptions cancelled_growth = growth_options;
    cancelled_growth.cancellation.cancel();
    auto cancelled =
        ObbGrower{}.grow(robot, empty, Configuration{-0.5, -0.5}, Configuration{0.5, 0.5}, cancelled_growth);
    CHECK(!cancelled);
    CHECK(cancelled.error().code == StatusCode::Cancelled);

    auto outside = CspaceObb::create({1.5, 0.0}, {1.0, 0.0, 0.0, 1.0}, {0.1, 0.1});
    CHECK(outside);
    auto outside_validation = validator.validate(robot, empty, outside.value());
    CHECK(!outside_validation);
    CHECK(outside_validation.error().code == StatusCode::InvalidArgument);

    std::mt19937_64 engine(42);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    for (int sample = 0; sample < 10'000; ++sample) {
        Configuration point = rotated.value().center();
        for (std::size_t axis = 0; axis < rotated.value().dimension(); ++axis) {
            const double local = unit(engine) * rotated.value().half_widths()[axis];
            for (std::size_t coordinate = 0; coordinate < point.size(); ++coordinate)
                point[coordinate] += local * rotated.value().basis()[axis * point.size() + coordinate];
        }
        CHECK(rotated.value().contains(point, 1e-12));
        CHECK(enclosure.contains(point, 1e-12));
    }
    return EXIT_SUCCESS;
}
