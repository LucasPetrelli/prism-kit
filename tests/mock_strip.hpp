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
  MOCK_METHOD(bool, IsReady, (), (const, override));
  MOCK_METHOD(int, SetColor, (const prism::RgbColor&), (override));
  MOCK_METHOD(prism::RgbColor, Color, (), (const, override));
  MOCK_METHOD(std::size_t, Index, (), (const, override));
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

  MOCK_METHOD(const char*, Name, (), (const, override));
  MOCK_METHOD(bool, IsReady, (), (const, override));
  MOCK_METHOD(std::size_t, LedCount, (), (const, override));
  MOCK_METHOD(prism::StripLed*, Led, (std::size_t), (override));
  MOCK_METHOD(const prism::StripLed*, Led, (std::size_t), (const, override));
  MOCK_METHOD(int, Fill, (const prism::RgbColor&), (override));
  MOCK_METHOD(int, Show, (), (override));

  /// @brief Return the mutable mock led at the given index.
  /// @param index Zero-based led index.
  /// @return Pointer to the led view, or nullptr when out of range.
  MockStripLed* mutable_led(std::size_t index) {
    return (index < led_views_.size()) ? &led_views_[index] : nullptr;
  }

 private:
  void init_defaults() {
    for (std::size_t i = 0; i < led_views_.size(); ++i) {
      ON_CALL(led_views_[i], Index()).WillByDefault(testing::Return(i));
      ON_CALL(led_views_[i], IsReady()).WillByDefault(testing::Return(true));
    }
    ON_CALL(*this, Led(testing::_))
      .WillByDefault(testing::Invoke(this, &MockStrip::mutable_led));
    ON_CALL(*this, IsReady()).WillByDefault(testing::Return(true));
    ON_CALL(*this, LedCount()).WillByDefault(testing::Return(kDefaultLedCount));
  }

  std::array<MockStripLed, kDefaultLedCount> led_views_{};
};

}  // namespace prism::test

#endif /* PRISM_TEST_MOCK_STRIP_HPP_ */
