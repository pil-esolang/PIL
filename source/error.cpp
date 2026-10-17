#include "cache.hpp"
#include "pil.hpp"
#include <cstdarg>

constexpr const char *PROGRAM_EXITED = "\e[0;31mProgram exited with error code %d.\e[0m\n";
constexpr const char *STACKTRACE_INFO = "\e[0;34mStack Trace\e[0m";
constexpr const char *ERROR_TEXT = "\e[0;31mError\e[0m";
constexpr const char *WARNING_TEXT = "\e[0;33mWarning\e[0m";

constexpr const char *LEAKED_OBJECTS = "\e[0;31mProgram leaked %zu %s.\e[0m\n";
constexpr const char *AND_N_OTHERS = "\e[0;34mAnd %zu others...\e[0m\n";
constexpr size_t MAX_LEAK_TRACE = 5;

bool shouldError(Diagnostics &diagnostics, ErrorSeverity errorSeverity) {
   return diagnostics.severity >= errorSeverity;
}

void warn(Diagnostics &diagnostics, size_t file, size_t line, const char *msg, ...) {
   va_list args, copy;
   va_start(args, msg);
   va_copy(copy, args);
   int len = vsnprintf(nullptr, 0, msg, copy) + 1; // null-terminator
   va_end(copy);
   char *message = (char*)malloc(len);
   vsnprintf(message, len, msg, args);
   va_end(args);

   diagnostics.diagnostics.emplace_back(SEVERITY_WARNING, message, file, line);
   diagnostics.severity = std::max(diagnostics.severity, SEVERITY_WARNING);
}

void error(Diagnostics &diagnostics, size_t file, size_t line, const char *msg, ...) {
   va_list args, copy;
   va_start(args, msg);
   va_copy(copy, args);
   int len = vsnprintf(nullptr, 0, msg, copy) + 1; // null-terminator
   va_end(copy);
   char *message = (char*)malloc(len);
   vsnprintf(message, len, msg, args);
   va_end(args);

   diagnostics.diagnostics.emplace_back(SEVERITY_ERROR, message, file, line);
   diagnostics.severity = std::max(diagnostics.severity, SEVERITY_ERROR);
}

void clear(Diagnostics &diagnostics) {
   for (Diagnostic &diagnostic: diagnostics.diagnostics) {
      free(diagnostic.message);
   }

   diagnostics.diagnostics.clear();
   diagnostics.severity = SEVERITY_NONE;
}

void printDiagnostic(LexemeCache &cache, Diagnostic &diagnostic) {
   const char *type = (diagnostic.severity == SEVERITY_ERROR ? ERROR_TEXT : WARNING_TEXT);
   const char *message = diagnostic.message;

   if (diagnostic.file == 0 && diagnostic.line == 0) {
      printf("%s: %s.\n", type, message);
   }
   else {
      const char *lexeme = getLexeme(cache, diagnostic.file).c_str();
      printf("%s: %s at %s:%zu.\n", type, message, lexeme, diagnostic.line);
   }
}

void errorIfSevereEnough(ErrorSeverity severity, ErrorSeverity quitSeverity) {
   if (severity >= quitSeverity) {
      int exitCode = EXIT_FAILURE;
      printf(PROGRAM_EXITED, exitCode);
      exit(exitCode);
   }
}

void log(Executor &executor, ErrorSeverity quitSeverity) {
   for (Diagnostic &diagnostic: executor.diagnostics.diagnostics) {
      printDiagnostic(executor.cache, diagnostic);
   }
   errorIfSevereEnough(executor.diagnostics.severity, quitSeverity);
   clear(executor.diagnostics);
}

void logStackTrace(Executor &executor, ErrorSeverity quitSeverity) {
   ErrorSeverity severity = executor.diagnostics.severity;
   log(executor, SEVERITY_IGNORE);

   if (executor.stackTrace.size() > 1) {
      printf("%s (newest first):\n", STACKTRACE_INFO);
   }

   while (executor.stackTrace.size() > 1) {
      Trace trace = executor.stackTrace.top();
      executor.stackTrace.pop();
   
      Command &command = executor.code[trace.position];
      const char *function = getLexeme(executor.cache, executor.code[trace.position].lexeme).c_str();
      const char *file = getLexeme(executor.cache, command.file).c_str();
      printf("%s:%zu @ %s:%zu.\n", function, trace.position, file, command.line);
   }
   errorIfSevereEnough(severity, quitSeverity);
   clear(executor.diagnostics);
}

template<typename Container, typename Print>
static void logLeakGroup(const char *name, const Container &values, Print print) {
   size_t size = values.size();
   if (size == 0) {
      return;
   }

   size_t shown = std::min(size, MAX_LEAK_TRACE);
   printf(LEAKED_OBJECTS, size, name);

   size_t i = 0;
   for (const auto &[id, value]: values) {
      if (i++ >= shown) {
         break;
      }
      print(id, value);
   }

   if (shown < size) {
      printf(AND_N_OTHERS, size - shown);
   }
}

void logMemoryLeaks(Executor &executor) {
   if (executor.silenceLeaks) {
      return;
   }

   logLeakGroup("strings", executor.strings, [](size_t id, const std::string &string) {
      printf("%zu: '%s' with size %zu.\n", id, string.c_str(), string.size());
   });

   logLeakGroup("arrays", executor.arrays, [&](size_t id, const PILArray &array) {
      if (array.structureId != 0) {
         size_t lexemeId = executor.functions[array.structureId].lexeme;
         const char *lexeme = getLexeme(executor.cache, lexemeId).c_str();
         printf("%zu: Structure '%s' with size %zu.\n", id, lexeme, array.array.size());
      } else {
         printf("%zu: Array with size %zu.\n", id, array.array.size());
      }
   });

   logLeakGroup("maps", executor.maps, [](size_t id, const PILMap &map) {
      printf("%zu: Map with size %zu.\n", id, map.size());
   });

   logLeakGroup("files", executor.files, [](size_t id, const std::fstream &file) {
      printf("%zu: File handle %zu.\n", id, id);
   });
}

void logDiagnostic(LexemeCache &cache, Diagnostic &diagnostic, ErrorSeverity quitSeverity) {
   printDiagnostic(cache, diagnostic);
   errorIfSevereEnough(diagnostic.severity, quitSeverity);
}
