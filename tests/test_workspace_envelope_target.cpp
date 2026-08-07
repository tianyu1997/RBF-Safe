#include <rbfsafe/modules/envelope.h>

#include <cstdlib>
#include <iostream>
#include <utility>

#define CHECK(condition)                                                                                     \
    do {                                                                                                     \
        if (!(condition)) {                                                                                  \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition "\n";                  \
            return EXIT_FAILURE;                                                                             \
        }                                                                                                    \
    } while (false)

int main() {
    using namespace rbfsafe;

    const WorkspaceEnvelope first(WorkspaceAabb{{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}});
    const WorkspaceEnvelope second(WorkspaceAabb{{2.0, 0.0, 0.0}, {3.0, 1.0, 1.0}});
    CHECK(first.valid());
    CHECK(second.valid());
    CHECK(!first.overlaps(second));
    CHECK(first.distance_lower_bound(second) == 1.0);

    auto obb = WorkspaceObb::create({0.5, 0.5, 0.5}, {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0},
                                    {0.25, 0.25, 0.25});
    CHECK(obb);
    const WorkspaceEnvelope typed_obb(std::move(obb).value());
    CHECK(typed_obb.type() == WorkspaceEnvelopeType::Obb);
    CHECK(first.overlaps(typed_obb));
    return EXIT_SUCCESS;
}
