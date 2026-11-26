#include "pil.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_set>

// helpers
static const std::unordered_map<char, char> escapeCodeMap {
   {'a', '\a'}, {'b', '\b'}, {'t', '\t'}, {'n', '\n'}, {'v', '\v'}, {'f', '\f'}, {'r', '\r'},
   {'e', '\e'}, {'\\', '\\'}, {'\'', '\''}, {'"', '"'}, {'{', '{'},
};

static char handleEscapeCode(Diagnostics &diagnostics, LexemeCache &cache, PILFile &file, size_t &i, size_t tokenLine) {
   char ch = file.code[i];
   if (ch != '\\') return ch;
   ch = file.code[++i];
   if (auto it = escapeCodeMap.find(ch); it != escapeCodeMap.end()) return it->second;
   warn(diagnostics, file.lexeme, tokenLine, "Unknown escape code '\\%c'", ch);
   return ch;
}

static void twoOpLexeme(TokenType def, char next, TokenType nextType, size_t &i, std::vector<Token> &tokens, const PILFile &file, size_t size, size_t line) {
   if (i + 1 < size && file.code[i + 1] == next) { def = nextType; i += 1; }
   tokens.emplace_back(def, 0, file.lexeme, line);
}

static void threeOpLexeme(TokenType def, char next1, TokenType nextType1, char next2, TokenType nextType2, size_t &i, std::vector<Token> &tokens, const PILFile &file, size_t size, size_t line) {
   if (i + 1 < size && file.code[i + 1] == next1) { def = nextType1; i += 1; }
   else if (i + 1 < size && file.code[i + 1] == next2) { def = nextType2; i += 1; }
   tokens.emplace_back(def, 0, file.lexeme, line);
}

static constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }
static constexpr bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static constexpr bool isAlnum(char c) { return isDigit(c) || isAlpha(c); }

// file reader
void readPIL(Diagnostics &diagnostics, LexemeCache &cache, const std::string &path, PILFile &pilFile, size_t parentFile, size_t line) {
   size_t fileLexeme = pushLexeme(cache, path);
   std::ifstream file (path, std::ios::binary | std::ios::ate);
   if (!file.is_open()) {
      error(diagnostics, parentFile, line, "Could not read file '%s'", path.c_str());
      return;
   }
   auto fileSize = file.tellg();
   file.seekg(std::ios::beg);
   pilFile.code.resize(fileSize);
   file.read(&pilFile.code[0], fileSize);
   pilFile.lexeme = fileLexeme;
}

// translate code into tokens. we cache common lexemes that repeat often like identifiers and ops but don't cache numbers,
// characters and strings, which could change during execution and are usually longer and don't repeat as often. Registers
// are safe to cache since they're constants
void lexPILFile(Diagnostics &diagnostics, LexemeCache &cache, PILFile &file, std::vector<Token> &tokens) {
   size_t size = file.code.size();
   size_t line = 1;
   tokens.reserve(size / 4);

   for (size_t i = 0; i < size; ++i) {
      char ch = file.code[i];
      if (isDigit(ch) || ((ch == '-' || ch == '+') && i + 1 < size && isDigit(file.code[i+1]))) {
         size_t end = i;
         bool dot = false;

         for (++end; end < size && (file.code[end] == '.' || isDigit(file.code[end])); ++end) {
            if (file.code[end] == '.' && dot) {
               error(diagnostics, file.lexeme, line, "Number '%s' contains multiple decimal points", std::string(&file.code[i], end - i).c_str());
               break;
            }
            dot = dot || (!dot && file.code[end] == '.');
         }
         size_t lexeme = cacheLexeme(cache, std::string_view(&file.code[i], end - i));
         tokens.emplace_back(dot ? TOKEN_FLOATING : TOKEN_INTEGER, lexeme, file.lexeme, line);
         i = end - 1;
         continue;
      }

      switch (ch) {
      case ' ': case '\t': case '\r': case '\f': case '\v': continue;
      case '\n': line += 1; continue;
      case '#': while (i < size && file.code[i] != '\n') i += 1; line += 1; continue;
      case '(': tokens.emplace_back(TOKEN_L_PAREN, 0, file.lexeme, line); continue;
      case ')': tokens.emplace_back(TOKEN_R_PAREN, 0, file.lexeme, line); continue;
      case ':': tokens.emplace_back(TOKEN_LABEL, 0, file.lexeme, line); continue;
      case '+': tokens.emplace_back(TOKEN_PLUS, 0, file.lexeme, line); continue;
      case '-': tokens.emplace_back(TOKEN_MINUS, 0, file.lexeme, line); continue;
      case '/': tokens.emplace_back(TOKEN_SLASH, 0, file.lexeme, line); continue;
      case '%': tokens.emplace_back(TOKEN_PERCENT, 0, file.lexeme, line); continue;
      case '^': tokens.emplace_back(TOKEN_BXOR, 0, file.lexeme, line); continue;
      case '~': tokens.emplace_back(TOKEN_BNOT, 0, file.lexeme, line); continue;
      case '[': tokens.emplace_back(TOKEN_L_BRACKET, 0, file.lexeme, line); continue;
      case ']': tokens.emplace_back(TOKEN_R_BRACKET, 0, file.lexeme, line); continue;
      case '.': tokens.emplace_back(TOKEN_DOT, 0, file.lexeme, line); continue;
      case ',': tokens.emplace_back(TOKEN_COMMA, 0, file.lexeme, line); continue;
      case ';': tokens.emplace_back(TOKEN_SEMICOLON, 0, file.lexeme, line); continue;
      case '*': twoOpLexeme(TOKEN_STAR, '*', TOKEN_STAR_STAR, i, tokens, file, size, line); continue;
      case '&': twoOpLexeme(TOKEN_BAND, '&', TOKEN_LAND, i, tokens, file, size, line); continue;
      case '|': twoOpLexeme(TOKEN_BOR, '|', TOKEN_LOR, i, tokens, file, size, line); continue;
      case '=': twoOpLexeme(TOKEN_ASSIGN, '=', TOKEN_EQUAL, i, tokens,  file, size, line); continue;
      case '!': twoOpLexeme(TOKEN_LNOT, '=', TOKEN_INEQUAL, i, tokens, file, size, line); continue;
      case '<': threeOpLexeme(TOKEN_LESSER, '=', TOKEN_LESSER_EQUAL, '<', TOKEN_BSHL, i, tokens, file, size, line); continue;
      case '>': threeOpLexeme(TOKEN_GREATER, '=', TOKEN_GREATER_EQUAL, '>', TOKEN_BSHR, i, tokens, file, size, line); continue;
      }

      if (ch == '\'') {
         if (i + 1 >= size || file.code[i + 1] == '\n') {
            error(diagnostics, file.lexeme, line, "Unterminated character");
            continue;
         }

         i += 1;
         std::string ch (1, handleEscapeCode(diagnostics, cache, file, i, line));

         if (i + 1 >= size || file.code[i + 1] != '\'') {
            error(diagnostics, file.lexeme, line, "Unterminated character");
            continue;
         }
         i += 1;
         tokens.emplace_back(TOKEN_CHARACTER, cacheLexeme(cache, ch), file.lexeme, line);
      }
      else if (ch == '"') {
         std::string string;
         size_t originalLine = line;

         for (++i; i < size && file.code[i] != '"' && file.code[i] != '\n'; ++i) {
            string.push_back(handleEscapeCode(diagnostics, cache, file, i, line));
         }

         if (i >= size || file.code[i] != '"') {
            i -= 1;
            error(diagnostics, file.lexeme, originalLine, "Unterminated string");
            continue;
         }
         tokens.emplace_back(TOKEN_STRING, pushLexeme(cache, string), file.lexeme, originalLine);
      }
      else if (ch == '_' || isAlpha(ch)) {
         size_t end = i;
         for (++end; end < size && (file.code[end] == '_' || file.code[end] == '-' || isAlnum(file.code[end])); ++end);
         size_t lexeme = cacheLexeme(cache, std::string_view(&file.code[i], end - i));
         tokens.emplace_back(TOKEN_IDENTIFIER, lexeme, file.lexeme, line);
         i = end - 1;
      }
      else {
         error(diagnostics, file.lexeme, line, "Unexpected character '%c'", ch);
      }
   }
   tokens.emplace_back(TOKEN_EOF, 0, file.lexeme, line);
}

// find all INCLUDE "FILE" statements and push their tokens if the files haven't been included yet. will erase all includes
// after and doesn't have more than a single file open at a time.
void translatePIL(Executor &executor, size_t fileLexeme, std::vector<Token> &tokens) {
   std::unordered_set<std::filesystem::path> includedFiles;
   size_t size = tokens.size();
   size_t includeLexeme = cacheLexeme(executor.cache, "include");
 
   for (size_t i = 0; i < size; ++i) {
      if (tokens[i].type == TOKEN_IDENTIFIER && tokens[i].lexeme == includeLexeme && i + 1 < size && tokens[i + 1].type == TOKEN_STRING) {
         // destroy all after the loop
         tokens[i].parsed = true;
         tokens[i + 1].parsed = true;
 
         // copy. do not get reference here. It might get invalidated in readPIL, which
         // pushes another lexeme into the vector.
         std::string filename = getLexeme(executor.cache, tokens[i + 1].lexeme);
         std::error_code ec;
         std::filesystem::path path = std::filesystem::canonical(filename, ec);

         if (ec.value()) {
            error(executor.diagnostics, tokens[i].file, tokens[i].line, "File '%s' cannot be included as it does not exist", filename.c_str());
            continue;
         }

         if (!includedFiles.insert(path).second) {
            continue;
         }

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
   tokens.emplace_back(TOKEN_EOF, 0, fileLexeme, EOFline);
}
