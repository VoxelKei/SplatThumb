#include "splatthumb/training_camera.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <string>
#include <numbers>

#include "splatthumb/limits.h"

namespace splatthumb {

namespace {

// A tiny cursor over JSON text: whitespace, punctuation and numbers only.
struct Cursor {
    std::string_view s;
    std::size_t i = 0;

    void skip_space() noexcept {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
            ++i;
        }
    }
    bool take(char c) noexcept {
        skip_space();
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }
    bool number(float& out) noexcept {
        skip_space();
        double v = 0;
        const auto [ptr, ec] = std::from_chars(s.data() + i, s.data() + s.size(), v);
        if (ec != std::errc() || !std::isfinite(v) || std::fabs(v) > limits::kMaxAbsCoordinate) {
            return false;
        }
        i = static_cast<std::size_t>(ptr - s.data());
        out = static_cast<float>(v);
        return true;
    }
};

bool vec3(Cursor& c, Vec3& out) noexcept {
    return c.take('[') && c.number(out[0]) && c.take(',') && c.number(out[1]) && c.take(',') &&
           c.number(out[2]) && c.take(']');
}

// Positions the cursor just after `"key":` inside obj, or returns false.
bool find_key(std::string_view obj, std::string_view key, Cursor& c) noexcept {
    std::size_t from = 0;
    while (true) {
        const std::size_t at = obj.find(key, from);
        if (at == std::string_view::npos) {
            return false;
        }
        from = at + 1;
        // The key must be a whole quoted string followed by a colon.
        if (at == 0 || obj[at - 1] != '"' || at + key.size() >= obj.size() ||
            obj[at + key.size()] != '"') {
            continue;
        }
        c = Cursor{obj, at + key.size() + 1};
        if (c.take(':')) {
            return true;
        }
    }
}

float dot(const Vec3& a, const Vec3& b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

} // namespace

std::optional<TrainingCamera> parse_first_training_camera(std::string_view json) noexcept {
    if (json.size() > kMaxCamerasJsonBytes) {
        json = json.substr(0, kMaxCamerasJsonBytes);
    }
    Cursor c{json};
    if (!c.take('[') || !c.take('{')) {
        return std::nullopt;
    }
    // The first object ends at its first closing brace: camera objects hold no nested objects.
    const std::size_t start = c.i;
    const std::size_t end = json.find('}', start);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view obj = json.substr(start, end - start);

    TrainingCamera cam;
    Cursor k;
    if (!find_key(obj, "position", k) || !vec3(k, cam.position)) {
        return std::nullopt;
    }
    if (!find_key(obj, "rotation", k) || !k.take('[') || !vec3(k, cam.rotation[0]) ||
        !k.take(',') || !vec3(k, cam.rotation[1]) || !k.take(',') || !vec3(k, cam.rotation[2]) ||
        !k.take(']')) {
        return std::nullopt;
    }
    if (!find_key(obj, "height", k) || !k.number(cam.height) || !(cam.height >= 1.0f)) {
        return std::nullopt;
    }
    if (!find_key(obj, "fy", k) || !k.number(cam.fy) || !(cam.fy > 0.0f)) {
        return std::nullopt;
    }
    // A rotation: orthonormal rows and determinant +1, within float tolerance.
    const auto& r = cam.rotation;
    for (std::size_t a = 0; a < 3; ++a) {
        for (std::size_t b = 0; b < 3; ++b) {
            const float expected = a == b ? 1.0f : 0.0f;
            if (!(std::fabs(dot(r[a], r[b]) - expected) < 1e-3f)) {
                return std::nullopt;
            }
        }
    }
    const float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) -
                      r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                      r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (!(det > 0.0f)) {
        return std::nullopt;
    }
    return cam;
}

std::optional<TrainingCamera> find_training_camera(const std::filesystem::path& ply) noexcept {
    try {
        std::filesystem::path dir = ply.parent_path();
        for (int level = 0; level <= 3 && !dir.empty(); ++level) {
            const std::filesystem::path candidate = dir / "cameras.json";
            std::error_code ec;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                std::ifstream in(candidate, std::ios::binary);
                std::string text(kMaxCamerasJsonBytes, '\0');
                in.read(text.data(), static_cast<std::streamsize>(text.size()));
                text.resize(static_cast<std::size_t>(in.gcount()));
                return parse_first_training_camera(text);
            }
            const std::filesystem::path parent = dir.parent_path();
            if (parent == dir) {
                break;
            }
            dir = parent;
        }
    } catch (...) {
        // bad_alloc or an unconvertible path: no training camera.
    }
    return std::nullopt;
}

std::optional<View> view_from_training_camera(const TrainingCamera& camera,
                                              const Camera& framing) noexcept {
    const auto& r = camera.rotation;
    const Vec3 forward{r[0][2], r[1][2], r[2][2]};
    const Vec3 down{r[0][1], r[1][1], r[2][1]};
    const Vec3 to_target{framing.target[0] - camera.position[0],
                         framing.target[1] - camera.position[1],
                         framing.target[2] - camera.position[2]};
    float depth = dot(to_target, forward);
    if (!(depth > 0.0f)) {
        // The scene centre is behind this camera; look ahead by the framing distance.
        const Vec3 d{framing.target[0] - framing.eye[0], framing.target[1] - framing.eye[1],
                     framing.target[2] - framing.eye[2]};
        depth = std::sqrt(dot(d, d));
    }
    View v;
    v.eye = camera.position;
    v.target = {camera.position[0] + forward[0] * depth, camera.position[1] + forward[1] * depth,
                camera.position[2] + forward[2] * depth};
    v.up = {-down[0], -down[1], -down[2]};
    v.fov_y_deg =
        2.0f * std::atan(camera.height / (2.0f * camera.fy)) * 180.0f / std::numbers::pi_v<float>;
    v.radius = framing.radius;
    if (camera_from_view(v).status != Status::Ok) {
        return std::nullopt;
    }
    return v;
}

} // namespace splatthumb
