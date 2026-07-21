#include <rbfsafe/rbfsafe.h>

int main() {
    const rbfsafe::Interval interval{-1.0, 1.0};
    const rbfsafe::TrajectoryAuditOptions options;
    const rbfsafe::TrajectoryAuditReport report;
    return interval.contains(0.0) && options.maximum_region_tests > 0 &&
                   report.status == rbfsafe::TrajectoryAuditStatus::Invalid
               ? 0
               : 1;
}
