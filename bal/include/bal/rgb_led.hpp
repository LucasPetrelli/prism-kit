#ifndef BAL_RGB_LED_HPP_
#define BAL_RGB_LED_HPP_

#include <cstdint>

namespace bal {

/// @brief Logical RGB color owned by BAL-facing APIs.
struct RgbColor {
  /// @brief Red intensity component.
  std::uint8_t red;
  /// @brief Green intensity component.
  std::uint8_t green;
  /// @brief Blue intensity component.
  std::uint8_t blue;
};

/// @brief Generic logical RGB LED interface exposed by BAL.
class RgbLed {
 public:
  RgbLed(const RgbLed&) = delete;
  RgbLed& operator=(const RgbLed&) = delete;
  virtual ~RgbLed() = default;

  /// @brief Report whether the RGB LED backend is ready.
  /// @return True when the backend is ready, otherwise false.
  virtual bool is_ready() const = 0;

  /// @brief Update the logical RGB color.
  /// @param color Requested logical RGB color.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int set_color(const RgbColor& color) = 0;

  /// @brief Return the currently staged logical RGB color.
  /// @return Current logical RGB color.
  virtual RgbColor color() const = 0;

 protected:
  RgbLed() = default;
};

}  // namespace bal

#endif /* BAL_RGB_LED_HPP_ */