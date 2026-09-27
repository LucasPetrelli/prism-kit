#include "controller_command_dispatch.hpp"

#include "hw/controller_command.hpp"
#include "prism/controller.hpp"
#include "prism/instruction.hpp"

namespace app {
namespace {

void StartAndTick(prism::Controller& controller, prism::LoopCount repeats,
                  ControllerCommandResult& result) {
  result.start_attempted = true;
  result.start_status = controller.Start(repeats);
  if (result.start_status != prism::ControllerStatus::kSuccess) {
    return;
  }

  result.initial_run_attempted = true;
  result.initial_run_status = controller.Run();
}

}  // namespace

ControllerCommandResult DispatchControllerCommand(
  prism::Controller& controller,
  const app::hw::ControllerCommandMessage& message) {
  ControllerCommandResult result{};
  switch (message.cmd) {
    case app::hw::ControllerCommand::kSetMultipleColor: {
      const prism::SetMultipleColor instruction{message.set_multiple};
      result.mutation_attempted = true;
      result.mutation_status = controller.AddInstruction(&instruction);
      break;
    }
    case app::hw::ControllerCommand::kSetSingleColor: {
      const prism::SetSingleColor instruction{message.set_single};
      result.mutation_attempted = true;
      result.mutation_status = controller.AddInstruction(&instruction);
      break;
    }
    case app::hw::ControllerCommand::kSetMultipleColorHsv: {
      const prism::SetMultipleColorHsv instruction{message.set_multiple_hsv};
      result.mutation_attempted = true;
      result.mutation_status = controller.AddInstruction(&instruction);
      break;
    }
    case app::hw::ControllerCommand::kSetSingleColorHsv: {
      const prism::SetSingleColorHsv instruction{message.set_single_hsv};
      result.mutation_attempted = true;
      result.mutation_status = controller.AddInstruction(&instruction);
      break;
    }
    case app::hw::ControllerCommand::kSetMultipleColorGrouped: {
      const auto& grouped = message.grouped_set_multiple;
      const prism::SetMultipleColorPayload payload{
        grouped.mark, grouped.r, grouped.g, grouped.b, grouped.range};
      prism::SetMultipleColor instruction{payload};
      instruction.group_id = grouped.group_id;
      result.mutation_attempted = true;
      result.mutation_status = controller.AddInstruction(&instruction);
      break;
    }
    case app::hw::ControllerCommand::kSetSingleColorGrouped: {
      const auto& grouped = message.grouped_set_single;
      const prism::SetSingleColorPayload payload{
        grouped.mark, grouped.r, grouped.g, grouped.b, grouped.index};
      prism::SetSingleColor instruction{payload};
      instruction.group_id = grouped.group_id;
      result.mutation_attempted = true;
      result.mutation_status = controller.AddInstruction(&instruction);
      break;
    }
    case app::hw::ControllerCommand::kSetMultipleColorHsvGrouped: {
      const auto& grouped = message.grouped_set_multiple_hsv;
      const prism::SetMultipleColorHsvPayload payload{
        grouped.mark, grouped.h, grouped.s, grouped.v, grouped.range};
      prism::SetMultipleColorHsv instruction{payload};
      instruction.group_id = grouped.group_id;
      result.mutation_attempted = true;
      result.mutation_status = controller.AddInstruction(&instruction);
      break;
    }
    case app::hw::ControllerCommand::kSetSingleColorHsvGrouped: {
      const auto& grouped = message.grouped_set_single_hsv;
      const prism::SetSingleColorHsvPayload payload{
        grouped.mark, grouped.h, grouped.s, grouped.v, grouped.index};
      prism::SetSingleColorHsv instruction{payload};
      instruction.group_id = grouped.group_id;
      result.mutation_attempted = true;
      result.mutation_status = controller.AddInstruction(&instruction);
      break;
    }
    case app::hw::ControllerCommand::kRunGroup: {
      const auto& run_group = message.run_group;
      const prism::RunGroupPayload payload{
        run_group.parent_group_id, run_group.mark, run_group.target_group_id,
        run_group.additional_repeats};
      const prism::RunGroupInstruction instruction{payload};
      result.mutation_attempted = true;
      result.mutation_status = controller.AddInstruction(&instruction);
      break;
    }
    case app::hw::ControllerCommand::kResetInstructions:
      controller.ResetInstructions();
      break;
    case app::hw::ControllerCommand::kRun:
      StartAndTick(controller, 0U, result);
      break;
    case app::hw::ControllerCommand::kStart:
      StartAndTick(controller, message.start.root_additional_repeats, result);
      break;
    default:
      result.mutation_attempted = true;
      result.mutation_status = prism::ControllerStatus::kInvalidArgument;
      break;
  }
  return result;
}

prism::ControllerStatus RunControllerProgress(prism::Controller& controller) {
  return controller.Run();
}

}  // namespace app
