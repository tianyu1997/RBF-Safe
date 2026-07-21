#include <rbfsafe/ompl.h>

#include <ompl/base/ScopedState.h>
#include <ompl/geometric/SimpleSetup.h>
#include <ompl/geometric/planners/rrt/RRTConnect.h>

#include <iostream>
#include <memory>

int main() {
    namespace ob = ompl::base;
    namespace og = ompl::geometric;
    using namespace rbfsafe;

    auto robot = SerialRobotModel::create(
        "planar-2r", {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}, {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
        {{-1.5, 1.5}, {-1.5, 1.5}}, {0.05, 0.05});
    if (!robot)
        return 1;
    SceneSnapshot scene({}, "ompl-quickstart-v1");
    auto built = AtlasBuilder{}.build(robot.value(), scene, {{0.0, 0.0}});
    if (!built)
        return 1;

    auto atlas = std::make_shared<SafeAtlas>(std::move(built.value().atlas));
    auto space_result = make_ompl_state_space(*atlas);
    if (!space_result)
        return 1;
    auto space = space_result.value();
    og::SimpleSetup setup(space);
    auto adapter = OmplAdapter::install(setup.getSpaceInformation(), atlas);
    if (!adapter)
        return 1;

    ob::ScopedState<ob::RealVectorStateSpace> start(space), goal(space);
    start[0] = -1.0;
    start[1] = -1.0;
    goal[0] = 1.0;
    goal[1] = 1.0;
    setup.setStartAndGoalStates(start, goal);
    setup.setPlanner(std::make_shared<og::RRTConnect>(setup.getSpaceInformation()));
    if (!setup.solve(1.0))
        return 1;

    const auto stats = adapter.value().stats();
    std::cout << "certified OMPL path with " << setup.getSolutionPath().getStateCount()
              << " states; certified motions=" << stats.certified_motions << '\n';
    return 0;
}
