#include "test_support.h"

#include <algorithm>
#include <array>
#include <cmath>

int main() {
    using namespace rbfsafe;
    const auto robot = planar_robot();
    const SceneSnapshot scene({}, "optimization-empty-v1");

    auto atlas = AtlasBuilder{}.build(robot, scene, {{0.0, 0.0}});
    CHECK(atlas);
    auto aabb_database = RegionDatabase::from_atlas(atlas.value().atlas, scene.version());
    CHECK(aabb_database);
    CHECK(aabb_database.value().records().size() == 1);
    const RegionId aabb_id = aabb_database.value().records().front().id;
    auto aabb_constraint = compile_region_constraint(aabb_database.value(), aabb_id);
    CHECK(aabb_constraint);
    CHECK(aabb_constraint.value().valid());
    CHECK(aabb_constraint.value().auxiliary_dimension == 0);
    CHECK(aabb_constraint.value().inequality_count() == 4);
    auto inside = aabb_constraint.value().evaluate(Configuration{0.0, 0.0});
    CHECK(inside);
    CHECK(inside.value().satisfied);
    auto outside = aabb_constraint.value().evaluate(Configuration{2.0, -2.0});
    CHECK(outside);
    CHECK(!outside.value().satisfied);
    CHECK(outside.value().squared_penalty > 0.0);
    CHECK(outside.value().configuration_gradient[0] > 0.0);
    CHECK(outside.value().configuration_gradient[1] < 0.0);
    auto aabb_projection = aabb_constraint.value().project(Configuration{2.0, -2.0});
    CHECK(aabb_projection);
    CHECK(aabb_projection.value().converged);
    CHECK(atlas.value().atlas.contains(aabb_projection.value().configuration));
    ConstraintProjectionOptions cancelled_projection;
    cancelled_projection.cancellation.cancel();
    auto stopped_projection = aabb_constraint.value().project(Configuration{2.0, -2.0}, cancelled_projection);
    CHECK(!stopped_projection);
    CHECK(stopped_projection.error().code == StatusCode::Cancelled);

    ObbAtlasBuildOptions obb_options;
    obb_options.initial_half_width = 0.02;
    obb_options.maximum_half_width = 0.08;
    auto obb_database = ObbAtlasBuilder{}.build(robot, scene, {{0.0, 0.0}}, obb_options);
    CHECK(obb_database);
    const auto obb_record = std::find_if(
        obb_database.value().database.records().begin(), obb_database.value().database.records().end(),
        [](const RegionRecord& record) { return region_type(record.geometry) == RegionType::Obb; });
    CHECK(obb_record != obb_database.value().database.records().end());
    auto obb_constraint = compile_region_constraint(obb_database.value().database, obb_record->id);
    CHECK(obb_constraint);
    const auto& obb = std::get<CspaceObb>(obb_record->geometry);
    CHECK(obb_constraint.value().evaluate(obb.center()).value().satisfied);
    auto obb_projected = obb_constraint.value().project(Configuration{0.5, 0.5});
    CHECK(obb_projected);
    CHECK(obb_projected.value().converged);
    CHECK(obb.contains(obb_projected.value().configuration, 1e-9));

    auto zonotope = CspaceZonotope::create({0.0, 0.0}, 2, {0.2, 0.0, 0.0, 0.1});
    CHECK(zonotope);
    auto taylor = CspaceTaylorRegion::create({0.4, -0.4}, 1, {0.1, -0.1}, {0.01, 0.02});
    CHECK(taylor);
    HigherOrderRegionValidator validator;
    auto zonotope_validation = validator.validate(robot, scene, zonotope.value());
    auto taylor_validation = validator.validate(robot, scene, taylor.value());
    CHECK(zonotope_validation);
    CHECK(taylor_validation);
    CHECK(zonotope_validation.value().disposition == ValidationDisposition::CertifiedFree);
    CHECK(taylor_validation.value().disposition == ValidationDisposition::CertifiedFree);
    auto zonotope_certificate = make_higher_order_region_certificate(robot, scene, zonotope.value(),
                                                                     validator, zonotope_validation.value());
    auto taylor_certificate = make_higher_order_region_certificate(robot, scene, taylor.value(), validator,
                                                                   taylor_validation.value());
    CHECK(zonotope_certificate);
    CHECK(taylor_certificate);
    std::vector<CertifiedRegionInput> higher_inputs;
    higher_inputs.push_back({zonotope.value(), zonotope_certificate.value(),
                             zonotope_validation.value().envelope, "optimization-zonotope"});
    higher_inputs.push_back({taylor.value(), taylor_certificate.value(), taylor_validation.value().envelope,
                             "optimization-taylor"});
    auto higher_database = RegionDatabase::create(robot, scene, std::move(higher_inputs));
    CHECK(higher_database);
    for (const auto& record : higher_database.value().records()) {
        auto constraint = compile_region_constraint(higher_database.value(), record.id);
        CHECK(constraint);
        CHECK(constraint.value().equality_count() == 2);
        const auto type = region_type(record.geometry);
        if (type == RegionType::Zonotope) {
            CHECK(constraint.value().auxiliary_dimension == 2);
            const std::array<double, 2> auxiliary{1.0, -1.0};
            auto residual = constraint.value().evaluate(Configuration{0.2, -0.1}, auxiliary);
            CHECK(residual);
            CHECK(residual.value().satisfied);
            auto projection = constraint.value().project(Configuration{0.5, 0.5});
            CHECK(projection);
            CHECK(projection.value().converged);
            CHECK(zonotope.value().contains(projection.value().configuration, 1e-8).value());
        } else {
            CHECK(type == RegionType::Taylor);
            CHECK(constraint.value().auxiliary_dimension == 3);
            const std::array<double, 3> auxiliary{0.0, 0.0, 0.0};
            auto residual = constraint.value().evaluate(taylor.value().center(), auxiliary);
            CHECK(residual);
            CHECK(residual.value().satisfied);
        }
    }

    const std::vector<Configuration> corridor_path{{-0.4, -0.2}, {0.0, 0.0}, {0.4, 0.2}};
    HipacOptions corridor_options;
    corridor_options.minimum_lateral_half_width = 0.01;
    corridor_options.maximum_lateral_half_width = 0.04;
    auto corridor = HipacCorridorBuilder{}.build(robot, scene, corridor_path, corridor_options);
    CHECK(corridor);
    CHECK(corridor.value().status == HipacBuildStatus::Certified);
    auto corridor_database = RegionDatabase::from_corridor(corridor.value().corridor, scene.version());
    CHECK(corridor_database);
    const auto portal_record = std::find_if(
        corridor_database.value().records().begin(), corridor_database.value().records().end(),
        [](const RegionRecord& record) { return region_type(record.geometry) == RegionType::Portal; });
    CHECK(portal_record != corridor_database.value().records().end());
    auto portal_constraint = compile_region_constraint(corridor_database.value(), portal_record->id);
    CHECK(portal_constraint);
    const auto& portal = std::get<PortalGeometry>(portal_record->geometry).intersection;
    CHECK(portal_constraint.value().evaluate(portal.witness()).value().satisfied);
    const auto tube_record =
        std::find_if(corridor_database.value().records().begin(), corridor_database.value().records().end(),
                     [](const RegionRecord& record) {
                         return region_type(record.geometry) == RegionType::TrajectoryTube;
                     });
    CHECK(tube_record != corridor_database.value().records().end());
    auto tube_constraint = compile_region_constraint(corridor_database.value(), tube_record->id);
    CHECK(!tube_constraint);
    CHECK(tube_constraint.error().code == StatusCode::InvalidArgument);

    const std::vector<Configuration> trajectory{{-1.0, 1.0}, {0.0, 0.0}, {1.0, -1.0}};
    auto assignment = assign_trajectory_regions(aabb_database.value(), trajectory);
    CHECK(assignment);
    CHECK(assignment.value().status == TrajectoryAssignmentStatus::Complete);
    CHECK(assignment.value().region_ids.size() == trajectory.size());
    auto partial =
        assign_trajectory_regions(aabb_database.value(), std::vector<Configuration>{{0.0, 0.0}, {2.0, 2.0}});
    CHECK(partial);
    CHECK(partial.value().status == TrajectoryAssignmentStatus::Partial);
    auto invalid = assign_trajectory_regions(aabb_database.value(), std::vector<Configuration>{{2.0, 2.0}});
    CHECK(invalid);
    CHECK(invalid.value().status == TrajectoryAssignmentStatus::Invalid);
    TrajectoryAssignmentOptions assignment_limit;
    assignment_limit.maximum_region_tests = 2;
    auto assignment_limited = assign_trajectory_regions(aabb_database.value(), trajectory, assignment_limit);
    CHECK(!assignment_limited);
    CHECK(assignment_limited.error().code == StatusCode::ResourceLimit);
    TrajectoryAssignmentOptions cancelled_assignment;
    cancelled_assignment.cancellation.cancel();
    auto stopped_assignment =
        assign_trajectory_regions(aabb_database.value(), trajectory, cancelled_assignment);
    CHECK(!stopped_assignment);
    CHECK(stopped_assignment.error().code == StatusCode::Cancelled);

    auto trajopt = TrajOptRegionAdapter{}.compile(aabb_database.value(), assignment.value().region_ids);
    auto chomp = ChompRegionAdapter{}.compile(aabb_database.value(), assignment.value().region_ids);
    auto stomp = StompRegionAdapter{}.compile(aabb_database.value(), assignment.value().region_ids);
    auto mpc = MpcRegionAdapter{}.compile(aabb_database.value(), assignment.value().region_ids);
    CHECK(trajopt);
    CHECK(chomp);
    CHECK(stomp);
    CHECK(mpc);
    CHECK(trajopt.value().backend == OptimizationBackend::TrajOpt);
    CHECK(chomp.value().backend == OptimizationBackend::Chomp);
    CHECK(stomp.value().backend == OptimizationBackend::Stomp);
    CHECK(mpc.value().backend == OptimizationBackend::Mpc);
    auto evaluated = evaluate_trajectory_constraints(trajopt.value(), trajectory);
    CHECK(evaluated);
    CHECK(evaluated.value().satisfied);
    auto projected = project_trajectory_constraints(
        trajopt.value(), std::vector<Configuration>{{-2.0, 2.0}, {0.0, 0.0}, {2.0, -2.0}});
    CHECK(projected);
    CHECK(projected.value().size() == trajectory.size());
    CHECK(std::all_of(projected.value().begin(), projected.value().end(),
                      [](const ConstraintProjection& value) { return value.converged; }));
    return EXIT_SUCCESS;
}
