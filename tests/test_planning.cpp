#include "test_support.h"

#include <cmath>
#include <memory>
#include <string>

namespace {

class SplitValidator final : public rbfsafe::RegionValidator {
  public:
    rbfsafe::Result<rbfsafe::RegionValidation> validate(const rbfsafe::SerialRobotModel& robot,
                                                        const rbfsafe::SceneSnapshot&,
                                                        const rbfsafe::CspaceAabb& domain) const override {
        if (domain.axes().front().width() > 1.0)
            return rbfsafe::RegionValidation{};
        auto envelope = rbfsafe::compute_ifk_aa_link_envelope(robot, domain);
        if (!envelope)
            return envelope.error();
        return rbfsafe::RegionValidation{rbfsafe::ValidationDisposition::CertifiedFree, 0.5,
                                         std::move(envelope).value()};
    }

    std::string algorithm_name() const override { return "test-planning-split"; }
    std::string algorithm_version() const override { return "1"; }
};

rbfsafe::SerialRobotModel one_revolute_robot() {
    return rbfsafe::SerialRobotModel("planning-1r", {{0.0, 1.0, 0.0, 0.0, rbfsafe::JointType::Revolute}},
                                     {{-1.0, 1.0}}, {0.02});
}

} // namespace

int main() {
    using namespace rbfsafe;
    const auto robot = one_revolute_robot();
    const SceneSnapshot scene({}, "planning-empty-v1");
    AtlasBuilder builder(std::make_shared<SplitValidator>());
    auto built = builder.build(robot, scene, {{-0.5}, {0.5}});
    CHECK(built);
    CHECK(built.value().atlas.regions().size() == 2);
    auto atlas = std::make_shared<SafeAtlas>(std::move(built.value().atlas));

    auto first_sampler = CertifiedRegionSampler::create(atlas, {.seed = 17});
    auto second_sampler = CertifiedRegionSampler::create(atlas, {.seed = 17});
    CHECK(first_sampler);
    CHECK(second_sampler);
    for (std::size_t index = 0; index < 32; ++index) {
        auto first = first_sampler.value().sample();
        auto second = second_sampler.value().sample();
        CHECK(first);
        CHECK(second);
        CHECK(first.value() == second.value());
        CHECK(atlas->contains(first.value()));
    }
    auto near = first_sampler.value().sample_near(Configuration{-0.9}, 0.1);
    CHECK(near);
    CHECK(atlas->contains(near.value()));
    CHECK(std::abs(near.value().front() + 0.9) <= 0.1 + 1e-12);
    auto impossible_near = first_sampler.value().sample_near(Configuration{3.0}, 0.1);
    CHECK(!impossible_near);
    CHECK(impossible_near.error().code == StatusCode::InvalidArgument);
    CHECK(first_sampler.value().stats().samples_requested == 34);
    CHECK(first_sampler.value().stats().samples_returned == 33);
    CertifiedSamplerOptions uniform_options;
    uniform_options.policy = CertifiedSamplingPolicy::UniformRegions;
    auto uniform_sampler = CertifiedRegionSampler::create(atlas, uniform_options);
    CHECK(uniform_sampler);
    CHECK(atlas->contains(uniform_sampler.value().sample().value()));
    CertifiedSamplerOptions invalid_sampler_options;
    invalid_sampler_options.maximum_attempts = 0;
    auto invalid_sampler = CertifiedRegionSampler::create(atlas, invalid_sampler_options);
    CHECK(!invalid_sampler);
    CHECK(invalid_sampler.error().code == StatusCode::InvalidArgument);

    auto roadmap = CertifiedRoadmapBuilder{}.build(*atlas);
    CHECK(roadmap);
    CHECK(roadmap.value().roadmap.valid());
    CHECK(roadmap.value().stats.region_nodes == 2);
    CHECK(roadmap.value().stats.portal_nodes == 1);
    CHECK(roadmap.value().stats.edges == 2);
    CHECK(roadmap.value().roadmap.nodes().size() == 3);
    CHECK(roadmap.value().roadmap.edges().size() == 2);
    CHECK(roadmap.value().roadmap.verify_compatible(robot, scene));
    auto nearest = roadmap.value().roadmap.nearest_node(Configuration{-0.9});
    CHECK(nearest);
    CHECK(nearest.value().has_value());

    for (const auto& edge : roadmap.value().roadmap.edges()) {
        const auto& first = roadmap.value().roadmap.nodes()[edge.first - 1].configuration;
        const auto& second = roadmap.value().roadmap.nodes()[edge.second - 1].configuration;
        auto audit = TrajectoryAuditor{}.audit(*atlas, std::vector<Configuration>{first, second});
        CHECK(audit);
        CHECK(audit.value().status == TrajectoryAuditStatus::Certified);
    }
    auto repeated = CertifiedRoadmapBuilder{}.build(*atlas);
    CHECK(repeated);
    CHECK(repeated.value().roadmap.nodes().size() == roadmap.value().roadmap.nodes().size());
    for (std::size_t index = 0; index < roadmap.value().roadmap.nodes().size(); ++index) {
        CHECK(repeated.value().roadmap.nodes()[index].id == roadmap.value().roadmap.nodes()[index].id);
        CHECK(repeated.value().roadmap.nodes()[index].configuration ==
              roadmap.value().roadmap.nodes()[index].configuration);
    }

    CertifiedRoadmapOptions insufficient;
    insufficient.maximum_nodes = 2;
    auto limited = CertifiedRoadmapBuilder{}.build(*atlas, insufficient);
    CHECK(!limited);
    CHECK(limited.error().code == StatusCode::ResourceLimit);
    CertifiedRoadmapOptions insufficient_edges;
    insufficient_edges.maximum_edges = 1;
    auto edge_limited = CertifiedRoadmapBuilder{}.build(*atlas, insufficient_edges);
    CHECK(!edge_limited);
    CHECK(edge_limited.error().code == StatusCode::ResourceLimit);
    CertifiedRoadmapOptions cancelled;
    cancelled.cancellation.cancel();
    auto stopped = CertifiedRoadmapBuilder{}.build(*atlas, cancelled);
    CHECK(!stopped);
    CHECK(stopped.error().code == StatusCode::Cancelled);
    SceneSnapshot changed({}, "planning-empty-v2");
    auto incompatible = roadmap.value().roadmap.verify_compatible(robot, changed);
    CHECK(!incompatible);
    CHECK(incompatible.error().code == StatusCode::IdentityMismatch);
    return EXIT_SUCCESS;
}
