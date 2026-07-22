#include <rbfsafe/rbfsafe.h>

int main() {
    const rbfsafe::Interval interval{-1.0, 1.0};
    const rbfsafe::TrajectoryAuditOptions options;
    const rbfsafe::TrajectoryAuditReport report;
    const rbfsafe::HipacOptions hipac_options;
    const rbfsafe::SafeIkOptions safe_ik_options;
    const rbfsafe::AtlasUpdateOptions update_options;
    const rbfsafe::AtlasUpdater updater;
    (void)updater;
    return interval.contains(0.0) && options.maximum_region_tests > 0 &&
                   hipac_options.maximum_validations > 0 && safe_ik_options.maximum_iterations > 0 &&
                   update_options.maximum_validations > 0 &&
                   report.status == rbfsafe::TrajectoryAuditStatus::Invalid
               ? 0
               : 1;
}
