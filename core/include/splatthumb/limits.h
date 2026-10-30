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

} // namespace splatthumb::limits
