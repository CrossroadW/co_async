#pragma once /*{export module co_async:threading.basic_loop;}*/

#include <cmake/clang_std_modules_source/std.hpp>/*{import std;}*/
#include <co_async/awaiter/concepts.hpp>         /*{import :awaiter.concepts;}*/
#include <co_async/awaiter/details/auto_destroy_promise.hpp>/*{import :awaiter.details.auto_destroy_promise;}*/
#include <co_async/awaiter/task.hpp>/*{import :awaiter.task;}*/

namespace co_async {

struct BasicLoop {
    void run() {
        while (!mQueue.empty()) {
            auto coroutine = mQueue.front();
            mQueue.pop_front();
            coroutine.resume();
        }
    }

    void push(std::coroutine_handle<> coroutine) {
        mQueue.push_back(coroutine);
    }

    BasicLoop() = default;
    BasicLoop(BasicLoop &&) = delete;

private:
    std::deque<std::coroutine_handle<>> mQueue;
};

template <Awaitable A>
inline Task<void, AutoDestroyPromise> taskEnqueueHelper(A a) {
    co_return co_await std::move(a);
}

template <class A>
    requires(!Awaitable<A> && std::invocable<A> &&
             Awaitable<std::invoke_result_t<A>>)
inline Task<void, AutoDestroyPromise> taskEnqueueHelper(A a) {
    return taskEnqueueHelper(std::invoke(std::move(a)));
}

inline void loop_enqueue(BasicLoop &loop, auto task) {
    auto t = taskEnqueueHelper(std::move(task));
    std::coroutine_handle<AutoDestroyPromise> coroutine = t;
    loop.push(coroutine);
    t.release();
}

} // namespace co_async
