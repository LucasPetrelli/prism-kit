#ifndef OSHAL_ZEPHYR_GPIO_INTERNAL_HPP_
#define OSHAL_ZEPHYR_GPIO_INTERNAL_HPP_

#include <zephyr/drivers/gpio.h>

#include "oshal/gpio.hpp"

namespace oshal::internal {

/// @brief Zephyr-backed GPIO implementation for a fixed device-tree binding.
class ZephyrGpio final : public Gpio {
 public:
  /// @brief Construct a Zephyr-backed GPIO object.
  /// @param gpio_name Static diagnostic name for the GPIO.
  /// @param gpio_spec Zephyr GPIO binding resolved from device tree.
  ZephyrGpio(const char* gpio_name, const gpio_dt_spec& gpio_spec);

  const char* name() const override;
  bool is_ready() const override;
  int configure_output(bool initial_high) const override;
  int set(bool high) const override;
  int toggle() const override;

 private:
  const char* name_;
  gpio_dt_spec spec_;
};

}  // namespace oshal::internal

#endif /* OSHAL_ZEPHYR_GPIO_INTERNAL_HPP_ */