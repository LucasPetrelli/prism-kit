#include <array>
#include <cstdint>

#include "app/app.hpp"
#include "hw/controller_command_sink.hpp"
#include "oshal/event.hpp"
#include "prism/color.hpp"
#include "prism/controller.hpp"
#include "prism/strip.hpp"

namespace {

/// @brief Rainbow colours for the 7 WS2812 LEDs at startup, with green
///     components attenuated for perceptual balance.
constexpr std::array<prism::RgbColor, 7U> kRainbowColors = {{
  {255U, 0U, 0U},    // Red
  {255U, 40U, 0U},   // Orange
  {180U, 60U, 0U},   // Yellow
  {0U, 80U, 0U},     // Green
  {0U, 0U, 255U},    // Blue
  {40U, 0U, 160U},   // Indigo
  {140U, 0U, 255U},  // Violet
}};

}  // namespace

namespace app {

AppTask& AppTask::Instance() {
  static AppTask instance;
  return instance;
}

bool AppTask::SetupTrampoline(void* context) {
  static_cast<void>(context);
  return Instance().Setup();
}

bool AppTask::LoopTrampoline(void* context) {
  static_cast<void>(context);
  return Instance().Loop();
}

bool AppTask::Setup() {
  if (prism::Initialize() < 0) {
    return false;
  }

  prism::Strip& strip = prism::GetStrip();
  led_count_ = static_cast<std::uint8_t>(strip.LedCount());
  controller_.SetStrip(&strip);

  /* Wire the command mailbox so protocol handlers can deliver commands. */
  app::hw::ControllerCommandSink::Instance().SetMailbox(&command_mailbox_);

  /* Rainbow default — one SetSingleColor instruction per LED.
   * Clamp to the actual LED count so boards with fewer pixels still work. */
  const std::uint8_t count =
    (led_count_ < kRainbowColors.size())
      ? led_count_
      : static_cast<std::uint8_t>(kRainbowColors.size());
  for (std::uint8_t i = 0U; i < count; ++i) {
    const prism::SetSingleColorPayload payload{kRainbowColors[i].red,
                                               kRainbowColors[i].green,
                                               kRainbowColors[i].blue, i};
    prism::SetSingleColor instr{payload};
    controller_.AddInstruction(&instr);
  }
  controller_.Run();
  return true;
}

prism::Controller& AppTask::GetController() { return controller_; }

void AppTask::UpdateInstructions() {
  app::hw::ControllerCommandMessage msg;
  while (command_mailbox_.Receive(&msg)) {
    switch (msg.cmd) {
      case app::hw::ControllerCommand::kSetMultipleColor: {
        const prism::SetMultipleColor instr{msg.set_multiple};
        controller_.AddInstruction(&instr);
        break;
      }
      case app::hw::ControllerCommand::kSetSingleColor: {
        const prism::SetSingleColor instr{msg.set_single};
        controller_.AddInstruction(&instr);
        break;
      }
      case app::hw::ControllerCommand::kResetInstructions:
        controller_.ResetInstructions();
        break;
      case app::hw::ControllerCommand::kRun:
        controller_.Run();
        break;
    }
  }
}

bool AppTask::Loop() {
  /* Block until a controller command arrives. */
  command_event_group_.WaitAny(kCommandEventMask, oshal::kEventWaitForever);

  /* Drain and dispatch all pending commands. */
  UpdateInstructions();
  return true;
}

}  // namespace app