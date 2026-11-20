#include "pil.hpp"
#include <algorithm>
#include <unordered_set>

// find all INCLUDE "FILE" statements and push their tokens if the files haven't been included yet. will erase all includes
// after and doesn't have more than a single file open at a time.
void translatePIL(Executor &executor, size_t fileLexeme, std::vector<Token> &tokens) {
   std::unordered_set<std::string> includedFiles;
   size_t size = tokens.size();
   size_t includeLexeme = cacheLexeme(executor.cache, "include");
 
   for (size_t i = 0; i < size; ++i) {
      if (tokens[i].type == TOKEN_IDENTIFIER && tokens[i].lexeme == includeLexeme && i + 1 < size && tokens[i + 1].type == TOKEN_STRING) {
         // destroy all after the loop
         tokens[i].parsed = true;
         tokens[i + 1].parsed = true;
 
         std::string &filename = getLexeme(executor.cache, tokens[i + 1].lexeme);
         if (includedFiles.find(filename) != includedFiles.end()) {
            continue;
         }
 
         includedFiles.insert(filename);
         PILFile newFile;
         std::vector<Token> newTokens;
 
         readPIL(executor.diagnostics, executor.cache, filename, newFile, tokens[i + 1].file, tokens[i + 1].line);
         lexPILFile(executor.diagnostics, executor.cache, newFile, newTokens);
         tokens.insert(tokens.begin() + i + 2, newTokens.begin(), newTokens.end());
         i += 1;
         size = tokens.size();
      }
   }
   // erase all includes and EOFs
   tokens.erase(std::remove_if(tokens.begin(), tokens.end(), [](const Token &t) { return t.parsed || t.type == TOKEN_EOF; }), tokens.end());
   size_t EOFline = (tokens.empty() ? 1 : tokens.back().line);
   tokens.emplace_back(TOKEN_EOF, cacheLexeme(executor.cache, "EOF"), fileLexeme, EOFline);
}
