#include <rbfsafe/ompl.h>

#include <rbfsafe/trajectory.h>

#include <ompl/base/MotionValidator.h>
#include <ompl/base/StateSampler.h>
#include <ompl/base/StateValidityChecker.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <span>
#include <utility>
#include <vector>

namespace rbfsafe {
namespace {

namespace ob = ompl::base;

constexpr double bounds_tolerance = 1e-12;

std::uint64_t mix_seed(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

Configuration configuration_from_state(const ob::State* state, std::size_t dimension) {
    const auto* vector_state = state->as<ob::RealVectorStateSpace::StateType>();
    return Configuration(vector_state->values, vector_state->values + dimension);
}

void configuration_to_state(std::span<const double> configuration, ob::State* state) {
    auto* vector_state = state->as<ob::RealVectorStateSpace::StateType>();
    std::copy(configuration.begin(), configuration.end(), vector_state->values);
}

double point_box_distance_squared(std::span<const double> point, const CspaceAabb& box) {
    double squared = 0.0;
    for (std::size_t axis = 0; axis < box.dimension(); ++axis) {
        double difference = 0.0;
        if (point[axis] < box.axes()[axis].lower)
            difference = box.axes()[axis].lower - point[axis];
        else if (point[axis] > box.axes()[axis].upper)
            difference = point[axis] - box.axes()[axis].upper;
        squared += difference * difference;
    }
    return squared;
}

bool close_bound(double left, double right) {
    const double scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= bounds_tolerance * scale;
}

} // namespace

namespace detail {

struct OmplAdapterState {
    std::shared_ptr<const SafeAtlas> atlas;
    OmplAdapterOptions options;
    std::vector<double> region_weights;
    std::atomic<std::uint64_t> sampler_stream{0};
    std::atomic<std::uint64_t> state_queries{0};
    std::atomic<std::uint64_t> certified_states{0};
    std::atomic<std::uint64_t> motion_queries{0};
    std::atomic<std::uint64_t> certified_motions{0};
    std::atomic<std::uint64_t> samples_requested{0};
    std::atomic<std::uint64_t> certified_samples{0};
    std::atomic<std::uint64_t> sampling_fallbacks{0};
    std::atomic<std::uint64_t> audit_failures{0};
};

} // namespace detail

namespace {

std::vector<double> make_region_weights(const SafeAtlas& atlas, RegionSamplingPolicy policy) {
    std::vector<double> weights(atlas.regions().size(), 1.0);
    if (policy == RegionSamplingPolicy::UniformRegions)
        return weights;

    const auto& root = atlas.lect().root_domain();
    double total = 0.0;
    for (std::size_t region_index = 0; region_index < atlas.regions().size(); ++region_index) {
        double weight = 1.0;
        for (std::size_t axis = 0; axis < atlas.dimension(); ++axis) {
            const double root_width = root.axes()[axis].width();
            if (!(root_width > 0.0)) {
                weight = 0.0;
                break;
            }
            weight *=
                std::clamp(atlas.regions()[region_index].bounds.axes()[axis].width() / root_width, 0.0, 1.0);
        }
        weights[region_index] = std::isfinite(weight) ? weight : 0.0;
        total += weights[region_index];
    }
    if (!(total > 0.0))
        std::fill(weights.begin(), weights.end(), 1.0);
    return weights;
}

class AtlasStateValidityChecker final : public ob::StateValidityChecker {
  public:
    AtlasStateValidityChecker(const ob::SpaceInformationPtr& space_information,
                              std::shared_ptr<detail::OmplAdapterState> state)
        : ob::StateValidityChecker(space_information), space_information_(space_information.get()),
          state_(std::move(state)) {}

    bool isValid(const ob::State* state) const override {
        state_->state_queries.fetch_add(1, std::memory_order_relaxed);
        if (state == nullptr || !space_information_->satisfiesBounds(state))
            return false;
        const auto configuration = configuration_from_state(state, state_->atlas->dimension());
        const bool certified = state_->atlas->contains(configuration);
        if (certified)
            state_->certified_states.fetch_add(1, std::memory_order_relaxed);
        return certified;
    }

  private:
    ob::SpaceInformation* space_information_;
    std::shared_ptr<detail::OmplAdapterState> state_;
};

class AtlasMotionValidator final : public ob::MotionValidator {
  public:
    AtlasMotionValidator(const ob::SpaceInformationPtr& space_information,
                         std::shared_ptr<detail::OmplAdapterState> state)
        : ob::MotionValidator(space_information), space_information_(space_information.get()),
          state_(std::move(state)) {}

    bool checkMotion(const ob::State* first, const ob::State* second) const override {
        auto report = audit(first, second);
        const bool certified = report && report.value().status == TrajectoryAuditStatus::Certified;
        if (certified) {
            std::atomic_ref<unsigned int>(valid_).fetch_add(1, std::memory_order_relaxed);
            state_->certified_motions.fetch_add(1, std::memory_order_relaxed);
        } else {
            std::atomic_ref<unsigned int>(invalid_).fetch_add(1, std::memory_order_relaxed);
        }
        return certified;
    }

    bool checkMotion(const ob::State* first, const ob::State* second,
                     std::pair<ob::State*, double>& last_valid) const override {
        auto report = audit(first, second);
        if (report && report.value().status == TrajectoryAuditStatus::Certified) {
            std::atomic_ref<unsigned int>(valid_).fetch_add(1, std::memory_order_relaxed);
            state_->certified_motions.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        std::atomic_ref<unsigned int>(invalid_).fetch_add(1, std::memory_order_relaxed);

        double fraction = 0.0;
        if (report && !report.value().uncovered_intervals.empty()) {
            const auto& first_gap = report.value().uncovered_intervals.front();
            fraction = first_gap.start_fraction;
            if (first_gap.start_included && fraction > 0.0)
                fraction = std::nextafter(fraction, 0.0);
        }
        fraction = std::clamp(fraction, 0.0, 1.0);
        last_valid.second = fraction;
        if (last_valid.first != nullptr)
            space_information_->getStateSpace()->interpolate(first, second, fraction, last_valid.first);
        return false;
    }

  private:
    Result<TrajectoryAuditReport> audit(const ob::State* first, const ob::State* second) const {
        state_->motion_queries.fetch_add(1, std::memory_order_relaxed);
        if (first == nullptr || second == nullptr) {
            state_->audit_failures.fetch_add(1, std::memory_order_relaxed);
            return Result<TrajectoryAuditReport>::failure(StatusCode::InvalidArgument,
                                                          "OMPL motion contains a null state", "ompl");
        }
        std::vector<Configuration> segment;
        segment.reserve(2);
        segment.push_back(configuration_from_state(first, state_->atlas->dimension()));
        segment.push_back(configuration_from_state(second, state_->atlas->dimension()));
        TrajectoryAuditOptions options;
        options.maximum_region_tests = state_->options.maximum_region_tests;
        auto report = TrajectoryAuditor{}.audit(*state_->atlas, segment, options);
        if (!report)
            state_->audit_failures.fetch_add(1, std::memory_order_relaxed);
        return report;
    }

    ob::SpaceInformation* space_information_;
    std::shared_ptr<detail::OmplAdapterState> state_;
};

class AtlasRegionStateSampler final : public ob::StateSampler {
  public:
    AtlasRegionStateSampler(const ob::StateSpace* space, std::shared_ptr<detail::OmplAdapterState> state,
                            std::uint64_t seed)
        : ob::StateSampler(space), state_(std::move(state)), engine_(seed) {}

    void sampleUniform(ob::State* state) override {
        state_->samples_requested.fetch_add(1, std::memory_order_relaxed);
        const auto region_index = choose_region(all_region_indices());
        sample_box(state_->atlas->regions()[region_index].bounds, state);
        state_->certified_samples.fetch_add(1, std::memory_order_relaxed);
    }

    void sampleUniformNear(ob::State* state, const ob::State* near, double distance) override {
        state_->samples_requested.fetch_add(1, std::memory_order_relaxed);
        if (sample_near(state, near, std::max(0.0, distance))) {
            state_->certified_samples.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        fallback_to_near(state, near);
    }

    void sampleGaussian(ob::State* state, const ob::State* mean, double standard_deviation) override {
        state_->samples_requested.fetch_add(1, std::memory_order_relaxed);
        if (mean != nullptr && standard_deviation > 0.0 && std::isfinite(standard_deviation)) {
            const auto center = configuration_from_state(mean, state_->atlas->dimension());
            Configuration candidate(center.size());
            for (std::size_t attempt = 0; attempt < state_->options.maximum_sampling_attempts; ++attempt) {
                for (std::size_t axis = 0; axis < center.size(); ++axis) {
                    std::normal_distribution<double> distribution(center[axis], standard_deviation);
                    candidate[axis] = distribution(engine_);
                }
                if (state_->atlas->lect().root_domain().contains(candidate) &&
                    state_->atlas->contains(candidate)) {
                    configuration_to_state(candidate, state);
                    state_->certified_samples.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }
            if (sample_near(state, mean, standard_deviation)) {
                state_->certified_samples.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        fallback_to_near(state, mean);
    }

  private:
    std::vector<std::size_t> all_region_indices() const {
        std::vector<std::size_t> indices(state_->atlas->regions().size());
        std::iota(indices.begin(), indices.end(), 0);
        return indices;
    }

    std::size_t choose_region(const std::vector<std::size_t>& candidates) {
        double total = 0.0;
        for (const auto index : candidates)
            total += state_->region_weights[index];
        if (!(total > 0.0)) {
            std::uniform_int_distribution<std::size_t> distribution(0, candidates.size() - 1);
            return candidates[distribution(engine_)];
        }
        std::uniform_real_distribution<double> distribution(0.0, total);
        double draw = distribution(engine_);
        for (const auto index : candidates) {
            draw -= state_->region_weights[index];
            if (draw <= 0.0)
                return index;
        }
        return candidates.back();
    }

    void sample_box(const CspaceAabb& box, ob::State* output) {
        Configuration candidate(box.dimension());
        for (std::size_t axis = 0; axis < box.dimension(); ++axis) {
            std::uniform_real_distribution<double> distribution(box.axes()[axis].lower,
                                                                box.axes()[axis].upper);
            candidate[axis] = distribution(engine_);
        }
        configuration_to_state(candidate, output);
    }

    bool sample_near(ob::State* output, const ob::State* near, double distance) {
        if (near == nullptr || !std::isfinite(distance))
            return false;
        const auto center = configuration_from_state(near, state_->atlas->dimension());
        const double squared_distance = distance * distance;
        std::vector<std::size_t> candidates;
        for (std::size_t index = 0; index < state_->atlas->regions().size(); ++index) {
            if (point_box_distance_squared(center, state_->atlas->regions()[index].bounds) <=
                squared_distance)
                candidates.push_back(index);
        }
        if (candidates.empty())
            return false;

        const auto region_index = choose_region(candidates);
        const auto& box = state_->atlas->regions()[region_index].bounds;
        Configuration candidate(box.dimension());
        for (std::size_t attempt = 0; attempt < state_->options.maximum_sampling_attempts; ++attempt) {
            for (std::size_t axis = 0; axis < box.dimension(); ++axis) {
                const double lower = std::max(box.axes()[axis].lower, center[axis] - distance);
                const double upper = std::min(box.axes()[axis].upper, center[axis] + distance);
                std::uniform_real_distribution<double> distribution(lower, upper);
                candidate[axis] = distribution(engine_);
            }
            double candidate_distance = 0.0;
            for (std::size_t axis = 0; axis < candidate.size(); ++axis) {
                const double difference = candidate[axis] - center[axis];
                candidate_distance += difference * difference;
            }
            if (candidate_distance <= squared_distance) {
                configuration_to_state(candidate, output);
                return true;
            }
        }

        for (std::size_t axis = 0; axis < box.dimension(); ++axis)
            candidate[axis] = std::clamp(center[axis], box.axes()[axis].lower, box.axes()[axis].upper);
        if (point_box_distance_squared(center, box) <= squared_distance) {
            configuration_to_state(candidate, output);
            return true;
        }
        return false;
    }

    void fallback_to_near(ob::State* output, const ob::State* near) {
        state_->sampling_fallbacks.fetch_add(1, std::memory_order_relaxed);
        if (near != nullptr) {
            space_->copyState(output, near);
            const auto configuration = configuration_from_state(near, state_->atlas->dimension());
            if (state_->atlas->contains(configuration))
                state_->certified_samples.fetch_add(1, std::memory_order_relaxed);
        } else {
            sample_box(state_->atlas->regions().front().bounds, output);
            state_->certified_samples.fetch_add(1, std::memory_order_relaxed);
        }
    }

    std::shared_ptr<detail::OmplAdapterState> state_;
    std::mt19937_64 engine_;
};

Result<void> validate_installation(const ob::SpaceInformationPtr& space_information,
                                   const std::shared_ptr<const SafeAtlas>& atlas,
                                   const OmplAdapterOptions& options) {
    if (!space_information)
        return Result<void>::failure(StatusCode::InvalidArgument, "OMPL SpaceInformation is null", "ompl");
    if (!atlas)
        return Result<void>::failure(StatusCode::InvalidArgument, "SafeAtlas is null", "ompl");
    if (options.maximum_sampling_attempts == 0 || options.maximum_region_tests == 0) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "OMPL adapter resource limits must be positive", "ompl");
    }
    if (options.sampling_policy != RegionSamplingPolicy::UniformRegions &&
        options.sampling_policy != RegionSamplingPolicy::VolumeWeighted) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "OMPL region sampling policy is unsupported", "ompl");
    }
    if (space_information->isSetup()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "install the OMPL adapter before SpaceInformation::setup", "ompl");
    }
    if (!atlas->lect().valid() || atlas->dimension() == 0 || atlas->regions().empty()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "SafeAtlas must contain a valid partition and certified regions",
                                     "ompl");
    }
    auto vector_space =
        std::dynamic_pointer_cast<ob::RealVectorStateSpace>(space_information->getStateSpace());
    if (!vector_space) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "v0.3 supports only OMPL RealVectorStateSpace", "ompl");
    }
    if (vector_space->getDimension() != atlas->dimension()) {
        return Result<void>::failure(StatusCode::DimensionMismatch,
                                     "OMPL state dimension does not match SafeAtlas", "ompl");
    }
    const auto& bounds = vector_space->getBounds();
    const auto& root = atlas->lect().root_domain();
    for (std::size_t axis = 0; axis < atlas->dimension(); ++axis) {
        if (!close_bound(bounds.low[axis], root.axes()[axis].lower) ||
            !close_bound(bounds.high[axis], root.axes()[axis].upper)) {
            return Result<void>::failure(StatusCode::InvalidArgument,
                                         "OMPL bounds must match the Atlas root domain", "ompl");
        }
    }
    return Result<void>::success();
}

} // namespace

Result<std::shared_ptr<ob::RealVectorStateSpace>> make_ompl_state_space(const SafeAtlas& atlas) {
    if (!atlas.lect().valid() || atlas.dimension() == 0) {
        return Result<std::shared_ptr<ob::RealVectorStateSpace>>::failure(
            StatusCode::InvalidArgument, "SafeAtlas has no valid root domain", "ompl");
    }
    auto space = std::make_shared<ob::RealVectorStateSpace>(static_cast<unsigned int>(atlas.dimension()));
    ob::RealVectorBounds bounds(static_cast<unsigned int>(atlas.dimension()));
    const auto& root = atlas.lect().root_domain();
    for (std::size_t axis = 0; axis < atlas.dimension(); ++axis) {
        bounds.setLow(static_cast<unsigned int>(axis), root.axes()[axis].lower);
        bounds.setHigh(static_cast<unsigned int>(axis), root.axes()[axis].upper);
    }
    space->setBounds(bounds);
    return Result<std::shared_ptr<ob::RealVectorStateSpace>>::success(std::move(space));
}

Result<OmplAdapter> OmplAdapter::install(const ob::SpaceInformationPtr& space_information,
                                         std::shared_ptr<const SafeAtlas> atlas,
                                         const OmplAdapterOptions& options) {
    auto validation = validate_installation(space_information, atlas, options);
    if (!validation)
        return Result<OmplAdapter>(validation.error());

    auto state = std::make_shared<detail::OmplAdapterState>();
    state->atlas = std::move(atlas);
    state->options = options;
    state->region_weights = make_region_weights(*state->atlas, options.sampling_policy);

    space_information->setStateValidityChecker(
        std::make_shared<AtlasStateValidityChecker>(space_information, state));
    space_information->setMotionValidator(std::make_shared<AtlasMotionValidator>(space_information, state));
    space_information->getStateSpace()->setStateSamplerAllocator(
        [state](const ob::StateSpace* space) -> ob::StateSamplerPtr {
            const auto stream = state->sampler_stream.fetch_add(1, std::memory_order_relaxed);
            return std::make_shared<AtlasRegionStateSampler>(space, state,
                                                             mix_seed(state->options.seed ^ stream));
        });
    return Result<OmplAdapter>::success(OmplAdapter(std::move(state)));
}

OmplAdapterStats OmplAdapter::stats() const noexcept {
    if (!state_)
        return {};
    return {state_->state_queries.load(std::memory_order_relaxed),
            state_->certified_states.load(std::memory_order_relaxed),
            state_->motion_queries.load(std::memory_order_relaxed),
            state_->certified_motions.load(std::memory_order_relaxed),
            state_->samples_requested.load(std::memory_order_relaxed),
            state_->certified_samples.load(std::memory_order_relaxed),
            state_->sampling_fallbacks.load(std::memory_order_relaxed),
            state_->audit_failures.load(std::memory_order_relaxed)};
}

void OmplAdapter::reset_stats() const noexcept {
    if (!state_)
        return;
    state_->state_queries.store(0, std::memory_order_relaxed);
    state_->certified_states.store(0, std::memory_order_relaxed);
    state_->motion_queries.store(0, std::memory_order_relaxed);
    state_->certified_motions.store(0, std::memory_order_relaxed);
    state_->samples_requested.store(0, std::memory_order_relaxed);
    state_->certified_samples.store(0, std::memory_order_relaxed);
    state_->sampling_fallbacks.store(0, std::memory_order_relaxed);
    state_->audit_failures.store(0, std::memory_order_relaxed);
}

} // namespace rbfsafe
