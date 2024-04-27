

#ifdef __linux__
#include <unistd.h>
#endif

#pragma once

#include <co_async/std.hpp>

#ifdef __linux__
#include <co_async/system/error_handling.hpp>
#include <co_async/system/fs.hpp>
#include <co_async/awaiter/task.hpp>
#include <co_async/iostream/file_stream.hpp>

namespace co_async {

struct PipeHandlePair {
    FileHandle mReader;
    FileHandle mWriter;

    std::tuple<FileIStream, FileOStream> stream() {
        return {FileIStream(reader()), FileOStream(writer())};
    }

    FileHandle reader() {
#if CO_ASYNC_DEBUG
        if (!mReader) [[unlikely]] {
            throw std::logic_error(
                "PipeHandlePair::reader() can only be called once");
        }
#endif
        return std::move(mReader);
    }

    FileHandle writer() {
#if CO_ASYNC_DEBUG
        if (!mWriter) [[unlikely]] {
            throw std::logic_error(
                "PipeHandlePair::writer() can only be called once");
        }
#endif
        return std::move(mWriter);
    }
};

inline Task<PipeHandlePair> fs_pipe() {
    int p[2];
    checkError(pipe2(p, 0));
    co_return {FileHandle(p[0]), FileHandle(p[1])};
}

inline Task<> send_file(FileHandle &sock, FileHandle &&file) {
    auto [readPipe, writePipe] = co_await fs_pipe();
    while (auto n = co_await fs_splice(file, writePipe, 65536)) {
        std::size_t m;
        while ((m = co_await fs_splice(readPipe, sock, n)) < n) [[unlikely]] {
            n -= m;
        }
    }
    co_await fs_close(std::move(file));
    co_await fs_close(std::move(readPipe));
    co_await fs_close(std::move(writePipe));
}

inline Task<> recv_file(FileHandle &sock, FileHandle &&file) {
    auto [readPipe, writePipe] = co_await fs_pipe();
    while (auto n = co_await fs_splice(sock, writePipe, 65536)) {
        std::size_t m;
        while ((m = co_await fs_splice(readPipe, file, n)) < n) [[unlikely]] {
            n -= m;
        }
    }
    co_await fs_close(std::move(file));
    co_await fs_close(std::move(readPipe));
    co_await fs_close(std::move(writePipe));
}

} // namespace co_async
#endif
