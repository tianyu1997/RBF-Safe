#include "internal/json.h"
#include "internal/sha256.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    const std::array<std::byte, 20> hmac_key{
        std::byte{0x0b}, std::byte{0x0b}, std::byte{0x0b}, std::byte{0x0b}, std::byte{0x0b},
        std::byte{0x0b}, std::byte{0x0b}, std::byte{0x0b}, std::byte{0x0b}, std::byte{0x0b},
        std::byte{0x0b}, std::byte{0x0b}, std::byte{0x0b}, std::byte{0x0b}, std::byte{0x0b},
        std::byte{0x0b}, std::byte{0x0b}, std::byte{0x0b}, std::byte{0x0b}, std::byte{0x0b}};
    constexpr std::string_view hmac_message = "Hi There";
    CHECK(hmac_sha256(hmac_key, std::as_bytes(std::span(hmac_message.data(), hmac_message.size()))) ==
          "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    const std::vector<std::byte> long_hmac_key(131, std::byte{0xaa});
    const std::string long_key_message = "Test Using Larger Than Block-Size Key - Hash Key First";
    CHECK(hmac_sha256(long_hmac_key,
                      std::as_bytes(std::span(long_key_message.data(), long_key_message.size()))) ==
          "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    CHECK(constant_time_equal("same", "same"));
    CHECK(!constant_time_equal("same", "different"));
    auto parsed = Json::parse(R"({"b":[true,null,"x"],"a":1.25})");
    CHECK(parsed);
    CHECK(parsed.value().is_object());
    CHECK(parsed.value().find("a") != nullptr);
    CHECK(parsed.value().dump(false) == R"({"a":1.25,"b":[true,null,"x"]})");
    CHECK(!Json::parse(R"({"a":1,"a":2})"));
    CHECK(!Json::parse("[1,]"));
    return EXIT_SUCCESS;
}
