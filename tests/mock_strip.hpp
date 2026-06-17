#ifndef PRISM_TEST_MOCK_STRIP_HPP_
#define PRISM_TEST_MOCK_STRIP_HPP_

#include <array>
#include <cstddef>

#include "gmock/gmock.h"
#include "prism/color.hpp"
#include "prism/strip.hpp"

namespace prism::test {

/// @brief GMock test double for prism::StripLed.
class MockStripLed : public prism::StripLed {
 public:
  MOCK_METHOD(bool, is_ready, (), (const, override));
  MOCK_METHOD(int, set_color, (const prism::RgbColor&), (override));
  MOCK_METHOD(prism::RgbColor, color, (), (const, override));
  MOCK_METHOD(std::size_t, index, (), (const, override));
};

/// @brief GMock test double for prism::Strip.
///
/// Owns a fixed array of MockStripLed views.  The non-const led() dispatches
/// to the corresponding element by default; the const led() returns nullptr
/// unless overridden.
class MockStrip : public prism::Strip {
 public:
  static constexpr std::size_t kDefaultLedCount = 8U;

  MockStrip() { init_defaults(); }

  MOCK_METHOD(const char*, name, (), (const, override));
  MOCK_METHOD(bool, is_ready, (), (const, override));
  MOCK_METHOD(std::size_t, led_count, (), (const, override));
  MOCK_METHOD(prism::StripLed*, led, (std::size_t), (override));
  MOCK_METHOD(const prism::StripLed*, led, (std::size_t), (const, override));
  MOCK_METHOD(int, fill, (const prism::RgbColor&), (override));
  MOCK_METHOD(int, show, (), (override));

  /// @brief Return the mutable mock led at the given index.
  /// @param index Zero-based led index.
  /// @return Pointer to the led view, or nullptr when out of range.
  MockStripLed* mutable_led(std::size_t index) {
    return (index < led_views_.size()) ? &led_views_[index] : nullptr;
  }

 private:
  void init_defaults() {
    for (std::size_t i = 0; i < led_views_.size(); ++i) {
      ON_CALL(led_views_[i], index()).WillByDefault(testing::Return(i));
      ON_CALL(led_views_[i], is_ready()).WillByDefault(testing::Return(true));
    }
    ON_CALL(*this, led(testing::_))
      .WillByDefault(testing::Invoke(this, &MockStrip::mutable_led));
    ON_CALL(*this, is_ready()).WillByDefault(testing::Return(true));
    ON_CALL(*this, led_count())
      .WillByDefault(testing::Return(kDefaultLedCount));
  }

  std::array<MockStripLed, kDefaultLedCount> led_views_{};
};

}  // namespace prism::test

#endif /* PRISM_TEST_MOCK_STRIP_HPP_ */
