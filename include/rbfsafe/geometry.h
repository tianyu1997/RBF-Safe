#pragma once

#include <rbfsafe/model.h>
#include <rbfsafe/result.h>
#include <rbfsafe/types.h>

#include <memory>
#include <string>
#include <vector>

namespace rbfsafe {

struct EnvelopeOptions {
    double obstacle_padding = 0.0;
};

struct LinkEnvelope {
    std::vector<WorkspaceAabb> links;
};

Result<LinkEnvelope> compute_ifk_aa_link_envelope(const SerialRobotModel& robot, const CspaceAabb& domain,
                                                  const EnvelopeOptions& options = {});

Result<bool> configuration_is_collision_free(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                             std::span<const double> configuration,
                                             double obstacle_padding = 0.0);

enum class ValidationDisposition : std::uint8_t {
    CertifiedFree = 0,
    Undetermined = 1,
};

struct RegionValidation {
    ValidationDisposition disposition = ValidationDisposition::Undetermined;
    double clearance_lower_bound = 0.0;
    // CertifiedFree results must provide one valid conservative workspace
    // AABB per robot link. Schema-2 Atlases persist this dependency for safe
    // scene-delta invalidation.
    LinkEnvelope envelope;
};

class RegionValidator {
  public:
    virtual ~RegionValidator() = default;
    virtual Result<RegionValidation> validate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                              const CspaceAabb& domain) const = 0;
    virtual std::string algorithm_name() const = 0;
    virtual std::string algorithm_version() const = 0;
};

class IfkAaLinkAabbValidator final : public RegionValidator {
  public:
    explicit IfkAaLinkAabbValidator(EnvelopeOptions options = {}) : options_(options) {}

    Result<RegionValidation> validate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                      const CspaceAabb& domain) const override;
    std::string algorithm_name() const override { return "ifk-aa-link-iaabb"; }
    std::string algorithm_version() const override { return "1"; }
    const EnvelopeOptions& options() const noexcept { return options_; }

  private:
    EnvelopeOptions options_;
};

} // namespace rbfsafe
