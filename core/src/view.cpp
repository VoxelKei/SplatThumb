#include "splatthumb/view.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <numbers>

#include "splatthumb/limits.h"

namespace splatthumb {

namespace {

constexpr std::string_view kMagic = "splatthumb-view 1";
constexpr std::size_t kMaxLines = 32;

bool in_range(float v) noexcept {
    return std::isfinite(v) && std::fabs(v) <= limits::kMaxAbsCoordinate;
}

// Splits off the next space-separated token of rest.
std::string_view next_token(std::string_view& rest) noexcept {
    const std::size_t start = rest.find_first_not_of(' ');
    if (start == std::string_view::npos) {
        rest = {};
        return {};
    }
    rest.remove_prefix(start);
    const std::size_t end = std::min(rest.find(' '), rest.size());
    const std::string_view token = rest.substr(0, end);
    rest.remove_prefix(end);
    return token;
}

bool parse_float(std::string_view token, float& out) noexcept {
    if (token.empty()) {
        return false;
    }
    const char* first = token.data();
    const char* last = first + token.size();
    const auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc() && ptr == last && in_range(out);
}

// Parses exactly n numbers from rest.
template <std::size_t N>
bool parse_floats(std::string_view rest, std::array<float, N>& out) noexcept {
    for (float& v : out) {
        if (!parse_float(next_token(rest), v)) {
            return false;
        }
    }
    return next_token(rest).empty();
}

void append_number(std::string& s, float v) {
    // Shortest text that reads back as the same float, independent of the C locale.
    std::array<char, 32> buf{};
    const auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v);
    s += ' ';
    if (ec == std::errc()) {
        s.append(buf.data(), ptr);
    }
}

// Reads "<n>" for the quality key. A number this version does not know leaves quality
// unset (the global setting applies) rather than rejecting the whole view.
bool parse_quality(std::string_view rest, std::optional<Quality>& out) noexcept {
    const std::string_view token = next_token(rest);
    if (token.empty() || !next_token(rest).empty() || token.size() > 9) {
        return false;
    }
    std::uint32_t n = 0;
    const char* last = token.data() + token.size();
    const auto [ptr, ec] = std::from_chars(token.data(), last, n);
    if (ec != std::errc() || ptr != last) {
        return false;
    }
    switch (n) {
    case 0: out = Quality::Fast; break;
    case 1: out = Quality::Standard; break;
    case 2: out = Quality::High; break;
    default: break;
    }
    return true;
}

} // namespace

View view_from_camera(const Camera& camera) noexcept {
    View v;
    v.eye = camera.eye;
    v.target = camera.target;
    v.up = camera.up;
    v.fov_y_deg = camera.fov_y * 180.0f / std::numbers::pi_v<float>;
    v.radius = camera.radius;
    return v;
}

CameraResult camera_from_view(const View& view) noexcept {
    return look_at(view.eye, view.target, view.up, view.fov_y_deg, view.radius);
}

std::string format_view(const View& view) {
    std::string s(kMagic);
    s += "\neye";
    for (const float v : view.eye) {
        append_number(s, v);
    }
    s += "\ntarget";
    for (const float v : view.target) {
        append_number(s, v);
    }
    s += "\nup";
    for (const float v : view.up) {
        append_number(s, v);
    }
    s += "\nfov";
    append_number(s, view.fov_y_deg);
    s += "\nradius";
    append_number(s, view.radius);
    s += '\n';
    if (view.quality) {
        s += "quality ";
        switch (*view.quality) {
        case Quality::Fast: s += '0'; break;
        case Quality::Standard: s += '1'; break;
        case Quality::High: s += '2'; break;
        }
        s += '\n';
    }
    return s;
}

std::optional<View> parse_view(std::string_view text) noexcept {
    if (text.size() > limits::kMaxViewFileBytes) {
        return std::nullopt;
    }
    View view;
    bool seen_magic = false, seen_eye = false, seen_target = false, seen_up = false,
         seen_fov = false, seen_radius = false, seen_quality = false;
    std::size_t lines = 0;
    while (!text.empty()) {
        if (++lines > kMaxLines) {
            return std::nullopt;
        }
        const std::size_t end = std::min(text.find('\n'), text.size());
        std::string_view line = text.substr(0, end);
        text.remove_prefix(std::min(end + 1, text.size()));
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (!seen_magic) {
            if (line != kMagic) {
                return std::nullopt;
            }
            seen_magic = true;
            continue;
        }
        if (line.empty()) {
            continue;
        }
        std::string_view rest = line;
        const std::string_view key = next_token(rest);
        const auto once = [](bool& seen) {
            const bool first = !seen;
            seen = true;
            return first;
        };
        bool ok = true;
        if (key == "eye") {
            ok = once(seen_eye) && parse_floats(rest, view.eye);
        } else if (key == "target") {
            ok = once(seen_target) && parse_floats(rest, view.target);
        } else if (key == "up") {
            ok = once(seen_up) && parse_floats(rest, view.up);
        } else if (key == "fov") {
            std::array<float, 1> v{};
            ok = once(seen_fov) && parse_floats(rest, v);
            view.fov_y_deg = v[0];
        } else if (key == "radius") {
            std::array<float, 1> v{};
            ok = once(seen_radius) && parse_floats(rest, v);
            view.radius = v[0];
        } else if (key == "quality") {
            ok = once(seen_quality) && parse_quality(rest, view.quality);
        }
        if (!ok) {
            return std::nullopt;
        }
    }
    if (!(seen_eye && seen_target && seen_up && seen_fov && seen_radius)) {
        return std::nullopt;
    }
    if (camera_from_view(view).status != Status::Ok) {
        return std::nullopt;
    }
    return view;
}

} // namespace splatthumb
