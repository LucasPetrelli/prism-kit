#include <cstdarg>
#include <cstdio>
#include <string>

#include "bal/led.hpp"
#include "bal/ws2812_strip.hpp"
#include "gtest/gtest.h"
#include "hw/command_manager.hpp"
#include "hw/hw_task.hpp"
#include "oshal/debug_port.hpp"
#include "oshal/serial_port.hpp"
#include "oshal/status.h"
#include "oshal/task.hpp"
#include "protocol.hpp"

namespace {

class TestDebugPort final : public oshal::DebugPort {
 public:
  const char* Name() const override { return "test-debug"; }
  bool IsReady() const override { return true; }
  int Write(const char* buffer, std::size_t length) const override {
    output_.append(buffer, length);
    return static_cast<int>(length);
  }
  int Vprintf(const char* format, std::va_list args) const override {
    char buffer[128]{};
    const int length = std::vsnprintf(buffer, sizeof(buffer), format, args);
    if (length >= 0) {
      output_.append(buffer, static_cast<std::size_t>(length));
    }
    return length;
  }
  void Clear() const { output_.clear(); }
  const std::string& Output() const { return output_; }

 private:
  mutable std::string output_;
};

class TestLed final : public bal::Led {
 public:
  const char* Name() const override { return "test-led"; }
  bool IsReady() const override { return true; }
  int Initialize() override { return STATUS_OK; }
  int Set(bool on) const override {
    static_cast<void>(on);
    return STATUS_OK;
  }
  int Toggle() const override { return STATUS_OK; }
};

class TestWs2812Strip final : public bal::Ws2812Strip {
 public:
  const char* Name() const override { return "test-strip"; }
  bool IsReady() const override { return true; }
  int Initialize() override { return STATUS_OK; }
  std::size_t LedCount() const override { return 7U; }
  bal::Ws2812Led* Led(std::size_t index) override {
    static_cast<void>(index);
    return nullptr;
  }
  const bal::Ws2812Led* Led(std::size_t index) const override {
    static_cast<void>(index);
    return nullptr;
  }
  int Fill(const bal::RgbColor& color) override {
    static_cast<void>(color);
    return STATUS_OK;
  }
  int Show() override { return STATUS_OK; }
};

TestDebugPort g_test_debug_port;

void IgnoreFrame(void* context, const std::uint8_t* data,
                 std::uint16_t length) {
  static_cast<void>(context);
  static_cast<void>(data);
  static_cast<void>(length);
}

}  // namespace

namespace oshal {

DebugPort& debug_port = g_test_debug_port;
SerialPort* command_port = nullptr;

TaskHandle::TaskHandle()
    : slot_index_(std::numeric_limits<std::uint8_t>::max()), generation_(0U) {}

int TaskHandle::Create(TaskHandle& handle, const TaskConfig& config) {
  static_cast<void>(handle);
  static_cast<void>(config);
  return STATUS_ERR_BACKEND;
}

TaskHandle TaskHandle::Current() { return TaskHandle{}; }

bool TaskHandle::IsValid() const { return false; }

bool TaskHandle::IsRunning() const { return false; }

bool TaskHandle::HasExited() const { return false; }

int TaskHandle::ExitCode(int* out_exit_code) const {
  if (out_exit_code == nullptr) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }
  return STATUS_ERR_NOT_READY;
}

int TaskHandle::RuntimeInfo(TaskRuntimeInfo* out_info) const {
  return (out_info == nullptr) ? STATUS_ERR_INVALID_ARGUMENT
                               : STATUS_ERR_NOT_READY;
}

int TaskHandle::Release() { return STATUS_ERR_INVALID_ARGUMENT; }

}  // namespace oshal

namespace app::hw {

class HwTaskSetupTestAccess {
 public:
  static bool Setup(HwTask& task) { return task.Setup(); }
};

}  // namespace app::hw

namespace bal {

Ws2812Strip& GetWs2812Strip() {
  static TestWs2812Strip strip;
  return strip;
}

Led& StatusLed() {
  static TestLed led;
  return led;
}

}  // namespace bal

namespace {

class HwTaskSetupTest : public ::testing::Test {
 protected:
  void SetUp() override {
    g_test_debug_port.Clear();
    auto& protocol = app::hw::CommandManager::Instance().Protocol();
    ASSERT_TRUE(
      protocol.AddHandler(static_cast<protocol::Tag>(0x0200U), &IgnoreFrame));
    ASSERT_TRUE(
      protocol.AddHandler(static_cast<protocol::Tag>(0x0201U), &IgnoreFrame));
  }
};

TEST_F(HwTaskSetupTest, RegistrationFailureLogsAndAbortsSetup) {
  app::hw::HwTask task;

  EXPECT_FALSE(app::hw::HwTaskSetupTestAccess::Setup(task));
  EXPECT_NE(g_test_debug_port.Output().find("registration failed"),
            std::string::npos);
  EXPECT_EQ(g_test_debug_port.Output().find("DebugPort online"),
            std::string::npos);
}

}  // namespace
