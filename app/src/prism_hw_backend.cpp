#include <array>
#include <cstddef>

#include "bal/led.hpp"
#include "bal/ws2812_strip.hpp"
#include "oshal/debug_port.hpp"
#include "oshal/status.h"
#include "oshal/time.hpp"
#include "prism/strip.hpp"
#include "prism/time.hpp"
#include "prism_hw_backend_internal.hpp"

namespace {

/*
 * The HW backend keeps APP-facing strip state entirely inside Prism Kit types.
 * APP mutates this local staged frame through the repo-owned contract, and the
 * committed frame is copied into the shared mailbox only when show() is called.
 * That preserves the existing fill-plus-show execution model while ensuring the
 * dedicated app_hw task remains the only code path that touches BAL strip APIs.
 */
class HardwareStrip;

class HardwareStripLed : public prism::StripLed {
 public:
  HardwareStripLed() = default;

  void bind(HardwareStrip* owner, std::size_t led_index) {
    owner_ = owner;
    led_index_ = led_index;
  }

  bool is_ready() const override;
  int set_color(const prism::RgbColor& color) override;
  prism::RgbColor color() const override;
  std::size_t index() const override { return led_index_; }

 private:
  HardwareStrip* owner_ = nullptr;
  std::size_t led_index_ = 0U;
};

class HardwareStrip : public prism::Strip {
 public:
  HardwareStrip() {
    for (std::size_t index = 0; index < led_views_.size(); ++index) {
      led_views_[index].bind(this, index);
    }
  }

  void configure(std::size_t led_count, const char* strip_name) {
    led_count_ = led_count;
    name_ = strip_name;
    ready_ = true;
  }

  const char* name() const override { return name_; }
  bool is_ready() const override { return ready_; }
  std::size_t led_count() const override { return led_count_; }

  prism::StripLed* led(std::size_t index) override {
    if (index >= led_count_) {
      return nullptr;
    }

    return &led_views_[index];
  }

  const prism::StripLed* led(std::size_t index) const override {
    if (index >= led_count_) {
      return nullptr;
    }

    return &led_views_[index];
  }

  int fill(const prism::RgbColor& color) override {
    if (!ready_) {
      return STATUS_ERR_NOT_READY;
    }

    for (std::size_t index = 0; index < led_count_; ++index) {
      staged_frame_.colors[index] = color;
    }

    return STATUS_OK;
  }

  int show() override {
    if (!ready_) {
      return STATUS_ERR_NOT_READY;
    }

    staged_frame_.led_count = led_count_;
    return app::internal::publish_prism_hw_frame(staged_frame_);
  }

  int set_led_color(std::size_t index, const prism::RgbColor& color) {
    if (!ready_) {
      return STATUS_ERR_NOT_READY;
    }

    if (index >= led_count_) {
      return STATUS_ERR_INVALID_ARGUMENT;
    }

    staged_frame_.colors[index] = color;
    return STATUS_OK;
  }

  prism::RgbColor led_color(std::size_t index) const {
    if (index >= led_count_) {
      return prism::RgbColor{};
    }

    return staged_frame_.colors[index];
  }

 private:
  bool ready_ = false;
  std::size_t led_count_ = 0U;
  const char* name_ = "prism_hw_strip";
  app::internal::SharedFrame staged_frame_ = {};
  std::array<HardwareStripLed, app::internal::kPrismHwMailboxFrameCapacity>
    led_views_ = {};
};

HardwareStrip g_hardware_strip;

bool HardwareStripLed::is_ready() const {
  return (owner_ != nullptr) && owner_->is_ready();
}

int HardwareStripLed::set_color(const prism::RgbColor& color) {
  if (owner_ == nullptr) {
    return STATUS_ERR_NOT_READY;
  }

  return owner_->set_led_color(led_index_, color);
}

prism::RgbColor HardwareStripLed::color() const {
  if (owner_ == nullptr) {
    return prism::RgbColor{};
  }

  return owner_->led_color(led_index_);
}

}  // namespace

namespace prism {

int initialize() {
  static bool initialized = false;
  if (initialized) {
    return STATUS_OK;
  }

  oshal::TaskHandle const app_task = oshal::TaskHandle::current();
  if (!app_task.is_valid()) {
    return STATUS_ERR_NOT_READY;
  }

  bal::Ws2812Strip& backend_strip = bal::ws2812_strip();
  if (!backend_strip.is_ready()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  bal::Led& status_led = bal::status_led();
  if (!status_led.is_ready()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  if (!oshal::debug_port.is_ready()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  const std::size_t led_count = backend_strip.led_count();
  if (led_count > app::internal::kPrismHwMailboxFrameCapacity) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  static app::TaskRuntimeReporter runtime_reporter(app_task);
  app::internal::g_prism_runtime_services.app_task = app_task;
  app::internal::g_prism_runtime_services.runtime_reporter = &runtime_reporter;
  app::internal::g_prism_runtime_services.status_led = &status_led;
  app::internal::g_prism_runtime_services.debug_port = &oshal::debug_port;

  /* Start the HW executor before publishing any committed strip frame. */
  const int start_ret = app::internal::ensure_prism_hw_started();
  if (start_ret < 0) {
    return start_ret;
  }

  g_hardware_strip.configure(led_count, backend_strip.name());
  initialized = true;
  return STATUS_OK;
}

Strip& strip() { return g_hardware_strip; }

void sleep_ms(std::uint32_t duration_ms) { oshal::sleep_ms(duration_ms); }

}  // namespace prism