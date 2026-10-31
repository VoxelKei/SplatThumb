#pragma once

#include <cstddef>
#include <cstdint>

// Every limit applied to untrusted input lives here.

namespace splatthumb::limits {

// Only this many bytes from the start of a file are read to find the header.
inline constexpr std::size_t kMaxHeaderBytes = 64 * 1024;

// Longest accepted header line, excluding the line break.
inline constexpr std::size_t kMaxHeaderLineLength = 1024;

// Longest accepted element or property name.
inline constexpr std::size_t kMaxNameLength = 64;

inline constexpr std::size_t kMaxElements = 16;
inline constexpr std::size_t kMaxPropertiesPerElement = 256;

// Largest vertex count accepted for a splat file.
inline constexpr std::uint64_t kMaxVertexCount = 50'000'000;

// ---- Rendering ----

// At most this many splats are read and drawn; larger files are sampled.
inline constexpr std::size_t kMaxRenderSplats = 200'000;

// Largest single read from the source while loading splats.
inline constexpr std::size_t kMaxReadChunkBytes = 1024 * 1024;

// Whole thumbnail generation is abandoned after this long.
inline constexpr int kRenderTimeLimitMs = 3000;

// Splats with lower opacity (after sigmoid) are skipped.
inline constexpr float kMinOpacity = 0.02f;

// Log-scale values are clamped to this range before exp().
inline constexpr float kMinLogScale = -20.0f;
inline constexpr float kMaxLogScale = 10.0f;

// Splats with any position coordinate beyond this magnitude are skipped.
inline constexpr float kMaxAbsCoordinate = 1.0e6f;

// Largest thumbnail side in pixels; larger requests are clamped.
inline constexpr int kMaxThumbnailSize = 1024;
inline constexpr int kMinThumbnailSize = 16;

// Requests up to this size are drawn at 2x and downsampled for anti-aliasing.
inline constexpr int kMaxSupersampledSize = 512;

// A single splat covers at most this fraction of the image side as its radius.
inline constexpr float kMaxSplatRadiusFraction = 0.5f;

} // namespace splatthumb::limits
