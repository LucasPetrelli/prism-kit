#ifndef PRISM_STRIP_HPP_
#define PRISM_STRIP_HPP_

#include <cstddef>
#include <cstdint>

#include "prism/color.hpp"

namespace prism {
/// @brief Logical RGB LED view exposed by Prism Kit.
class StripLed {
 public:
  StripLed(const StripLed&) = delete;
  StripLed& operator=(const StripLed&) = delete;
  virtual ~StripLed() = default;

  /// @brief Report whether the owning strip backend is ready.
  /// @return True when the backend is ready, otherwise false.
  virtual bool IsReady() const = 0;

  /// @brief Update the staged logical RGB color for this pixel.
  /// @param color Requested logical RGB color.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int SetColor(const RgbColor& color) = 0;

  /// @brief Return the currently staged logical RGB color for this pixel.
  /// @return Current staged logical RGB color.
  virtual RgbColor Color() const = 0;

  /// @brief Return the zero-based pixel index inside the owning strip.
  /// @return Zero-based strip index.
  virtual std::size_t Index() const = 0;

 protected:
  StripLed() = default;
};

/// @brief Logical RGB strip interface exposed by Prism Kit.
class Strip {
 public:
  Strip(const Strip&) = delete;
  Strip& operator=(const Strip&) = delete;
  virtual ~Strip() = default;

  /// @brief Return a human-readable strip name.
  /// @return Pointer to a static string describing the strip.
  virtual const char* Name() const = 0;

  /// @brief Report whether the underlying backend is ready.
  /// @return True when the backend is ready, otherwise false.
  virtual bool IsReady() const = 0;

  /// @brief Report the number of addressable pixels in this strip.
  /// @return Number of pixels owned by this strip object.
  virtual std::size_t LedCount() const = 0;

  /// @brief Return a pixel view for the requested index.
  /// @param index Zero-based pixel index.
  /// @return Pointer to the requested pixel view, or nullptr when index is out
  ///     of range.
  virtual StripLed* Led(std::size_t index) = 0;

  /// @brief Return a const pixel view for the requested index.
  /// @param index Zero-based pixel index.
  /// @return Pointer to the requested pixel view, or nullptr when index is out
  ///     of range.
  virtual const StripLed* Led(std::size_t index) const = 0;

  /// @brief Fill every pixel with the same staged logical RGB color.
  /// @param color Requested logical RGB color.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int Fill(const RgbColor& color) = 0;

  /// @brief Commit the staged frame so the active backend can process it.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int Show() = 0;

 protected:
  Strip() = default;
};

/// @brief Initialize the selected Prism Kit strip backend.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int Initialize();

/// @brief Return the selected Prism Kit strip object.
/// @return Reference to the selected Prism Kit strip.
Strip& GetStrip();

}  // namespace prism

#endif /* PRISM_STRIP_HPP_ */