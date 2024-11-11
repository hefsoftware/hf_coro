#pragma
#include <QCoreApplication>
#include <coroutine>
#include <optional>
#include <functional>
struct CoroException: public std::exception {

};

struct CoroAwaiterFinalSuspend {
  CoroAwaiterFinalSuspend(std::coroutine_handle<> resume): m_resume(resume) { }
  bool await_ready() const noexcept { return false; }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> calling) noexcept {
    return m_resume;
  };
  void await_resume() const noexcept {

  }
private:
  std::coroutine_handle<> m_resume;

};

template <typename T> struct PromiseBasic;

template <typename T> struct CoroBasicAwaiter {
  CoroBasicAwaiter(std::coroutine_handle<PromiseBasic<T>> coroutine): m_coroutine{coroutine} {

  }
  bool await_ready() const noexcept { return false; }
  auto await_suspend(std::coroutine_handle<> calling) {
    m_coroutine.promise().m_terminatedCall=[calling](T &value){
      return calling;
    };
    return m_coroutine;
  }
  T await_resume() const noexcept {
    T ret{std::move(*m_coroutine.promise().m_value)};
    m_coroutine.destroy();
    return ret;
  }
private:
  std::coroutine_handle<PromiseBasic<T>> m_coroutine;
};

template <typename T> struct CoroBasicBase {
  using promise_type=PromiseBasic<T>;
  CoroBasicBase(std::coroutine_handle<promise_type> handle): m_promise {handle} { }
  CoroBasicBase(CoroBasicBase<T> &&other): m_promise{other.m_promise} { other.m_promise=std::coroutine_handle<promise_type>{}; }
  CoroBasicBase(const CoroBasicBase<T> &)=delete;
  CoroBasicBase<T> &operator=(const CoroBasicBase<T> &)=delete;
  CoroBasicBase<T> &operator=(CoroBasicBase<T> &&other) { m_promise.destroy(); m_promise=other.m_promise; other.m_promise=std::coroutine_handle<promise_type>{}; }
  explicit operator bool () { return m_promise && !m_promise.done(); }
  auto operator co_await()
  {
    if(!*this)
      throw CoroException{};
    std::coroutine_handle<promise_type> promise;
    std::swap(promise, m_promise);
    return CoroBasicAwaiter<T> { promise };
  }
  ~CoroBasicBase() {
    if(m_promise)
      m_promise.destroy();
  }
protected:
  std::coroutine_handle<promise_type> m_promise;
};

template <typename T> struct CoroBasic: public CoroBasicBase<T> {
  bool detach(const std::function<void(T &)> &terminated);
};

template <> struct CoroBasic<void>: public CoroBasicBase<void> {
  bool detach(const std::function<void()> &terminated);
};

template <typename T> struct PromiseBasicBase {
  void unhandled_exception() noexcept { }

  std::suspend_always initial_suspend() noexcept {
    return {};
  }
};

template <typename _T> struct PromiseBasicTerminateCallback {
  using T=std::function<std::coroutine_handle<>(_T &)>;
};

template <> struct PromiseBasicTerminateCallback<void> {
  using T=std::function<std::coroutine_handle<>()>;
};

template <typename T> struct PromiseBasic: public PromiseBasicBase<T> {
  CoroBasic<T> get_return_object() { return CoroBasic<T>{std::coroutine_handle<PromiseBasic<T>>::from_promise(*this)}; }
  CoroAwaiterFinalSuspend final_suspend() noexcept {
    return CoroAwaiterFinalSuspend{m_terminatedCall?m_terminatedCall(*m_value):std::noop_coroutine()};
  }
  void return_value(T &&value)  {
    m_value.emplace(std::move(value));
  }

  std::optional<T> m_value;
  PromiseBasicTerminateCallback<T>::T m_terminatedCall;
};

template <> struct PromiseBasic<void>: public PromiseBasicBase<void> {
  CoroBasic<void> get_return_object() { return CoroBasic<void>{std::coroutine_handle<PromiseBasic<void>>::from_promise(*this)}; }
  CoroAwaiterFinalSuspend final_suspend() noexcept {
    return CoroAwaiterFinalSuspend{m_terminatedCall?m_terminatedCall():std::noop_coroutine()};
  }
  void return_void()  {
  }
  std::function<std::coroutine_handle<>()> m_terminatedCall;
};

template <typename T> bool CoroBasic<T>::detach(const std::function<void(T &)> &terminated) {
  bool ret=false;
  auto &promise=this->m_promise;
  if(promise && !promise.done()) {
    promise.promise().m_terminatedCall=[terminated](T &value)->std::coroutine_handle<> { terminated(value); return std::noop_coroutine(); } ;
    promise.resume();
  }
  return ret;
}

bool CoroBasic<void>::detach(const std::function<void ()> &terminated) {
  bool ret=false;
  auto &promise=this->m_promise;
  if(promise && !promise.done()) {
    promise.promise().m_terminatedCall=[terminated]()->std::coroutine_handle<> { terminated(); return std::noop_coroutine(); } ;
    promise.resume();
  }
  return ret;
}
