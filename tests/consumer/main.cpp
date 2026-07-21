#include <rbfsafe/rbfsafe.h>

int main() {
    const rbfsafe::Interval interval{-1.0, 1.0};
    return interval.contains(0.0) ? 0 : 1;
}
