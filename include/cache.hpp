#pragma once
#include <string>
#include <vector>
#include <unordered_map>

struct LexemeCache {
   // what this does is make it so that not only strings can be hashed, but
   // also string views, which makes the lexeme cache blazingly fast
   struct TransparentHash {
      using is_transparent = void;
      size_t operator () (std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
   };

   struct TransparentEq {
      using is_transparent = void;
      bool operator () (std::string_view a, std::string_view b) const { return a == b; }
   };

   std::vector<std::string> lexemes {""}; // 0 - nil
   std::unordered_map<std::string, size_t, TransparentHash, TransparentEq> lexemeCache;
};

// lexeme class methods
size_t pushLexeme(LexemeCache &cache, const std::string &lexeme);
size_t cacheLexeme(LexemeCache &cache, std::string_view sv);
std::string &getLexeme(LexemeCache &cache, size_t id);
