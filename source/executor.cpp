#include "builtin.hpp"
#include "builtinhelpers.hpp"
#include "pil.hpp"

void call(Executor &executor, const Command &command, Function &function, size_t functionPos, size_t returnCount, size_t args) {
   if (function.native) {
      // printf("called native %s. Stack trace: %zu.\n", getLexeme(executor.cache, command.lexeme).c_str(), executor.stackTrace.size());
      BUILTIN_DEFINITIONS[function.nativeFn].fn(command, executor);
   }
   else if (!function.isLabel) {
      // printf("called %s @ %s:%zu. Stack trace: %zu.\n", getLexeme(executor.cache, function.lexeme).c_str(), getLexeme(executor.cache, executor.code[function.position].file).c_str(), executor.code[function.position].line, executor.stackTrace.size());
      size_t params = function.paramCount;
      size_t offset = command.argStart + functionPos + 1;

      Trace trace (executor.pointer, command.argStart, returnCount);
      trace.localStart = executor.locals.size();
      trace.localCount = function.localCount;
      executor.locals.resize(trace.localStart + trace.localCount, NULL_VALUE);

      for (size_t i = 0; i < params; ++i) {
         Value value = resolveVariable(executor, executor.arguments[i + offset]);
         executor.locals[trace.localStart + i] = value;
      }
      executor.stackTrace.push(trace);
      executor.pointer = function.position - 1;
   }
   else {
      const char *lexeme = getLexeme(executor.cache, command.lexeme).c_str();
      error(executor.diagnostics, command.file, command.line, "Stray label '%s'", lexeme);
   }
}

// execute the function that the pointer is on. return and call logic can be found in the builtin header since they're just
// callable functions.
void callMain(Executor &executor, ErrorSeverity stopSeverity) {
   Function &main = executor.functions[executor.main];
   if (executor.registers.empty()) {
      executor.registers.resize(DEFAULT_REGISTER_COUNT, NULL_VALUE);
   }

   if (executor.returnRegisters.empty()) {
      executor.returnRegisters.resize(DEFAULT_RETURN_REGISTER_COUNT, NULL_VALUE);
   }
   executor.locals.reserve(DEFAULT_LOCAL_RESERVE);
   executor.locals.resize(main.localCount, NULL_VALUE);

   executor.stackTrace = {};
   executor.stackTrace.emplace(main.position, std::string::npos, std::string::npos);
   executor.stackTrace.top().localStart = 0;
   executor.stackTrace.top().localCount = main.localCount;

   executor.pointer = main.position;
   executor.returnCount = 0;
   executor.exitCalled = false;

   while (true) {
      // printf("Pointer @ %zu.\n", executor.pointer);
      Command &command = executor.code[executor.pointer];
      Function &function = executor.functions[command.functionId];
      call(executor, command, function, -1, std::string::npos, command.argCount);
      if (executor.exitCalled || shouldError(executor.diagnostics, stopSeverity)) {
         return;
      }
      executor.pointer += 1;
   }
   executor.locals.resize(0);
   executor.stackTrace.pop();
}
