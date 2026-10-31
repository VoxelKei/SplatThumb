#include "splatthumb/status.h"

namespace splatthumb {

std::string_view to_string(Status status) noexcept {
    switch (status) {
    case Status::Ok: return "ok";
    case Status::ReadError: return "read error";
    case Status::OutOfMemory: return "out of memory";
    case Status::NotPly: return "not a PLY file";
    case Status::HeaderTooLarge: return "header is too large";
    case Status::MissingEndHeader: return "end_header not found";
    case Status::LineTooLong: return "header line is too long";
    case Status::InvalidCharacter: return "invalid character in header";
    case Status::UnsupportedFormat: return "unsupported format (only binary_little_endian 1.0)";
    case Status::MissingFormat: return "format line missing or misplaced";
    case Status::UnknownKeyword: return "unknown header keyword";
    case Status::MalformedElement: return "malformed element line";
    case Status::MalformedProperty: return "malformed property line";
    case Status::UnknownPropertyType: return "unknown property type";
    case Status::NameTooLong: return "name is too long";
    case Status::DuplicateName: return "duplicate element or property name";
    case Status::TooManyElements: return "too many elements";
    case Status::TooManyProperties: return "too many properties";
    case Status::HasFaces: return "has faces (mesh)";
    case Status::HasListProperty: return "has a list property";
    case Status::MissingVertexElement: return "no vertex element";
    case Status::MissingSplatProperties: return "vertex element lacks Gaussian splat properties";
    case Status::EmptyVertexElement: return "vertex element is empty";
    case Status::TooManyVertices: return "too many vertices";
    case Status::SizeOverflow: return "declared data size overflows";
    case Status::FileTooSmall: return "file is smaller than the header declares";
    case Status::ChunkCountMismatch: return "too few chunks for the vertex count";
    case Status::UnsupportedKind: return "this kind of splat file cannot be drawn yet";
    case Status::Timeout: return "time limit exceeded";
    case Status::NothingToDraw: return "no drawable splats";
    }
    return "unknown";
}

} // namespace splatthumb
