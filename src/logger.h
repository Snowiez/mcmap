#ifndef LOGGER_H_
#define LOGGER_H_

#include <chrono>
#include <fmt/color.h>
#include <fmt/core.h>
#include <unistd.h>

namespace logger {

enum levels {
  INFO,
  WARNING,
  ERROR,
  DEBUG,
};

void vinfo(const char *format, fmt::format_args args);
void vwarning(const char *format, fmt::format_args args);
void verror(const char *format, fmt::format_args args);
void vdebug(const char *format, fmt::format_args args);

template <typename... Args>
void info(const char *format, const Args &... args) {
  vinfo(format, fmt::make_format_args(args...));
}

template <typename... Args>
void warn(const char *format, const Args &... args) {
  vwarning(format, fmt::make_format_args(args...));
}

template <typename... Args>
void error(const char *format, const Args &... args) {
  verror(format, fmt::make_format_args(args...));
}

template <typename... Args>
void debug(const char *format, const Args &... args) {
  vdebug(format, fmt::make_format_args(args...));
}

void setDebug();

void printProgress(const std::string label, const uint64_t current,
                   const uint64_t max);

} // namespace logger

#endif
