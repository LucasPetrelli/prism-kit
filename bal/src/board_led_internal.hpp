#ifndef BAL_BOARD_LED_INTERNAL_HPP_
#define BAL_BOARD_LED_INTERNAL_HPP_

#include "bal/led.hpp"

namespace oshal {

class Gpio;

}  // namespace oshal

namespace bal::internal {

class LedStatus final : public Led {
 public:
  LedStatus(const char* led_name, const oshal::Gpio& gpio, bool is_active_low);

  const char* name() const override;
  bool is_ready() const override;
  int initialize() override;
  int set(bool on) const override;
  int toggle() const override;

 private:
  bool output_level_for_state(bool on) const;

  const char* led_name_;
  const oshal::Gpio& gpio_;
  bool is_active_low_;
  mutable bool is_initialized_;
};

/// @brief Return the board-specific status LED backend selected for this build.
/// @return Reference to a lazily-initialized backend object.
Led& status_led_backend();

}  // namespace bal::internal

#endif /* BAL_BOARD_LED_INTERNAL_HPP_ */