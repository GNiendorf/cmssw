#ifndef RecoTracker_LSTCore_interface_LSTTask_h
#define RecoTracker_LSTCore_interface_LSTTask_h

#include <coroutine>
#include <utility>

namespace lst {

  // Eager coroutine for an LST stage that reads device counts on the host: its sync points block in the synchronous
  // mode (callers may ignore the task), and suspend it in the asynchronous mode until the queue has reached them.
  class LSTTask {
  public:
    struct promise_type {
      std::coroutine_handle<> continuation_ = std::noop_coroutine();

      LSTTask get_return_object() { return LSTTask{std::coroutine_handle<promise_type>::from_promise(*this)}; }
      std::suspend_never initial_suspend() noexcept { return {}; }
      struct FinalAwaiter {
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> finishing) noexcept {
          return finishing.promise().continuation_;  // the awaiting stage, or back to the resumer
        }
        void await_resume() noexcept {}
      };
      FinalAwaiter final_suspend() noexcept { return {}; }
      void return_void() noexcept {}
      void unhandled_exception() { throw; }  // to the caller or the resumer
    };

    LSTTask() = default;
    explicit LSTTask(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
    LSTTask(LSTTask&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    LSTTask& operator=(LSTTask&& other) noexcept {
      if (this != &other) {
        reset();
        handle_ = std::exchange(other.handle_, {});
      }
      return *this;
    }
    LSTTask(LSTTask const&) = delete;
    LSTTask& operator=(LSTTask const&) = delete;
    ~LSTTask() { reset(); }

    bool valid() const { return static_cast<bool>(handle_); }
    bool done() const { return !handle_ || handle_.done(); }
    void reset() {
      if (handle_)
        handle_.destroy();
      handle_ = {};
    }

    // co_await of a sub-stage: continue at once if it finished, otherwise when it finishes
    bool await_ready() const noexcept { return done(); }
    void await_suspend(std::coroutine_handle<> awaiting) noexcept { handle_.promise().continuation_ = awaiting; }
    void await_resume() const noexcept {}

  private:
    std::coroutine_handle<promise_type> handle_;
  };

}  // namespace lst

#endif
