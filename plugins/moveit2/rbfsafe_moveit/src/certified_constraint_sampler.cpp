#include <rbfsafe_moveit/plugin_resources.hpp>

#include <moveit/constraint_samplers/constraint_sampler.hpp>
#include <moveit/constraint_samplers/constraint_sampler_allocator.hpp>
#include <moveit/planning_scene/planning_scene.hpp>
#include <moveit/robot_state/robot_state.hpp>
#include <pluginlib/class_list_macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>

namespace rbfsafe_moveit {
namespace {

std::atomic<std::uint64_t> sampler_stream{0};

std::uint64_t mix_seed(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

} // namespace

class CertifiedConstraintSampler final : public constraint_samplers::ConstraintSampler {
  public:
    CertifiedConstraintSampler(const planning_scene::PlanningSceneConstPtr& scene,
                               std::shared_ptr<const PluginResources> resources)
        : constraint_samplers::ConstraintSampler(scene, resources ? resources->group_name : std::string{}),
          resources_(std::move(resources)), stream_(sampler_stream.fetch_add(1, std::memory_order_relaxed)),
          engine_(mix_seed((resources_ ? resources_->sampler_options.seed : 0) ^ stream_)) {
        if (!resources_)
            return;
        auto sampler_options = resources_->sampler_options;
        sampler_options.seed = mix_seed(sampler_options.seed ^ stream_ ^ 0xD1B54A32D192ED03ULL);
        auto sampler = rbfsafe::CertifiedRegionSampler::create(
            std::make_shared<const rbfsafe::SafeAtlas>(resources_->atlas), sampler_options);
        if (sampler)
            sampler_ = std::move(sampler).value();
    }

    bool configure(const moveit_msgs::msg::Constraints& constraints) override {
        clear();
        constraints_ = constraints;
        is_valid_ = resources_ && sampler_.has_value() && jmg_ != nullptr && scene_ &&
                    jmg_->getVariableNames() == resources_->joint_names;
        return is_valid_;
    }

    bool sample(moveit::core::RobotState& state, const moveit::core::RobotState& reference_state,
                unsigned int maximum_attempts) override {
        (void)reference_state;
        if (!is_valid_ || maximum_attempts == 0)
            return false;
        std::uniform_real_distribution<double> probability(0.0, 1.0);
        for (unsigned int attempt = 0; attempt < maximum_attempts; ++attempt) {
            rbfsafe::Result<rbfsafe::Configuration> candidate =
                rbfsafe::Result<rbfsafe::Configuration>::failure(rbfsafe::StatusCode::InternalError,
                                                                 "certified sampler did not run");
            if (!resources_->roadmap.nodes().empty() && probability(engine_) < resources_->roadmap_bias) {
                std::uniform_int_distribution<std::size_t> node_distribution(
                    0, resources_->roadmap.nodes().size() - 1);
                const auto& node = resources_->roadmap.nodes()[node_distribution(engine_)];
                if (resources_->roadmap_jitter > 0.0)
                    candidate = sampler_->sample_near(node.configuration, resources_->roadmap_jitter);
                else
                    candidate = node.configuration;
            }
            if (!candidate)
                candidate = sampler_->sample();
            if (!candidate || !resources_->atlas.contains(candidate.value()))
                continue;
            state.setJointGroupPositions(jmg_, candidate.value());
            state.update();
            if (!scene_->isStateConstrained(state, constraints_, verbose_))
                continue;
            if (group_state_validity_callback_ &&
                !group_state_validity_callback_(&state, jmg_, candidate.value().data()))
                continue;
            return true;
        }
        return false;
    }

    const std::string& getName() const override {
        static const std::string name = "RBFSafeCertifiedConstraintSampler";
        return name;
    }

  private:
    std::shared_ptr<const PluginResources> resources_;
    std::uint64_t stream_ = 0;
    std::optional<rbfsafe::CertifiedRegionSampler> sampler_;
    moveit_msgs::msg::Constraints constraints_;
    std::mt19937_64 engine_;
};

class CertifiedConstraintSamplerAllocator final : public constraint_samplers::ConstraintSamplerAllocator {
  public:
    constraint_samplers::ConstraintSamplerPtr
    alloc(const planning_scene::PlanningSceneConstPtr& scene, const std::string& group_name,
          const moveit_msgs::msg::Constraints& constraints) override {
        if (!scene)
            return {};
        auto resources = registered_resources(group_name, *scene->getRobotModel());
        if (!resources)
            return {};
        auto sampler = std::make_shared<CertifiedConstraintSampler>(scene, std::move(resources));
        if (!sampler->configure(constraints))
            return {};
        return sampler;
    }

    bool canService(const planning_scene::PlanningSceneConstPtr& scene, const std::string& group_name,
                    const moveit_msgs::msg::Constraints&) const override {
        return scene && static_cast<bool>(registered_resources(group_name, *scene->getRobotModel()));
    }
};

} // namespace rbfsafe_moveit

PLUGINLIB_EXPORT_CLASS(rbfsafe_moveit::CertifiedConstraintSamplerAllocator,
                       constraint_samplers::ConstraintSamplerAllocator)
