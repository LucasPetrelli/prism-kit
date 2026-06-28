#include "strip_manager.hpp"

#include "bal/rgb_led.hpp"
#include "oshal/status.h"

namespace app::hw {

/* ========================================================================
 * StripLedView
 * ======================================================================== */

bool StripLedView::is_ready() const {
  return StripManager::Instance().is_ready();
}

int StripLedView::set_color(const prism::RgbColor& color) {
  return StripManager::Instance().set_led_color(index_, color);
}

prism::RgbColor StripLedView::color() const {
  return StripManager::Instance().led_color(index_);
}

/* ========================================================================
 * StripManager
 * ======================================================================== */

StripManager::StripManager(oshal::EventFlagGroup& event_group)
    : event_group_(event_group) {
  for (std::size_t i = 0; i < led_views_.size(); ++i) {
    led_views_[i].set_index(i);
  }
}

void StripManager::Configure(bal::Ws2812Strip* backend_strip,
                             std::size_t led_count, const char* strip_name) {
  backend_strip_ = backend_strip;
  led_count_ = led_count;
  name_ = strip_name;
  ready_ = true;
}

// ---- prism::Strip interface --------------------------------------------

const char* StripManager::name() const { return name_; }

bool StripManager::is_ready() const { return ready_; }

std::size_t StripManager::led_count() const { return led_count_; }

prism::StripLed* StripManager::led(std::size_t index) {
  if (index >= led_count_) {
    return nullptr;
  }

  return &led_views_[index];
}

const prism::StripLed* StripManager::led(std::size_t index) const {
  if (index >= led_count_) {
    return nullptr;
  }

  return &led_views_[index];
}

int StripManager::fill(const prism::RgbColor& color) {
  if (!ready_) {
    return STATUS_ERR_NOT_READY;
  }

  for (std::size_t i = 0; i < led_count_; ++i) {
    staged_frame_.colors[i] = color;
  }

  return STATUS_OK;
}

int StripManager::show() {
  if (!ready_) {
    return STATUS_ERR_NOT_READY;
  }

  staged_frame_.led_count = led_count_;
  return mailbox_.Send(&staged_frame_);
}

// ---- HW task side ------------------------------------------------------

bool StripManager::TryApplyLatest() {
  SharedFrame frame;
  while (mailbox_.Receive(&frame)) {
    if (ApplyFrame(frame) < 0) {
      return false;
    }
  }

  return true;
}

// ---- Internal helpers used by StripLedView -----------------------------

int StripManager::set_led_color(std::size_t index,
                                const prism::RgbColor& color) {
  if (!ready_) {
    return STATUS_ERR_NOT_READY;
  }

  if (index >= led_count_) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  staged_frame_.colors[index] = color;
  return STATUS_OK;
}

prism::RgbColor StripManager::led_color(std::size_t index) const {
  if (index >= led_count_) {
    return prism::RgbColor{};
  }

  return staged_frame_.colors[index];
}

// ---- Private -----------------------------------------------------------

int StripManager::ApplyFrame(const SharedFrame& frame) {
  if (frame.led_count > backend_strip_->led_count()) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  for (std::size_t index = 0; index < frame.led_count; ++index) {
    bal::Ws2812Led* const pixel = backend_strip_->led(index);
    if ((pixel == nullptr) || !pixel->is_ready()) {
      return STATUS_ERR_DEVICE_UNAVAILABLE;
    }

    const prism::RgbColor& color = frame.colors[index];
    const int set_ret =
      pixel->set_color(bal::RgbColor{color.red, color.green, color.blue});
    if (set_ret < 0) {
      return set_ret;
    }
  }

  return backend_strip_->show();
}

}  // namespace app::hw
