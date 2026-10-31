#include "splatthumb/render.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <new>
#include <numeric>

#include "splatthumb/limits.h"

namespace splatthumb {

namespace {

constexpr float kGray = 0.18f;
constexpr float kWhite = 1.0f;
constexpr float kBlack = 0.0f;
constexpr float kMinAlpha = 1.0f / 255.0f;
constexpr float kMaxAlpha = 0.99f;
// Low-pass filter added to the 2D covariance so tiny splats still cover a pixel.
constexpr float kDilation = 0.3f;

struct Projected {
    float u = 0, v = 0;               // pixel center
    float conic_a = 0, conic_b = 0, conic_c = 0; // inverse 2D covariance
    float depth = 0;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // inclusive pixel bounds, already clipped
    float r = 0, g = 0, b = 0, opacity = 0;
};

float dot3(const Vec3& a, const Vec3& b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Returns false if the splat is not visible.
bool project(const Splat& s, const Camera& cam, int side, float focal, Projected& out) noexcept {
    const Vec3 p{s.position[0] - cam.eye[0], s.position[1] - cam.eye[1],
                 s.position[2] - cam.eye[2]};
    const float x = dot3(p, cam.right);
    const float y = dot3(p, cam.up);
    const float z = dot3(p, cam.forward);
    const float near_z = std::max(cam.radius * 0.01f, cam.near_clip);
    if (!(z > near_z)) {
        return false;
    }

    // Rotation matrix from the unit quaternion (w, x, y, z).
    const float qw = s.rotation[0], qx = s.rotation[1], qy = s.rotation[2], qz = s.rotation[3];
    const float R[3][3] = {
        {1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qw * qz), 2 * (qx * qz + qw * qy)},
        {2 * (qx * qy + qw * qz), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qw * qx)},
        {2 * (qx * qz - qw * qy), 2 * (qy * qz + qw * qx), 1 - 2 * (qx * qx + qy * qy)}};
    // M = W * R * S, where W's rows are the camera basis. Covariance in camera space is M M^T.
    const Vec3* basis[3] = {&cam.right, &cam.up, &cam.forward};
    float M[3][3];
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            const Vec3& w = *basis[i];
            M[i][j] = (w[0] * R[0][j] + w[1] * R[1][j] + w[2] * R[2][j]) * s.scale[j];
        }
    }
    float C[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            C[i][j] = M[i][0] * M[j][0] + M[i][1] * M[j][1] + M[i][2] * M[j][2];
        }
    }

    // Screen v grows downward, so the y row of the Jacobian is negated.
    const float inv_z = 1.0f / z;
    const float J[2][3] = {{focal * inv_z, 0.0f, -focal * x * inv_z * inv_z},
                           {0.0f, -focal * inv_z, focal * y * inv_z * inv_z}};
    float JC[2][3];
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 3; ++j) {
            JC[i][j] = J[i][0] * C[0][j] + J[i][1] * C[1][j] + J[i][2] * C[2][j];
        }
    }
    const float a = JC[0][0] * J[0][0] + JC[0][1] * J[0][1] + JC[0][2] * J[0][2] + kDilation;
    const float b = JC[0][0] * J[1][0] + JC[0][1] * J[1][1] + JC[0][2] * J[1][2];
    const float c = JC[1][0] * J[1][0] + JC[1][1] * J[1][1] + JC[1][2] * J[1][2] + kDilation;
    const float det = a * c - b * b;
    if (!(det > 0.0f) || !std::isfinite(det)) {
        return false;
    }

    const float half = static_cast<float>(side) * 0.5f;
    const float u = half + focal * x * inv_z;
    const float v = half - focal * y * inv_z;
    const float mid = 0.5f * (a + c);
    const float lambda = mid + std::sqrt(std::max(0.1f, mid * mid - det));
    const float max_radius = static_cast<float>(side) * limits::kMaxSplatRadiusFraction;
    const float radius = std::min(std::ceil(3.0f * std::sqrt(lambda)), max_radius);
    if (!std::isfinite(u) || !std::isfinite(v) || !std::isfinite(radius)) {
        return false;
    }
    const float fx0 = std::max(u - radius, 0.0f);
    const float fy0 = std::max(v - radius, 0.0f);
    const float fx1 = std::min(u + radius, static_cast<float>(side - 1));
    const float fy1 = std::min(v + radius, static_cast<float>(side - 1));
    if (fx0 > fx1 || fy0 > fy1) {
        return false;
    }

    out.u = u;
    out.v = v;
    out.conic_a = c / det;
    out.conic_b = -b / det;
    out.conic_c = a / det;
    out.depth = z;
    out.x0 = static_cast<int>(fx0);
    out.y0 = static_cast<int>(fy0);
    out.x1 = static_cast<int>(fx1);
    out.y1 = static_cast<int>(fy1);
    out.r = s.color[0];
    out.g = s.color[1];
    out.b = s.color[2];
    out.opacity = s.opacity;
    return true;
}

// Linear gray level of an opaque background.
float background_level(Background background) noexcept {
    switch (background) {
    case Background::White: return kWhite;
    case Background::Black: return kBlack;
    case Background::Gray:
    case Background::Transparent: break;
    }
    return kGray;
}

std::uint8_t to_u8(float v) noexcept {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

} // namespace

RenderResult render_splats(std::span<const Splat> splats, const Camera& camera,
                           const RenderOptions& options, const Deadline& deadline) noexcept {
    RenderResult result;
    const int size = std::clamp(options.size, limits::kMinThumbnailSize, limits::kMaxThumbnailSize);
    const int scale = size <= limits::kMaxSupersampledSize ? 2 : 1;
    const int side = size * scale;
    const float focal = static_cast<float>(side) * 0.5f / std::tan(camera.fov_y * 0.5f);
    if (!std::isfinite(focal) || !(focal > 0.0f)) {
        result.status = Status::NothingToDraw;
        return result;
    }

    try {
        std::vector<Projected> projected;
        projected.reserve(splats.size());
        for (std::size_t i = 0; i < splats.size(); ++i) {
            if ((i & 1023) == 0 && deadline.expired()) {
                result.status = Status::Timeout;
                return result;
            }
            Projected p;
            if (project(splats[i], camera, side, focal, p)) {
                projected.push_back(p);
            }
        }
        if (projected.empty()) {
            result.status = Status::NothingToDraw;
            return result;
        }

        // Back to front: farthest first.
        std::vector<std::uint32_t> order(projected.size());
        std::iota(order.begin(), order.end(), 0u);
        std::sort(order.begin(), order.end(), [&](std::uint32_t l, std::uint32_t r) {
            return projected[l].depth > projected[r].depth;
        });

        // Premultiplied RGBA accumulation buffer.
        const auto pixels = static_cast<std::size_t>(side) * static_cast<std::size_t>(side);
        std::vector<float> buf(pixels * 4, 0.0f);
        std::size_t drawn = 0;
        // Check the clock about every million pixels touched, so a few huge splats
        // cannot overshoot the deadline by much.
        std::size_t pixels_since_check = 0;
        for (std::size_t k = 0; k < order.size(); ++k) {
            const Projected& p = projected[order[k]];
            pixels_since_check += static_cast<std::size_t>(p.x1 - p.x0 + 1) *
                                  static_cast<std::size_t>(p.y1 - p.y0 + 1);
            if (pixels_since_check >= (std::size_t{1} << 20)) {
                pixels_since_check = 0;
                if (deadline.expired()) {
                    result.status = Status::Timeout;
                    return result;
                }
            }
            bool any = false;
            for (int py = p.y0; py <= p.y1; ++py) {
                const float dy = static_cast<float>(py) + 0.5f - p.v;
                float* row = buf.data() + static_cast<std::size_t>(py) * static_cast<std::size_t>(side) * 4;
                for (int px = p.x0; px <= p.x1; ++px) {
                    const float dx = static_cast<float>(px) + 0.5f - p.u;
                    const float power =
                        -0.5f * (p.conic_a * dx * dx + p.conic_c * dy * dy) - p.conic_b * dx * dy;
                    if (power > 0.0f) {
                        continue;
                    }
                    const float alpha = std::min(kMaxAlpha, p.opacity * std::exp(power));
                    if (alpha < kMinAlpha) {
                        continue;
                    }
                    float* d = row + static_cast<std::size_t>(px) * 4;
                    const float keep = 1.0f - alpha;
                    d[0] = p.r * alpha + d[0] * keep;
                    d[1] = p.g * alpha + d[1] * keep;
                    d[2] = p.b * alpha + d[2] * keep;
                    d[3] = alpha + d[3] * keep;
                    any = true;
                }
            }
            drawn += any ? 1 : 0;
        }
        if (drawn == 0) {
            result.status = Status::NothingToDraw;
            return result;
        }

        // Box downsample, optional background, convert to 8 bits.
        const float bg = background_level(options.background);
        Image& img = result.image;
        img.size = size;
        img.rgba.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);
        const float inv = 1.0f / static_cast<float>(scale * scale);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                float acc[4] = {0, 0, 0, 0};
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        const std::size_t idx =
                            (static_cast<std::size_t>(y * scale + sy) * static_cast<std::size_t>(side) +
                             static_cast<std::size_t>(x * scale + sx)) * 4;
                        for (int ch = 0; ch < 4; ++ch) {
                            acc[ch] += buf[idx + static_cast<std::size_t>(ch)];
                        }
                    }
                }
                for (float& ch : acc) {
                    ch *= inv;
                }
                if (options.background != Background::Transparent) {
                    const float keep = 1.0f - acc[3];
                    acc[0] += bg * keep;
                    acc[1] += bg * keep;
                    acc[2] += bg * keep;
                    acc[3] = 1.0f;
                }
                std::uint8_t* out =
                    img.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                                       static_cast<std::size_t>(x)) * 4;
                const std::uint8_t a8 = to_u8(acc[3]);
                out[3] = a8;
                // Keep premultiplied invariant: color <= alpha after rounding.
                for (int ch = 0; ch < 3; ++ch) {
                    out[ch] = std::min(to_u8(acc[ch]), a8);
                }
            }
        }
        result.splats_drawn = drawn;
        result.status = Status::Ok;
    } catch (const std::bad_alloc&) {
        result = {};
        result.status = Status::OutOfMemory;
    }
    return result;
}

RenderResult render_thumbnail(ByteSource& source, const RenderOptions& options,
                              const LoadOptions& load_options, const Deadline& deadline,
                              ThumbnailStats* stats, const View* view) noexcept {
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    ThumbnailStats local;
    ThumbnailStats& st = stats != nullptr ? *stats : local;
    RenderResult result;

    auto t0 = Clock::now();
    const Classification c = inspect(source);
    auto t1 = Clock::now();
    st.inspect_ms = ms(t0, t1);
    st.vertex_count = c.vertex_count;
    if (c.kind == SplatKind::NotSplat) {
        result.status = c.status;
        return result;
    }

    const LoadResult loaded = load_splats(source, c, load_options, deadline);
    auto t2 = Clock::now();
    st.load_ms = ms(t1, t2);
    st.splats_loaded = loaded.splats.size();
    if (loaded.status != Status::Ok) {
        result.status = loaded.status;
        return result;
    }

    CameraResult cam;
    cam.status = Status::NothingToDraw;
    if (view != nullptr) {
        cam = camera_from_view(*view);
    }
    if (cam.status != Status::Ok) {
        cam = auto_camera(loaded.splats);
    }
    auto t3 = Clock::now();
    st.camera_ms = ms(t2, t3);
    if (cam.status != Status::Ok) {
        result.status = cam.status;
        return result;
    }
    if (deadline.expired()) {
        result.status = Status::Timeout;
        return result;
    }

    result = render_splats(loaded.splats, cam.camera, options, deadline);
    st.render_ms = ms(t3, Clock::now());
    st.splats_drawn = result.splats_drawn;
    return result;
}

} // namespace splatthumb
