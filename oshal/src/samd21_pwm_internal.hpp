#ifndef OSHAL_SAMD21_PWM_INTERNAL_HPP_
#define OSHAL_SAMD21_PWM_INTERNAL_HPP_

#include <soc.h>

#include <cstddef>
#include <cstdint>

#include "oshal/pwm.hpp"
#include "zephyr_samd21_dmac_channel.hpp"

namespace oshal::internal {

/// @brief SAMD21 TCC-backed PWM implementation for a fixed waveform output.
class Samd21PwmOutput : public PwmOutput {
 public:
  /// @brief Construct a SAMD21 PWM object.
  /// @param pwm_name Static diagnostic name for the PWM output.
  /// @param regs TCC register block that drives the output.
  /// @param channel_index Compare channel index used by the output.
  /// @param port_group_index PORT group index that owns the physical pin.
  /// @param port_pin_index Physical pin index inside the PORT group.
  /// @param pin_mux_value Peripheral mux value for the waveform route.
  Samd21PwmOutput(const char* pwm_name, Tcc* regs, std::uint8_t channel_index,
                  std::uint8_t port_group_index, std::uint8_t port_pin_index,
                  std::uint8_t pin_mux_value);

  /// @brief Return the diagnostic name for this physical PWM output.
  /// @return Static output name string.
  const char* name() const override;

  /// @brief Report whether backend hardware initialization completed.
  /// @return True when the backend is ready for use.
  bool is_ready() const override;

  /// @brief Configure period and pulse width for this PWM output.
  /// @param period_ns Requested period in nanoseconds.
  /// @param pulse_ns Requested pulse width in nanoseconds.
  /// @return STATUS_OK on success, or a negative status code on failure.
  int configure(std::uint32_t period_ns, std::uint32_t pulse_ns) override;

  /// @brief Update pulse width while preserving the configured period.
  /// @param pulse_ns Requested pulse width in nanoseconds.
  /// @return STATUS_OK on success, or a negative status code on failure.
  int set_pulse(std::uint32_t pulse_ns) override;

  /// @brief Enable waveform output generation.
  /// @return STATUS_OK on success, or a negative status code on failure.
  int enable() override;

  /// @brief Disable waveform output generation.
  /// @return STATUS_OK on success, or a negative status code on failure.
  int disable() override;

  /// @brief Initialize the backend hardware for this PWM output.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int initialize();

 protected:
  /// @brief Wait for synchronization of the device-global generic clock
  /// controller.
  /// @details On SAMD21, the GCLK peripheral is exposed through the MCU headers
  /// as
  ///     a single global register block rather than a per-instance object, so
  ///     this helper intentionally does not take a register pointer. This
  ///     differs from wait_for_tcc_sync(), which synchronizes a specific TCC
  ///     instance provided by the caller.
  static void wait_for_clock_sync();
  static void wait_for_tcc_sync(Tcc* regs);
  static int choose_prescaler(std::uint32_t period_ns, std::uint16_t* divisor,
                              std::uint32_t* ctrla_bits,
                              std::uint32_t* period_cycles);
  static int ns_to_cycles(std::uint32_t duration_ns,
                          std::uint16_t prescaler_divisor,
                          std::uint32_t* cycles);

  void configure_pin_mux() const;
  /// @brief Program period and pulse count registers into TCC.
  /// @param period_cycles Timer period count.
  /// @param pulse_cycles Timer pulse count.
  /// @return STATUS_OK on success, or a negative status code on failure.
  int program_waveform(std::uint32_t period_cycles, std::uint32_t pulse_cycles);
  /// @brief Stop any subclass-managed transient activity before
  /// reconfiguration.
  /// @return STATUS_OK on success, or a negative status code on failure.
  virtual int stop_transient_activity();
  /// @brief Reset cached software waveform state after hardware reset.
  void reset_waveform_state();

  /// @brief Human-readable name for diagnostics and logs.
  const char* name_;
  /// @brief TCC register block that owns this PWM channel.
  Tcc* regs_;
  /// @brief Compare channel index used by this output (WO[x]).
  std::uint8_t channel_index_;
  /// @brief PORT group index for mux configuration.
  std::uint8_t port_group_index_;
  /// @brief PORT pin index for mux configuration.
  std::uint8_t port_pin_index_;
  /// @brief Peripheral mux value for the output pin.
  std::uint8_t pin_mux_value_;
  /// @brief Backend initialization status code.
  int backend_status_;
  /// @brief True once at least one valid waveform was programmed.
  bool configured_;
  /// @brief True while the PWM output is enabled.
  bool enabled_;
  /// @brief Active TCC prescaler divisor used for ns<->cycle conversion.
  std::uint16_t prescaler_divisor_;
  /// @brief TCC CTRLA prescaler bitfield for reprogramming.
  std::uint32_t prescaler_bits_;
  /// @brief Cached configured period in nanoseconds.
  std::uint32_t period_ns_;
  /// @brief Cached configured period in timer cycles.
  std::uint32_t period_cycles_;
  /// @brief Cached configured pulse in timer cycles.
  std::uint32_t pulse_cycles_;
};

/// @brief SAMD21 PWM output with DMA-backed pulse sequencing support.
class Samd21DmaPwmOutput final : public Samd21PwmOutput,
                                 public PwmSequenceOutput {
 public:
  /// @brief Construct a SAMD21 PWM object with a dedicated DMAC channel.
  /// @param pwm_name Static diagnostic name for the PWM output.
  /// @param regs TCC register block that drives the output.
  /// @param channel_index Compare channel index used by the output.
  /// @param port_group_index PORT group index that owns the physical pin.
  /// @param port_pin_index Physical pin index inside the PORT group.
  /// @param pin_mux_value Peripheral mux value for the waveform route.
  /// @param dmac_channel_index Zero-based DMAC channel assigned for sequencing.
  Samd21DmaPwmOutput(const char* pwm_name, Tcc* regs,
                     std::uint8_t channel_index, std::uint8_t port_group_index,
                     std::uint8_t port_pin_index, std::uint8_t pin_mux_value,
                     std::uint8_t dmac_channel_index);

  /// @brief Initialize the PWM backend plus DMA routing needed for sequencing.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  /// failure.
  int initialize() override;

  /// @brief Start DMA-driven pulse playback for the configured period.
  /// @param pulse_ns_sequence Sequence of pulse widths in nanoseconds.
  /// @param pulse_count Number of entries in pulse_ns_sequence.
  /// @param repeat_count Number of full-sequence repetitions.
  /// @details repeat_count == 0 requests repeat-forever mode.
  /// @return STATUS_OK on success, or a negative status code on failure.
  int play_pulse_sequence(const std::uint32_t* pulse_ns_sequence,
                          std::size_t pulse_count,
                          std::uint32_t repeat_count) override;

  /// @brief Stop active DMA sequence playback and keep static PWM state.
  /// @return STATUS_OK.
  int stop_pulse_sequence() override;

  /// @brief Report whether DMA sequence playback is currently active.
  /// @return True when a sequence is active and no DMA error was observed.
  bool is_pulse_sequence_active() const override;

  /// @brief Report the maximum pulse count supported by this DMA staging
  ///     buffer.
  /// @return Maximum supported sequence length.
  std::size_t max_pulse_sequence_length() const override;

 protected:
  /// @brief Stop active DMA activity before base PWM reconfiguration or
  /// disable.
  /// @return STATUS_OK.
  int stop_transient_activity() override;

 private:
  /// @brief Resolve the DMAC trigger source ID for a TCC instance.
  /// @param regs TCC register block pointer.
  /// @param channel_index Waveform compare channel index.
  /// @return Trigger ID on success, or negative status code on failure.
  static int resolve_dma_trigger_source(const Tcc* regs,
                                        std::uint8_t channel_index,
                                        std::uint8_t* trigger_source);
  static void on_dma_sequence_event(void* context, bool transfer_error);
  void handle_dma_sequence_event(bool transfer_error);
  /// @brief Convert/validate pulse sequence values and stage DMA source data.
  /// @param pulse_ns_sequence Sequence of pulse widths in nanoseconds.
  /// @param pulse_count Number of sequence entries.
  /// @return STATUS_OK on success, or a negative status code on failure.
  int build_dma_sequence(const std::uint32_t* pulse_ns_sequence,
                         std::size_t pulse_count);
  /// @brief Reset sequencing runtime state after DMA setup or stop.
  void reset_sequence_state();

  /// @brief Maximum pulse entries staged by the DMA backend.
  /// @note A 7-pixel WS2812 frame needs 168 pulse entries
  ///     (7 pixels * 3 bytes/pixel * 8 bits/byte). This buffer is rounded up
  ///     to 256 entries so the staging capacity stays a simple power-of-two
  ///     size while leaving 88 entries of headroom for sequencing overhead and
  ///     modest growth beyond the minimum frame.
  static constexpr std::size_t kMaxDmaPulseCount = 256U;

  /// @brief Resolved DMAC trigger source for this TCC instance/channel.
  std::uint8_t dma_trigger_source_;
  /// @brief Dedicated DMAC channel helper used for pulse sequencing.
  Samd21DmacChannel dmac_channel_;
  /// @brief Staged pulse cycle values consumed by DMAC (reverse order layout).
  std::uint32_t dma_pulse_cycles_[kMaxDmaPulseCount];
  /// @brief Number of valid entries in dma_pulse_cycles_.
  std::size_t dma_pulse_count_;
  /// @brief Remaining non-infinite sequence repetitions.
  std::uint32_t dma_remaining_repeats_;
  /// @brief True when repeat-forever mode is active.
  bool dma_repeat_forever_;
  /// @brief True when sequence playback is currently active.
  bool dma_sequence_active_;
  /// @brief True after a DMA transfer error was observed.
  bool dma_sequence_error_;
};

}  // namespace oshal::internal

#endif /* OSHAL_SAMD21_PWM_INTERNAL_HPP_ */