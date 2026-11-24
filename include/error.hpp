#pragma once
#include <vector>

#if defined(_WIN32) || defined(__APPLE__)
#  define LONG_FMT "%lld"
#else
#  define LONG_FMT "%ld"
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define PRINTF_ATTR(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#  define PRINTF_FMT_PARAM
#elif defined(_MSC_VER)
#  include <sal.h>
#  define PRINTF_ATTR(fmt_idx, arg_idx)
#  define PRINTF_FMT_PARAM _Printf_format_string_
#else
#  define PRINTF_ATTR(fmt_idx, arg_idx)
#  define PRINTF_FMT_PARAM
#endif

// none - there are no errors. warning - there are only warnings present.
// error - there are critical errors. ignore - ignore all diagnostics.
enum ErrorSeverity: char {
   SEVERITY_NONE, SEVERITY_WARNING, SEVERITY_ERROR, SEVERITY_IGNORE
};

struct Diagnostic {
   Diagnostic(ErrorSeverity severity, char *message, size_t file, size_t line)
      : severity(severity), message(message), file(file), line(line) {}

   ErrorSeverity severity;
   char *message;
   size_t file;
   size_t line;
};

struct Diagnostics {
   std::vector<Diagnostic> diagnostics;
   ErrorSeverity severity = SEVERITY_NONE;
};

struct LexemeCache;
struct Executor;

bool shouldError(Diagnostics &diagnostics, ErrorSeverity errorSeverity);

void warn(PRINTF_FMT_PARAM Diagnostics &diagnostics, size_t file, size_t line, const char *msg, ...) PRINTF_ATTR(4, 5);
void error(PRINTF_FMT_PARAM Diagnostics &diagnostics, size_t file, size_t line, const char *msg, ...) PRINTF_ATTR(4, 5);
void clear(Diagnostics &diagnostics);

void log(Executor &executor, ErrorSeverity quitSeverity);
void logStackTrace(Executor &executor, ErrorSeverity quitSeverity);
void logMemoryLeaks(Executor &executor);
void logDiagnostic(LexemeCache &cache, Diagnostic &diagnostic, ErrorSeverity quitSeverity);
