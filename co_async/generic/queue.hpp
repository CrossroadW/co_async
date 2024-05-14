#pragma once

#include <co_async/std.hpp>
#include <co_async/awaiter/task.hpp>
#include <co_async/generic/condition_variable.hpp>
#include <co_async/utils/non_void_helper.hpp>
#include <co_async/utils/cacheline.hpp>

namespace co_async {

template <class T, std::size_t Capacity = 0>
struct Queue {
private:
    ConcurrentQueue<T, Capacity> mQueue;
    ConditionVariable mNonFull;
    ConditionVariable mNonEmpty;

public:
    Task<> push(T value) {
        while (!mQueue.push(std::move(value))) {
            co_await mNonEmpty;
        }
        mNonFull.notify_one();
    }

    Task<T> pop(T value) {
        while (true) {
            if (auto value = mQueue.pop()) {
                mNonEmpty.notify_one();
                co_return std::move(*value);
            }
            co_await mNonFull;
        }
    }
};

template <class T, std::size_t Capacity = 0>
struct TimedQueue {
private:
    ConcurrentQueue<T, Capacity> mQueue;
    TimedConditionVariable mNonEmpty;
    TimedConditionVariable mNonFull;

public:
    Task<> push(T value) {
        while (!mQueue.push(std::move(value))) {
            co_await mNonFull.wait();
        }
        mNonEmpty.notify_one();
        co_return;
    }

    Task<Expected<>> push(T value,
                          std::chrono::steady_clock::duration timeout) {
        return push(std::move(value),
                    std::chrono::steady_clock::now() + timeout);
    }

    Task<Expected<>> push(T value,
                          std::chrono::steady_clock::time_point expires) {
        while (!mQueue.push(std::move(value))) {
            if (auto e = co_await mNonFull.wait(expires); e.has_error()) {
                co_return Unexpected{e.error()};
            }
        }
        mNonEmpty.notify_one();
        co_return;
    }

    Task<T> pop() {
        while (true) {
            if (auto value = mQueue.pop()) {
                mNonFull.notify_one();
                co_return std::move(*value);
            }
            co_await mNonEmpty.wait();
        }
    }

    Task<Expected<T>> pop(std::chrono::steady_clock::duration timeout) {
        return pop(std::chrono::steady_clock::now() + timeout);
    }

    Task<Expected<T>> pop(std::chrono::steady_clock::time_point expires) {
        while (true) {
            if (auto value = mQueue.pop()) {
                mNonFull.notify_one();
                co_return std::move(*value);
            }
            if (auto e = co_await mNonEmpty.wait(expires); e.has_error()) {
                co_return Unexpected{e.error()};
            }
        }
    }
};

} // namespace co_async
