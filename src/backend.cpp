#include <checkpointstore/storage/backend.hpp>

#include <algorithm>
#include <cctype>

namespace checkpointstore {

namespace {

bool is_reserved_windows_name(std::string_view comp) {
    // Reserved device names under Windows; reject them as path components to
    // prevent device/alternate-data-stream confusion.
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    std::string c;
    c.reserve(comp.size());
    for (char ch : comp) {
        c.push_back(lower(ch));
    }
    static const char* const kReserved[] = {
        "con", "prn", "aux", "nul", "clock$",
        "conin$", "conout$",
    };
    for (const char* r : kReserved) {
        if (c == r) {
            return true;
        }
    }
    // COM1..COM9, LPT1..LPT9
    auto digits_ok = [](const std::string& s, const char* prefix) {
        if (s.size() != 4) {
            return false;
        }
        if (s[0] != prefix[0] || s[1] != prefix[1] || s[2] != prefix[2]) {
            return false;
        }
        return s[3] >= '1' && s[3] <= '9';
    };
    if (digits_ok(c, "com") || digits_ok(c, "lpt")) {
        return true;
    }
    return false;
}

}  // namespace

bool validate_backend_key(std::string_view key) {
    if (key.empty()) {
        return false;
    }
    if (key.find('\0') != std::string_view::npos) {
        return false;
    }
    if (key.size() > 1024) {
        return false;
    }
    // Absolute path detection: leading separator, drive letter, or UNC.
    if (key.front() == '/' || key.front() == '\\') {
        return false;
    }
    if (key.size() >= 2 && std::isalpha(static_cast<unsigned char>(key[0])) && key[1] == ':') {
        return false;
    }
    // Split on '/' and '\\', reject empty/dot/dotdot/reserved components.
    std::size_t start = 0;
    while (start <= key.size()) {
        std::size_t end = key.find('/', start);
        if (end == std::string_view::npos) {
            end = key.size();
        }
        std::string_view comp = key.substr(start, end - start);
        if (comp.empty()) {
            return false;  // empty component (e.g. foo//bar, trailing slash)
        }
        if (comp == "." || comp == "..") {
            return false;
        }
        if (comp.find("://") != std::string_view::npos) {
            return false;  // reject scheme-like components
        }
        if (comp.find(':') != std::string_view::npos) {
            return false;  // reject ADS / colon
        }
        if (comp.size() > 255) {
            return false;
        }
        if (comp.front() == '.' || comp.back() == '.') {
            return false;  // hidden/trailing-dot components are risky on Windows
        }
        if (comp.back() == ' ') {
            return false;
        }
        if (is_reserved_windows_name(comp)) {
            return false;
        }
        if (end == key.size()) {
            break;
        }
        start = end + 1;
    }
    return true;
}

}  // namespace checkpointstore
