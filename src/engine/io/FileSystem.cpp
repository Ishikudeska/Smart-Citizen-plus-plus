#include "engine/io/FileSystem.h"

#include "engine/Try.h"

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

#ifdef _WIN32

std::string systemErrorMessage(unsigned long code)
{
    wchar_t *buffer = nullptr;
    const DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                         FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    std::wstring text = len ? std::wstring(buffer, len) : std::wstring();
    if (buffer)
        LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' '))
        text.pop_back();
    if (text.empty())
        return "system error " + std::to_string(code);

    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                          nullptr, nullptr);
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), bytes, nullptr,
                        nullptr);
    return out + " (" + std::to_string(code) + ")";
}

std::wstring longPath(const std::filesystem::path &path)
{
    std::wstring native = path.native();
    if (native.starts_with(LR"(\\?\)"))
        return native;

    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    if (ec)
        absolute = path;
    native = absolute.lexically_normal().make_preferred().native();
    if (native.starts_with(LR"(\\)"))
        return LR"(\\?\UNC\)" + native.substr(2);
    return LR"(\\?\)" + native;
}

bool exists(const std::filesystem::path &path)
{
    return GetFileAttributesW(longPath(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

namespace {

std::string toUtf8(std::wstring_view text)
{
    if (text.empty())
        return {};
    const int bytes =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), bytes, nullptr,
                        nullptr);
    return out;
}

// Depth-first walk over `dir` (a \\?\ path). `relative` is the UTF-8 prefix
// for entries found in it.
Result<void> walk(const std::wstring &dir, const std::string &relative,
                  const std::function<void(std::string_view)> *visitFile,
                  const std::function<Result<void>(const std::wstring &, bool)> *visitAny)
{
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch,
                                   nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND)
            return {};
        return fail(Errc::Io, "cannot list " + toUtf8(dir) + ": " + systemErrorMessage(err));
    }
    Result<void> result;
    do {
        const std::wstring_view name = data.cFileName;
        if (name == L"." || name == L"..")
            continue;
        const std::wstring full = dir + L"\\" + std::wstring(name);
        const bool isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool isLink = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        const std::string rel = relative.empty() ? toUtf8(name) : relative + "/" + toUtf8(name);
        if (isDir && !isLink) {
            result = walk(full, rel, visitFile, visitAny);
            if (!result)
                break;
        } else if (visitFile && !isDir) {
            (*visitFile)(rel);
        }
        if (visitAny) {
            result = (*visitAny)(full, isDir);
            if (!result)
                break;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return result;
}

} // namespace

Result<void> forEachFile(const std::filesystem::path &root,
                         const std::function<void(std::string_view relativePath)> &visit)
{
    return walk(longPath(root), {}, &visit, nullptr);
}

Result<void> removeAll(const std::filesystem::path &path)
{
    const std::wstring top = longPath(path);
    const DWORD attrs = GetFileAttributesW(top.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return {};
    auto removeOne = [](const std::wstring &target, bool isDir) -> Result<void> {
        SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL);
        const BOOL ok = isDir ? RemoveDirectoryW(target.c_str()) : DeleteFileW(target.c_str());
        if (!ok) {
            const DWORD err = GetLastError();
            return fail(Errc::Io, "cannot remove " + toUtf8(target) + ": " + systemErrorMessage(err));
        }
        return {};
    };
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
        return removeOne(top, (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
    const std::function<Result<void>(const std::wstring &, bool)> visitAny = removeOne;
    SC_TRY(walk(top, {}, nullptr, &visitAny));
    return removeOne(top, true);
}

Result<void> createDirectories(const std::filesystem::path &dir)
{
    const std::wstring full = longPath(dir);
    const DWORD attrs = GetFileAttributesW(full.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (attrs & FILE_ATTRIBUTE_DIRECTORY)
            return {};
        return fail(Errc::Io, dir.string() + " exists and is not a directory");
    }

    // Skip the `\\?\C:\` or `\\?\UNC\server\share\` root, then create each level.
    std::size_t pos = 4;
    if (full.compare(4, 4, L"UNC\\") == 0) {
        pos = full.find(L'\\', 8); // end of server
        if (pos != std::wstring::npos)
            pos = full.find(L'\\', pos + 1); // end of share
    } else {
        pos = full.find(L'\\', pos); // after drive
    }
    while (pos != std::wstring::npos) {
        pos = full.find(L'\\', pos + 1);
        const std::wstring prefix = full.substr(0, pos);
        if (!CreateDirectoryW(prefix.c_str(), nullptr)) {
            const DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS)
                return fail(Errc::Io, "cannot create " + dir.string() + ": " + systemErrorMessage(err));
        }
    }
    return {};
}

OutputFile::OutputFile(OutputFile &&other) noexcept
{
    *this = std::move(other);
}

OutputFile &OutputFile::operator=(OutputFile &&other) noexcept
{
    if (this != &other) {
        discard();
        handle_ = std::exchange(other.handle_, nullptr);
        target_ = std::move(other.target_);
        writingTo_ = std::move(other.writingTo_);
    }
    return *this;
}

OutputFile::~OutputFile()
{
    discard();
}

void OutputFile::discard()
{
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
        DeleteFileW(longPath(writingTo_).c_str());
    }
}

Result<OutputFile> OutputFile::create(const std::filesystem::path &target, bool atomic)
{
    OutputFile file;
    file.target_ = target;
    file.writingTo_ = atomic ? std::filesystem::path(target.native() + L".part") : target;
    HANDLE h = CreateFileW(longPath(file.writingTo_).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION)
            return fail(Errc::Locked, target.string() + " is in use by another process");
        return fail(Errc::Io, "cannot create " + target.string() + ": " + systemErrorMessage(err));
    }
    file.handle_ = h;
    return file;
}

Result<void> OutputFile::write(std::span<const std::uint8_t> bytes)
{
    const std::uint8_t *src = bytes.data();
    std::size_t remaining = bytes.size();
    while (remaining > 0) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 1u << 30));
        DWORD written = 0;
        if (!WriteFile(handle_, src, chunk, &written, nullptr)) {
            const DWORD err = GetLastError();
            return fail(Errc::Io, "cannot write " + target_.string() + ": " + systemErrorMessage(err));
        }
        src += written;
        remaining -= written;
    }
    return {};
}

Result<void> OutputFile::commit()
{
    if (!handle_)
        return fail(Errc::Io, "file already closed");
    const bool closed = CloseHandle(handle_);
    handle_ = nullptr;
    if (!closed) {
        const DWORD err = GetLastError();
        DeleteFileW(longPath(writingTo_).c_str());
        return fail(Errc::Io, "cannot close " + target_.string() + ": " + systemErrorMessage(err));
    }
    if (writingTo_ != target_) {
        if (!MoveFileExW(longPath(writingTo_).c_str(), longPath(target_).c_str(), MOVEFILE_REPLACE_EXISTING)) {
            const DWORD err = GetLastError();
            DeleteFileW(longPath(writingTo_).c_str());
            return fail(Errc::Io, "cannot replace " + target_.string() + ": " + systemErrorMessage(err));
        }
    }
    return {};
}

#else

bool exists(const std::filesystem::path &path)
{
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

Result<void> forEachFile(const std::filesystem::path &root,
                         const std::function<void(std::string_view relativePath)> &visit)
{
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != decltype(it)();
         it.increment(ec)) {
        if (it->is_regular_file(ec))
            visit(it->path().lexically_relative(root).generic_string());
    }
    if (ec && ec != std::errc::no_such_file_or_directory)
        return fail(Errc::Io, "cannot list " + root.string() + ": " + ec.message());
    return {};
}

Result<void> removeAll(const std::filesystem::path &path)
{
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec)
        return fail(Errc::Io, "cannot remove " + path.string() + ": " + ec.message());
    return {};
}

Result<void> createDirectories(const std::filesystem::path &dir)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
        return fail(Errc::Io, "cannot create " + dir.string() + ": " + ec.message());
    return {};
}

OutputFile::OutputFile(OutputFile &&other) noexcept
{
    *this = std::move(other);
}

OutputFile &OutputFile::operator=(OutputFile &&other) noexcept
{
    if (this != &other) {
        discard();
        fd_ = std::exchange(other.fd_, -1);
        target_ = std::move(other.target_);
        writingTo_ = std::move(other.writingTo_);
    }
    return *this;
}

OutputFile::~OutputFile()
{
    discard();
}

void OutputFile::discard()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
        ::unlink(writingTo_.c_str());
    }
}

Result<OutputFile> OutputFile::create(const std::filesystem::path &target, bool atomic)
{
    OutputFile file;
    file.target_ = target;
    file.writingTo_ = atomic ? std::filesystem::path(target.native() + ".part") : target;
    const int fd = ::open(file.writingTo_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
        return fail(Errc::Io, "cannot create " + target.string() + ": " + std::strerror(errno));
    file.fd_ = fd;
    return file;
}

Result<void> OutputFile::write(std::span<const std::uint8_t> bytes)
{
    const std::uint8_t *src = bytes.data();
    std::size_t remaining = bytes.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd_, src, remaining);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return fail(Errc::Io, "cannot write " + target_.string() + ": " + std::strerror(errno));
        }
        src += written;
        remaining -= static_cast<std::size_t>(written);
    }
    return {};
}

Result<void> OutputFile::commit()
{
    if (fd_ < 0)
        return fail(Errc::Io, "file already closed");
    const int rc = ::close(fd_);
    fd_ = -1;
    if (rc != 0) {
        ::unlink(writingTo_.c_str());
        return fail(Errc::Io, "cannot close " + target_.string() + ": " + std::strerror(errno));
    }
    if (writingTo_ != target_ && ::rename(writingTo_.c_str(), target_.c_str()) != 0) {
        const int err = errno;
        ::unlink(writingTo_.c_str());
        return fail(Errc::Io, "cannot replace " + target_.string() + ": " + std::strerror(err));
    }
    return {};
}

#endif

Result<void> writeFile(const std::filesystem::path &target, std::span<const std::uint8_t> bytes, bool atomic)
{
    auto file = OutputFile::create(target, atomic);
    if (!file)
        return std::unexpected(file.error());
    if (auto ok = file->write(bytes); !ok)
        return ok;
    return file->commit();
}

} // namespace engine::io
