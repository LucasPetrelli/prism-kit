#ifndef OSHAL_GPIO_HPP_
#define OSHAL_GPIO_HPP_

namespace oshal {

/// @brief Generic GPIO interface exposed by OSHAL.
/// @note OSHAL owns physical pin bindings while higher layers own any logical
///     meaning, polarity, or board policy attached to a pin.
class Gpio {
 public:
  Gpio(const Gpio&) = delete;
  Gpio& operator=(const Gpio&) = delete;
  virtual ~Gpio() = default;

  /// @brief Return a human-readable physical pin name.
  /// @return Pointer to a static string describing the pin.
  virtual const char* name() const = 0;

  /// @brief Report whether the backend for this GPIO object is ready.
  /// @return True when the backend is ready, otherwise false.
  virtual bool is_ready() const = 0;

  /// @brief Configure the GPIO as an output.
  /// @param initial_high True drives the pin high after configuration, false
  ///     drives it low.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int configure_output(bool initial_high) const = 0;

  /// @brief Drive the GPIO to the requested physical level.
  /// @param high True drives the pin high, false drives it low.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int set(bool high) const = 0;

  /// @brief Toggle the GPIO output.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int toggle() const = 0;

 protected:
  Gpio() = default;
};

}  // namespace oshal

#endif /* OSHAL_GPIO_HPP_ */