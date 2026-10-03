#include "pil.hpp"
#include <algorithm>
#include <unordered_set>

// find all INCLUDE "FILE" statements and push their tokens if the files haven't been included yet. will erase all includes
// after and doesn't have more than a single file open at a time. also handles some other misc. directives.
void translatePIL(Executor &executor, size_t fileLexeme, std::vector<Token> &tokens) {
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
   tokens.emplace_back(TOKEN_EOF, cacheLexeme(executor.cache, "EOF"), fileLexeme, EOFline);
}
