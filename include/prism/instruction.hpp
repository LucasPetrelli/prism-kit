#ifndef PRISM_INSTRUCTION_HPP_
#define PRISM_INSTRUCTION_HPP_

#include "prism/controller_instruction.hpp"
#include "prism/fade.hpp"
#include "prism/gradient.hpp"
#include "prism/instruction_memory_slot.hpp"
#include "prism/instruction_types.hpp"
#include "prism/pending_instruction_queue_view.hpp"
#include "prism/run_group_instruction.hpp"
#include "prism/set_multiple_color.hpp"
#include "prism/set_multiple_color_hsv.hpp"
#include "prism/set_single_color.hpp"
#include "prism/set_single_color_hsv.hpp"
#if defined(PRISM_CONTROLLER_TESTING)
#include "prism/test/timed_instruction.hpp"
#endif

#endif  // PRISM_INSTRUCTION_HPP_
