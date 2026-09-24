#include "pil.hpp"
#include <cmath>
#include <numeric>

static bool isFloating;
pilfloat_t parseOr(Executor&, std::vector<Token>&, size_t&, const std::unordered_map<size_t, Value>&);

struct MEEFunc {
   union { // just works
      pilfloat_t(*f0)();
      pilfloat_t(*f1)(pilfloat_t);
      pilfloat_t(*f2)(pilfloat_t, pilfloat_t);
      pilfloat_t(*f3)(pilfloat_t, pilfloat_t, pilfloat_t);
   };
   int args;
   bool flt = false;
};

static const std::unordered_map<std::string, MEEFunc> MEEFuncMap {
   {"abs", {.f1=fabs, .args=1}},
   {"min", {.f2=fmin, .args=2}},
   {"max", {.f2=fmax, .args=2}},
   {"sqrt", {.f1=sqrt, .args=1, .flt=true}},
   {"cbrt", {.f1=cbrt, .args=1, .flt=true}},
   {"sin", {.f1=sin, .args=1, .flt=true}},
   {"cos", {.f1=cos, .args=1, .flt=true}},
   {"tan", {.f1=tan, .args=1, .flt=true}},
   {"asin", {.f1=asin, .args=1, .flt=true}},
   {"acos", {.f1=acos, .args=1, .flt=true}},
   {"atan", {.f1=atan, .args=1, .flt=true}},
   {"atan2", {.f2=atan2, .args=2, .flt=true}},
   {"asinh", {.f1=asinh, .args=1, .flt=true}},
   {"acosh", {.f1=acosh, .args=1, .flt=true}},
   {"atanh", {.f1=atanh, .args=1, .flt=true}},
   {"sinh", {.f1=sinh, .args=1, .flt=true}},
   {"cosh", {.f1=cosh, .args=1, .flt=true}},
   {"tanh", {.f1=tanh, .args=1, .flt=true}},
   {"clamp", {.f3=[](pilfloat_t x, pilfloat_t lo, pilfloat_t hi){ return (x < lo ? lo : x > hi ? hi : x); }, .args=3}},
   {"sign", {.f1=[](pilfloat_t x){ return (x == 0.0 ? 0.0 : x > 0.0 ? 1.0 : -1.0);}, .args=1}},
   {"trunc", {.f1=trunc, .args=1}},
   {"ceil", {.f1=ceil, .args=1}},
   {"floor", {.f1=floor, .args=1}},
   {"round", {.f1=round, .args=1}},
   {"exp", {.f1=exp, .args=1, .flt=true}},
   {"ln", {.f1=log, .args=1, .flt=true}},
   {"log", {.f2=[](pilfloat_t a, pilfloat_t b){ return log(a) / log(b); }, .args=2, .flt=true}},
   {"log2", {.f1=log2, .args=1, .flt=true}},
   {"log10", {.f1=log10, .args=1, .flt=true}},
   {"lerp", {.f3=[](pilfloat_t a, pilfloat_t b, pilfloat_t t){ return a + (b - a) * t; }, .args=3, .flt=true}},
   {"if", {.f3=[](pilfloat_t cond, pilfloat_t yes, pilfloat_t no){ return cond != 0.0 ? yes : no; }, .args=3}},
   {"pi", {.f0=[]{ return M_PI; }, .args=0, .flt=true}},
   {"tau", {.f0=[]{ return M_PI * 2.0; }, .args=0, .flt=true}},
   {"e", {.f0=[]{ return M_E; }, .args=0, .flt=true}},
   {"hypot", {.f2=hypot, .args=2, .flt=true}},
   {"gcd", {.f2=[](pilfloat_t a, pilfloat_t b) -> pilfloat_t { return std::gcd((pilint_t)a, (pilint_t)b); }, .args=2}},
   {"lcm", {.f2=[](pilfloat_t a, pilfloat_t b) -> pilfloat_t { return std::lcm((pilint_t)a, (pilint_t)b); }, .args=2}},
};

pilfloat_t parseExpression(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, Value> &constants) {
   if (tokens[i].type == TOKEN_L_PAREN) {
      i += 1;
      pilfloat_t v = parseOr(executor, tokens, i, constants);
      if (tokens[i].type != TOKEN_R_PAREN) {
         error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected Right Parentheses, got %s instead", getTokenName(tokens[i].type));
      }
      i += 1;
      return v;
   }
   else if (tokens[i].type == TOKEN_IDENTIFIER && tokens[i + 1].type == TOKEN_L_PAREN) {
      std::string &lexeme = getLexeme(executor.cache, tokens[i].lexeme);
      auto it = MEEFuncMap.find(lexeme);
      if (it == MEEFuncMap.end()) {
         error(executor.diagnostics, tokens[i].file, tokens[i].line, "No such constant evaluator function '%s'", lexeme.c_str());
         return 0.0;
      }

      std::vector<pilfloat_t> args;
      for (i += 2; i < tokens.size() && tokens[i].type != TOKEN_EOF && tokens[i].type != TOKEN_R_PAREN;) {
         args.push_back(parseOr(executor, tokens, i, constants));
      }

      if (tokens[i].type != TOKEN_R_PAREN) {
         error(executor.diagnostics, tokens[i].file, tokens[i].line, "Unterminated parentheses");
         return 0.0;
      }

      i += 1;
      if (args.size() != (size_t)it->second.args) {
         error(executor.diagnostics, tokens[i].file, tokens[i].line, "%s: Expected %d parameters, got %zu instead", lexeme.c_str(), it->second.args, args.size());
         return 0.0;
      }

      if (it->second.flt) isFloating = true;
      switch (it->second.args) {
      case 0: return it->second.f0();
      case 1: return it->second.f1(args[0]);
      case 2: return it->second.f2(args[0], args[1]);
      case 3: return it->second.f3(args[0], args[1], args[2]);
      default:
         printf("PIL::parseExpression: Invalid internal constant evaluator function argument count %d", it->second.args);
         exit(EXIT_FAILURE);
      }
   }

   Value value = parseToken(executor, tokens[i], {}, constants);
   i += 1;
   if (value.type != VALUE_INTEGER && value.type != VALUE_FLOATING) {
      error(executor.diagnostics, tokens[i].file, tokens[i].line, "Invalid value in constant evaluator - %s", getValueName(value.type));
      return 0.0;
   }
   if (value.type == VALUE_FLOATING) isFloating = true;
   return (value.type == VALUE_FLOATING ? value.floating : value.integer);
}

pilfloat_t parseUnary(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, Value> &constants) {
   if (tokens[i].type == TOKEN_MINUS || tokens[i].type == TOKEN_PLUS || tokens[i].type == TOKEN_BNOT || tokens[i].type == TOKEN_LNOT) {
      TokenType type = tokens[i].type;
      i += 1;
      pilfloat_t a = parseUnary(executor, tokens, i, constants);
      if (type == TOKEN_MINUS) return -a;
      else if (type == TOKEN_BNOT) return ~(piluint_t)a;
      else if (type == TOKEN_LNOT) return a == 0.0;
      else return a;
   }
   return parseExpression(executor, tokens, i, constants);
}

pilfloat_t parseExponentiative(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, Value> &constants) {
   pilfloat_t left = parseUnary(executor, tokens, i, constants);
   if (tokens[i].type == TOKEN_STAR_STAR) {
      i += 1;
      return pow(left, parseExponentiative(executor, tokens, i, constants));
   }
   return left;
}

pilfloat_t parseMultiplicative(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, Value> &constants) {
   pilfloat_t left = parseExponentiative(executor, tokens, i, constants);
   while (tokens[i].type == TOKEN_STAR || tokens[i].type == TOKEN_SLASH || tokens[i].type == TOKEN_PERCENT) {
      TokenType type = tokens[i].type;
      i += 1;
      pilfloat_t right = parseExponentiative(executor, tokens, i, constants);
      if (right == 0.0 && type != TOKEN_STAR) {
         error(executor.diagnostics, tokens[i].file, tokens[i].line, "Division by zero in constant evaluator");
         left = 0.0;
      }
      else left = (type == TOKEN_STAR ? left * right : (type == TOKEN_SLASH ? left / right : fmod(left, right)));
   }
   return left;
}

pilfloat_t parseAdditive(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, Value> &constants) {
   pilfloat_t left = parseMultiplicative(executor, tokens, i, constants);
   while (tokens[i].type == TOKEN_PLUS || tokens[i].type == TOKEN_MINUS) {
      TokenType type = tokens[i].type;
      i += 1;
      pilfloat_t right = parseMultiplicative(executor, tokens, i, constants);
      left = (type == TOKEN_PLUS ? left + right : left - right);
   }
   return left;
}

pilfloat_t parseShifts(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, Value> &constants) {
   pilfloat_t left = parseAdditive(executor, tokens, i, constants);
   while (tokens[i].type == TOKEN_BSHL || tokens[i].type == TOKEN_BSHR) {
      TokenType type = tokens[i].type;
      i += 1;
      pilfloat_t right = parseAdditive(executor, tokens, i, constants);
      left = (type == TOKEN_BSHL ? (piluint_t)left << (piluint_t)right : (piluint_t)left >> (piluint_t)right);
   }
   return left;
}

pilfloat_t parseBand(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, Value> &constants) {
   pilfloat_t left = parseShifts(executor, tokens, i, constants);
   while (tokens[i].type == TOKEN_BAND) {
      i += 1;
      pilfloat_t right = parseShifts(executor, tokens, i, constants);
      left = (piluint_t)left & (piluint_t)right;
   }
   return left;
}

pilfloat_t parseBxor(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, Value> &constants) {
   pilfloat_t left = parseBand(executor, tokens, i, constants);
   while (tokens[i].type == TOKEN_BXOR) {
      i += 1;
      pilfloat_t right = parseBand(executor, tokens, i, constants);
      left = (piluint_t)left ^ (piluint_t)right;
   }
   return left;
}

pilfloat_t parseBor(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, Value> &constants) {
   pilfloat_t left = parseBxor(executor, tokens, i, constants);
   while (tokens[i].type == TOKEN_BOR) {
      i += 1;
      pilfloat_t right = parseBxor(executor, tokens, i, constants);
      left = (piluint_t)left | (piluint_t)right;
   }
   return left;
}

pilfloat_t parseRelationalOps(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, Value> &constants) {
   pilfloat_t left = parseBor(executor, tokens, i, constants);
   while (tokens[i].type == TOKEN_LESSER || tokens[i].type == TOKEN_GREATER || tokens[i].type == TOKEN_LESSER_EQUAL || tokens[i].type == TOKEN_GREATER_EQUAL) {
      TokenType type = tokens[i].type;
      i += 1;
      pilfloat_t right = parseBor(executor, tokens, i, constants);
      if (type == TOKEN_LESSER) left = (left < right);
      else if (type == TOKEN_GREATER) left = (left > right);
      else if (type == TOKEN_LESSER_EQUAL) left = (left <= right);
      else left = (left >= right);
   }
   return left;
}

pilfloat_t parseEqualityOps(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, Value> &constants) {
   pilfloat_t left = parseRelationalOps(executor, tokens, i, constants);
   while (tokens[i].type == TOKEN_EQUAL || tokens[i].type == TOKEN_INEQUAL) {
      TokenType type = tokens[i].type;
      i += 1;
      pilfloat_t right = parseRelationalOps(executor, tokens, i, constants);
      left = (left == right) == (type == TOKEN_EQUAL);
   }
   return left;
}

pilfloat_t parseAnd(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, Value> &constants) {
   pilfloat_t left = parseEqualityOps(executor, tokens, i, constants);
   while (tokens[i].type == TOKEN_LAND) {
      i += 1;
      pilfloat_t right = parseEqualityOps(executor, tokens, i, constants);
      left = (left != 0.0 && right != 0.0);
   }
   return left;
}

pilfloat_t parseOr(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, Value> &constants) {
   pilfloat_t left = parseAnd(executor, tokens, i, constants);
   while (tokens[i].type == TOKEN_LOR) {
      i += 1;
      pilfloat_t right = parseAnd(executor, tokens, i, constants);
      left = (left != 0.0 || right != 0.0);
   }
   return left;
}

Value evaluateMath(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, Value> &constants) {
   isFloating = false;
   i += 1;
   pilfloat_t result = parseOr(executor, tokens, i, constants);
   if (tokens[i].type != TOKEN_R_BRACKET && tokens[i].type != TOKEN_EVAL_END) {
      error(executor.diagnostics, tokens[i].file, tokens[i].line, "Unterminated constant evaluator. Expected Right Bracket, got %s instead", getTokenName(tokens[i].type));
   }
   Value value {isFloating ? VALUE_FLOATING : VALUE_INTEGER};
   if (isFloating) value.floating = result;
   else value.integer = result;
   return value;
}
