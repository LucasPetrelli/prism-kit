# Controller Timeline
[ctrl-hdr]: ../../include/prism/controller.hpp

## Goal
Allow Controller ([controller.hpp][ctrl-hdr]) to time instruction execution. Instructions will have a "mark" atribute that tells when they should start. Controller keeps track of instructions that are ongoing on their slots.

## Timing Diagram
Considering
- "Set" as an instant instruction that sets a color.
- "Fade" a timed instruction that gradualy fades a color.
- "Grad" a timed instruction that transition from a color to another over time.

```plantuml
concise "Slot 1" as s1
concise "Slot 2" as s2
concise "Slot n" as sn

@0
s1 is {-}
s2 is {-}
sn is {-}

@1
s1 is Set

@2
s1 is Set

@3
s1 is Fade
s2 is Set

@4
s2 is {-}

@5
s2 is Grad

@6
sn is Set

@7
s1 is {-}
sn is {-}

@8
s1 is Set

@9
s1 is {-}
s2 is {-}

```

## Requirements
- Instruction base class to receive a "Mark mark" attribute.
  - Mark is uint16_t as initial implementation, hidden behing typedef.
- Instruction flow changes slightly
  - Begin a run by checking for instructions that hit the mark.
    - Perhaps re-purpose "PickNewInstructions".
  - Then "DrainExecuting".
- Instruction list should be sorted by "mark".
- Look ahead should keep the "scheduled timeout" calculation, but peek at the pending instruction list to add the next instruction's mark in consideration.
  - Either use an index, pointer or custom iterator for keeping track of the next instruction on the instruction list.