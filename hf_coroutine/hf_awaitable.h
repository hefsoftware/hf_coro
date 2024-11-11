#pragma once
#include <QCoreApplication>
#include <optional>
#include <coroutine>
#include "hf_common.h"
// #include <chrono>
// #include <QTimer>

// using namespace std::literals::chrono_literals;
namespace HfCoro {
template <typename T> struct AwaitableBase;
namespace Helper {
  struct AwaitableBaseCore;
}
struct AwaitableTerminator
{
  friend class Helper::AwaitableBaseCore;
  private:
  using AwaitableBaseCore=Helper::AwaitableBaseCore;
  protected:
  enum class Suspend {
    Terminate, ///< @brief There was a request to terminate the coroutine. Nobody will be interested in results.
    NoWait, ///< @brief We don't want to suspend. However the coroutine is still interested in any results, if immediately available.
    Wait ///< @brief Coroutine will suspend while waiting for result.
  };
  AwaitableTerminator() { }
  AwaitableTerminator &operator =(const AwaitableTerminator &other)=delete;
  // AwaitingBase &operator =(AwaitingBase &&other) {
  //   m_awaiter=std::exchange(other.m_value, nullptr);
  //   return *this;
  // }
  protected:
  virtual void startWait() {

  }
  virtual void endWait() {

  }
  virtual Suspend noWait()=0;
  void terminate(bool noResult);
  private:
  Helper::AwaitableBaseCore *m_awaitable=nullptr;

};

template <typename T> struct AwaitableBase;
namespace Helper {
struct AwaitableBaseCore {
  friend class HfCoro::AwaitableTerminator;
  template <typename T> friend struct HfCoro::AwaitableBase;
  protected:
  /**
   * @brief Resets the awaitable value (if needed) before doing an await on it again.
   */
  virtual void reset() { }
  /**
   * @brief Called when the object is about to be awaited.
   *
   * The behaviour should depend on value of zeroAwait. If the paramter is:
   * - True awaitable must not wait and just returning any result that is immediately available (through returnResult). endAwait will not be called.
   * - False awaitable can still result immediately a result (in which case the coroutine will not suspend). Otherwise the coroutine will suspend
   * until either the awaitable returns a result through returnResult or the waiting is terminated (in this case endAwait will be called).
   * @param zeroAwait Awaitable should not actually wait but should return any result that is immediately available. See description.
   * Can call returnResult() if a result is immediately available
   */
  virtual void startAwait(bool zeroAwait) { }
  /**
   * @brief Called when the await on the object is about to terminate.
   *
   * Will not be called if returnResult was called.
   * The implementation can still call returnResult() if the awaitable wants to publish a result with the temporary data it has received so far.
   * @param noResult If true the corouting will not be interested in the result (we are terminating). Awaitable can avoid to generate a result in this case.
   */
  virtual void endAwait(bool noResult) { }
  void gotResult() {
    switch(m_state) {
    case State::AwaitReady:
    case State::WaitResumeWithoutResult:
    case State::WaitResumeWithResult:
      m_state=State::WaitResumeWithResult;
      break;
    case State::Suspended:
      m_state=State::WaitResumeWithResult;
      if(!m_terminator->m_awaitable)
        throw CoroException{};
      m_terminator->endWait();
      m_terminator->m_awaitable=nullptr;
      m_suspended.resume();
      break;
    default:
      throw CoroException{};
    }
  }
  private:
  AwaitableTerminator *m_terminator=nullptr;
  // std::optional<T> m_value;
  std::coroutine_handle<> m_suspended;
  enum class State {
    Init,  ///< @brief Initial state. Waiting for await_ready to be called.
    WithResult, ///< @brief Same as initial but we have a result.
    AwaitReady, ///< @brief Coroutine called await_ready to check if it should suspend.
    AwaitSuspend, ///< @brief Waiting for await_suspend to be called.
    Suspended, ///< @brief Waiting for a result (Awaitable::returnResult) or a termination (AwaitingBase::terminate)
    WaitResumeWithResult, ///< @brief Waiting for resume of the corouting with a result
    WaitResumeWithoutResult, ///< @brief Waiting for resume of the corouting without a result
  };
  State m_state=State::Init;
  public:
  AwaitableBaseCore(AwaitableTerminator *terminator): m_terminator{terminator} { }
  bool await_ready() {
    bool ret=false;
    if(m_state!=State::Init && m_state!=State::WithResult)
      throw CoroException{};
    else {
      if(m_state==State::WithResult) {
        reset();
        m_state=State::Init;
      }
      auto suspend=m_terminator?m_terminator->noWait():AwaitableTerminator::Suspend::Wait;
      if(suspend==AwaitableTerminator::Suspend::Terminate)
        ret=true;
      else {
        m_state=State::AwaitReady;
        // Check if awaitable is already in use, in this case attach it to this.
        if(m_terminator && m_terminator->m_awaitable)
          throw CoroException{};
        startAwait(suspend==AwaitableTerminator::Suspend::NoWait);
        if(m_state==State::WaitResumeWithResult) {
          // Awaitable::startAwait called gotResult();
          ret=true;
        }
        else if(suspend==AwaitableTerminator::Suspend::NoWait) {
          // we should not suspend
          m_state=State::WaitResumeWithoutResult;
          ret=true;
        }
        else {
          if(m_terminator) {
            m_terminator->m_awaitable=this;
            m_terminator->startWait();
            // Note: start wait may be calling terminate which will move the state to WaitResumeWithResult (and possibly call endAwait in the process) or WaitResumeWithoutResult
            if(m_state==State::WaitResumeWithoutResult || m_state==State::WaitResumeWithResult) {
              // Awaiting::startWait called terminate();
              m_terminator->m_awaitable=nullptr;
              ret=true;
            }
            else
              m_state=State::AwaitSuspend;
          }
          else
            m_state=State::AwaitSuspend;
        }
      }
    }
    return ret;
  }
  void await_suspend(std::coroutine_handle<> suspended) {
    m_suspended=suspended;
    if(m_state!=State::AwaitSuspend)
      throw CoroException{};
    m_state=State::Suspended;
  }
};
}

void AwaitableTerminator::terminate(bool noResult) {
  if(!m_awaitable)
    throw CoroException{};
  else {
    switch(m_awaitable->m_state) {
    case AwaitableBaseCore::State::AwaitReady:
      m_awaitable->m_state=AwaitableBaseCore::State::WaitResumeWithoutResult;
      m_awaitable->endAwait(noResult);
      break;
    case AwaitableBaseCore::State::Suspended:
    {
      m_awaitable->m_state=AwaitableBaseCore::State::WaitResumeWithoutResult;
      m_awaitable->endAwait(noResult);
      auto suspended=m_awaitable->m_suspended;
      m_awaitable=nullptr;
      suspended.resume();
    }
    break;
    case AwaitableBaseCore::State::WaitResumeWithResult:
      // Ignores terminate if we already have a result.
      break;
    default:
      throw CoroException{};
    }
  }
}

template <typename T> struct AwaitableBase: public Helper::AwaitableBaseCore {
  AwaitableBase(AwaitableTerminator *terminator): Helper::AwaitableBaseCore{terminator} { }

  T &await_resume() {
    if(m_state==State::WaitResumeWithResult)
      m_state=State::WithResult;
    else if(m_state==State::WaitResumeWithoutResult)
      m_state=State::Init;
    else
      throw CoroException{};
    return value();
  }
  protected:
  virtual T &value()=0;
};

template <> struct AwaitableBase<void>: public Helper::AwaitableBaseCore {
  AwaitableBase(AwaitableTerminator *terminator): Helper::AwaitableBaseCore{terminator} { }

  void await_resume() {
    if(m_state==State::WaitResumeWithResult)
      m_state=State::WithResult;
    else if(m_state==State::WaitResumeWithoutResult)
      m_state=State::Init;
    else
      throw CoroException{};
  }
};

template <typename T> requires (!std::is_void_v<T>) struct AwaitableEasy: public AwaitableBase<std::optional<T>> {
  AwaitableEasy(AwaitableTerminator *terminator): AwaitableBase<std::optional<T>>{terminator} { }
  std::optional<T> &value() override { return m_value; }
  protected:
  void returnResult(T &&value) {
    m_value.emplace(std::move(value));
    this->gotResult();
  }
  void resetValue() {
    m_value.reset();
  }
  private:
  std::optional<T> m_value;
};


// template <typename T> struct QtAwaiter: public AwaitingBase<T> {
//   QtAwaiter(AwaitableBase<T> *awaitable): AwaitingBase<T>(awaitable) {
//     qDebug()<<"Qt"<<awaitable;
//   }
//   std::optional<T> await_resume() { return std::move(this->m_value); }
// };
// public:


//   QtAwaiter<T> &setTimeout(std::chrono::milliseconds timeOut) {
//     // qDebug()<<"Changed timeout"<<timeOut;
//     // m_timeout=timeOut;
//     return *this;
//   }
// protected:
//   void returnValue(T &&value) {
//     m_value.emplace(std::move(value));
//   }
// private:
//   // void timeoutOccoured() {
//   //   qDebug()<<"Timeout"<<(bool)m_suspended;
//   //   qDebug()<<"Timeout"<<(bool)m_suspended.done();
//   //   m_suspended.resume();
//   // }
//   std::optional<T> m_value;
//   std::coroutine_handle<> m_suspended;
//   std::optional<std::chrono::milliseconds> m_timeout;
//   QObject *m_timerConnection=nullptr;
// };

// template <typename T> struct AwaitableRet {
//   AwaitableRet(AwaitableBase<T> *awaiter): m_awaiter(awaiter) { }
//   AwaitableRet(AwaitableRet &&other): m_awaiter{other.takeAwaitable()} { }
//   AwaitableRet(const AwaitableRet &other)=delete;
//   AwaitableRet &operator =(const AwaitableRet &other)=delete;
//   AwaitableRet &operator =(AwaitableRet &&other) {
//     m_awaiter=std::exchange(other.m_value, nullptr);
//     return *this;
//   }
//   QtAwaiter<T> operator co_await() {
//     return QtAwaiter<T>{takeAwaitable()};
//   }
//   private:
//   constexpr AwaitableBase<T> *takeAwaitable() {
//     return std::exchange(m_awaiter, nullptr);
//   }
//   AwaitableBase<T> *m_awaiter;
// };
}
