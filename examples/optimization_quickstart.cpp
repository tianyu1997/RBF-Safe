#include <rbfsafe/optimization.h>

#include <iostream>
#include <vector>

int main() {
    using namespace rbfsafe;
    auto robot = SerialRobotModel::create(
        "planar-2r", {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}, {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
        {{-1.5, 1.5}, {-1.5, 1.5}}, {0.05, 0.05});
    if (!robot)
        return 1;
    const SceneSnapshot scene({}, "optimization-quickstart-v1");
    auto built = AtlasBuilder{}.build(robot.value(), scene, {{0.0, 0.0}});
    if (!built)
        return 1;
    auto database = RegionDatabase::from_atlas(built.value().atlas, scene.version());
    if (!database)
        return 1;
    const std::vector<Configuration> trajectory{{-1.0, 1.0}, {0.0, 0.0}, {1.0, -1.0}};
    auto assignment = assign_trajectory_regions(database.value(), trajectory);
    if (!assignment || assignment.value().status != TrajectoryAssignmentStatus::Complete)
        return 1;
    auto program = TrajOptRegionAdapter{}.compile(database.value(), assignment.value().region_ids);
    if (!program)
        return 1;
    auto evaluation = evaluate_trajectory_constraints(program.value(), trajectory);
    if (!evaluation || !evaluation.value().satisfied)
        return 1;
    std::cout << "compiled " << program.value().stages.size() << " certified TrajOpt stages\n";
    return 0;
}
