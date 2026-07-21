#include "internal/json.h"
#include "internal/sha256.h"

#include <cstdlib>
#include <iostream>

#define CHECK(condition)                                                                                     \
    do {                                                                                                     \
        if (!(condition)) {                                                                                  \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition "\n";                  \
            return EXIT_FAILURE;                                                                             \
        }                                                                                                    \
    } while (false)

int main() {
    using namespace rbfsafe::internal;
    CHECK(sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    auto parsed = Json::parse(R"({"b":[true,null,"x"],"a":1.25})");
    CHECK(parsed);
    CHECK(parsed.value().is_object());
    CHECK(parsed.value().find("a") != nullptr);
    CHECK(parsed.value().dump(false) == R"({"a":1.25,"b":[true,null,"x"]})");
    CHECK(!Json::parse(R"({"a":1,"a":2})"));
    CHECK(!Json::parse("[1,]"));
    return EXIT_SUCCESS;
}
