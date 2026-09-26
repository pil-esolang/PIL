#include "pil.hpp"
#include <algorithm>
#include <unordered_set>

// find all INCLUDE "FILE" statements and push their tokens if the files haven't been included yet. will erase all includes
// after and doesn't have more than a single file open at a time. also handles some other misc. directives.
void translatePIL(Executor &executor, PILFile &file, std::vector<Token> &tokens) {
   std::unordered_set<std::string> includedFiles;
   size_t size = tokens.size();
 
   size_t includeLexeme = cacheLexeme(executor.cache, "include");
   size_t registerLexeme = cacheLexeme(executor.cache, "reg-size");
   size_t returnRegisterLexeme = cacheLexeme(executor.cache, "return-reg-size");
   size_t loopLexeme = cacheLexeme(executor.cache, "loop");
   size_t endLexeme = cacheLexeme(executor.cache, "end");
 
   for (size_t i = 0; i < size; ++i) {
      if (tokens[i].type != TOKEN_DIRECTIVE) continue;
      
      // handle includes
      if (tokens[i].lexeme == includeLexeme && i + 1 < size && tokens[i + 1].type == TOKEN_STRING) {
         if (i + 2 >= size || tokens[i + 2].type != TOKEN_NEWLINE) {
            error(executor.diagnostics, tokens[i].file, tokens[i].line, "Excess tokens (or EOF) after include directive");
            continue;
         }
 
         // destroy all after the loop
         tokens[i].parsed = true;
         tokens[i + 1].parsed = true;
         tokens[i + 2].parsed = true;
 
         std::string &filename = getLexeme(executor.cache, tokens[i + 1].lexeme);
         if (includedFiles.find(filename) != includedFiles.end()) {
            continue;
         }
 
         includedFiles.insert(filename);
         PILFile newFile;
         std::vector<Token> newTokens;
 
         readPIL(executor.diagnostics, executor.cache, filename, newFile, tokens[i + 1].file, tokens[i + 1].line);
         lexPILFile(executor.diagnostics, executor.cache, newFile, newTokens);
         tokens.insert(tokens.begin() + i + 3, newTokens.begin(), newTokens.end());
         i += 2;
         size = tokens.size();
      }
      // handle register config
      else if ((tokens[i].lexeme == registerLexeme || tokens[i].lexeme == returnRegisterLexeme) && i + 1 < size && tokens[i + 1].type == TOKEN_INTEGER) {
         if (i + 2 >= size || tokens[i + 2].type != TOKEN_NEWLINE) {
            error(executor.diagnostics, tokens[i].file, tokens[i].line, "Excess tokens (or EOF) after register configuration directive");
            continue;
         }
         tokens[i].parsed = true;
         tokens[i + 1].parsed = true;
         tokens[i + 2].parsed = true;
 
         Value value = parseToken(executor, tokens[i + 1], {}, {});
         if (tokens[i].lexeme == registerLexeme) {
            executor.registers.resize(value.integer);
         }
         else {
            executor.returnRegisters.resize(value.integer);
         }
         i += 2;
      }
      // skip for snippet pass later
      else if (tokens[i].lexeme == loopLexeme || tokens[i].lexeme == endLexeme) {
         continue;
      }
      // unknown directive
      else {
         error(executor.diagnostics, tokens[i].file, tokens[i].line, "Invalid directive '@%s'", getLexeme(executor.cache, tokens[i].lexeme).c_str());
      }
   }
   // erase all includes and EOFs
   tokens.erase(std::remove_if(tokens.begin(), tokens.end(), [](const Token &t) { return t.parsed || t.type == TOKEN_EOF; }), tokens.end());
   size_t EOFline = (tokens.empty() ? 1 : tokens.back().line);
   tokens.emplace_back(TOKEN_EOF, cacheLexeme(executor.cache, "EOF"), file.lexeme, EOFline);
}

// the rest of the file is responsible for parsing @loop and other control flow directives
struct Operand {
   bool isRegister;
   union {
      size_t reg;
      struct { size_t start, end; };
   };
};

struct LoopFrame {
   size_t startLabel;
   size_t endLabel;
   size_t file;
   size_t line;
};

static const std::unordered_map<TokenType, const char*> RELATIONAL_BUILTINS {
   {TOKEN_LESSER, "le"}, {TOKEN_LESSER_EQUAL, "leeq"}, {TOKEN_GREATER, "gr"}, {TOKEN_GREATER_EQUAL, "greq"}, {TOKEN_EQUAL, "eq"}, {TOKEN_INEQUAL, "neq"},
};

size_t allocateTempRegister(Executor &executor, size_t registerCount, size_t &tempsUsed, size_t file, size_t line) {
   tempsUsed += 1;
   if (tempsUsed >= registerCount) {
      return 0;
   }
   return registerCount - tempsUsed;
}

void emitNewline(Executor &executor, std::vector<Token> &out, size_t file, size_t line) {
   out.emplace_back(TOKEN_NEWLINE, cacheLexeme(executor.cache, ""), file, line);
}

void emitOperand(Executor &executor, std::vector<Token> &tokens, std::vector<Token> &out, const Operand &operand, size_t file, size_t line) {
   if (operand.isRegister) {
      out.emplace_back(TOKEN_REGISTER, cacheLexeme(executor.cache, ""), file, line);
      out.emplace_back(TOKEN_INTEGER, cacheLexeme(executor.cache, std::to_string(operand.reg)), file, line);
   }
   else {
      out.insert(out.end(), tokens.begin() + operand.start, tokens.begin() + operand.end);
   }
}

void emitCall(Executor &executor, std::vector<Token> &tokens, std::vector<Token> &out, const char *name, const Operand &a, const Operand *b, const Operand &dest, size_t file, size_t line) {
   out.emplace_back(TOKEN_IDENTIFIER, cacheLexeme(executor.cache, name), file, line);
   emitOperand(executor, tokens, out, a, file, line);
   if (b) emitOperand(executor, tokens, out, *b, file, line);
   emitOperand(executor, tokens, out, dest, file, line);
   emitNewline(executor, out, file, line);
}

Operand compileOrExpr(Executor &executor, std::vector<Token> &tokens, size_t &i, size_t endIdx, size_t registerCount, size_t &tempsUsed, std::vector<Token> &out, size_t file, size_t line);

Operand compileAtom(Executor &executor, std::vector<Token> &tokens, size_t &i, size_t endIdx, size_t registerCount, size_t &tempsUsed, std::vector<Token> &out, size_t file, size_t line) {
   if (i >= endIdx) {
      error(executor.diagnostics, file, line, "@loop: Expected a value in condition");
      return {.isRegister = false, .start = i, .end = i};
   }

   TokenType type = tokens[i].type;
   if (type == TOKEN_L_PAREN) {
      size_t parenStart = i;
      i += 1;
      Operand inner = compileOrExpr(executor, tokens, i, endIdx, registerCount, tempsUsed, out, file, line);
      if (i < endIdx && tokens[i].type == TOKEN_R_PAREN) {
         i += 1;
      }
      else {
         error(executor.diagnostics, tokens[parenStart].file, tokens[parenStart].line, "@loop: Expected ')' to close '('");
      }
      return inner;
   }
   else if (type == TOKEN_L_BRACKET) {
      size_t start = i;
      while (i < endIdx && tokens[i].type != TOKEN_R_BRACKET) i += 1;
      if (i >= endIdx) {
         error(executor.diagnostics, tokens[start].file, tokens[start].line, "@loop: Unterminated '[' in condition");
      }
      else {
         i += 1;
      }
      return {.isRegister = false, .start = start, .end = i};
   }
   else if (type == TOKEN_REGISTER || type == TOKEN_RETURN_REGISTER) {
      if (i + 1 >= endIdx) {
         error(executor.diagnostics, tokens[i].file, tokens[i].line, "@loop: Expected a value after register in condition");
         size_t start = i;
         i = endIdx;
         return {.isRegister = false, .start = start, .end = start};
      }
      size_t start = i;
      i += 2;
      return {.isRegister = false, .start = start, .end = i};
   }
   else if (type == TOKEN_STRING && i + 1 < endIdx && (tokens[i + 1].type == TOKEN_FMT_START || tokens[i + 1].type == TOKEN_EVAL_START)) {
      error(executor.diagnostics, tokens[i].file, tokens[i].line, "@loop: Interpolated strings aren't supported in conditions yet - assign it to a variable first");
      size_t start = i;
      i = endIdx;
      return {.isRegister = false, .start = start, .end = start};
   }
   else if (type == TOKEN_IDENTIFIER || type == TOKEN_INTEGER || type == TOKEN_FLOATING || type == TOKEN_STRING || type == TOKEN_CHARACTER) {
      size_t start = i;
      i += 1;
      return {.isRegister = false, .start = start, .end = i};
   }

   error(executor.diagnostics, tokens[i].file, tokens[i].line, "@loop: Expected a value in condition, got %s instead", getTokenName(type));
   size_t start = i;
   i += 1;
   return {.isRegister = false, .start = start, .end = start};
}

Operand compileUnary(Executor &executor, std::vector<Token> &tokens, size_t &i, size_t endIdx, size_t registerCount, size_t &tempsUsed, std::vector<Token> &out, size_t file, size_t line) {
   if (i < endIdx && tokens[i].type == TOKEN_LNOT) {
      i += 1;
      Operand value = compileUnary(executor, tokens, i, endIdx, registerCount, tempsUsed, out, file, line);
      Operand dest = {.isRegister = true, .reg = allocateTempRegister(executor, registerCount, tempsUsed, file, line)};
      emitCall(executor, tokens, out, "not", value, nullptr, dest, file, line);
      return dest;
   }
   return compileAtom(executor, tokens, i, endIdx, registerCount, tempsUsed, out, file, line);
}

Operand compileComparison(Executor &executor, std::vector<Token> &tokens, size_t &i, size_t endIdx, size_t registerCount, size_t &tempsUsed, std::vector<Token> &out, size_t file, size_t line) {
   Operand left = compileUnary(executor, tokens, i, endIdx, registerCount, tempsUsed, out, file, line);
   if (i < endIdx) {
      if (auto it = RELATIONAL_BUILTINS.find(tokens[i].type); it != RELATIONAL_BUILTINS.end()) {
         i += 1;
         Operand right = compileUnary(executor, tokens, i, endIdx, registerCount, tempsUsed, out, file, line);
         Operand dest = {.isRegister = true, .reg = allocateTempRegister(executor, registerCount, tempsUsed, file, line)};
         emitCall(executor, tokens, out, it->second, left, &right, dest, file, line);
         return dest;
      }
   }
   return left;
}

Operand compileAndExpr(Executor &executor, std::vector<Token> &tokens, size_t &i, size_t endIdx, size_t registerCount, size_t &tempsUsed, std::vector<Token> &out, size_t file, size_t line) {
   Operand left = compileComparison(executor, tokens, i, endIdx, registerCount, tempsUsed, out, file, line);
   while (i < endIdx && tokens[i].type == TOKEN_LAND) {
      i += 1;
      Operand right = compileComparison(executor, tokens, i, endIdx, registerCount, tempsUsed, out, file, line);
      Operand dest = {.isRegister = true, .reg = allocateTempRegister(executor, registerCount, tempsUsed, file, line)};
      emitCall(executor, tokens, out, "and", left, &right, dest, file, line);
      left = dest;
   }
   return left;
}

Operand compileOrExpr(Executor &executor, std::vector<Token> &tokens, size_t &i, size_t endIdx, size_t registerCount, size_t &tempsUsed, std::vector<Token> &out, size_t file, size_t line) {
   Operand left = compileAndExpr(executor, tokens, i, endIdx, registerCount, tempsUsed, out, file, line);
   while (i < endIdx && tokens[i].type == TOKEN_LOR) {
      i += 1;
      Operand right = compileAndExpr(executor, tokens, i, endIdx, registerCount, tempsUsed, out, file, line);
      Operand dest = {.isRegister = true, .reg = allocateTempRegister(executor, registerCount, tempsUsed, file, line)};
      emitCall(executor, tokens, out, "or", left, &right, dest, file, line);
      left = dest;
   }
   return left;
}

size_t expandLoop(Executor &executor, std::vector<Token> &tokens, size_t loopIdx, size_t registerCount, size_t &loopCounter, std::vector<LoopFrame> &openLoops) {
   size_t file = tokens[loopIdx].file;
   size_t line = tokens[loopIdx].line;

   size_t condStart = loopIdx + 1;
   size_t condEnd = condStart;
   while (condEnd < tokens.size() && tokens[condEnd].type != TOKEN_NEWLINE && tokens[condEnd].type != TOKEN_EOF) {
      condEnd += 1;
   }

   if (condStart == condEnd) {
      error(executor.diagnostics, file, line, "@loop: Expected a condition");
   }
   if (condEnd >= tokens.size() || tokens[condEnd].type != TOKEN_NEWLINE) {
      error(executor.diagnostics, file, line, "@loop: Expected a newline after the condition");
   }

   loopCounter += 1;
   std::string label = "@loop" + std::to_string(loopCounter);
   size_t startLabel = cacheLexeme(executor.cache, label + "-start");
   size_t endLabel = cacheLexeme(executor.cache, label + "-end");

   std::vector<Token> out;
   out.emplace_back(TOKEN_IDENTIFIER, startLabel, file, line);
   out.emplace_back(TOKEN_LABEL, cacheLexeme(executor.cache, ""), file, line);
   emitNewline(executor, out, file, line);

   size_t tempsUsed = 0;
   size_t i = condStart;
   Operand result = compileOrExpr(executor, tokens, i, condEnd, registerCount, tempsUsed, out, file, line);

   if (i != condEnd) {
      error(executor.diagnostics, tokens[i].file, tokens[i].line, "@loop: Unexpected '%s' in condition", getTokenName(tokens[i].type));
   }

   if (registerCount < tempsUsed) {
      error(executor.diagnostics, file, line, "@loop: Condition needs more registers than are available (%zu). Simplify the condition or bump up register count with '@reg-size %zu' directive", registerCount, tempsUsed);
   }

   out.emplace_back(TOKEN_IDENTIFIER, cacheLexeme(executor.cache, "jmpn"), file, line);
   emitOperand(executor, tokens, out, result, file, line);
   out.emplace_back(TOKEN_IDENTIFIER, endLabel, file, line);
   emitNewline(executor, out, file, line);

   for (size_t k = loopIdx; k <= condEnd && k < tokens.size(); ++k) {
      tokens[k].parsed = true;
   }

   size_t insertPos = std::min(condEnd, tokens.size() - 1) + 1;
   tokens.insert(tokens.begin() + insertPos, out.begin(), out.end());

   openLoops.push_back({startLabel, endLabel, file, line});
   return insertPos + out.size() - 1;
}

size_t expandEnd(Executor &executor, std::vector<Token> &tokens, size_t endIdx, std::vector<LoopFrame> &openLoops) {
   size_t file = tokens[endIdx].file;
   size_t line = tokens[endIdx].line;

   if (openLoops.empty()) {
      error(executor.diagnostics, file, line, "@end: No matching '@loop'");
      tokens[endIdx].parsed = true;
      return endIdx;
   }

   LoopFrame frame = openLoops.back();
   openLoops.pop_back();

   size_t afterEnd = endIdx + 1;
   bool hasNewline = (afterEnd < tokens.size() && tokens[afterEnd].type == TOKEN_NEWLINE);
   if (!hasNewline) {
      error(executor.diagnostics, file, line, "@end: Expected a newline after '@end'");
   }

   std::vector<Token> out;
   out.emplace_back(TOKEN_IDENTIFIER, cacheLexeme(executor.cache, "goto"), file, line);
   out.emplace_back(TOKEN_IDENTIFIER, frame.startLabel, file, line);
   emitNewline(executor, out, file, line);
   out.emplace_back(TOKEN_IDENTIFIER, frame.endLabel, file, line);
   out.emplace_back(TOKEN_LABEL, cacheLexeme(executor.cache, ""), file, line);
   emitNewline(executor, out, file, line);

   tokens[endIdx].parsed = true;
   size_t lastMarked = endIdx;
   if (hasNewline) {
      tokens[afterEnd].parsed = true;
      lastMarked = afterEnd;
   }

   size_t insertPos = lastMarked + 1;
   tokens.insert(tokens.begin() + insertPos, out.begin(), out.end());
   return insertPos + out.size() - 1;
}

void expandSnippets(Executor &executor, std::vector<Token> &tokens) {
   size_t loopLexeme = cacheLexeme(executor.cache, "loop");
   size_t endLexeme = cacheLexeme(executor.cache, "end");
   size_t registerCount = (executor.registers.empty() ? DEFAULT_REGISTER_COUNT : executor.registers.size());

   size_t loopCounter = 0;
   std::vector<LoopFrame> openLoops;

   for (size_t i = 0; i < tokens.size(); ++i) {
      if (tokens[i].type != TOKEN_DIRECTIVE) continue;

      if (tokens[i].lexeme == loopLexeme) {
         i = expandLoop(executor, tokens, i, registerCount, loopCounter, openLoops);
      }
      else if (tokens[i].lexeme == endLexeme) {
         i = expandEnd(executor, tokens, i, openLoops);
      }
   }

   for (const LoopFrame &frame: openLoops) {
      error(executor.diagnostics, frame.file, frame.line, "@loop: Missing matching '@end'");
   }
   tokens.erase(std::remove_if(tokens.begin(), tokens.end(), [](const Token &t) { return t.parsed; }), tokens.end());
}
