#pragma once

#include <co_async/std.hpp>
#include <co_async/awaiter/task.hpp>
#include <co_async/utils/expected.hpp>

namespace co_async {

inline constexpr std::size_t kStreamBufferSize = 8192;

struct StreamRaw {
    virtual void raw_timeout(std::chrono::nanoseconds timeout) {}

    virtual Task<Expected<void, std::errc>> raw_seek(std::uint64_t pos) {
        co_return Unexpected{std::errc::invalid_seek};
    }

    virtual Task<Expected<void, std::errc>> raw_flush() {
        co_return {};
    }

    virtual Task<> raw_close() {
        co_return;
    }

    virtual Task<Expected<std::size_t, std::errc>>
    raw_read(std::span<char> buffer) {
        co_return Unexpected{std::errc::not_supported};
    }

    virtual Task<Expected<std::size_t, std::errc>>
    raw_write(std::span<char const> buffer) {
        co_return Unexpected{std::errc::not_supported};
    }

    StreamRaw &operator=(StreamRaw &&) = delete;

    virtual ~StreamRaw() = default;
};

struct BorrowedStream {
    BorrowedStream() : mRaw() {}

    explicit BorrowedStream(StreamRaw *raw) : mRaw(raw) {}

    virtual ~BorrowedStream() = default;

    BorrowedStream(BorrowedStream &&) = default;
    BorrowedStream &operator=(BorrowedStream &&) = default;

    Task<Expected<char, std::errc>> getchar() {
        if (bufempty()) {
            co_await co_await fillbuf();
        }
        char c = mInBuffer[mInIndex];
        ++mInIndex;
        co_return c;
    }

    Task<Expected<void, std::errc>> getline(std::string &s, char eol) {
        std::size_t start = mInIndex;
        while (true) {
            for (std::size_t i = start; i < mInEnd; ++i) {
                if (mInBuffer[i] == eol) {
                    s.append(mInBuffer.get() + start, i - start);
                    mInIndex = i + 1;
                    co_return {};
                }
            }
            s.append(mInBuffer.get() + start, mInEnd - start);
            co_await co_await fillbuf();
            start = 0;
        }
    }

    Task<Expected<void, std::errc>> dropline(char eol) {
        std::size_t start = mInIndex;
        while (true) {
            for (std::size_t i = start; i < mInEnd; ++i) {
                if (mInBuffer[i] == eol) {
                    mInIndex = i + 1;
                    co_return {};
                }
            }
            co_await co_await fillbuf();
            start = 0;
        }
    }

    Task<Expected<void, std::errc>> getline(std::string &s,
                                            std::string_view eol) {
    again:
        co_await co_await getline(s, eol.front());
        for (std::size_t i = 1; i < eol.size(); ++i) {
            if (bufempty()) {
                co_await co_await fillbuf();
            }
            char c = mInBuffer[mInIndex];
            if (eol[i] == c) [[likely]] {
                ++mInIndex;
            } else {
                s.append(eol.data(), i);
                goto again;
            }
        }
        co_return {};
    }

    Task<Expected<void, std::errc>> dropline(std::string_view eol) {
    again:
        co_await co_await dropline(eol.front());
        for (std::size_t i = 1; i < eol.size(); ++i) {
            if (bufempty()) {
                co_await co_await fillbuf();
            }
            char c = mInBuffer[mInIndex];
            if (eol[i] == c) [[likely]] {
                ++mInIndex;
            } else {
                goto again;
            }
        }
        co_return {};
    }

    Task<Expected<std::string, std::errc>> getline(char eol) {
        std::string s;
        co_await co_await getline(s, eol);
        co_return s;
    }

    Task<Expected<std::string, std::errc>> getline(std::string_view eol) {
        std::string s;
        co_await co_await getline(s, eol);
        co_return s;
    }

    Task<Expected<void, std::errc>> getspan(std::span<char> s) {
        auto p = s.data();
        auto n = s.size();
        std::size_t start = mInIndex;
        while (true) {
            auto end = start + n;
            if (end <= mInEnd) {
                p = std::copy(mInBuffer.get() + start, mInBuffer.get() + end,
                              p);
                mInIndex = end;
                co_return {};
            }
            p = std::copy(mInBuffer.get() + start, mInBuffer.get() + mInEnd, p);
            co_await co_await fillbuf();
            start = 0;
        }
    }

    Task<Expected<void, std::errc>> dropn(std::size_t n) {
        std::size_t start = mInIndex;
        while (true) {
            auto end = start + n;
            if (end <= mInEnd) {
                mInIndex = end;
                co_return {};
            }
            co_await co_await fillbuf();
            start = 0;
        }
    }

    Task<Expected<void, std::errc>> getn(std::string &s, std::size_t n) {
        while (true) {
            if (mInIndex + n <= mInEnd) {
                s.append(mInBuffer.get() + mInIndex, n);
                mInIndex += n;
                co_return {};
            }
            s.append(mInBuffer.get() + mInIndex, mInEnd - mInIndex);
            n -= mInEnd - mInIndex;
            co_await co_await fillbuf();
        }
    }

    Task<Expected<std::string, std::errc>> getn(std::size_t n) {
        std::string s;
        s.reserve(n);
        co_await co_await getn(s, n);
        co_return s;
    }

    Task<> getall(std::string &s) {
        std::size_t start = mInIndex;
        do {
            s.append(mInBuffer.get() + start, mInEnd - start);
            start = 0;
        } while (co_await fillbuf());
    }

    Task<std::string> getall() {
        std::string s;
        co_await getall(s);
        co_return s;
    }

    template <class T>
        requires std::is_trivial_v<T>
    Task<Expected<void, std::errc>> getstruct(T &ret) {
        return getspan(std::span<char>((char *)&ret, sizeof(T)));
    }

    template <class T>
        requires std::is_trivial_v<T>
    Task<Expected<T, std::errc>> getstruct() {
        T ret;
        co_await co_await getstruct(ret);
        co_return ret;
    }

    std::span<char const> peekbuf() const noexcept {
        return {mInBuffer.get() + mInIndex, mInEnd - mInIndex};
    }

    Task<Expected<char, std::errc>> peekchar() {
        if (bufempty()) {
            co_await co_await fillbuf();
        }
        co_return mInBuffer[mInIndex];
    }

    Task<Expected<void, std::errc>> peekn(std::string &s, std::size_t n) {
        while (mInEnd - mInIndex < n) {
            co_await co_await fillbuf();
        }
        s.append(mInBuffer.get() + mInIndex, n);
        co_return {};
    }

    Task<Expected<std::string, std::errc>> peekn(std::size_t n) {
        std::string s;
        co_await co_await peekn(s, n);
        co_return s;
    }

    void allocinbuf(std::size_t size = kStreamBufferSize) {
        mInBuffer = std::make_unique<char[]>(size);
        mInBufSize = size;
    }

    Task<Expected<void, std::errc>> fillbuf() {
        if (!mInBuffer) {
            allocinbuf();
        }
        mInIndex = 0;
        mInEnd = co_await co_await mRaw->raw_read(
            std::span(mInBuffer.get(), mInBufSize));
        if (mInEnd == 0) [[unlikely]] {
            co_return Unexpected{std::errc::broken_pipe};
        }
        co_return {};
    }

    bool bufempty() const noexcept {
        return mInIndex == mInEnd;
    }

    Task<Expected<void, std::errc>> putchar(char c) {
        if (buffull()) {
            co_await co_await flush();
        }
        mOutBuffer[mOutIndex] = c;
        ++mOutIndex;
        co_return {};
    }

    Task<Expected<void, std::errc>> putspan(std::span<char const> s) {
        auto p = s.data();
        auto const pe = s.data() + s.size();
    again:
        if (std::size_t(pe - p) <= mOutBufSize - mOutIndex) {
            auto b = mOutBuffer.get() + mOutIndex;
            mOutIndex += pe - p;
            while (p < pe) {
                *b++ = *p++;
            }
        } else {
            auto b = mOutBuffer.get() + mOutIndex;
            auto const be = mOutBuffer.get() + mOutBufSize;
            mOutIndex = mOutBufSize;
            while (b < be) {
                *b++ = *p++;
            }
            co_await co_await flush();
            mOutIndex = 0;
            goto again;
        }
        co_return {};
    }

    Task<Expected<void, std::errc>> puts(std::string_view s) {
        return putspan(std::span<char const>(s.data(), s.size()));
    }

    template <class T>
    Task<Expected<void, std::errc>> putstruct(T const &s) {
        return putspan(
            std::span<char const>((char const *)std::addressof(s), sizeof(T)));
    }

    Task<Expected<void, std::errc>> putline(std::string_view s) {
        co_await co_await puts(s);
        co_await co_await putchar('\n');
        co_return co_await flush();
    }

    void allocoutbuf(std::size_t size = kStreamBufferSize) {
        mOutBuffer = std::make_unique<char[]>(size);
        mOutBufSize = size;
    }

    Task<Expected<void, std::errc>> flush() {
        if (!mOutBuffer) {
            allocoutbuf();
            co_return {};
        }
        if (mOutIndex) [[likely]] {
            auto buf = std::span(mOutBuffer.get(), mOutIndex);
            auto len = co_await mRaw->raw_write(buf);
            while (len.has_value() && *len > 0 && *len != buf.size())
                [[unlikely]] {
                buf = buf.subspan(*len);
                len = co_await mRaw->raw_write(buf);
            }
            if (len.has_error()) [[unlikely]] {
                co_return Unexpected{len.error()};
            }
            if (*len == 0) [[unlikely]] {
                co_return Unexpected{std::errc::broken_pipe};
            }
            mOutIndex = 0;
            co_await co_await mRaw->raw_flush();
        }
        co_return {};
    }

    bool buffull() const noexcept {
        return mOutIndex == mOutBufSize;
    }

    StreamRaw &raw() const noexcept {
        return *mRaw;
    }

    template <std::derived_from<StreamRaw> DerivedRaw>
    DerivedRaw &raw() const {
        return dynamic_cast<DerivedRaw &>(*mRaw);
    }

    Task<> close() {
        return mRaw->raw_close();
    }

    Task<Expected<std::size_t, std::errc>> read(std::span<char> buffer) {
        if (!bufempty()) {
            auto n = std::min(mInEnd - mInIndex, buffer.size());
            std::memcpy(buffer.data(), mInBuffer.get() + mInIndex, n);
            mInIndex += n;
            co_return n;
        }
        co_return co_await mRaw->raw_read(buffer);
    }

    Task<Expected<std::size_t, std::errc>> write(std::span<char const> buffer) {
        if (!buffull()) {
            auto n = std::min(mInBufSize - mInIndex, buffer.size());
            co_await co_await putspan(buffer.subspan(0, n));
            co_return n;
        }
        co_return co_await mRaw->raw_write(buffer);
    }

    void timeout(std::chrono::nanoseconds timeout) {
        mRaw->raw_timeout(timeout);
    }

    Task<Expected<void, std::errc>> seek(std::uint64_t pos) {
        co_await co_await mRaw->raw_seek(pos);
        mInIndex = 0;
        mInEnd = 0;
        mOutIndex = 0;
        co_return {};
    }

private:
    std::unique_ptr<char[]> mInBuffer;
    std::size_t mInIndex = 0;
    std::size_t mInEnd = 0;
    std::size_t mInBufSize = 0;
    std::unique_ptr<char[]> mOutBuffer;
    std::size_t mOutIndex = 0;
    std::size_t mOutBufSize = 0;
    StreamRaw *mRaw;
};

struct OwningStream : BorrowedStream {
    explicit OwningStream() : BorrowedStream(), mRawUnique() {}

    explicit OwningStream(std::unique_ptr<StreamRaw> raw)
        : BorrowedStream(raw.get()),
          mRawUnique(std::move(raw)) {}

    std::unique_ptr<StreamRaw> releaseraw() noexcept {
        return std::move(mRawUnique);
    }

private:
    std::unique_ptr<StreamRaw> mRawUnique;
};

template <std::derived_from<StreamRaw> StreamRaw, class... Args>
OwningStream make_stream(Args &&...args) {
    return OwningStream(
        std::make_unique<StreamRaw>(std::forward<Args>(args)...));
}

} // namespace co_async
