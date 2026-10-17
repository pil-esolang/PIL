#include "pil.hpp"
#include <chrono>

static inline std::chrono::steady_clock::time_point point;

void measure() {
   point = std::chrono::steady_clock::now();
}

float measureEnd() {
   return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - point).count() / 1000.0f;
}

void debugTokens(bool debug, LexemeCache &cache, const std::vector<Token> &tokens) {
   if (!debug) return;
   puts("\nTokens:");

   for (const Token &token: tokens) {
      const char *file = getLexeme(cache, token.file).c_str();
      const char *lexeme = getLexeme(cache, token.lexeme).c_str();
      const char *tokenName = getTokenName(token.type);
      printf("%s:%-5zu %s: '%s'.\n", file, token.line, tokenName, lexeme);
   }
}

void debugBytecode(bool debug, Executor &executor) {
   if (!debug) return;
   puts("\nBytecode:");

   for (Command &command: executor.code) {
      const char *file = getLexeme(executor.cache, command.file).c_str();
      const char *lexeme = getLexeme(executor.cache, command.lexeme).c_str();
      printf("%s:%-5zu %s: ", file, command.line, lexeme);

      size_t argEnd = command.argStart + command.argCount;
      for (size_t i = command.argStart; i < argEnd; ++i) {
         printf("%s, ", getValueName(executor.arguments[i].type));
      }
      putchar('\n');
   }
}

void debugExecutionTime(bool debug, float file, float lexer, float parser, float runtime) {
   if (!debug) return;
   printf("\nExecution time:\n");
   printf("File Read: %.3fms.\n", file);
   printf("Lexer: %.3fms.\n", lexer);
   printf("Parser: %.3fms.\n", parser);
   printf("Runtime: %.3fms.\n", runtime);
}

void debugCompilationTime(bool debug, float file, float lexer, float parser, float writing) {
   if (!debug) return;
   printf("\nCompilation time:\n");
   printf("File Read: %.3fms.\n", file);
   printf("Lexer: %.3fms.\n", lexer);
   printf("Parser: %.3fms.\n", parser);
   printf("Writing: %.3fms.\n", writing);
}

void debugCacheExecutionTime(bool debug, float file, float runtime) {
   if (!debug) return;
   printf("\nExecution time:\n");
   printf("File Read: %.3fms.\n", file);
   printf("Runtime: %.3fms.\n", runtime);
}
