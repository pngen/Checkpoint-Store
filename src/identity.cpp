#include <checkpointstore/identity/identities.hpp>

#include <cstdint>

namespace checkpointstore {

std::string to_hex_string(std::uint64_t value) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(16);
    for (int i = 0; i < 16; ++i) {
        const int shift = (15 - i) * 4;
        out[static_cast<std::size_t>(i)] =
            kHex[static_cast<int>((value >> shift) & 0xFULL)];
    }
    return out;
}

}  // namespace checkpointstore
