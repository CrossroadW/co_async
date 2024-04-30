

#ifdef __linux__
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#endif

#pragma once

#include <co_async/std.hpp>

#ifdef __linux__

#include <co_async/awaiter/task.hpp>
#include <co_async/system/system_loop.hpp>
#include <co_async/system/error_handling.hpp>

namespace co_async {

struct [[nodiscard]] FileHandle {
    FileHandle() noexcept : mFileNo(-1) {}

    explicit FileHandle(int fileNo) noexcept : mFileNo(fileNo) {}

    int fileNo() const noexcept {
        return mFileNo;
    }

    int releaseFile() noexcept {
        int ret = mFileNo;
        mFileNo = -1;
        return ret;
    }

    explicit operator bool() noexcept {
        return mFileNo != -1;
    }

    FileHandle(FileHandle &&that) noexcept : mFileNo(that.releaseFile()) {}

    FileHandle &operator=(FileHandle &&that) noexcept {
        std::swap(mFileNo, that.mFileNo);
        return *this;
    }

    ~FileHandle() {
        if (mFileNo != -1)
            close(mFileNo);
    }

protected:
    int mFileNo;
};

struct [[nodiscard]] DirFilePath {
    DirFilePath(std::filesystem::path path) : mPath(path), mDirFd(AT_FDCWD) {}

    explicit DirFilePath(std::filesystem::path path, FileHandle const &dir)
        : mPath(path),
          mDirFd(dir.fileNo()) {}

    char const *c_str() const noexcept {
        return mPath.c_str();
    }

    std::filesystem::path const &path() const {
        return mPath;
    }

    int dir_file() const noexcept {
        return mDirFd;
    }

private:
    std::filesystem::path mPath;
    int mDirFd;
};

struct FileStat {
    struct statx *getNativeStatx() {
        return &mStatx;
    }

    std::uint64_t size() const noexcept {
        return mStatx.stx_size;
    }

    std::uint64_t num_blocks() const noexcept {
        return mStatx.stx_blocks;
    }

    mode_t mode() const noexcept {
        return mStatx.stx_mode;
    }

    int uid() const noexcept {
        return mStatx.stx_uid;
    }

    int gid() const noexcept {
        return mStatx.stx_gid;
    }

    bool is_directory() const noexcept {
        return (mStatx.stx_mode & S_IFDIR) != 0;
    }

    bool is_regular_file() const noexcept {
        return (mStatx.stx_mode & S_IFREG) != 0;
    }

    bool is_symlink() const noexcept {
        return (mStatx.stx_mode & S_IFLNK) != 0;
    }

    bool is_readable() const noexcept {
        return (mStatx.stx_mode & S_IRUSR) != 0;
    }

    bool is_writable() const noexcept {
        return (mStatx.stx_mode & S_IWUSR) != 0;
    }

    bool is_executable() const noexcept {
        return (mStatx.stx_mode & S_IXUSR) != 0;
    }

    std::chrono::system_clock::time_point accessed_time() const {
        return statTimestampToTimePoint(mStatx.stx_atime);
    }

    std::chrono::system_clock::time_point attribute_changed_time() const {
        return statTimestampToTimePoint(mStatx.stx_ctime);
    }

    std::chrono::system_clock::time_point created_time() const {
        return statTimestampToTimePoint(mStatx.stx_btime);
    }

    std::chrono::system_clock::time_point modified_time() const {
        return statTimestampToTimePoint(mStatx.stx_mtime);
    }

private:
    struct statx mStatx;

    static std::chrono::system_clock::time_point
    statTimestampToTimePoint(struct statx_timestamp const &time) {
        return std::chrono::system_clock::time_point(
            std::chrono::seconds(time.tv_sec) +
            std::chrono::nanoseconds(time.tv_nsec));
    }
};

enum class OpenMode : int {
    Read = O_RDONLY | O_LARGEFILE | O_CLOEXEC,
    Write = O_WRONLY | O_TRUNC | O_CREAT | O_LARGEFILE | O_CLOEXEC,
    ReadWrite = O_RDWR | O_CREAT | O_LARGEFILE | O_CLOEXEC,
    Append = O_WRONLY | O_APPEND | O_CREAT | O_LARGEFILE | O_CLOEXEC,
    Directory = O_RDONLY | O_DIRECTORY | O_LARGEFILE | O_CLOEXEC,
};

inline std::filesystem::path make_path(std::string_view path) {
    return std::filesystem::path((char8_t const *)std::string(path).c_str());
}

template <std::convertible_to<std::string_view>... Ts>
    requires(sizeof...(Ts) >= 2)
inline std::filesystem::path make_path(Ts &&...chunks) {
    return (make_path(chunks) / ...);
}

inline Task<Expected<FileHandle, std::errc>> fs_open(DirFilePath path, OpenMode mode,
                                mode_t access = 0644) {
    int oflags = (int)mode;
    int fd = co_await expectError(
        co_await uring_openat(path.dir_file(), path.c_str(), oflags, access));
    FileHandle file(fd);
    co_return file;
}

inline Task<Expected<void, std::errc>> fs_close(FileHandle file) {
    co_await expectError(co_await uring_close(file.fileNo()));
    file.releaseFile();
    co_return {};
}

inline Task<Expected<void, std::errc>> fs_mkdir(DirFilePath path, mode_t access = 0755) {
    co_await expectError(
        co_await uring_mkdirat(path.dir_file(), path.c_str(), access));
    co_return {};
}

inline Task<Expected<void, std::errc>> fs_link(DirFilePath oldpath, DirFilePath newpath) {
    co_await expectError(co_await uring_linkat(oldpath.dir_file(), oldpath.c_str(),
                                           newpath.dir_file(), newpath.c_str(),
                                           0));
    co_return {};
}

inline Task<Expected<void, std::errc>> fs_symlink(DirFilePath target, DirFilePath linkpath) {
    co_await expectError(co_await uring_symlinkat(
        target.c_str(), linkpath.dir_file(), linkpath.c_str()));
    co_return {};
}

inline Task<Expected<void, std::errc>> fs_unlink(DirFilePath path) {
    co_await expectError(co_await uring_unlinkat(path.dir_file(), path.c_str(), 0));
    co_return {};
}

inline Task<Expected<void, std::errc>> fs_rmdir(DirFilePath path) {
    co_await expectError(co_await uring_unlinkat(path.dir_file(), path.c_str(), AT_REMOVEDIR));
    co_return {};
}

inline Task<Expected<FileStat, std::errc>>
fs_stat(DirFilePath path, int mask = STATX_BASIC_STATS | STATX_BTIME) {
    FileStat ret;
    co_await expectError(co_await uring_statx(path.dir_file(), path.c_str(), 0, mask, ret.getNativeStatx()));
    co_return ret;
}

inline Task<Expected<std::uint64_t, std::errc>> fs_stat_size(DirFilePath path) {
    FileStat ret;
    co_await expectError(co_await uring_statx(path.dir_file(), path.c_str(), 0, STATX_SIZE, ret.getNativeStatx()));
    co_return ret.size();
}

inline Task<Expected<std::size_t>> fs_read(FileHandle &file, std::span<char> buffer,
                                 std::uint64_t offset = -1) {
    co_return co_await expectError(
        co_await uring_read(file.fileNo(), buffer, offset));
}

inline Task<Expected<std::size_t, std::errc>> fs_write(FileHandle &file,
                                  std::span<char const> buffer,
                                  std::uint64_t offset = -1) {
    co_return co_await expectError(
        co_await uring_write(file.fileNo(), buffer, offset));
}

inline Task<Expected<void, std::errc>> fs_truncate(FileHandle &file, std::uint64_t size = 0) {
    co_await expectError(co_await uring_ftruncate(file.fileNo(), size));
    co_return {};
}

inline Task<Expected<std::size_t, std::errc>> fs_splice(FileHandle &fileIn, FileHandle &fileOut,
                                   std::size_t size,
                                   std::uint64_t offsetIn = -1,
                                   std::uint64_t offsetOut = -1) {
    co_return co_await expectError(co_await uring_splice(
        fileIn.fileNo(), offsetIn, fileOut.fileNo(), offsetOut, size, 0));
}

inline Task<Expected<std::size_t, std::errc>> fs_getdents(FileHandle &dirFile,
                                     std::span<char> buffer) {
    int res = getdents64(dirFile.fileNo(), buffer.data(), buffer.size());
    if (res < 0) [[unlikely]] {
        res = -errno;
    }
    co_return co_await expectError(res);
}

inline Task<int> fs_nop() {
    co_return co_await uring_nop();
}

inline Task<Expected<void, std::errc>> fs_cancel_fd(FileHandle &file) {
    co_await expectError(co_await uring_cancel_fd(
        file.fileNo(), IORING_ASYNC_CANCEL_FD | IORING_ASYNC_CANCEL_ALL));
    co_return {};
}

} // namespace co_async
#endif
