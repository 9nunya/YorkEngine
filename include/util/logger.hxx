#ifndef YORK_LOGGER_HXX
#define YORK_LOGGER_HXX
#include <algorithm>
#include <format>
#include <iostream>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// The build system bakes the project root into the binary, so the absolute
// source paths the compiler hands out can be cut back down to the paths that
// are actually useful in a log line.
#ifndef YORK_SOURCE_ROOT
#define YORK_SOURCE_ROOT "."
#endif

namespace york::util {
    enum LogLevel {
        Info, Warn, Error, Debug
    };

    // Titled logs need the title to be distinguishable from the format string at
    // the call site. A bare std::string cannot do that: std::format_string is
    // consteval, so it hard-errors on a non-literal first argument before any
    // better overload is considered, and a literal matches both overloads and is
    // ambiguous. An explicit tag makes the intent unambiguous at every call site.
    struct Title {
        std::string_view value;
        constexpr Title(std::string_view v) noexcept : value(v) {}
    };

    class Logger {
    public:
#define YORK_LOG_LEVEL(LEVEL, FN)                                                     \
    template<typename... Args>                                                        \
    static void FN(std::format_string<std::type_identity_t<Args>...> fmt,              \
                   Args &&...args) {                                                   \
        emit(LEVEL, {}, {}, fmt, std::forward<Args>(args)...);                         \
    }                                                                                  \
    template<typename... Args>                                                        \
    static void FN(Title title,                                                        \
                   std::format_string<std::type_identity_t<Args>...> fmt,              \
                   Args &&...args) {                                                   \
        emit(LEVEL, title.value, {}, fmt, std::forward<Args>(args)...);                \
    }                                                                                  \
    template<typename... Args>                                                        \
    static void FN(std::source_location loc,                                           \
                   std::format_string<std::type_identity_t<Args>...> fmt,              \
                   Args &&...args) {                                                   \
        emit(LEVEL, {}, loc, fmt, std::forward<Args>(args)...);                        \
    }                                                                                  \
    template<typename... Args>                                                        \
    static void FN(std::source_location loc, Title title,                               \
                   std::format_string<std::type_identity_t<Args>...> fmt,              \
                   Args &&...args) {                                                   \
        emit(LEVEL, title.value, loc, fmt, std::forward<Args>(args)...);               \
    }

        YORK_LOG_LEVEL(Info, info)
        YORK_LOG_LEVEL(Warn, warn)
        YORK_LOG_LEVEL(Error, error)
        YORK_LOG_LEVEL(Debug, debug)

#undef YORK_LOG_LEVEL

    private:
        // Turns the absolute path the compiler reports into a path relative
        // to the project root.
        [[nodiscard]] static std::string relativePath(std::string_view fileName) {
            constexpr std::string_view root{YORK_SOURCE_ROOT};
            if (root.empty() || root == "." || fileName.size() <= root.size())
                return std::string{fileName};

            // The compiler may hand out backslashes on Windows, the build
            // system always uses forward slashes.
            std::string path{fileName};
            std::ranges::replace(path, '\\', '/');

            if (!path.starts_with(root))
                return path;

            std::string_view relative{path};
            relative.remove_prefix(root.size());

            // The root has to be a whole path segment, otherwise a directory
            // that merely starts with the same characters would be cut in
            // half.
            if (relative.empty() || relative.front() != '/')
                return path;

            relative.remove_prefix(1);
            return relative.empty() ? path : std::string{relative};
        }

        template<typename... Args>
        static void emit(LogLevel level, std::string_view title,
                         std::source_location loc,
                         std::format_string<std::type_identity_t<Args>...> fmt, Args&&... args) {
            write(level, title, loc,
                  std::vformat(fmt.get(), std::make_format_args(args...)));
        }

        static void write(LogLevel level, std::string_view title,
                          std::source_location loc, std::string_view body) {
            // An empty std::source_location means the call site was not supplied
            // (bare overloads). Print no prefix rather than a bogus ":0".
            if (loc.line() == 0) {
                if (title.empty())
                    std::cout << std::format("{}: {}\n", levelToString(level), body);
                else
                    std::cout << std::format("{} [{}]: {}\n", levelToString(level), title, body);
            } else if (title.empty())
                std::cout << std::format("{}:{} {}: {}\n",
                                         relativePath(loc.file_name()), loc.line(),
                                         levelToString(level), body);
            else
                std::cout << std::format("{}:{} {} [{}]: {}\n",
                                         relativePath(loc.file_name()), loc.line(),
                                         levelToString(level), title, body);
        }

        static constexpr std::string_view levelToString(LogLevel level) {
            switch (level) {
                case Debug:
                    return "DEBUG";
                case Info:
                    return "INFO";
                case Warn:
                    return "WARN";
                case Error:
                    return "ERROR";
            }

            return "HOW TF DID YOU GET HERE";
        }
    };
}

// std::source_location must be the FIRST parameter: a parameter pack absorbs
// every parameter after it, so a trailing defaulted location would be pack
// element #2 and deduction would demand your first format argument convert to
// std::source_location. These macros supply the call site so you never write it.
#define LOG_INFO(...)  ::york::util::Logger::info(::std::source_location::current(), __VA_ARGS__)
#define LOG_WARN(...)  ::york::util::Logger::warn(::std::source_location::current(), __VA_ARGS__)
#define LOG_ERROR(...) ::york::util::Logger::error(::std::source_location::current(), __VA_ARGS__)
#define LOG_DEBUG(...) ::york::util::Logger::debug(::std::source_location::current(), __VA_ARGS__)

#endif //YORK_LOGGER_HXX