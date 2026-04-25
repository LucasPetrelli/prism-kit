#include "zephyr_samd21_dmac_channel.hpp"

#include <soc.h>
#include <zephyr/irq.h>

#include <cstddef>
#include <cstdint>

#include "oshal/status.h"

namespace {

using oshal::internal::Samd21DmacChannel;

extern "C" void DMAC_Handler(void);

constexpr unsigned int kDmacIrqPriority = 0U;
constexpr std::uint32_t kDmacIrqFlags = 0U;

void zephyr_dmac_irq_handler(const void*) { DMAC_Handler(); }

/* Descriptor section consumed by hardware fetches for all channels. */
alignas(16) DmacDescriptor g_dmac_descriptors[DMAC_CH_NUM];
/* Write-back section where DMAC stores runtime descriptor state. */
alignas(16) DmacDescriptor g_dmac_writeback_descriptors[DMAC_CH_NUM];
/* Per-channel owner lookup used by the shared DMAC IRQ handler. */
Samd21DmacChannel* g_dmac_channel_owners[DMAC_CH_NUM] = {};
/* One-time controller initialization guard for shared DMAC registers. */
bool g_dmac_controller_initialized = false;

}  // namespace

namespace oshal::internal {

/* Constructor only captures immutable channel identity and resets software
 * state. */
Samd21DmacChannel::Samd21DmacChannel(std::uint8_t channel_index)
    : channel_index_(channel_index),
      configured_(false),
      running_(false),
      loop_forever_(false),
      callback_(nullptr),
      callback_context_(nullptr) {}

int Samd21DmacChannel::initialize_controller() {
  /* DMAC is a singleton peripheral, so initialize global register state once.
   */
  if (g_dmac_controller_initialized) {
    return STATUS_OK;
  }

  PM->AHBMASK.reg |= PM_AHBMASK_DMAC;
  PM->APBBMASK.reg |= PM_APBBMASK_DMAC;

  DMAC->CTRL.bit.SWRST = 1;
  while (DMAC->CTRL.bit.SWRST != 0U) {
  }

  DMAC->BASEADDR.reg = reinterpret_cast<std::uint32_t>(&g_dmac_descriptors[0]);
  DMAC->WRBADDR.reg =
    reinterpret_cast<std::uint32_t>(&g_dmac_writeback_descriptors[0]);
  DMAC->CTRL.reg = DMAC_CTRL_DMAENABLE | DMAC_CTRL_LVLEN0;

  IRQ_CONNECT(DMAC_IRQn, kDmacIrqPriority, zephyr_dmac_irq_handler, nullptr,
              kDmacIrqFlags);
  NVIC_ClearPendingIRQ(DMAC_IRQn);
  irq_enable(DMAC_IRQn);

  g_dmac_controller_initialized = true;
  return STATUS_OK;
}

/* DMAC channel-local registers are multiplexed through CHID selection. */
void Samd21DmacChannel::select_channel(std::uint8_t channel_index) {
  DMAC->CHID.reg = DMAC_CHID_ID(channel_index);
}

int Samd21DmacChannel::configure_word_stream(
  const volatile std::uint32_t* destination_register,
  const std::uint32_t* source_words, std::size_t word_count,
  std::uint8_t trigger_source, bool loop_forever, CompletionCallback callback,
  void* callback_context) {
  int ret;
  DmacDescriptor* descriptor;

  if ((destination_register == nullptr) || (source_words == nullptr) ||
      (word_count == 0U) || (word_count > 0xFFFFU) ||
      (channel_index_ >= DMAC_CH_NUM)) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  descriptor = &g_dmac_descriptors[channel_index_];

  ret = initialize_controller();
  if (ret < 0) {
    return ret;
  }

  select_channel(channel_index_);
  /* Disable and clear channel state before writing a new descriptor contract.
   */
  DMAC->CHCTRLA.bit.ENABLE = 0;
  DMAC->CHINTENCLR.reg = DMAC_CHINTENCLR_TERR | DMAC_CHINTENCLR_TCMPL;
  DMAC->CHINTFLAG.reg =
    DMAC_CHINTFLAG_TERR | DMAC_CHINTFLAG_TCMPL | DMAC_CHINTFLAG_SUSP;

  /* Source pointer must be end-addressed when SRCINC is enabled on SAMD21 DMAC.
   */
  descriptor->BTCTRL.reg =
    DMAC_BTCTRL_VALID | DMAC_BTCTRL_BEATSIZE_WORD | DMAC_BTCTRL_SRCINC;
  descriptor->BTCNT.reg = static_cast<std::uint16_t>(word_count);
  descriptor->SRCADDR.reg = reinterpret_cast<std::uint32_t>(
    source_words + static_cast<std::ptrdiff_t>(word_count));
  descriptor->DSTADDR.reg =
    reinterpret_cast<std::uint32_t>(destination_register);
  descriptor->DESCADDR.reg =
    loop_forever ? reinterpret_cast<std::uint32_t>(descriptor) : 0U;

  DMAC->CHCTRLB.reg = DMAC_CHCTRLB_LVL(0U) | DMAC_CHCTRLB_TRIGACT_BEAT |
                      DMAC_CHCTRLB_TRIGSRC(trigger_source);
  DMAC->CHINTENSET.reg =
    DMAC_CHINTENSET_TERR |
    (loop_forever ? 0U : static_cast<std::uint8_t>(DMAC_CHINTENSET_TCMPL));

  g_dmac_channel_owners[channel_index_] = this;
  configured_ = true;
  running_ = false;
  loop_forever_ = loop_forever;
  callback_ = callback;
  callback_context_ = callback_context;
  return STATUS_OK;
}

int Samd21DmacChannel::start() {
  if (!configured_) {
    return STATUS_ERR_NOT_READY;
  }

  select_channel(channel_index_);
  /* Clear stale flags so completion/error handling reflects only the new run.
   */
  DMAC->CHINTFLAG.reg =
    DMAC_CHINTFLAG_TERR | DMAC_CHINTFLAG_TCMPL | DMAC_CHINTFLAG_SUSP;
  DMAC->CHCTRLA.bit.ENABLE = 1;
  running_ = true;
  return STATUS_OK;
}

int Samd21DmacChannel::stop() {
  if (!configured_) {
    return STATUS_OK;
  }

  select_channel(channel_index_);
  /* Always clear enable/interrupt state so a subsequent start is deterministic.
   */
  DMAC->CHCTRLA.bit.ENABLE = 0;
  DMAC->CHINTENCLR.reg = DMAC_CHINTENCLR_TERR | DMAC_CHINTENCLR_TCMPL;
  DMAC->CHINTFLAG.reg =
    DMAC_CHINTFLAG_TERR | DMAC_CHINTFLAG_TCMPL | DMAC_CHINTFLAG_SUSP;
  running_ = false;
  return STATUS_OK;
}

bool Samd21DmacChannel::is_running() const { return configured_ && running_; }

void Samd21DmacChannel::handle_interrupt() {
  std::uint8_t channel_flags;
  bool transfer_error;
  bool transfer_complete;

  select_channel(channel_index_);
  channel_flags = DMAC->CHINTFLAG.reg;
  if (channel_flags == 0U) {
    return;
  }

  DMAC->CHINTFLAG.reg = channel_flags;
  transfer_error = (channel_flags & DMAC_CHINTFLAG_TERR) != 0U;
  transfer_complete = (channel_flags & DMAC_CHINTFLAG_TCMPL) != 0U;

  if (transfer_error || (transfer_complete && !loop_forever_)) {
    running_ = false;
  }

  if (callback_ != nullptr) {
    callback_(callback_context_, transfer_error);
  }
}

}  // namespace oshal::internal

extern "C" void DMAC_Handler(void) {
  /* DMAC uses one IRQ for all channels, so dispatch through INTPEND.ID. */
  while (DMAC->INTSTATUS.reg != 0U) {
    const std::uint8_t channel_id = DMAC->INTPEND.bit.ID;

    if ((channel_id < DMAC_CH_NUM) &&
        (g_dmac_channel_owners[channel_id] != nullptr)) {
      g_dmac_channel_owners[channel_id]->handle_interrupt();
    } else {
      DMAC->CHID.reg = DMAC_CHID_ID(channel_id);
      DMAC->CHINTFLAG.reg = DMAC->CHINTFLAG.reg;
    }
  }
}