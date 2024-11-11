#include <QCoreApplication>
#include <coroutine>
#include <iostream>
#include <QCoreApplication>
template <typename T> struct Coro;
template <typename T> struct Promise;
struct AwaitableBase;

struct AwaitableBase {
  virtual bool await_can_resume()=0;
};



struct PromiseBase {
  enum class State: uint8_t {
    Initial, // Initial state (std::monostate)
    Callee, // This coroutine was called inside another coroutine which must be resumed after this finishes
    CallerRoot, // This coroutine is the head of a nested call stack.
  };

  struct StateCallerRoot {
    StateCallerRoot(PromiseBase *promise, std::coroutine_handle<> handle): resumePromise(promise), resumeHandle(handle) { }
    PromiseBase *resumePromise;
    std::coroutine_handle<> resumeHandle;
  };
  struct StateCallee {
    StateCallee(PromiseBase *rootPromise, std::coroutine_handle<> callerHandle, std::optional<StateCallerRoot> previousCaller):
        rootPromise(rootPromise),
        callerHandle(callerHandle),
        previousCaller(previousCaller) { }
    PromiseBase *rootPromise;
    std::coroutine_handle<> callerHandle;
    std::optional<StateCallerRoot> previousCaller;
  };
  struct InitialAwaitable: public AwaitableBase {
    bool await_can_resume() override { return true; }
  };
  static InitialAwaitable g_initialAwait;
  AwaitableBase *m_awaiting=&g_initialAwait;
  std::variant<std::monostate, StateCallee, StateCallerRoot> m_state;
};
PromiseBase::InitialAwaitable PromiseBase::g_initialAwait;
struct CoroBase {

};



template <typename T> struct Awaitable: public AwaitableBase {
  bool await_ready() { return await_can_resume(); }
  template <typename U> auto await_suspend(std::coroutine_handle<Promise<U>> calling);
};

template <typename T> struct CoroAwaiter {
  CoroAwaiter(std::coroutine_handle<Promise<T>> coroutine): m_coroutine{coroutine} {

  }
  bool await_ready() const noexcept { return false; }
  template <typename U> auto await_suspend(std::coroutine_handle<Promise<U>> calling);
  T await_resume() const noexcept {
    return std::move(*m_coroutine.promise().m_value);
  }
  private:
  std::coroutine_handle<Promise<T>> m_coroutine;
};

template <typename T> struct Coro: public CoroBase {
  using promise_type=Promise<T>;
  explicit Coro(std::coroutine_handle<Promise<T>> handle);
  Coro(Coro<T> &&other): m_promise{other.m_promise} { other.m_promise=std::coroutine_handle<Promise<T>>{}; }
  Coro(const Coro<T> &)=delete;
  Coro<T> &operator=(const Coro<T> &)=delete;
  Coro<T> &operator=(Coro<T> &&other) { m_promise.destroy(); m_promise=other.m_promise; other.m_promise=std::coroutine_handle<Promise<T>>{}; }
  ~Coro() { m_promise.destroy(); }
  auto operator co_await() noexcept
  {
    return CoroAwaiter<T> { m_promise };
  }
  std::optional<T> resume();
  private:
  std::coroutine_handle<Promise<T>> m_promise;
};


template <typename T> struct Promise: public PromiseBase {
  Coro<T> get_return_object()
  {
    auto handle=std::coroutine_handle<Promise<T>>::from_promise(*this);
    return Coro { handle };
  }
  std::suspend_always initial_suspend() noexcept {
    // std::suspend_always
    // m_suspendedTask=std::coroutine_handle<promise_type>::from_promise(*this);
    // qDebug()<<"Initial suspend"<<this;
    return {};
  }
  auto final_suspend() noexcept; // TODO: noexcept
  void unhandled_exception() noexcept { }
  void return_value(T value)  {
    m_value.emplace(std::move(value));
  }
  // ~Promise() {
  //   // qDebug()<<"Destroy promise";
  // }
  std::optional<T> m_value;
};

template<typename T>
Coro<T>::Coro(std::coroutine_handle<Promise<T>> handle): m_promise{handle}
{

}


struct CalleData {
  PromiseBase *m_root;
  std::coroutine_handle<> caller;
};

template<typename T> template <typename U> auto CoroAwaiter<T>::await_suspend(std::coroutine_handle<Promise<U>> caller) {
  // qDebug()<<"Suspending"<<&caller.promise()<<"to call"<<&m_coroutine.promise();
  auto &calleeState=m_coroutine.promise().m_state;
  auto calleeStateIndex=static_cast<PromiseBase::State>(calleeState.index());
  if(calleeStateIndex==PromiseBase::State::Initial) {
    PromiseBase *rootCaller=&caller.promise();
    if(static_cast<PromiseBase::State>(rootCaller->m_state.index())==PromiseBase::State::Callee)
      rootCaller=std::get<static_cast<int>(PromiseBase::State::Callee)>(rootCaller->m_state).rootPromise;
    std::optional<PromiseBase::StateCallerRoot> m_previousCaller;
    if(auto *oldRootCaller=std::get_if<static_cast<int>(PromiseBase::State::CallerRoot)>(&rootCaller->m_state))
      m_previousCaller.emplace(std::move(*oldRootCaller));
    calleeState.template emplace<static_cast<int>(PromiseBase::State::Callee)>(rootCaller, caller, m_previousCaller);
    rootCaller->m_state.template emplace<static_cast<int>(PromiseBase::State::CallerRoot)>(&m_coroutine.promise(), m_coroutine);
    return m_coroutine;
  }
  else {
    throw "Callee is not in initial state";
  }
}

template<typename T> struct FinalAwaiter {
  bool await_ready() const noexcept { return false; }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise<T>> calling) noexcept {
    PromiseBase &exitingPromise=calling.promise();
    if(PromiseBase::StateCallee *callee=std::get_if<static_cast<int>(PromiseBase::State::Callee)>(&calling.promise().m_state)) {
      if(callee->previousCaller) {
        callee->rootPromise->m_state.emplace<static_cast<int>(PromiseBase::State::CallerRoot)>(*callee->previousCaller);
        // qDebug()<<"Return to previous";
      }
      else {
        callee->rootPromise->m_state.emplace<static_cast<int>(PromiseBase::State::Initial)>();
        // qDebug()<<"Return to root";
      }
      auto retHandle=callee->callerHandle;

      calling.promise().m_state.template emplace<static_cast<int>(PromiseBase::State::Initial)>();
      return retHandle;
    }
    else
      return std::noop_coroutine();
  };
  void await_resume() const noexcept {
    // qDebug()<<"Final awaiter resume";
  }
};

template<typename T>
auto Promise<T>::final_suspend() noexcept {
  return FinalAwaiter<T>{};
}

template<typename T> template<typename U>
auto Awaitable<T>::await_suspend(std::coroutine_handle<Promise<U> > calling)
{
  calling.promise().m_awaiting=this;
}

template <typename T> std::optional<T> Coro<T>::resume() {
  if(m_promise.address() && !m_promise.done()) {
    PromiseBase *promise=&m_promise.promise();
    std::coroutine_handle<> resumeHandle=m_promise;
    if(PromiseBase::StateCallerRoot *rootData=std::get_if<static_cast<int>(PromiseBase::State::CallerRoot)>(&m_promise.promise().m_state)) {
      // qDebug()<<"Nested from root";
      promise=rootData->resumePromise;
      resumeHandle=rootData->resumeHandle;
    }
    if(promise->m_awaiting!=nullptr) {
      // qDebug()<<"Awaiting";
      if(promise->m_awaiting->await_can_resume()) {
        resumeHandle.resume();
        if(m_promise.done()) {
          // qDebug()<<"Promise done!";
          if constexpr(!std::is_void_v<T>) {
            return std::move(m_promise.promise().m_value);
          }
        }
      }
    }
    else {
      qDebug()<<"We are not sure what the coroutine is awaiting. Can't resume";
    }
  }
  else
    qDebug()<<"Coroutine ended";
  return {};
}

struct TestStruct {
  TestStruct(int value): m_value(value) { /*qDebug()<<"Construct TestStruct"<<value;*/ }
  TestStruct(const TestStruct &)=delete;
  TestStruct(TestStruct &&value): m_value(value.m_value) { value.m_value=-1; /*qDebug()<<"Construct TestStruct move"<<m_value;*/ }
  TestStruct &operator =(const TestStruct &)=delete;
  TestStruct &operator =(TestStruct &&other) { m_value=other.m_value; other.m_value=-1; /*qDebug()<<"Assign TestStruct move"<<m_value;*/ return *this; }
  ~TestStruct() { /*qDebug()<<"Destroy TestStruct"<<m_value;*/ }
  int value() const { return m_value; }
  private:
  int m_value=-1;
};

struct TestAwaitable: public Awaitable<TestStruct> {
  TestAwaitable(unsigned numSleep=0): numSleep(numSleep) { }
  unsigned numSleep;
  bool await_ready() { return (numSleep==0); }
  bool await_can_resume() override { return numSleep==0 || (--numSleep)==0; }
  TestStruct await_resume() {
    return TestStruct{15};
  }
};


struct TestAwaitable2 {
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> calling) { }
  void await_resume() {

  }
};

Promise<int> *task=nullptr;

Coro<TestStruct> testTask2() {
  qDebug()<<"*** Enter task 2";
  co_await TestAwaitable2{};
  qDebug()<<"*** In task 2";
  auto result=co_await TestAwaitable{1};
  qDebug()<<"*** End task 2";
  co_return result;
}


Coro<int> testTask1() {
  qDebug()<<"*** In task 1";
  auto x=co_await testTask2();
  qDebug()<<"*** End task 1"<<x.value();
  co_return 4;
}

Coro<TestStruct> testTask0() {
  qDebug()<<"*** In task 0";
  co_await testTask1();
  qDebug()<<"*** End task 0";
  co_return 13;
}

static int test()
{
  auto x=testTask0();
  qDebug()<<sizeof(x)<<sizeof(Promise<int>)<<sizeof(Promise<int>::m_state)<<sizeof(std::coroutine_handle<>);
  // task=&x.m_promise.promise();
  // qDebug()<<"Task 0 address="<<x.m_promise.address();
  x.resume();
  qDebug()<<"----- Resume 2";
  x.resume();
  qDebug()<<"Resume 3";
  auto y=x.resume();
  if(y) {
    qDebug()<<"Has value!"<<y->value();
  }
  qDebug()<<"Resume 4";
  y=x.resume();
  if(y) {
    qDebug()<<"Has value!"<<y->value();
  }
}
