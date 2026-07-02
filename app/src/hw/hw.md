# app/src/hw/ — HW Executor Classes

## Class Diagram

```plantuml
@startuml
skinparam backgroundColor #FEFEFE
skinparam class {
  BorderColor #2C3E50
  BackgroundColor #ECF0F1
}

' ============================================================
' External types (OSHal, BAL, Protocol, Prism Kit)
' ============================================================

package "OSHal" {
  class oshal::TaskHandle <<external>> {
    + IsValid()
    + HasExited()
    + ExitCode()
    + {static} Create(handle, config)
  }

  class oshal::EventFlagGroup <<external>> {
    + Post(events)
    + WaitAny(events, timeout)
    + Clear(events)
  }

  class oshal::EventMailbox<M,C> <<external>> {
    + Send(msg)
    + Receive(out)
  }

  class oshal::DebugPort <<external>> {
    + {abstract} Printf(...)
    + {abstract} Vprintf(...)
    + {abstract} Name()
  }

  class oshal::SerialPort <<external>> {
    + {abstract} Read(buf, len)
    + {abstract} Write(data, len)
    + SetRxEvent(event_group, mask)
  }
}

package "BAL" {
  class bal::Ws2812Strip <<external>> {
    + {abstract} Name()
    + {abstract} LedCount()
    + {abstract} Led(index)
    + {abstract} Show()
  }

  class bal::Led <<external>> {
    + {abstract} Toggle()
  }
}

package "Protocol" {
  class protocol::Protocol <<external>> {
    + Run()
  }
}

package "Prism Kit" {
  class prism::Strip <<external>> {
    + {abstract} Name()
    + {abstract} IsReady()
    + {abstract} LedCount()
    + {abstract} Led(index)
    + {abstract} Fill(color)
    + {abstract} Show()
  }

  class prism::StripLed <<external>> {
    + {abstract} IsReady()
    + {abstract} SetColor(color)
    + {abstract} Color()
    + {abstract} Index()
  }

  struct prism::RgbColor <<external>> {
    red : uint8
    green : uint8
    blue : uint8
  }
}

' ============================================================
' IPC types
' ============================================================

struct app::hw::SharedFrame {
  led_count : size_t
  colors : array<RgbColor, 16>
}

' ============================================================
' Our classes
' ============================================================

package "app::hw" {

  class HwTask {
    - event_group_ : EventFlagGroup
    - status_led_ : StatusLed
    - strip_manager_ : StripManager
    - task_ : TaskHandle
    ..
    + {static} Instance() : HwTask&
    + Start(name, stack_size_bytes, priority) : int
    + IsRunning() : bool
    + HasExited() : bool
    + ExitCode(out) : int
    + EventGroup() : EventFlagGroup&
    + GetStatusLed() : StatusLed&
    + GetStrip() : StripManager&
    - Setup() : bool
    - Loop() : bool
    - {static} SetupTrampoline(ctx) : bool
    - {static} LoopTrampoline(ctx) : bool
  }

  class StripManager {
    - event_group_ : EventFlagGroup&
    - mailbox_ : EventMailbox<sizeof(SharedFrame), 1U>
    - backend_strip_ : Ws2812Strip*
    - ready_ : bool
    - led_count_ : size_t
    - name_ : const char*
    - staged_frame_ : SharedFrame
    - led_views_ : array<StripLedView, kSharedFrameCapacity>
    ..
    + {static} kFrameEventMask : uint32
    + StripManager(event_group : EventFlagGroup&)
    + Configure(strip, count, name)
    + Name() : const char*
    + IsReady() : bool
    + LedCount() : size_t
    + Led(index) : StripLed*
    + Led(index) : const StripLed* {const}
    + Fill(color) : int
    + Show() : int
    + TryApplyLatest() : bool
    + EventGroup() : EventFlagGroup&
    + FrameEventMask() : uint32
    + SetLedColor(index, color) : int
    + LedColor(index) : RgbColor
    - ApplyFrame(frame) : int
  }

  class StripLedView {
    - manager_ : StripManager*
    - index_ : size_t
    ..
    + SetIndex(index)
    + SetManager(manager)
    + IsReady() : bool
    + SetColor(color) : int
    + Color() : RgbColor
    + Index() : size_t
  }

  class StatusLed {
    - led_ : Led*
    - blink_half_period_ticks_ : uint32
    - blink_tick_ : uint32
    ..
    + {static} kBlinkHalfPeriodMs : uint32 = 1000
    + Configure(led, idle_sleep_ms)
    + Blink() : bool
  }

  class CommandManager {
    - command_port_ : SerialPort*
    - debug_port_ : DebugPort*
    - protocol_ : Protocol
    ..
    + {static} kCommandRxEventMask : uint32
    + {static} Instance() : CommandManager&
    + Configure(port, debug, event_group)
    + Run()
    + PrintBanner(strip_name) : bool
    + CommandPort() : SerialPort*
    + RxEventMask() : uint32
    - {static} ReadAdapter(buf, len) : uint32
    - {static} WriteAdapter(data, len) : bool
    - {static} DebugPrintfAdapter(fmt, ...) : int
  }
}

' ============================================================
' Relationships
' ============================================================

HwTask         *---> oshal::EventFlagGroup : "owns"
HwTask         *---> oshal::TaskHandle
HwTask         *---> StatusLed : "owns"
HwTask         *---> StripManager : "owns"

StripManager    ---|> prism::Strip
StripManager    *---> oshal::EventFlagGroup : "ref"
StripManager    *---> oshal::EventMailbox<sizeof(SharedFrame),1U>
StripManager    *---> bal::Ws2812Strip : "ref"
StripManager    *---* StripLedView : "array<kSharedFrameCapacity>"
StripLedView     ---|> prism::StripLed
StripLedView     ..> StripManager : "delegates via manager_ pointer"

StatusLed       *---> bal::Led : "ref"

CommandManager  *---> oshal::SerialPort : "ref"
CommandManager  *---> oshal::DebugPort : "ref"
CommandManager  *---> protocol::Protocol

SharedFrame       ..> prism::RgbColor : "contains array<kSharedFrameCapacity>"
StripManager      ..> SharedFrame : "mailbox IPC"

' Coordinator
note top of HwTask
  HwTask and CommandManager are
  process-wide singletons. StatusLed
  and StripManager are owned by value
  inside HwTask. StartHwExecutor()
  in app_hw.cpp wires them together
  and launches the task loop.
end note

@enduml
```

## Construction Order

`HwTask` is a process-wide singleton (`HwTask::Instance()`, defined in
`hw_task.cpp`).  It owns `EventFlagGroup`, `StatusLed`, and `StripManager`
by value.  Construction order is implicit in the member declaration order:

1. `EventFlagGroup event_group_` — constructed first
2. `StatusLed status_led_` — default construction
3. `StripManager strip_manager_{event_group_}` — receives ref to `event_group_`

`CommandManager` is a separate process-wide singleton (`CommandManager::Instance()`,
defined in `command_manager.cpp`).  Its `protocol_` member stores static
adapter function pointers, so no direct reference to the HwTask event group is
needed at construction time.

`StartHwExecutor()` (declared in `hw_coordinator.hpp`, defined in `app_hw.cpp`)
wires everything together and launches the task loop.

## Ownership & Lifecycle

| Instance | Scope | Pattern |
|---|---|---|
| `HwTask` | `hw_task.cpp` anonymous ns | `HwTask::Instance()` |
| `event_group_` | Member of `HwTask` | Owned by value |
| `strip_manager_` | Member of `HwTask` | Owned by value, accessed via `HwTask::GetStrip()` |
| `status_led_` | Member of `HwTask` | Owned by value, accessed via `HwTask::GetStatusLed()` |
| `g_command_manager` | `command_manager.cpp` anonymous ns | `CommandManager::Instance()` |

## Event Flow

```
APP task                              app_hw task
─────────                             ──────────
StripManager::Fill()  ──┐
                        │
StripManager::Show()  ──┤── mailbox_.Send() ──▶ mailbox (posts kFrameEventMask
                        │                       to event_group_ internally)
                        │                       │
                        │              event_group_.WaitAny( frame_mask
                        │                       │          | rx_mask,
                        │                       │          kTaskIdleSleepMs)
                        │                       │
                        │                       ├── TryApplyLatest()
                        │                       │     └── ApplyFrame()
                        │                       │
                        │                       ├── cmd_mgr.Run()
                        │                       │     (only if rx events)
                        │                       │
                        │                       └── status_led_.Blink()
```

## Supporting Files

| File | Purpose |
|------|---------|
| `hw_constants.hpp` | `kTaskIdleSleepMs`, `kTaskStackSizeBytes`, `kTaskPriority` |
| `hw_coordinator.hpp` | Declares `StartHwExecutor()` (defined in `app_hw.cpp`) |
| `shared_frame.hpp` | `kSharedFrameCapacity`, `SharedFrame` struct |
