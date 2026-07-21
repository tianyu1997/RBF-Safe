#pragma once

#include <rbfsafe/rbfsafe.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(condition)                                                                                     \
    do {                                                                                                     \
        if (!(condition)) {                                                                                  \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition "\n";                  \
            return EXIT_FAILURE;                                                                             \
        }                                                                                                    \
    } while (false)

inline rbfsafe::SerialRobotModel planar_robot() {
    return rbfsafe::SerialRobotModel("test-planar-2r",
                                     {{0.0, 1.0, 0.0, 0.0, rbfsafe::JointType::Revolute},
                                      {0.0, 1.0, 0.0, 0.0, rbfsafe::JointType::Revolute}},
                                     {{-1.5, 1.5}, {-1.5, 1.5}}, {0.05, 0.05});
}

inline bool close(double left, double right, double tolerance = 1e-11) {
    return std::abs(left - right) <= tolerance;
}
