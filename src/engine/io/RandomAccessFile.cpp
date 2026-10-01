#include "engine/io/RandomAccessFile.h"

#include "engine/io/FileSystem.h"

#include <algorithm>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine::io {

RandomAccessFile::RandomAccessFile(RandomAccessFile &&other) noexcept
{
    *this = std::move(other);
}

RandomAccessFile &RandomAccessFile::operator=(RandomAccessFile &&other) noexcept
{
    if (this != &other) {
        close();
#ifdef _WIN32
        handle_ = std::exchange(other.handle_, nullptr);
#else
        fd_ = std::exchange(other.fd_, -1);
#endif
        size_ = std::exchange(other.size_, 0);
        path_ = std::move(other.path_);
    }
    return *this;
}

RandomAccessFile::~RandomAccessFile()
{
    close();
}

#ifdef _WIN32

bool RandomAccessFile::isOpen() const
{
    return handle_ != nullptr;
}

void RandomAccessFile::close()
{
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}

Result<RandomAccessFile> RandomAccessFile::open(const std::filesystem::path &path)
{
    const std::wstring native = longPath(path);
    HANDLE h = CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        const std::string where = path.string();
        switch (err) {
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            return fail(Errc::Locked, where + " is in use by another process");
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return fail(Errc::NotFound, where + " not found");
        default:
            return fail(Errc::Io, where + ": " + systemErrorMessage(err));
        }
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size)) {
        const DWORD err = GetLastError();
        CloseHandle(h);
        return fail(Errc::Io, path.string() + ": " + systemErrorMessage(err));
    }

    RandomAccessFile file;
    file.handle_ = h;
    file.size_ = static_cast<std::uint64_t>(size.QuadPart);
    file.path_ = path;
    return file;
}

Result<void> RandomAccessFile::readAt(std::uint64_t offset, std::span<std::uint8_t> buffer) const
{
    if (offset > size_ || buffer.size() > size_ - offset)
        return fail(Errc::Corrupt, "read past end of " + path_.string());

    std::uint8_t *dst = buffer.data();
    std::size_t remaining = buffer.size();
    while (remaining > 0) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 1u << 30));
        OVERLAPPED ov{};
        ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
        ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD got = 0;
        if (!ReadFile(handle_, dst, chunk, &got, &ov)) {
            const DWORD err = GetLastError();
            return fail(Errc::Io, path_.string() + ": " + systemErrorMessage(err));
        }
        if (got == 0)
            return fail(Errc::Io, path_.string() + ": unexpected end of file");
        dst += got;
        offset += got;
        remaining -= got;
    }
    return {};
}

#else

bool RandomAccessFile::isOpen() const
{
    return fd_ >= 0;
}

void RandomAccessFile::close()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

Result<RandomAccessFile> RandomAccessFile::open(const std::filesystem::path &path)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        const int err = errno;
        if (err == ENOENT)
            return fail(Errc::NotFound, path.string() + " not found");
        return fail(Errc::Io, path.string() + ": " + std::strerror(err));
    }
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        const int err = errno;
        ::close(fd);
        return fail(Errc::Io, path.string() + ": " + std::strerror(err));
    }
    RandomAccessFile file;
    file.fd_ = fd;
    file.size_ = static_cast<std::uint64_t>(st.st_size);
    file.path_ = path;
    return file;
}

Result<void> RandomAccessFile::readAt(std::uint64_t offset, std::span<std::uint8_t> buffer) const
{
    if (offset > size_ || buffer.size() > size_ - offset)
        return fail(Errc::Corrupt, "read past end of " + path_.string());

    std::uint8_t *dst = buffer.data();
    std::size_t remaining = buffer.size();
    while (remaining > 0) {
        const ssize_t got = ::pread(fd_, dst, remaining, static_cast<off_t>(offset));
        if (got < 0) {
            if (errno == EINTR)
                continue;
            return fail(Errc::Io, path_.string() + ": " + std::strerror(errno));
        }
        if (got == 0)
            return fail(Errc::Io, path_.string() + ": unexpected end of file");
        dst += got;
        offset += static_cast<std::uint64_t>(got);
        remaining -= static_cast<std::size_t>(got);
    }
    return {};
}

#endif

} // namespace engine::io
