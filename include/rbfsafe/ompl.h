#pragma once

#include <rbfsafe/atlas.h>
#include <rbfsafe/result.h>

#include <ompl/base/SpaceInformation.h>
#include <ompl/base/spaces/RealVectorStateSpace.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace rbfsafe {

enum class RegionSamplingPolicy : std::uint8_t {
    UniformRegions = 0,
    VolumeWeighted = 1,
};

struct OmplAdapterOptions {
    RegionSamplingPolicy sampling_policy = RegionSamplingPolicy::VolumeWeighted;
    std::uint64_t seed = 42;
    std::size_t maximum_sampling_attempts = 64;
    std::size_t maximum_region_tests = 10'000'000;
};

struct OmplAdapterStats {
    std::uint64_t state_queries = 0;
    std::uint64_t certified_states = 0;
    std::uint64_t motion_queries = 0;
    std::uint64_t certified_motions = 0;
    std::uint64_t samples_requested = 0;
    std::uint64_t certified_samples = 0;
    std::uint64_t sampling_fallbacks = 0;
    std::uint64_t audit_failures = 0;
};

namespace detail {
struct OmplAdapterState;
}

class OmplAdapter {
  public:
    OmplAdapter() = default;

    static Result<OmplAdapter> install(const ompl::base::SpaceInformationPtr& space_information,
                                       std::shared_ptr<const SafeAtlas> atlas,
                                       const OmplAdapterOptions& options = {});

    bool valid() const noexcept { return static_cast<bool>(state_); }
    OmplAdapterStats stats() const noexcept;
    void reset_stats() const noexcept;

  private:
    explicit OmplAdapter(std::shared_ptr<detail::OmplAdapterState> state) : state_(std::move(state)) {}

    std::shared_ptr<detail::OmplAdapterState> state_;
};

Result<std::shared_ptr<ompl::base::RealVectorStateSpace>> make_ompl_state_space(const SafeAtlas& atlas);

} // namespace rbfsafe
