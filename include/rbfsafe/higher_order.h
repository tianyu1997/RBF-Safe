#pragma once

#include <rbfsafe/certificate.h>
#include <rbfsafe/geometry.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

// A C-space zonotope q = center + sum(generator_k * xi_k), xi_k in [-1, 1].
// Generators use generator-major row order: generators[k * dimension + axis].
class CspaceZonotope {
  public:
    CspaceZonotope() = default;

    static Result<CspaceZonotope> create(Configuration center, std::size_t generator_count,
                                         std::vector<double> generators);

    std::size_t dimension() const noexcept { return center_.size(); }
    std::size_t generator_count() const noexcept { return generator_count_; }
    const Configuration& center() const noexcept { return center_; }
    const std::vector<double>& generators() const noexcept { return generators_; }

    bool valid() const noexcept;
    CspaceAabb enclosing_aabb() const;
    Result<bool> contains(std::span<const double> configuration, double tolerance = 1e-10,
                          std::size_t maximum_iterations = 512) const;

  private:
    Configuration center_;
    std::size_t generator_count_ = 0;
    std::vector<double> generators_;
};

// First-order Taylor region with an independent interval remainder per joint:
// q = center + sum(linear_k * xi_k) + remainder, |remainder_i| <= radius_i.
class CspaceTaylorRegion {
  public:
    CspaceTaylorRegion() = default;

    static Result<CspaceTaylorRegion> create(Configuration center, std::size_t variable_count,
                                             std::vector<double> linear, Configuration remainder_radii);
    static Result<CspaceTaylorRegion> from_zonotope(const CspaceZonotope& region);

    std::size_t dimension() const noexcept { return center_.size(); }
    std::size_t variable_count() const noexcept { return variable_count_; }
    const Configuration& center() const noexcept { return center_; }
    const std::vector<double>& linear() const noexcept { return linear_; }
    const Configuration& remainder_radii() const noexcept { return remainder_radii_; }

    bool valid() const noexcept;
    CspaceAabb enclosing_aabb() const;
    Result<bool> contains(std::span<const double> configuration, double tolerance = 1e-10,
                          std::size_t maximum_iterations = 512) const;

  private:
    Configuration center_;
    std::size_t variable_count_ = 0;
    std::vector<double> linear_;
    Configuration remainder_radii_;
};

struct HigherOrderValidation {
    ValidationDisposition disposition = ValidationDisposition::Undetermined;
    double clearance_lower_bound = 0.0;
    CspaceAabb conservative_enclosure;
    LinkEnvelope envelope;
};

// Correlation-preserving first-order Taylor FK. Shared generator variables are
// retained through trigonometric linearization and matrix products; nonlinear
// and floating-point residuals are accumulated conservatively.
Result<LinkEnvelope> compute_ifk_taylor_link_envelope(const SerialRobotModel& robot,
                                                      const CspaceTaylorRegion& region,
                                                      const EnvelopeOptions& options = {});
Result<LinkEnvelope> compute_ifk_zonotope_link_envelope(const SerialRobotModel& robot,
                                                        const CspaceZonotope& region,
                                                        const EnvelopeOptions& options = {});

class HigherOrderRegionValidator {
  public:
    explicit HigherOrderRegionValidator(EnvelopeOptions options = {}) : options_(options) {}

    Result<HigherOrderValidation> validate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                           const CspaceZonotope& region) const;
    Result<HigherOrderValidation> validate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                           const CspaceTaylorRegion& region) const;
    std::string algorithm_name() const { return "ifk-taylor1-link-iaabb"; }
    std::string algorithm_version() const { return "1"; }
    const EnvelopeOptions& options() const noexcept { return options_; }

  private:
    EnvelopeOptions options_;
};

Result<Certificate> make_higher_order_region_certificate(const SerialRobotModel& robot,
                                                         const SceneSnapshot& scene,
                                                         const CspaceZonotope& region,
                                                         const HigherOrderRegionValidator& validator,
                                                         const HigherOrderValidation& validation,
                                                         double obstacle_padding = 0.0);
Result<Certificate> make_higher_order_region_certificate(const SerialRobotModel& robot,
                                                         const SceneSnapshot& scene,
                                                         const CspaceTaylorRegion& region,
                                                         const HigherOrderRegionValidator& validator,
                                                         const HigherOrderValidation& validation,
                                                         double obstacle_padding = 0.0);

} // namespace rbfsafe
