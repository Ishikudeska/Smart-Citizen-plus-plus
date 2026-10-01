#pragma once

#include <expected>
#include <string>
#include <utility>

namespace engine {

enum class Errc {
    Io,          // a read or write failed
    NotFound,    // file or archive entry missing
    Locked,      // file held open by another process (e.g. the RSI Launcher patching Data.p4k)
    Format,      // not the format we expected
    Unsupported, // valid data using a feature we don't implement
    Corrupt,     // data failed to decode or verify
    Cancelled,
};

struct Error
{
    Errc code = Errc::Io;
    std::string message;
};

template <class T>
using Result = std::expected<T, Error>;

inline std::unexpected<Error> fail(Errc code, std::string message)
{
    return std::unexpected(Error{code, std::move(message)});
}

} // namespace engine
