#include "test_support.h"

#include <array>
#include <filesystem>
#include <random>

int main() {
    using namespace rbfsafe;

    Interval interval{-2.0, 3.0};
    CHECK(interval.valid());
    CHECK(close(interval.width(), 5.0));
    CHECK(interval.contains(-2.0));
    CHECK(interval.overlaps({3.0, 4.0}));

    const WorkspaceEnvelope unit_box(WorkspaceAabb{{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}});
    CHECK(unit_box.valid());
    CHECK(unit_box.type() == WorkspaceEnvelopeType::Aabb);
    CHECK(unit_box.aabb() != nullptr);

    constexpr double inverse_sqrt_two = 0.7071067811865475244;
    auto workspace_obb = WorkspaceObb::create(
        {2.0, 0.5, 0.5},
        {inverse_sqrt_two, inverse_sqrt_two, 0.0, -inverse_sqrt_two, inverse_sqrt_two, 0.0, 0.0, 0.0, 1.0},
        {0.25, 0.1, 0.1});
    CHECK(workspace_obb);
    CHECK(workspace_obb.value().valid());
    const WorkspaceEnvelope obb_envelope(workspace_obb.value());
    CHECK(obb_envelope.type() == WorkspaceEnvelopeType::Obb);
    CHECK(!unit_box.overlaps(obb_envelope));
    CHECK(unit_box.distance_lower_bound(obb_envelope) > 0.6);
    CHECK(!WorkspaceObb::create({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0, 0.5, 1.0, 0.0, 0.0, 0.0, 1.0},
                                {1.0, 1.0, 1.0}));

    const std::vector<WorkspacePoint> kdop_points{{2.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, {2.0, 1.0, 0.0},
                                                  {3.0, 1.0, 0.0}, {2.0, 0.0, 1.0}, {3.0, 0.0, 1.0},
                                                  {2.0, 1.0, 1.0}, {3.0, 1.0, 1.0}};
    auto workspace_kdop = WorkspaceKdop::from_points(
        kdop_points, {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 1.0, 0.0}});
    CHECK(workspace_kdop);
    CHECK(workspace_kdop.value().valid());
    CHECK(workspace_kdop.value().k() == 8);
    CHECK(WorkspaceKdop::standard_directions(6).value().size() == 3);
    CHECK(WorkspaceKdop::standard_directions(14).value().size() == 7);
    CHECK(WorkspaceKdop::standard_directions(18).value().size() == 9);
    CHECK(WorkspaceKdop::standard_directions(26).value().size() == 13);
    CHECK(!WorkspaceKdop::standard_directions(10));
    const WorkspaceEnvelope kdop_envelope(workspace_kdop.value());
    CHECK(!unit_box.overlaps(kdop_envelope));
    CHECK(close(unit_box.distance_lower_bound(kdop_envelope), 1.0));
    CHECK(!WorkspaceKdop::create({{1.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 1.0, 0.0}},
                                 {{0.0, 1.0}, {0.0, 2.0}, {0.0, 1.0}}));

    auto workspace_hull = WorkspaceSupportHull::create({{2.0, 0.5, 0.5}, {3.0, 0.5, 0.5}}, 0.25);
    CHECK(workspace_hull);
    const WorkspaceEnvelope hull_envelope(workspace_hull.value());
    CHECK(hull_envelope.type() == WorkspaceEnvelopeType::SupportHull);
    CHECK(!unit_box.overlaps(hull_envelope));
    CHECK(close(unit_box.distance_lower_bound(hull_envelope), 0.75));
    CHECK(unit_box.overlaps(WorkspaceEnvelope(WorkspaceSupportHull::create({{1.1, 0.5, 0.5}}, 0.2).value())));
    CHECK(!WorkspaceSupportHull::create({}, 0.0));

    auto robot = planar_robot();
    CHECK(robot.validate());
    CHECK(robot.digest().size() == 64);
    CHECK(robot.digest() == robot.digest());
    auto loaded_robot =
        SerialRobotModel::from_json(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "planar_2r.json");
    CHECK(loaded_robot);
    CHECK(loaded_robot.value().dimension() == 2);
    auto loaded_scene =
        SceneSnapshot::from_json(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "empty_scene.json");
    CHECK(loaded_scene);
    CHECK(loaded_scene.value().obstacles().empty());
    auto mixed_scene =
        SceneSnapshot::from_json(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "mixed_workspace_scene.json");
    CHECK(mixed_scene);
    CHECK(mixed_scene.value().obstacles().size() == 4);
    CHECK(mixed_scene.value().obstacles()[0].bounds.type() == WorkspaceEnvelopeType::Aabb);
    CHECK(mixed_scene.value().obstacles()[1].bounds.type() == WorkspaceEnvelopeType::Kdop);
    CHECK(mixed_scene.value().obstacles()[2].bounds.type() == WorkspaceEnvelopeType::Obb);
    CHECK(mixed_scene.value().obstacles()[3].bounds.type() == WorkspaceEnvelopeType::SupportHull);
    CHECK(mixed_scene.value().canonical_json().find("\"schema\":2") != std::string::npos);
    CHECK(mixed_scene.value().digest().size() == 64);
    auto point_fk = robot.forward_kinematics(Configuration{0.2, -0.3});
    CHECK(point_fk);
    CHECK(point_fk.value().size() == 3);
    auto end_pose = robot.end_effector_pose(Configuration{0.2, -0.3});
    CHECK(end_pose);
    CHECK(end_pose.value().valid());
    CHECK(close(end_pose.value().position[0], point_fk.value().back()[0]));
    CHECK(close(end_pose.value().position[1], point_fk.value().back()[1]));
    CHECK(close(end_pose.value().position[2], point_fk.value().back()[2]));
    CHECK(close(end_pose.value().orientation[2], std::sin(-0.05)));
    CHECK(close(end_pose.value().orientation[3], std::cos(-0.05)));

    auto jacobian = robot.end_effector_geometric_jacobian(Configuration{0.2, -0.3});
    CHECK(jacobian);
    CHECK(jacobian.value().valid());
    CHECK(jacobian.value().columns == 2);
    CHECK(jacobian.value().values.size() == 12);
    CHECK(close(jacobian.value().at(0, 0), -std::sin(0.2)));
    CHECK(close(jacobian.value().at(1, 0), std::cos(0.2)));
    CHECK(close(jacobian.value().at(2, 0), 0.0));
    CHECK(close(jacobian.value().at(0, 1), 0.0));
    CHECK(close(jacobian.value().at(1, 1), 0.0));
    CHECK(close(jacobian.value().at(2, 1), 0.0));
    CHECK(close(jacobian.value().at(3, 0), 0.0));
    CHECK(close(jacobian.value().at(4, 0), 0.0));
    CHECK(close(jacobian.value().at(5, 0), 1.0));
    CHECK(close(jacobian.value().at(5, 1), 1.0));

    constexpr double difference_step = 1e-6;
    const Configuration jacobian_configuration{0.2, -0.3};
    for (std::size_t joint = 0; joint < robot.dimension(); ++joint) {
        auto lower = jacobian_configuration;
        auto upper = jacobian_configuration;
        lower[joint] -= difference_step;
        upper[joint] += difference_step;
        auto lower_pose = robot.end_effector_pose(lower);
        auto upper_pose = robot.end_effector_pose(upper);
        CHECK(lower_pose);
        CHECK(upper_pose);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double numerical = (upper_pose.value().position[axis] - lower_pose.value().position[axis]) /
                                     (2.0 * difference_step);
            CHECK(close(jacobian.value().at(axis, joint), numerical, 1e-9));
        }
    }

    SerialRobotModel prismatic("test-prismatic", {{0.0, 0.0, 0.0, 0.0, JointType::Prismatic}}, {{0.0, 2.0}},
                               {0.05});
    auto prismatic_jacobian = prismatic.end_effector_geometric_jacobian(Configuration{0.75});
    CHECK(prismatic_jacobian);
    CHECK(prismatic_jacobian.value().valid());
    CHECK(close(prismatic_jacobian.value().at(0, 0), 0.0));
    CHECK(close(prismatic_jacobian.value().at(1, 0), 0.0));
    CHECK(close(prismatic_jacobian.value().at(2, 0), 1.0));
    CHECK(close(prismatic_jacobian.value().at(3, 0), 0.0));
    CHECK(close(prismatic_jacobian.value().at(4, 0), 0.0));
    CHECK(close(prismatic_jacobian.value().at(5, 0), 0.0));
    CHECK(!robot.end_effector_geometric_jacobian(Configuration{0.0}));
    CHECK(!robot.end_effector_geometric_jacobian(Configuration{2.0, 0.0}));

    CspaceAabb domain({{-0.7, 0.9}, {-0.5, 0.8}});
    auto ifk_endpoints = compute_endpoint_aabbs(robot, domain);
    CHECK(ifk_endpoints);
    CHECK(ifk_endpoints.value().source == EndpointAabbSource::IfkAa);
    CHECK(ifk_endpoints.value().certified);
    CHECK(ifk_endpoints.value().evaluated_configurations == 0);
    CHECK(ifk_endpoints.value().endpoints.size() == robot.link_count() * 2);

    EnvelopeOptions crit_options;
    crit_options.endpoint_aabb_source = EndpointAabbSource::CritSample;
    auto critical_endpoints = compute_endpoint_aabbs(robot, domain, crit_options);
    CHECK(critical_endpoints);
    CHECK(critical_endpoints.value().source == EndpointAabbSource::CritSample);
    CHECK(!critical_endpoints.value().certified);
    // Each interval contributes {lower, upper, 0}; CritSample enumerates their
    // Cartesian product exactly as the RapidBoxForest reference does.
    CHECK(critical_endpoints.value().evaluated_configurations == 9);
    CHECK(critical_endpoints.value().endpoints.size() == ifk_endpoints.value().endpoints.size());
    for (std::size_t endpoint = 0; endpoint < critical_endpoints.value().endpoints.size(); ++endpoint) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK(critical_endpoints.value().endpoints[endpoint].lower[axis] >=
                  ifk_endpoints.value().endpoints[endpoint].lower[axis]);
            CHECK(critical_endpoints.value().endpoints[endpoint].upper[axis] <=
                  ifk_endpoints.value().endpoints[endpoint].upper[axis]);
        }
    }

    SerialRobotModel crit_reference_robot("crit-reference", {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
                                          {{-2.0, 2.0}}, {0.0});
    auto kpi2_endpoints =
        compute_endpoint_aabbs(crit_reference_robot, CspaceAabb({{-2.0, 2.0}}), crit_options);
    CHECK(kpi2_endpoints);
    CHECK(kpi2_endpoints.value().evaluated_configurations == 5);
    auto narrow_endpoints =
        compute_endpoint_aabbs(crit_reference_robot, CspaceAabb({{0.0, 0.005}}), crit_options);
    CHECK(narrow_endpoints);
    CHECK(narrow_endpoints.value().evaluated_configurations == 1);

    const std::vector<DhJoint> cap_joints(4, DhJoint{0.0, 1.0, 0.0, 0.0, JointType::Revolute});
    const std::vector<Interval> cap_limits(4, Interval{-7.0, 7.0});
    SerialRobotModel cap_robot("crit-cap", cap_joints, cap_limits, std::vector<double>(4, 0.0));
    auto capped_endpoints = compute_endpoint_aabbs(cap_robot, CspaceAabb(cap_limits), crit_options);
    CHECK(capped_endpoints);
    // Eleven candidates per dimension exceed 8192 combinations; the largest
    // first candidate set is reduced to {lo, midpoint, hi}.
    CHECK(capped_endpoints.value().evaluated_configurations == 3 * 11 * 11 * 11);

    auto envelope = compute_ifk_aa_link_envelope(robot, domain);
    CHECK(envelope);
    CHECK(envelope.value().links.size() == robot.dimension());

    // The explicitly named IFK-AA compatibility API cannot be switched to an
    // unsafe source through EnvelopeOptions.
    auto forced_ifk_envelope = compute_ifk_aa_link_envelope(robot, domain, crit_options);
    CHECK(forced_ifk_envelope);
    CHECK(forced_ifk_envelope.value().links == envelope.value().links);

    for (const auto type : {WorkspaceEnvelopeType::Aabb, WorkspaceEnvelopeType::Obb,
                            WorkspaceEnvelopeType::Kdop, WorkspaceEnvelopeType::SupportHull}) {
        EnvelopeOptions typed_options;
        typed_options.workspace_envelope_type = type;
        typed_options.kdop_k = 26;
        auto typed_envelope = compute_ifk_aa_workspace_link_envelope(robot, domain, typed_options);
        CHECK(typed_envelope);
        CHECK(typed_envelope.value().links.size() == robot.link_count());
        CHECK(std::all_of(typed_envelope.value().links.begin(), typed_envelope.value().links.end(),
                          [type](const auto& link) { return link.valid() && link.type() == type; }));
    }
    crit_options.workspace_envelope_type = WorkspaceEnvelopeType::SupportHull;
    auto critical_workspace = compute_workspace_link_envelope(robot, domain, crit_options);
    CHECK(critical_workspace);
    CHECK(critical_workspace.value().endpoint_aabb_source == EndpointAabbSource::CritSample);
    CHECK(!critical_workspace.value().endpoint_bounds_certified);
    CHECK(critical_workspace.value().evaluated_configurations == 9);
    CHECK(std::all_of(critical_workspace.value().links.begin(), critical_workspace.value().links.end(),
                      [](const auto& link) { return link.type() == WorkspaceEnvelopeType::SupportHull; }));
    auto forced_ifk_workspace = compute_ifk_aa_workspace_link_envelope(robot, domain, crit_options);
    CHECK(forced_ifk_workspace);
    CHECK(forced_ifk_workspace.value().endpoint_aabb_source == EndpointAabbSource::IfkAa);
    CHECK(forced_ifk_workspace.value().endpoint_bounds_certified);
    CHECK(forced_ifk_workspace.value().evaluated_configurations == 0);
    EnvelopeOptions invalid_kdop_options;
    invalid_kdop_options.workspace_envelope_type = WorkspaceEnvelopeType::Kdop;
    invalid_kdop_options.kdop_k = 10;
    CHECK(!compute_ifk_aa_workspace_link_envelope(robot, domain, invalid_kdop_options));

    // Property regression: every sampled endpoint must lie inside its
    // conservative AA endpoint-pair link box.
    std::mt19937_64 random(42);
    std::uniform_real_distribution<double> first(domain.axes()[0].lower, domain.axes()[0].upper);
    std::uniform_real_distribution<double> second(domain.axes()[1].lower, domain.axes()[1].upper);
    for (int sample = 0; sample < 10'000; ++sample) {
        Configuration q{first(random), second(random)};
        auto fk = robot.forward_kinematics(q);
        CHECK(fk);
        for (std::size_t link = 0; link < robot.dimension(); ++link) {
            const auto& bounds = envelope.value().links[link];
            for (std::size_t endpoint = link; endpoint <= link + 1; ++endpoint) {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    CHECK(fk.value()[endpoint][axis] >= bounds.lower[axis] - 1e-12);
                    CHECK(fk.value()[endpoint][axis] <= bounds.upper[axis] + 1e-12);
                }
            }
        }
    }

    SceneSnapshot empty;
    IfkAaLinkAabbValidator validator;
    auto certified = validator.validate(robot, empty, domain);
    CHECK(certified);
    CHECK(certified.value().disposition == ValidationDisposition::CertifiedFree);
    auto certificate = make_region_certificate(robot, empty, validator, certified.value(), 0.0);
    CHECK(certificate);
    CHECK(certificate.value().level == EvidenceLevel::CertifiedRegion);
    CHECK(certificate.value().id.size() == 64);
    auto bound_certificate = make_region_certificate(robot, empty, domain, validator, certified.value(), 0.0);
    CHECK(bound_certificate);
    CHECK(bound_certificate.value().subject_digest.size() == 64);
    auto incomplete = certified.value();
    incomplete.envelope.links.clear();
    auto rejected_incomplete = make_region_certificate(robot, empty, domain, validator, incomplete, 0.0);
    CHECK(!rejected_incomplete);
    CHECK(rejected_incomplete.error().code == StatusCode::InternalError);

    SceneSnapshot blocked({{"block", {{0.4, -0.2, -0.2}, {1.2, 0.2, 0.2}}}}, "blocked-v1");
    CHECK(blocked.validate());
    auto undetermined = validator.validate(robot, blocked, domain);
    CHECK(undetermined);
    CHECK(undetermined.value().disposition == ValidationDisposition::Undetermined);
    CHECK(!make_region_certificate(robot, blocked, validator, undetermined.value(), 0.0));

    const CspaceAabb point_domain({{0.0, 0.0}, {0.0, 0.0}});
    const SceneSnapshot corner_obstacle({{"corner", {{0.49, 0.044, 0.044}, {0.51, 0.046, 0.046}}}},
                                        "corner-v1");
    auto aabb_corner = validator.validate(robot, corner_obstacle, point_domain);
    CHECK(aabb_corner);
    CHECK(aabb_corner.value().disposition == ValidationDisposition::Undetermined);
    EnvelopeOptions hull_options;
    hull_options.workspace_envelope_type = WorkspaceEnvelopeType::SupportHull;
    IfkAaWorkspaceEnvelopeValidator hull_validator(hull_options);
    auto hull_corner = hull_validator.validate(robot, corner_obstacle, point_domain);
    CHECK(hull_corner);
    CHECK(hull_corner.value().disposition == ValidationDisposition::CertifiedFree);
    CHECK(hull_corner.value().clearance_lower_bound > 0.0);
    CHECK(hull_validator.algorithm_name() == "ifk-aa-link-support-hull");

    return EXIT_SUCCESS;
}
