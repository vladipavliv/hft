/**
 * @author Vladimir Pavliv
 * @date 2025-02-10
 */

#ifndef HFT_COMMON_CONTROLCENTERBASE_HPP
#define HFT_COMMON_CONTROLCENTERBASE_HPP

#include <deque>
#include <map>
#include <memory>
#include <spdlog/spdlog.h>

#include "boost_types.hpp"
#include "config/config.hpp"
#include "console_input_parser.hpp"
#include "logger_manager.hpp"
#include "market_types.hpp"
#include "utils/string_utils.hpp"
#include "utils/utils.hpp"

namespace hft {

/**
 * @brief Base for control center class that periodically checks console for input commands,
 * posts them to a command sink, and processes service commands like switching log level
 * and switch to monitoring mode that turns default logger off and wites stats to a console
 */
template <typename CommandType, typename SinkType>
class ControlCenterBase {
public:
  using Command = CommandType;
  using Sink = SinkType;
  using ConsoleParser = ConsoleInputParser<Command>;

  ControlCenterBase(Sink &sink, std::map<std::string, Command> &&cmdMap)
      : mSink{sink}, mConsoleParser{std::move(cmdMap)}, mTimer{mSink.ctx()},
        mMonitorRate{Config::cfg.monitorRateS},
        mMonitorTimestamp{std::chrono::system_clock::now()} {
    scheduleTimer();
  }

private:
  void scheduleTimer() {
    mTimer.expires_after(MilliSeconds(100));
    mTimer.async_wait([this](BoostErrorRef ec) {
      if (ec) {
        return;
      }
      monitorMode();
      processCommands();
      scheduleTimer();
    });
  }

  void processCommands() {
    auto cmdRes = mConsoleParser.getCommand();
    while (cmdRes.ok()) {
      auto cmd = cmdRes.value();
      spdlog::info(utils::toString(cmd));
      switch (cmd) {
      case Command::MonitorModeStart:
        spdlog::info("monitor mode on");
        Config::cfg.monitorStats = true;
        switchMode(true);
        break;
      case Command::MonitorModeStop:
        spdlog::info("monitor mode off");
        Config::cfg.monitorStats = false;
        switchMode(false);
        break;
      case Command::MonitorModeSwitch:
        spdlog::info("monitor mode {}", mMonitorMode ? "off" : "on");
        Config::cfg.monitorStats = !mMonitorMode;
        switchMode(!mMonitorMode);
        break;
      case Command::LogLevelUp:
        LoggerManager::switchLogLevel(true);
        spdlog::info("log level {}", utils::toString(spdlog::get_level()));
        break;
      case Command::LogLevelDown:
        LoggerManager::switchLogLevel(false);
        spdlog::info("log level {}", utils::toString(spdlog::get_level()));
        break;
      default:
        mSink.controlSink.onCommand(cmd);
        break;
      }
      cmdRes = mConsoleParser.getCommand();
    }
  }

  void monitorMode() {
    if (!mMonitorMode) {
      return;
    }
    // Trigger stats collecting if needed
    auto now = std::chrono::system_clock::now();
    if (std::chrono::duration_cast<Seconds>(now - mMonitorTimestamp) < mMonitorRate) {
      return;
    }
    mMonitorTimestamp = now;
    mSink.controlSink.onCommand(Command::CollectStats);
  }

  void switchMode(bool monitorMode) {
    mMonitorMode = monitorMode;
    if (monitorMode) {
      LoggerManager::toMonitorMode();
    } else {
      LoggerManager::toNormalMode();
    }
  }

private:
  Sink &mSink;
  ConsoleParser mConsoleParser;
  SteadyTimer mTimer;

  Seconds mMonitorRate;
  bool mMonitorMode{false};
  Timestamp mMonitorTimestamp;
};

} // namespace hft

#endif // HFT_COMMON_CONTROLCENTERBASE_HPP