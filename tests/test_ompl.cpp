#include "test_support.h"

#include <rbfsafe/ompl.h>

#include <ompl/base/ScopedState.h>
#include <ompl/geometric/SimpleSetup.h>
#include <ompl/geometric/planners/rrt/RRTConnect.h>

#include <array>
#include <cmath>
#include <memory>
#include <utility>

namespace {

namespace ob = ompl::base;
namespace og = ompl::geometric;

void set_state(ob::State* state, std::initializer_list<double> values) {
    auto* vector_state = state->as<ob::RealVectorStateSpace::StateType>();
    std::size_t index = 0;
    for (const double value : values)
        vector_state->values[index++] = value;
}

double state_value(const ob::State* state, std::size_t index) {
    return state->as<ob::RealVectorStateSpace::StateType>()->values[index];
}

rbfsafe::Configuration configuration_from(const ob::State* state, std::size_t dimension) {
    const auto* vector_state = state->as<ob::RealVectorStateSpace::StateType>();
    return rbfsafe::Configuration(vector_state->values, vector_state->values + dimension);
}

} // namespace

int main() {
    using namespace rbfsafe;

    const auto robot = planar_robot();
    const SceneSnapshot empty_scene({}, "ompl-empty-v1");
    auto complete_build = AtlasBuilder{}.build(robot, empty_scene, {{0.0, 0.0}});
    CHECK(complete_build);
    auto complete_atlas = std::make_shared<SafeAtlas>(std::move(complete_build.value().atlas));

    auto made_space = make_ompl_state_space(*complete_atlas);
    CHECK(made_space);
    auto space = made_space.value();
    auto space_information = std::make_shared<ob::SpaceInformation>(space);
    OmplAdapterOptions options;
    options.seed = 7;
    auto installed = OmplAdapter::install(space_information, complete_atlas, options);
    CHECK(installed);
    CHECK(installed.value().valid());
    space_information->setup();

    ob::ScopedState<ob::RealVectorStateSpace> inside(space);
    inside[0] = 0.0;
    inside[1] = 0.0;
    ob::ScopedState<ob::RealVectorStateSpace> other(space);
    other[0] = 1.0;
    other[1] = -1.0;
    CHECK(space_information->isValid(inside.get()));
    CHECK(space_information->checkMotion(inside.get(), other.get()));
    ob::State* untouched_state = space_information->allocState();
    set_state(untouched_state, {-0.75, 0.5});
    std::pair<ob::State*, double> untouched_last_valid(untouched_state, 0.25);
    CHECK(space_information->checkMotion(inside.get(), other.get(), untouched_last_valid));
    CHECK(close(untouched_last_valid.second, 0.25));
    CHECK(close(state_value(untouched_state, 0), -0.75));
    CHECK(close(state_value(untouched_state, 1), 0.5));
    space_information->freeState(untouched_state);
    CHECK(space_information->getMotionValidator()->getValidMotionCount() == 2);

    ob::State* outside = space_information->allocState();
    set_state(outside, {2.0, 0.0});
    CHECK(!space_information->isValid(outside));
    space_information->freeState(outside);

    auto sampler = space_information->allocStateSampler();
    ob::State* sampled = space_information->allocState();
    for (int iteration = 0; iteration < 64; ++iteration) {
        sampler->sampleUniform(sampled);
        CHECK(space_information->isValid(sampled));
    }
    sampler->sampleUniformNear(sampled, inside.get(), 0.1);
    CHECK(space_information->isValid(sampled));
    CHECK(space->distance(sampled, inside.get()) <= 0.1 + 1e-12);
    space_information->freeState(sampled);

    const auto stats = installed.value().stats();
    CHECK(stats.state_queries >= 66);
    CHECK(stats.certified_states >= 65);
    CHECK(stats.motion_queries == 2);
    CHECK(stats.certified_motions == 2);
    CHECK(stats.samples_requested == 65);
    CHECK(stats.certified_samples == 65);
    CHECK(stats.audit_failures == 0);
    installed.value().reset_stats();
    CHECK(installed.value().stats().state_queries == 0);

    OmplAdapterOptions deterministic_options;
    deterministic_options.seed = 1234;
    deterministic_options.sampling_policy = RegionSamplingPolicy::UniformRegions;
    auto deterministic_space_a = make_ompl_state_space(*complete_atlas);
    auto deterministic_space_b = make_ompl_state_space(*complete_atlas);
    CHECK(deterministic_space_a);
    CHECK(deterministic_space_b);
    auto deterministic_information_a = std::make_shared<ob::SpaceInformation>(deterministic_space_a.value());
    auto deterministic_information_b = std::make_shared<ob::SpaceInformation>(deterministic_space_b.value());
    CHECK(OmplAdapter::install(deterministic_information_a, complete_atlas, deterministic_options));
    CHECK(OmplAdapter::install(deterministic_information_b, complete_atlas, deterministic_options));
    deterministic_information_a->setup();
    deterministic_information_b->setup();
    auto deterministic_sampler_a = deterministic_information_a->allocStateSampler();
    auto deterministic_sampler_b = deterministic_information_b->allocStateSampler();
    ob::State* deterministic_state_a = deterministic_information_a->allocState();
    ob::State* deterministic_state_b = deterministic_information_b->allocState();
    deterministic_sampler_a->sampleUniform(deterministic_state_a);
    deterministic_sampler_b->sampleUniform(deterministic_state_b);
    CHECK(close(state_value(deterministic_state_a, 0), state_value(deterministic_state_b, 0)));
    CHECK(close(state_value(deterministic_state_a, 1), state_value(deterministic_state_b, 1)));
    deterministic_information_a->freeState(deterministic_state_a);
    deterministic_information_b->freeState(deterministic_state_b);

    OmplAdapterOptions default_sampling_options;
    default_sampling_options.sampling_mode = OmplSamplingMode::OmplDefault;
    auto default_sampling_space = make_ompl_state_space(*complete_atlas);
    CHECK(default_sampling_space);
    auto default_sampling_information =
        std::make_shared<ob::SpaceInformation>(default_sampling_space.value());
    auto default_sampling_adapter =
        OmplAdapter::install(default_sampling_information, complete_atlas, default_sampling_options);
    CHECK(default_sampling_adapter);
    default_sampling_information->setup();
    auto default_sampler = default_sampling_information->allocStateSampler();
    auto* default_state = default_sampling_information->allocState();
    default_sampler->sampleUniform(default_state);
    CHECK(default_sampling_information->isValid(default_state));
    CHECK(default_sampling_adapter.value().stats().samples_requested == 0);
    default_sampling_information->freeState(default_state);

    auto late_space = make_ompl_state_space(*complete_atlas);
    CHECK(late_space);
    auto late_information = std::make_shared<ob::SpaceInformation>(late_space.value());
    late_information->setStateValidityChecker([](const ob::State*) { return false; });
    late_information->setup();
    auto late_install = OmplAdapter::install(late_information, complete_atlas);
    CHECK(!late_install);
    CHECK(late_install.error().code == StatusCode::InvalidArgument);

    auto wrong_space = std::make_shared<ob::RealVectorStateSpace>(1);
    ob::RealVectorBounds wrong_bounds(1);
    wrong_bounds.setLow(0, -1.5);
    wrong_bounds.setHigh(0, 1.5);
    wrong_space->setBounds(wrong_bounds);
    auto wrong_information = std::make_shared<ob::SpaceInformation>(wrong_space);
    auto wrong_install = OmplAdapter::install(wrong_information, complete_atlas);
    CHECK(!wrong_install);
    CHECK(wrong_install.error().code == StatusCode::DimensionMismatch);

    auto mismatched_space = std::make_shared<ob::RealVectorStateSpace>(2);
    ob::RealVectorBounds mismatched_bounds(2);
    mismatched_bounds.setLow(0, -1.5);
    mismatched_bounds.setHigh(0, 1.5);
    mismatched_bounds.setLow(1, -1.5);
    mismatched_bounds.setHigh(1, 1.4);
    mismatched_space->setBounds(mismatched_bounds);
    auto mismatched_information = std::make_shared<ob::SpaceInformation>(mismatched_space);
    auto mismatched_install = OmplAdapter::install(mismatched_information, complete_atlas);
    CHECK(!mismatched_install);
    CHECK(mismatched_install.error().code == StatusCode::InvalidArgument);

    OmplAdapterOptions invalid_options;
    invalid_options.maximum_sampling_attempts = 0;
    auto invalid_space = make_ompl_state_space(*complete_atlas);
    CHECK(invalid_space);
    auto invalid_information = std::make_shared<ob::SpaceInformation>(invalid_space.value());
    auto invalid_install = OmplAdapter::install(invalid_information, complete_atlas, invalid_options);
    CHECK(!invalid_install);
    CHECK(invalid_install.error().code == StatusCode::InvalidArgument);
    OmplAdapterOptions invalid_policy_options;
    invalid_policy_options.sampling_policy = static_cast<RegionSamplingPolicy>(99);
    auto invalid_policy_space = make_ompl_state_space(*complete_atlas);
    CHECK(invalid_policy_space);
    auto invalid_policy_information = std::make_shared<ob::SpaceInformation>(invalid_policy_space.value());
    auto invalid_policy_install =
        OmplAdapter::install(invalid_policy_information, complete_atlas, invalid_policy_options);
    CHECK(!invalid_policy_install);
    CHECK(invalid_policy_install.error().code == StatusCode::InvalidArgument);
    OmplAdapterOptions invalid_sampling_mode;
    invalid_sampling_mode.sampling_mode = static_cast<OmplSamplingMode>(99);
    auto invalid_mode_space = make_ompl_state_space(*complete_atlas);
    CHECK(invalid_mode_space);
    auto invalid_mode_information = std::make_shared<ob::SpaceInformation>(invalid_mode_space.value());
    auto invalid_mode_install =
        OmplAdapter::install(invalid_mode_information, complete_atlas, invalid_sampling_mode);
    CHECK(!invalid_mode_install);
    CHECK(invalid_mode_install.error().code == StatusCode::InvalidArgument);

    SerialRobotModel prismatic("ompl-prismatic", {{0.0, 0.0, 0.0, 0.0, JointType::Prismatic}}, {{0.0, 2.0}},
                               {0.05});
    SceneSnapshot split_scene({{"high-block", {{-0.1, -0.1, 1.1}, {0.1, 0.1, 1.2}}}}, "ompl-split-v1");
    auto split_build = AtlasBuilder{}.build(prismatic, split_scene, {{0.25}});
    CHECK(split_build);
    auto split_atlas = std::make_shared<SafeAtlas>(std::move(split_build.value().atlas));
    auto split_space_result = make_ompl_state_space(*split_atlas);
    CHECK(split_space_result);
    auto split_space = split_space_result.value();
    auto split_information = std::make_shared<ob::SpaceInformation>(split_space);
    auto split_adapter = OmplAdapter::install(split_information, split_atlas);
    CHECK(split_adapter);
    split_information->setup();

    ob::ScopedState<ob::RealVectorStateSpace> low(split_space);
    low[0] = 0.25;
    ob::ScopedState<ob::RealVectorStateSpace> high(split_space);
    high[0] = 1.5;
    CHECK(split_information->isValid(low.get()));
    CHECK(!split_information->isValid(high.get()));
    CHECK(!split_information->checkMotion(low.get(), high.get()));
    ob::State* last_state = split_information->allocState();
    std::pair<ob::State*, double> last_valid(last_state, 0.0);
    CHECK(!split_information->checkMotion(low.get(), high.get(), last_valid));
    CHECK(close(last_valid.second, 0.6));
    CHECK(close(state_value(last_state, 0), 1.0));
    CHECK(split_information->isValid(last_state));
    split_information->freeState(last_state);

    auto planning_space_result = make_ompl_state_space(*complete_atlas);
    CHECK(planning_space_result);
    auto planning_space = planning_space_result.value();
    og::SimpleSetup setup(planning_space);
    auto planning_adapter = OmplAdapter::install(setup.getSpaceInformation(), complete_atlas);
    CHECK(planning_adapter);
    ob::ScopedState<ob::RealVectorStateSpace> start(planning_space);
    start[0] = -1.0;
    start[1] = -1.0;
    ob::ScopedState<ob::RealVectorStateSpace> goal(planning_space);
    goal[0] = 1.0;
    goal[1] = 1.0;
    setup.setStartAndGoalStates(start, goal);
    setup.setPlanner(std::make_shared<og::RRTConnect>(setup.getSpaceInformation()));
    CHECK(setup.solve(0.25));
    CHECK(TrajectoryAuditor{}
              .audit(*complete_atlas,
                     [&setup]() {
                         std::vector<Configuration> path;
                         for (const auto* state : setup.getSolutionPath().getStates())
                             path.push_back(configuration_from(state, 2));
                         return path;
                     }())
              .value()
              .status == TrajectoryAuditStatus::Certified);

    const Configuration helper_start{-1.0, -1.0};
    const Configuration helper_goal{1.0, 1.0};
    const std::array<OmplPlannerKind, 4> planner_kinds{OmplPlannerKind::Rrt, OmplPlannerKind::RrtStar,
                                                       OmplPlannerKind::Prm, OmplPlannerKind::BitStar};
    for (const auto kind : planner_kinds) {
        OmplPlannerOptions planner_options;
        planner_options.planner = kind;
        planner_options.maximum_planning_time = 0.5;
        planner_options.adapter.seed = 77;
        auto planned = OmplPlanner{}.solve(complete_atlas, helper_start, helper_goal, planner_options);
        CHECK(planned);
        CHECK(planned.value().status == OmplPlanStatus::CertifiedExactSolution);
        CHECK(planned.value().audit.has_value());
        CHECK(planned.value().audit->status == TrajectoryAuditStatus::Certified);
        CHECK(planned.value().stats.exact_solution);
        CHECK(planned.value().stats.solution_states >= 2);
        CHECK(planned.value().stats.planner_vertices > 0);
        CHECK(planned.value().stats.adapter.motion_queries > 0);
        if (kind == OmplPlannerKind::Prm) {
            CHECK(planned.value().stats.seeded_roadmap_nodes == 1);
            CHECK(planned.value().stats.seeded_roadmap_edges == 0);
        }
    }

    OmplPlannerOptions default_helper_options;
    default_helper_options.maximum_planning_time = 0.5;
    default_helper_options.adapter.sampling_mode = OmplSamplingMode::OmplDefault;
    auto default_helper =
        OmplPlanner{}.solve(complete_atlas, helper_start, helper_goal, default_helper_options);
    CHECK(default_helper);
    CHECK(default_helper.value().status == OmplPlanStatus::CertifiedExactSolution);
    CHECK(default_helper.value().stats.adapter.samples_requested == 0);

    OmplPlannerOptions invalid_prm_range;
    invalid_prm_range.planner = OmplPlannerKind::Prm;
    invalid_prm_range.range = 0.1;
    auto bad_range = OmplPlanner{}.solve(complete_atlas, helper_start, helper_goal, invalid_prm_range);
    CHECK(!bad_range);
    CHECK(bad_range.error().code == StatusCode::InvalidArgument);
    OmplPlannerOptions cancelled_planning;
    cancelled_planning.cancellation.cancel();
    auto cancelled_plan = OmplPlanner{}.solve(complete_atlas, helper_start, helper_goal, cancelled_planning);
    CHECK(!cancelled_plan);
    CHECK(cancelled_plan.error().code == StatusCode::Cancelled);

    auto invalid_start = OmplPlanner{}.solve(split_atlas, Configuration{1.5}, Configuration{0.25});
    CHECK(invalid_start);
    CHECK(invalid_start.value().status == OmplPlanStatus::InvalidStart);
    auto invalid_goal = OmplPlanner{}.solve(split_atlas, Configuration{0.25}, Configuration{1.5});
    CHECK(invalid_goal);
    CHECK(invalid_goal.value().status == OmplPlanStatus::InvalidGoal);

    auto roadmap = CertifiedRoadmapBuilder{}.build(*complete_atlas);
    CHECK(roadmap);
    auto seeded_space = make_ompl_state_space(*complete_atlas);
    CHECK(seeded_space);
    auto seeded_information = std::make_shared<ob::SpaceInformation>(seeded_space.value());
    CHECK(OmplAdapter::install(seeded_information, complete_atlas));
    seeded_information->setup();
    CHECK(make_ompl_planner(seeded_information, OmplPlannerKind::Prm, 0.0, &roadmap.value().roadmap));
    auto invalid_seed_target =
        make_ompl_planner(seeded_information, OmplPlannerKind::Rrt, 0.0, &roadmap.value().roadmap);
    CHECK(!invalid_seed_target);
    CHECK(invalid_seed_target.error().code == StatusCode::InvalidArgument);
    return EXIT_SUCCESS;
}
