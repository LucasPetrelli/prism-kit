#include "oklab.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

constexpr std::int32_t kQ13One = 8192;
constexpr std::int32_t kQ12CoefficientScale = 4096;

// sRGB decode values are rounded to nearest in unsigned Q0.16. The table is
// monotonic, contains both endpoints, and occupies exactly 512 bytes.
constexpr std::array<std::uint16_t, 256U> kSrgbToLinearQ16{
  0U,     20U,    40U,    60U,    80U,    99U,    119U,   139U,   159U,
  179U,   199U,   219U,   241U,   264U,   288U,   313U,   340U,   367U,
  396U,   427U,   458U,   491U,   526U,   562U,   599U,   637U,   677U,
  718U,   761U,   805U,   851U,   898U,   947U,   997U,   1048U,  1101U,
  1156U,  1212U,  1270U,  1330U,  1391U,  1453U,  1517U,  1583U,  1651U,
  1720U,  1790U,  1863U,  1937U,  2013U,  2090U,  2170U,  2250U,  2333U,
  2418U,  2504U,  2592U,  2681U,  2773U,  2866U,  2961U,  3058U,  3157U,
  3258U,  3360U,  3464U,  3570U,  3678U,  3788U,  3900U,  4014U,  4129U,
  4247U,  4366U,  4488U,  4611U,  4736U,  4864U,  4993U,  5124U,  5257U,
  5392U,  5530U,  5669U,  5810U,  5953U,  6099U,  6246U,  6395U,  6547U,
  6700U,  6856U,  7014U,  7174U,  7335U,  7500U,  7666U,  7834U,  8004U,
  8177U,  8352U,  8528U,  8708U,  8889U,  9072U,  9258U,  9445U,  9635U,
  9828U,  10022U, 10219U, 10417U, 10619U, 10822U, 11028U, 11235U, 11446U,
  11658U, 11873U, 12090U, 12309U, 12530U, 12754U, 12980U, 13209U, 13440U,
  13673U, 13909U, 14146U, 14387U, 14629U, 14874U, 15122U, 15371U, 15623U,
  15878U, 16135U, 16394U, 16656U, 16920U, 17187U, 17456U, 17727U, 18001U,
  18277U, 18556U, 18837U, 19121U, 19407U, 19696U, 19987U, 20281U, 20577U,
  20876U, 21177U, 21481U, 21787U, 22096U, 22407U, 22721U, 23038U, 23357U,
  23678U, 24002U, 24329U, 24658U, 24990U, 25325U, 25662U, 26001U, 26344U,
  26688U, 27036U, 27386U, 27739U, 28094U, 28452U, 28813U, 29176U, 29542U,
  29911U, 30282U, 30656U, 31033U, 31412U, 31794U, 32179U, 32567U, 32957U,
  33350U, 33745U, 34143U, 34544U, 34948U, 35355U, 35764U, 36176U, 36591U,
  37008U, 37429U, 37852U, 38278U, 38706U, 39138U, 39572U, 40009U, 40449U,
  40891U, 41337U, 41785U, 42236U, 42690U, 43147U, 43606U, 44069U, 44534U,
  45002U, 45473U, 45947U, 46423U, 46903U, 47385U, 47871U, 48359U, 48850U,
  49344U, 49841U, 50341U, 50844U, 51349U, 51858U, 52369U, 52884U, 53401U,
  53921U, 54445U, 54971U, 55500U, 56032U, 56567U, 57105U, 57646U, 58190U,
  58737U, 59287U, 59840U, 60396U, 60955U, 61517U, 62082U, 62650U, 63221U,
  63795U, 64372U, 64952U, 65535U,
};
static_assert(sizeof(kSrgbToLinearQ16) == 512U,
              "The sRGB decode table must remain within 512 bytes.");
static_assert(kSrgbToLinearQ16[0U] == 0U && kSrgbToLinearQ16[255U] == 65535U,
              "The sRGB decode table must contain exact endpoint codes.");

// OKLab matrices use signed Q2.13 coefficients. RGB decode coefficients use
// unsigned Q0.16 and the inverse RGB matrix uses signed Q4.12 coefficients.
// Coefficients are rounded to nearest.
constexpr std::array<std::uint16_t, 9U> kLinearRgbToLmsQ16{
  27015U, 35149U, 3372U, 13887U, 44610U, 7038U, 5787U, 18463U, 41286U,
};
constexpr std::array<std::int16_t, 9U> kLmsRootToOklabQ13{
  1724, 6501, -33, 16204, -19895, 3691, 212, 6412, -6625,
};
constexpr std::array<std::int16_t, 9U> kOklabToLmsRootQ13{
  8192, 3247, 1768, 8192, -865, -523, 8192, -733, -10580,
};
constexpr std::array<std::int16_t, 9U> kLmsCubeToLinearRgbQ12{
  16698, -13548, 946, -5196, 10690, -1398, -17, -2881, 6994,
};

constexpr std::int32_t Abs(std::int32_t value) {
  return value < 0 ? -value : value;
}

constexpr std::int32_t AbsRowSum(const std::array<std::int16_t, 9U>& matrix,
                                 std::size_t row) {
  return Abs(matrix[row * 3U]) + Abs(matrix[row * 3U + 1U]) +
         Abs(matrix[row * 3U + 2U]);
}

// Range proof for the fixed-point math:
// - Decode products are at most 65535^2 = 4,294,836,225, below UINT32_MAX.
// - Every forward Q13 matrix product sum is bounded by its coefficient
//   absolute row sum times 8,192; the largest is 39,790*8,192 = 326,369,280.
// - The LMS roots are in [0,1]. L is bounded by the L row's absolute sum,
//   8,258/8,192 < 1.009. The a row sums to zero, so rewrite it as
//   1.978*(l_root-m_root)+0.451*(s_root-m_root); pairwise root differences
//   are at most 1, giving |a| < 2.429. Likewise b is
//   0.026*(l_root-s_root)+0.783*(m_root-s_root); coefficient rounding leaves
//   at most 1/8,192 extra, giving |b| < 0.810. These bounds keep every valid
//   sRGB endpoint in signed Q2.13.
// - Inverse root coefficient absolute row sums are <=19,505. Even a full
//   int16 coordinate has products bounded by 19,505*32,768 = 639,139,840,
//   below INT32_MAX.
// - Inverse RGB uses Q12 coefficients and Q13 cubes. Row sums are <=31,192;
//   clamped cubes are at most 8,192, so the largest dot product is
//   31,192*8,192 = 255,524,864, below 2^31. The Q12 scale is then divided
//   out to return a Q13 linear channel.
// - The cubic intermediates are clamped Q2.13 roots: 8,192^2+4,096 fits
//   signed 32-bit arithmetic; the rounded square is at most 8,192 before the
//   second multiply.
static_assert(65535ULL * 65535ULL < std::numeric_limits<std::uint32_t>::max(),
              "Q0.16 multiplication must fit uint32_t.");
static_assert(AbsRowSum(kLmsRootToOklabQ13, 0U) * kQ13One <
                  std::numeric_limits<std::int32_t>::max() &&
                AbsRowSum(kLmsRootToOklabQ13, 1U) * kQ13One <
                  std::numeric_limits<std::int32_t>::max() &&
                AbsRowSum(kLmsRootToOklabQ13, 2U) * kQ13One <
                  std::numeric_limits<std::int32_t>::max(),
              "Forward matrix products must fit int32_t.");
static_assert(AbsRowSum(kOklabToLmsRootQ13, 0U) * 32768 <
                  std::numeric_limits<std::int32_t>::max() &&
                AbsRowSum(kOklabToLmsRootQ13, 1U) * 32768 <
                  std::numeric_limits<std::int32_t>::max() &&
                AbsRowSum(kOklabToLmsRootQ13, 2U) * 32768 <
                  std::numeric_limits<std::int32_t>::max(),
              "Inverse root matrix products must fit int32_t.");
static_assert(AbsRowSum(kLmsCubeToLinearRgbQ12, 0U) * kQ13One <
                  std::numeric_limits<std::int32_t>::max() &&
                AbsRowSum(kLmsCubeToLinearRgbQ12, 1U) * kQ13One <
                  std::numeric_limits<std::int32_t>::max() &&
                AbsRowSum(kLmsCubeToLinearRgbQ12, 2U) * kQ13One <
                  std::numeric_limits<std::int32_t>::max(),
              "Inverse RGB matrix products must fit int32_t.");
static_assert(AbsRowSum(kLmsRootToOklabQ13, 0U) < 32768 &&
                Abs(kLmsRootToOklabQ13[3U]) + Abs(kLmsRootToOklabQ13[5U]) <
                  32768 &&
                Abs(kLmsRootToOklabQ13[6U]) + Abs(kLmsRootToOklabQ13[7U]) +
                    Abs(kLmsRootToOklabQ13[6U] + kLmsRootToOklabQ13[7U] +
                        kLmsRootToOklabQ13[8U]) <
                  32768,
              "The forward OKLab bounds must fit int16_t Q2.13.");

std::uint16_t MultiplyQ16(std::uint16_t lhs, std::uint16_t rhs) {
  const std::uint32_t product =
    static_cast<std::uint32_t>(lhs) * static_cast<std::uint32_t>(rhs);
  std::uint32_t result = product >> 16U;
  // Round half up without adding to a near-UINT32_MAX product.
  if ((product & 0xFFFFU) >= 0x8000U) {
    ++result;
  }
  return static_cast<std::uint16_t>((result > 65535U) ? 65535U : result);
}

std::uint16_t SumQ16Products(const std::uint16_t* coefficients, std::uint16_t x,
                             std::uint16_t y, std::uint16_t z) {
  const std::uint32_t sum =
    static_cast<std::uint32_t>(MultiplyQ16(coefficients[0], x)) +
    MultiplyQ16(coefficients[1], y) + MultiplyQ16(coefficients[2], z);
  return static_cast<std::uint16_t>((sum > 65535U) ? 65535U : sum);
}

std::uint16_t CubeQ16(std::uint16_t value) {
  return MultiplyQ16(MultiplyQ16(value, value), value);
}

std::uint16_t CubeRootQ16(std::uint16_t value) {
  std::uint32_t low = 0U;
  std::uint32_t high = 65535U;
  while (low < high) {
    const std::uint32_t middle = low + (high - low) / 2U;
    if (CubeQ16(static_cast<std::uint16_t>(middle)) < value) {
      low = middle + 1U;
    } else {
      high = middle;
    }
  }

  const auto upper = static_cast<std::uint16_t>(low);
  const auto lower = static_cast<std::uint16_t>((low == 0U) ? 0U : low - 1U);
  const std::uint16_t upper_cube = CubeQ16(upper);
  const std::uint16_t lower_cube = CubeQ16(lower);
  if (upper_cube < value) {
    // The top representable Q0.16 value can cube just below 1.0.
    return upper;
  }
  return (static_cast<std::uint32_t>(value - lower_cube) <=
          static_cast<std::uint32_t>(upper_cube - value))
           ? lower
           : upper;
}

std::int32_t RoundDivideSigned(std::int32_t numerator,
                               std::int32_t denominator) {
  if (numerator >= 0) {
    return (numerator + denominator / 2) / denominator;
  }
  return -((-numerator + denominator / 2) / denominator);
}

std::int32_t Dot3Q13(const std::int16_t* coefficients, std::int32_t x,
                     std::int32_t y, std::int32_t z) {
  const std::int32_t sum = static_cast<std::int32_t>(coefficients[0]) * x +
                           static_cast<std::int32_t>(coefficients[1]) * y +
                           static_cast<std::int32_t>(coefficients[2]) * z;
  return RoundDivideSigned(sum, kQ13One);
}

std::int32_t Dot3Q12CoefficientsToQ13(const std::int16_t* coefficients,
                                      std::int32_t x, std::int32_t y,
                                      std::int32_t z) {
  const std::int32_t sum = static_cast<std::int32_t>(coefficients[0]) * x +
                           static_cast<std::int32_t>(coefficients[1]) * y +
                           static_cast<std::int32_t>(coefficients[2]) * z;
  return RoundDivideSigned(sum, kQ12CoefficientScale);
}

std::int32_t CubeQ13(std::int32_t value) {
  const std::int32_t square = (value * value + kQ13One / 2) / kQ13One;
  return (square * value + kQ13One / 2) / kQ13One;
}

std::int32_t ClampQ13Root(std::int32_t value) {
  if (value < 0) {
    return 0;
  }
  if (value > kQ13One) {
    return kQ13One;
  }
  return value;
}

std::uint8_t EncodeSrgbQ13(std::int32_t linear) {
  const std::uint32_t target_q16 = static_cast<std::uint32_t>(linear) << 3U;
  std::size_t low = 0U;
  std::size_t high = kSrgbToLinearQ16.size();
  while (low < high) {
    const std::size_t middle = low + (high - low) / 2U;
    if (kSrgbToLinearQ16[middle] < target_q16) {
      low = middle + 1U;
    } else {
      high = middle;
    }
  }

  if (low == 0U) {
    return 0U;
  }
  if (low >= kSrgbToLinearQ16.size()) {
    return 255U;
  }

  const std::uint32_t lower_distance = target_q16 - kSrgbToLinearQ16[low - 1U];
  const std::uint32_t upper_distance = kSrgbToLinearQ16[low] - target_q16;
  // Nearest decoded-linear entry; exact ties deterministically choose lower.
  return static_cast<std::uint8_t>((lower_distance <= upper_distance) ? low - 1U
                                                                      : low);
}

std::uint32_t InterpolateMagnitude(std::uint32_t magnitude,
                                   std::uint16_t elapsed_ms,
                                   std::uint16_t duration_ms) {
  const std::uint32_t whole = magnitude / duration_ms;
  const std::uint32_t remainder = magnitude % duration_ms;
  // Here remainder < duration <= 65535 and elapsed < duration, so this
  // product is at most 65534^2, which fits uint32_t.
  const std::uint32_t partial = remainder * elapsed_ms;
  std::uint32_t result = whole * elapsed_ms + partial / duration_ms;
  const std::uint32_t fractional_remainder = partial % duration_ms;
  // Round half away from the starting sample and toward the destination.
  if (fractional_remainder * 2U >= duration_ms) {
    ++result;
  }
  return result;
}

std::int16_t InterpolateComponent(std::int16_t source, std::int16_t target,
                                  std::uint16_t elapsed_ms,
                                  std::uint16_t duration_ms) {
  if (duration_ms == 0U || elapsed_ms >= duration_ms) {
    return target;
  }
  if (elapsed_ms == 0U) {
    return source;
  }

  const std::int32_t difference =
    static_cast<std::int32_t>(target) - static_cast<std::int32_t>(source);
  const std::uint32_t magnitude =
    static_cast<std::uint32_t>((difference < 0) ? -difference : difference);
  const std::int32_t change = static_cast<std::int32_t>(
    InterpolateMagnitude(magnitude, elapsed_ms, duration_ms));
  const std::int32_t value = (difference < 0)
                               ? static_cast<std::int32_t>(source) - change
                               : static_cast<std::int32_t>(source) + change;
  return static_cast<std::int16_t>(value);
}

}  // namespace

prism::controller_detail::OklabColor prism::controller_detail::ToOklab(
  prism::color::RgbColor rgb) noexcept {
  const std::uint16_t r = kSrgbToLinearQ16[rgb.red];
  const std::uint16_t g = kSrgbToLinearQ16[rgb.green];
  const std::uint16_t b = kSrgbToLinearQ16[rgb.blue];

  const std::uint16_t l = SumQ16Products(&kLinearRgbToLmsQ16[0U], r, g, b);
  const std::uint16_t m = SumQ16Products(&kLinearRgbToLmsQ16[3U], r, g, b);
  const std::uint16_t s = SumQ16Products(&kLinearRgbToLmsQ16[6U], r, g, b);

  const std::int32_t l_root =
    (static_cast<std::uint32_t>(CubeRootQ16(l)) + 4U) / 8U;
  const std::int32_t m_root =
    (static_cast<std::uint32_t>(CubeRootQ16(m)) + 4U) / 8U;
  const std::int32_t s_root =
    (static_cast<std::uint32_t>(CubeRootQ16(s)) + 4U) / 8U;
  return OklabColor{
    static_cast<std::int16_t>(
      Dot3Q13(&kLmsRootToOklabQ13[0U], l_root, m_root, s_root)),
    static_cast<std::int16_t>(
      Dot3Q13(&kLmsRootToOklabQ13[3U], l_root, m_root, s_root)),
    static_cast<std::int16_t>(
      Dot3Q13(&kLmsRootToOklabQ13[6U], l_root, m_root, s_root)),
  };
}

prism::color::RgbColor prism::controller_detail::FromOklab(
  prism::controller_detail::OklabColor lab) noexcept {
  // The inverse root transform of a line between converted RGB endpoints is
  // a convex interpolation of their [0,1] LMS roots. Clamp only matrix
  // quantization excursions at those bounds before cubing.
  const std::int32_t l_root =
    ClampQ13Root(Dot3Q13(&kOklabToLmsRootQ13[0U], lab.l, lab.a, lab.b));
  const std::int32_t m_root =
    ClampQ13Root(Dot3Q13(&kOklabToLmsRootQ13[3U], lab.l, lab.a, lab.b));
  const std::int32_t s_root =
    ClampQ13Root(Dot3Q13(&kOklabToLmsRootQ13[6U], lab.l, lab.a, lab.b));

  const std::int32_t l = CubeQ13(l_root);
  const std::int32_t m = CubeQ13(m_root);
  const std::int32_t s = CubeQ13(s_root);
  const std::int32_t red = ClampQ13Root(
    Dot3Q12CoefficientsToQ13(&kLmsCubeToLinearRgbQ12[0U], l, m, s));
  const std::int32_t green = ClampQ13Root(
    Dot3Q12CoefficientsToQ13(&kLmsCubeToLinearRgbQ12[3U], l, m, s));
  const std::int32_t blue = ClampQ13Root(
    Dot3Q12CoefficientsToQ13(&kLmsCubeToLinearRgbQ12[6U], l, m, s));

  return prism::color::RgbColor{EncodeSrgbQ13(red), EncodeSrgbQ13(green),
                                EncodeSrgbQ13(blue)};
}

prism::controller_detail::OklabColor prism::controller_detail::Interpolate(
  prism::controller_detail::OklabColor source,
  prism::controller_detail::OklabColor target, std::uint16_t elapsed_ms,
  std::uint16_t duration_ms) noexcept {
  if (duration_ms == 0U || elapsed_ms >= duration_ms) {
    return target;
  }
  if (elapsed_ms == 0U) {
    return source;
  }
  return OklabColor{
    InterpolateComponent(source.l, target.l, elapsed_ms, duration_ms),
    InterpolateComponent(source.a, target.a, elapsed_ms, duration_ms),
    InterpolateComponent(source.b, target.b, elapsed_ms, duration_ms),
  };
}

prism::color::RgbColor prism::controller_detail::InterpolateRgb(
  prism::color::RgbColor source, prism::color::RgbColor target,
  std::uint16_t elapsed_ms, std::uint16_t duration_ms) noexcept {
  if (duration_ms == 0U || elapsed_ms >= duration_ms) {
    return target;
  }
  if (elapsed_ms == 0U) {
    return source;
  }
  return FromOklab(
    Interpolate(ToOklab(source), ToOklab(target), elapsed_ms, duration_ms));
}
