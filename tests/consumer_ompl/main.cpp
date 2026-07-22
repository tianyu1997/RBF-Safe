#include <rbfsafe/ompl.h>

int main() {
    rbfsafe::OmplAdapterOptions options;
    rbfsafe::OmplPlannerOptions planner_options;
    return options.maximum_sampling_attempts == 64 && options.maximum_region_tests > 0 &&
                   planner_options.maximum_planning_time > 0.0 && planner_options.seed_prm_from_atlas
               ? 0
               : 1;
}
