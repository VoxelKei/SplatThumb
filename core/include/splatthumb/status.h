#pragma once

#include <string_view>

namespace splatthumb {

// Why a file was or was not accepted. Explorer only cares whether it is Ok;
// the cli prints the reason.
enum class Status {
    Ok,

    // Reading
    ReadError,
    OutOfMemory,

    // Header syntax
    NotPly,
    HeaderTooLarge,
    MissingEndHeader,
    LineTooLong,
    InvalidCharacter,
    UnsupportedFormat,
    MissingFormat,
    UnknownKeyword,
    MalformedElement,
    MalformedProperty,
    UnknownPropertyType,
    NameTooLong,
    DuplicateName,
    TooManyElements,
    TooManyProperties,

    // Classification
    HasFaces,
    HasListProperty,
    MissingVertexElement,
    MissingSplatProperties,
    EmptyVertexElement,
    TooManyVertices,
    SizeOverflow,
    FileTooSmall,
    ChunkCountMismatch,

    // Rendering
    UnsupportedKind,
    Timeout,
    NothingToDraw,
};

std::string_view to_string(Status status) noexcept;

} // namespace splatthumb
