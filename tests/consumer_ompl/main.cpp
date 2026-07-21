#include <rbfsafe/ompl.h>

int main() {
    rbfsafe::OmplAdapterOptions options;
    return options.maximum_sampling_attempts == 64 && options.maximum_region_tests > 0 ? 0 : 1;
}
