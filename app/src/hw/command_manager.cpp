#include "command_manager.hpp"

#include <cstdarg>
#include <cstdio>

namespace {

app::hw::CommandManager g_command_manager;

}  // namespace

namespace app::hw {

CommandManager& CommandManager::Instance() { return g_command_manager; }

void CommandManager::Configure(oshal::SerialPort* command_port,
                               oshal::DebugPort* debug_port,
                               oshal::EventFlagGroup* event_group) {
  command_port_ = command_port;
  debug_port_ = debug_port;

  if (command_port_ != nullptr) {
    command_port_->set_rx_event(event_group, kCommandRxEventMask);
  }
}

void CommandManager::Run() { protocol_.run(); }

bool CommandManager::PrintBanner(const char* strip_name) {
  if (debug_port_ == nullptr) {
    return false;
  }

  return debug_port_->printf("DebugPort online on %s, strip on %s\n",
                             debug_port_->name(), strip_name) >= 0;
}

std::uint32_t CommandManager::ReadAdapter(std::uint8_t* buffer,
                                          std::uint32_t length) {
  auto& self = Instance();
  auto* port = self.command_port_;
  if (port == nullptr) {
    return 0U;
  }

  const int ret = port->read(buffer, static_cast<std::size_t>(length));
  return ret < 0 ? 0U : static_cast<std::uint32_t>(ret);
}

bool CommandManager::WriteAdapter(const std::uint8_t* data,
                                  std::uint32_t length) {
  auto& self = Instance();
  auto* port = self.command_port_;
  if (port == nullptr) {
    return false;
  }

  return port->write(data, static_cast<std::size_t>(length)) >= 0;
}

int CommandManager::DebugPrintfAdapter(const char* fmt, ...) {
  auto& self = Instance();
  auto* dbg = self.debug_port_;
  if (dbg == nullptr) {
    return -1;
  }

  std::va_list args;
  va_start(args, fmt);
  const int ret = dbg->vprintf(fmt, args);
  va_end(args);
  return ret;
}

}  // namespace app::hw
