#ifndef BAL_LED_HPP_
#define BAL_LED_HPP_

namespace bal {

/// @brief Generic board-owned LED interface exposed by BAL.
/// @note BAL owns logical LED policy while OSHAL continues to own physical GPIO
///     bindings.
class Led {
 public:
  Led(const Led&) = delete;
  Led& operator=(const Led&) = delete;
  virtual ~Led() = default;

  /// @brief Return a human-readable board LED name.
  /// @return Pointer to a static string describing the LED object.
  virtual const char* name() const = 0;

  /// @brief Report whether the backend for this LED object is ready.
  /// @return True when the underlying GPIO backend is ready, otherwise false.
  virtual bool is_ready() const = 0;

  /// @brief Prepare the LED object for application use.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int initialize() = 0;

  /// @brief Set the logical LED state.
  /// @param on True drives the logical LED state on, false drives it off.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int set(bool on) const = 0;

  /// @brief Toggle the logical LED state.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int toggle() const = 0;

 protected:
  Led() = default;
};

/// @brief Initialize BAL-owned LED objects.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int initialize_leds();

/// @brief Return the board-owned status LED object.
/// @return Reference to the board-owned status LED object.
Led& status_led();

}  // namespace bal

#endif /* BAL_LED_HPP_ */