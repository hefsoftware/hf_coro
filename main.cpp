#include <QCoreApplication>
// #include <coroutine>
// #include <iostream>

#include "hf_coroutine/hf_coroutine.h"
#include "hf_coroutine/hf_awaitable.h"
#include <catch2/catch_test_macros.hpp>
// struct TestStruct2 {
//   int m_value;
//   int value() const { return m_value; }
// };
// struct TestStruct {
//   TestStruct(int value): m_value(value) { qDebug()<<"Construct TestStruct"<<value; }
//   TestStruct(const TestStruct &)=delete;
//   TestStruct(TestStruct &&value): m_value(value.m_value) { value.m_value=-1; qDebug()<<"Construct TestStruct move"<<m_value; }
//   TestStruct &operator =(const TestStruct &)=delete;
//   TestStruct &operator =(TestStruct &&other) {
//     m_value=other.m_value; other.m_value=-1;
//     qDebug()<<"Assign TestStruct move"<<m_value;
//     return *this;
//   }
//   ~TestStruct() { qDebug()<<"Destroy TestStruct"<<m_value; }
//   int value() const { return m_value; }
// private:
//   int m_value=-1;
// };

// static CoroBasic<TestStruct2> testTask1() {
//   qDebug()<<"In coroutine 1";
//   co_return TestStruct2{6};
// }

// static CoroBasic<int> testTask0() {
//   qDebug()<<"In coroutine";
//   co_return (co_await testTask1()).value();
//   // co_return 4;
// }


// static CoroBasic<void> testTask1_void() {
//   qDebug()<<"Task1 void";
//   co_return;
// }

// static CoroBasic<void> testTask0_void() {
//   qDebug()<<"Task 0 void";
//   co_await testTask1();
//   // co_return 4;
// }

// int main(int argc, const char *argv[]) {
//   auto x=testTask0();
//   x.detach([](int v) { qDebug()<<"Coroutine returned"<<v; });
//   // x.detach([]() { qDebug()<<"Coroutine returned"; });
//   qDebug()<<"End";
//   return 4;
// }


// enum class TestAType {
//   Terminate, // Terminate immediately without a result
//   ZeroAwaitNoResult, // Zero await but no result
//   ZeroAwaitResult, // Zero await with result
//   StopAwaitingStarted, // Gracefully stops on awaiting::started()
//   TerminateAwaitingStarted, // Stops without result on awaiting::started()
//   SuspendNoValue // Suspends, a manual result or terminate will be generated.
//   SuspendWithValue // Suspends, a manual result or terminate will be generated.
// };
int numPromises=0;

struct TestAwaitingA: public HfCoro::AwaitableTerminator {
  enum class Type {
    Terminate, // noWait will return Suspend::Terminate
    NoWait, // noWait will return Suspend::NoWait
    Wait_TerminateStart, // noWait will return Suspend::Wait but starting will terminate (not ask for result)
    Wait_StopStart, // noWait will return Suspend::Wait but starting will stop (ask for result)
    Wait, // noWait will return Suspend::Wait
    Wait_TerminateEnd // noWait will return Suspend::Wait but ending will terminate (not ask for result)
  };

  TestAwaitingA(Type type): type(type) { }
  Type type;
  unsigned numNoWait=0, numStartWait=0, numEndWait=0;
  void manualTerminate(bool noResult) {
    terminate(noResult);
  }
  void reset(Type type) {
    this->type=type;
    numNoWait=numStartWait=numEndWait=0;
  }
  // AwaitingBase interface
  protected:
  void startWait() override {
    numStartWait++;
    switch(type) {
    case Type::Wait_StopStart:
      terminate(false);
      break;
    case Type::Wait_TerminateStart:
      terminate(true);
      break;
    default:
      break;
    }
  }
  void endWait() override {
    numEndWait++;
    switch(type) {
    case Type::Wait_TerminateEnd:
      terminate(true);
      break;
    default:
      break;
    }
  }
  Suspend noWait() override {
    numNoWait++;
    switch(type) {
    case Type::Terminate:
      return Suspend::Terminate;
    case Type::NoWait:
      return Suspend::NoWait;
    default:
      return Suspend::Wait;
    }
  }
};

struct TestAwaitableA: public HfCoro::AwaitableEasy<int> {
  enum class Type {
    StartWait_Result, // startWait will generate a result
    NoResult, // startWait and endWait will not generate a result
    EndWait_Result, // endWait will generate a result (but only when noResult is false)
  };
  TestAwaitableA(Type type, TestAwaitingA *awaiting): AwaitableEasy<int>{awaiting}, type(type) {
  }
  Type type;
  unsigned numStartWait=0, numEndWait=0, numDoReturn=0, numReset=0;
  void manualSetValue(int value) {
    doReturn(std::move(value));
  }
  void reset(Type type) {
    this->type=type;
    numStartWait=numEndWait=numDoReturn=numReset=0;
  }
  // AwaitableBase interface
  protected:
  void reset() override { numReset++; }


  void startAwait(bool zeroAwait) override {
    numStartWait++;
    switch(type) {
    case Type::StartWait_Result:
      doReturn(5);
      break;
    default:
      break;
    }
  }
  void endAwait(bool noResult) override {
    numEndWait++;
    switch(type) {
    case Type::EndWait_Result:
      if(!noResult)
        doReturn(8);
      break;
    default:
      break;
    }
  }
  private:
  void doReturn(int &&value) {
    numDoReturn++;
    returnResult(std::move(value));
  }
};



CoroBasic<std::optional<int>> testA(TestAwaitableA &awaiting) {
  auto result=co_await awaiting;
  co_return result;
}

TEST_CASE( "Terminate immediately", "" ) {
  TestAwaitingA awaiting(TestAwaitingA::Type::Terminate);
  TestAwaitableA awaitable(TestAwaitableA::Type::StartWait_Result, &awaiting);
  bool finished=false;
  std::optional<int> value;
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==0);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==0);
  REQUIRE(finished);
  REQUIRE(!value);
  // Check we can await again
  finished=false;
  value.reset();
  awaiting.reset(TestAwaitingA::Type::NoWait);
  awaitable.reset(TestAwaitableA::Type::StartWait_Result);
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==5);
  REQUIRE(numPromises==0);
}

TEST_CASE( "Zero await, no result", "" ) {
  TestAwaitingA awaiting(TestAwaitingA::Type::NoWait);
  TestAwaitableA awaitable(TestAwaitableA::Type::NoResult, &awaiting);
  bool finished=false;
  std::optional<int> value;
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==0);
  REQUIRE(finished);
  REQUIRE(!value);

  // Check we can await again
  finished=false;
  value.reset();
  awaiting.reset(TestAwaitingA::Type::NoWait);
  awaitable.reset(TestAwaitableA::Type::StartWait_Result);
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==5);
}

TEST_CASE( "Zero await, result", "" ) {
  TestAwaitingA awaiting(TestAwaitingA::Type::NoWait);
  TestAwaitableA awaitable(TestAwaitableA::Type::StartWait_Result, &awaiting);
  bool finished=false;
  std::optional<int> value;
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==5);
  // Check we can await again
  finished=false;
  value.reset();
  awaiting.reset(TestAwaitingA::Type::NoWait);
  awaitable.reset(TestAwaitableA::Type::StartWait_Result);
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==1);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==5);
}

TEST_CASE( "Wait, result on start", "" ) {
  TestAwaitingA awaiting(TestAwaitingA::Type::Wait);
  TestAwaitableA awaitable(TestAwaitableA::Type::StartWait_Result, &awaiting);
  bool finished=false;
  std::optional<int> value;
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==5);

  // Check we can await again
  finished=false;
  value.reset();
  awaiting.reset(TestAwaitingA::Type::NoWait);
  awaitable.reset(TestAwaitableA::Type::StartWait_Result);
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==1);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==5);
}

TEST_CASE( "Terminate on Awaiting::started", "" ) {
  TestAwaitingA awaiting(TestAwaitingA::Type::Wait_TerminateStart);
  TestAwaitableA awaitable(TestAwaitableA::Type::EndWait_Result, &awaiting);
  bool finished=false;
  std::optional<int> value;
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==1);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==1);
  REQUIRE(awaitable.numDoReturn==0);
  REQUIRE(finished);
  REQUIRE(!value);

  // Check we can await again
  finished=false;
  value.reset();
  awaiting.reset(TestAwaitingA::Type::NoWait);
  awaitable.reset(TestAwaitableA::Type::StartWait_Result);
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==5);
}

TEST_CASE( "Stop on Awaiting::started", "" ) {
  TestAwaitingA awaiting(TestAwaitingA::Type::Wait_StopStart);
  TestAwaitableA awaitable(TestAwaitableA::Type::EndWait_Result, &awaiting);
  bool finished=false;
  std::optional<int> value;
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==1);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==1);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==8);

  // Check we can await again
  finished=false;
  value.reset();
  awaiting.reset(TestAwaitingA::Type::NoWait);
  awaitable.reset(TestAwaitableA::Type::StartWait_Result);
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==1);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==5);
}

TEST_CASE( "Stop on Awaiting::started, but no result", "" ) {
  TestAwaitingA awaiting(TestAwaitingA::Type::Wait_StopStart);
  TestAwaitableA awaitable(TestAwaitableA::Type::NoResult, &awaiting);
  bool finished=false;
  std::optional<int> value;
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==1);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==1);
  REQUIRE(awaitable.numDoReturn==0);
  REQUIRE(finished);
  REQUIRE(!value);

  // Check we can await again
  finished=false;
  value.reset();
  awaiting.reset(TestAwaitingA::Type::NoWait);
  awaitable.reset(TestAwaitableA::Type::StartWait_Result);
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==5);
}

TEST_CASE( "Generate value during suspend", "" ) {
  TestAwaitingA awaiting(TestAwaitingA::Type::Wait);
  TestAwaitableA awaitable(TestAwaitableA::Type::NoResult, &awaiting);
  bool finished=false;
  std::optional<int> value;
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==1);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==0);
  REQUIRE(!finished);
  REQUIRE(!value);
  awaitable.manualSetValue(15);
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==1);
  REQUIRE(awaiting.numEndWait==1);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==15);

  // Check we can await again
  finished=false;
  value.reset();
  awaiting.reset(TestAwaitingA::Type::NoWait);
  awaitable.reset(TestAwaitableA::Type::StartWait_Result);
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==1);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==5);
}

TEST_CASE( "Generate value during suspend, ignore terminate on awaiting endWait", "" ) {
  TestAwaitingA awaiting(TestAwaitingA::Type::Wait_TerminateEnd);
  TestAwaitableA awaitable(TestAwaitableA::Type::NoResult, &awaiting);
  bool finished=false;
  std::optional<int> value;
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==1);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==0);
  REQUIRE(!finished);
  REQUIRE(!value);
  awaitable.manualSetValue(15);
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==1);
  REQUIRE(awaiting.numEndWait==1);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==15);

  // Check we can await again
  finished=false;
  value.reset();
  awaiting.reset(TestAwaitingA::Type::NoWait);
  awaitable.reset(TestAwaitableA::Type::StartWait_Result);
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==1);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==5);
}

TEST_CASE( "Terminate during suspend", "" ) {
  TestAwaitingA awaiting(TestAwaitingA::Type::Wait);
  TestAwaitableA awaitable(TestAwaitableA::Type::NoResult, &awaiting);
  bool finished=false;
  std::optional<int> value;
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==1);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==0);
  REQUIRE(!finished);
  REQUIRE(!value);
  awaiting.manualTerminate(true);
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==1);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==1);
  REQUIRE(awaitable.numDoReturn==0);
  REQUIRE(finished);
  REQUIRE(!value);

  // Check we can await again
  finished=false;
  value.reset();
  awaiting.reset(TestAwaitingA::Type::NoWait);
  awaitable.reset(TestAwaitableA::Type::StartWait_Result);
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==5);
}

TEST_CASE( "Stop during suspend does not generate a value during end", "" ) {
  TestAwaitingA awaiting(TestAwaitingA::Type::Wait);
  TestAwaitableA awaitable(TestAwaitableA::Type::NoResult, &awaiting);
  bool finished=false;
  std::optional<int> value;
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==1);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==0);
  REQUIRE(!finished);
  REQUIRE(!value);
  awaiting.manualTerminate(false);
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==1);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==1);
  REQUIRE(awaitable.numDoReturn==0);
  REQUIRE(finished);
  REQUIRE(!value);

  // Check we can await again
  finished=false;
  value.reset();
  awaiting.reset(TestAwaitingA::Type::NoWait);
  awaitable.reset(TestAwaitableA::Type::StartWait_Result);
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==5);
}

TEST_CASE( "Stop during suspend, generates a value during end", "" ) {
  TestAwaitingA awaiting(TestAwaitingA::Type::Wait);
  TestAwaitableA awaitable(TestAwaitableA::Type::EndWait_Result, &awaiting);
  bool finished=false;
  std::optional<int> value;
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==1);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==0);
  REQUIRE(!finished);
  REQUIRE(!value);
  awaiting.manualTerminate(false);
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==1);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==0);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==1);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==8);

  // Check we can await again
  finished=false;
  value.reset();
  awaiting.reset(TestAwaitingA::Type::NoWait);
  awaitable.reset(TestAwaitableA::Type::StartWait_Result);
  testA(awaitable).detach([&finished, &value](std::optional<int> ret) {
    finished=true;
    value=ret;
  });
  REQUIRE(awaiting.numNoWait==1);
  REQUIRE(awaiting.numStartWait==0);
  REQUIRE(awaiting.numEndWait==0);
  REQUIRE(awaitable.numReset==1);
  REQUIRE(awaitable.numStartWait==1);
  REQUIRE(awaitable.numEndWait==0);
  REQUIRE(awaitable.numDoReturn==1);
  REQUIRE(finished);
  REQUIRE(value);
  REQUIRE(*value==5);
  REQUIRE(numPromises==0);
}
