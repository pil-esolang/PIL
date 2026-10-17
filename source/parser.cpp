#include "builtin.hpp"
#include "pil.hpp"
#include <cassert>
#include <map>

// we only define built-in functions that actually get used. thanks, cache. there are reserved built-ins that
// always get pushed
void pushBuiltin(Executor &executor, const BuiltinDef &def, size_t i, std::unordered_map<size_t, Value> &constants) {
   size_t functionId = executor.functions.size();
   Function function;
   function.native = true;
   function.variadic = def.flags & VARIADIC;
   function.nativeFn = i;

   Value value {VALUE_FUNCTION};
   value.function = functionId;

   if (def.flags & RESERVED) {
      assert(!(def.flags & (FILE_LOCK | ENV_LOCK | EXEC_LOCK)) && "A reserved function cannot be locked.");
      size_t cached = cacheLexeme(executor.cache, def.name);
      function.lexeme = cached;
      function.paramCount = def.params;
      executor.functions.push_back(function);
      constants[cached] = value;
   }
   else if (auto it = executor.cache.lexemeCache.find(def.name); it != executor.cache.lexemeCache.end()) {
      if (!executor.allowFileio && (def.flags & FILE_LOCK)) {
         if (executor.fileioErrored) return;
         executor.fileioErrored = true;
         error(executor.diagnostics, 0, 0, "Program contains file I/O built-ins. If you are the author or trust this file then rerun it with the '--allow-fileio' flag. Allowing file access can put your computer at risk. You can review all file I/O calls at the succeeding errors");
         return;
      }

      if (!executor.allowEnv && (def.flags & ENV_LOCK)) {
         if (executor.envErrored) return;
         executor.envErrored = true;
         error(executor.diagnostics, 0, 0, "Program contains environment variable built-ins. If you are the author or trust this file then rerun it with the '--allow-env' flag. Allowing environment variable access can put your privacy and secrets at risk. Review all calls at the succeeding errors");
         return;
      }

      if (!executor.allowExec && (def.flags & EXEC_LOCK)) {
         if (executor.execErrored) return;
         executor.execErrored = true;
         error(executor.diagnostics, 0, 0, "Program contains 'os-exec'. If you are the author or trust this file then rerun it with the '--allow-exec' flag. Allowing 'os-exec' puts your computer AT A MASSIVE RISK! Review all calls at the succeeding errors");
         return;
      }

      function.lexeme = it->second;
      function.paramCount = def.params;
      executor.functions.push_back(function);
      constants[it->second] = value;
   }
}

Value parseToken(Executor &executor, Token token, const std::unordered_map<size_t, size_t> &functionParamMap, const std::unordered_map<size_t, Value> &constants) {
   Value value {VALUE_COUNT};
   switch (token.type) {
   case TOKEN_IDENTIFIER:
      if (auto it = functionParamMap.find(token.lexeme); it != functionParamMap.end()) {
         value.type = VALUE_LOCAL;
         value.local = it->second;
      }
      else if (auto it = constants.find(token.lexeme); it != constants.end()) {
         return it->second;
      }
      else {
         error(executor.diagnostics, token.file, token.line, "Variable '%s' does not exist", getLexeme(executor.cache, token.lexeme).c_str());
      }
      break;
   case TOKEN_INTEGER:
      value.type = VALUE_INTEGER;
      try {
         value.integer = std::stoll(getLexeme(executor.cache, token.lexeme));
      }
      catch (...) {
         value.integer = 0;
         error(executor.diagnostics, token.file, token.line, "Invalid integer: %s", getLexeme(executor.cache, token.lexeme).c_str());
      }
      break;
   case TOKEN_FLOATING:
      value.type = VALUE_FLOATING;
      try {
         value.floating = std::stod(getLexeme(executor.cache, token.lexeme));
      }
      catch (...) {
         value.floating = 0;
         error(executor.diagnostics, token.file, token.line, "Invalid floating point number: %s", getLexeme(executor.cache, token.lexeme).c_str());
      }
      break;
   case TOKEN_STRING:
      value.type = VALUE_CSTRING;
      value.string = token.lexeme;
      break;
   case TOKEN_CHARACTER:
      value.type = VALUE_CHARACTER;
      value.character = getLexeme(executor.cache, token.lexeme).front();
      break;
   default:
      error(executor.diagnostics, token.file, token.line, "Unexpected %s while parsing", getTokenName(token.type));
   }
   return value;
}

Value internalParse(Executor &executor, std::vector<Token> &tokens, size_t &i, const std::unordered_map<size_t, size_t> &functionParamMap, const std::unordered_map<size_t, Value> &constants, const std::map<std::pair<size_t, size_t>, Value> &enumerationMap) {
   if (tokens[i].type == TOKEN_L_BRACKET) {
      return evaluateMath(executor, tokens, i, constants);
   }
   else if (tokens[i].type == TOKEN_RETURN_REGISTER || tokens[i].type == TOKEN_REGISTER) {
      Token treg = tokens[i];
      Token tval = tokens[i + 1];
      i += 1;

      Value value = internalParse(executor, tokens, i, functionParamMap, constants, enumerationMap);
      if (value.type != VALUE_INTEGER && value.type != VALUE_FLOATING) {
         error(executor.diagnostics, treg.file, treg.line, "Expected an Integer/Floating after register, got %s instead", getTokenName(tval.type));
         return NULL_VALUE;
      }

      std::vector<Value> &container = (treg.type == TOKEN_RETURN_REGISTER ? executor.returnRegisters : executor.registers);
      size_t maxDefaultValue = (treg.type == TOKEN_RETURN_REGISTER ? DEFAULT_RETURN_REGISTER_COUNT : DEFAULT_REGISTER_COUNT);
      size_t maxValue = (container.empty() ? maxDefaultValue : container.size());
      size_t reg = (value.type == VALUE_INTEGER ? value.integer : value.floating);
      if (reg >= maxValue) {
         error(executor.diagnostics, treg.file, treg.line, "Register %s$%zu is out of bounds. Define '@%sreg-size %zu' directive to mitigate", treg.type == TOKEN_REGISTER ? "" : "R", reg, treg.type == TOKEN_REGISTER ? "" : "return-", reg + 1);
         return NULL_VALUE;
      }
      return Value{.type = (treg.type == TOKEN_REGISTER ? VALUE_REGISTER : VALUE_RETURN_REGISTER), .reg = reg};
   }
   else {
      // enumeration entries
      if (tokens[i].type == TOKEN_IDENTIFIER && tokens[i + 1].type == TOKEN_DOT && tokens[i + 2].type == TOKEN_IDENTIFIER) {
         if (auto it = enumerationMap.find({tokens[i].lexeme, tokens[i + 2].lexeme}); it != enumerationMap.end()) {
            i += 2;
            return it->second;
         }
      }

      // regular values and structure access
      Value value = parseToken(executor, tokens[i], functionParamMap, constants);
      while ((value.type == VALUE_LOCAL || value.type == VALUE_STRUCT_ACCESS) && tokens[i + 1].type == TOKEN_DOT) {
         i += 2;
         if (tokens[i].type != TOKEN_IDENTIFIER) {
            error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected Identifier after structure field access, got %s instead", getTokenName(tokens[i].type));
            return value;
         }
         StructChainLink link;
         if (value.type == VALUE_LOCAL) {
            link.parentIsChain = false;
            link.parent = value.local;
         }
         else {
            link.parentIsChain = true;
            link.parent = value.chain;
         }
         link.field = tokens[i].lexeme;

         size_t index = executor.structChains.size();
         executor.structChains.push_back(link);
         value = Value{.type = VALUE_STRUCT_ACCESS, .chain = index};
      }
      return value;
   }
}

// take the tokens and turn them into executable function blocks and commands. we have 3 levels here: file -> functions ->
// commands. there can be no commands in the file level and no functions in the command level.
void parsePIL(Executor &executor, std::vector<Token> &tokens) {   
   std::unordered_map<size_t, Value> constants;
   executor.main = std::string::npos;

   // estimate code size. some rough estimates
   size_t size = tokens.size();
   executor.code.reserve(size / 3);
   executor.arguments.reserve(size / 4);
   executor.functions.reserve(size / 16 + 4); // I don't remember why I put it as this anymore

   for (size_t i = 0; i < arraySize(BUILTIN_DEFINITIONS); ++i) {
      pushBuiltin(executor, BUILTIN_DEFINITIONS[i], i, constants);
   }

   // function name and label prepass
   std::unordered_map<size_t, size_t> functionParamMap;
   std::map<std::pair<size_t, size_t>, Value> enumerationMap;

   size_t returnLexeme = cacheLexeme(executor.cache, "return");
   size_t returnId = constants[returnLexeme].function;
   size_t returnRegisterCount = (executor.returnRegisters.empty() ? DEFAULT_RETURN_REGISTER_COUNT : executor.returnRegisters.size());

   size_t defineLexeme = cacheLexeme(executor.cache, "local");
   size_t callLexeme = cacheLexeme(executor.cache, "call");
   size_t mainLexeme = cacheLexeme(executor.cache, "main");
   size_t constLexeme = cacheLexeme(executor.cache, "const");
   size_t globalLexeme = cacheLexeme(executor.cache, "global");
   size_t structLexeme = cacheLexeme(executor.cache, "struct");
   size_t arrayLexeme = cacheLexeme(executor.cache, "array");
   size_t enumLexeme = cacheLexeme(executor.cache, "enum");

   for (size_t i = 0; i < size; ++i) {
      if (tokens[i].type == TOKEN_IDENTIFIER && tokens[i].lexeme == structLexeme) {
         bool isArray = false;
         if (tokens[i + 1].type == TOKEN_IDENTIFIER && tokens[i + 1].lexeme == arrayLexeme) {
            i += 1;
            isArray = true;
         }

         i += 1;
         if (tokens[i].type != TOKEN_IDENTIFIER) {
            error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected Identifier after structure declaration, got %s instead", getTokenName(tokens[i].type));
            continue;
         }
         
         size_t position = tokens[i].lexeme;
         if (auto it = constants.find(position); it != constants.end()) {
            error(executor.diagnostics, tokens[i].file, tokens[i].line, "%s '%s' redefined", getValueName(it->second.type), getLexeme(executor.cache, position).c_str());
         }

         size_t functionId = executor.functions.size();
         Function function;
         function.isStructure = true;
         function.isStructureArray = isArray;
         function.lexeme = position;
         function.position = functionId;
         function.localCount = 0;
         function.paramCount = 0;

         Value value {VALUE_STRUCT};
         value.structure = functionId;
         constants[position] = value;
         executor.functions.push_back(function);
      }
      else if (tokens[i].type == TOKEN_IDENTIFIER && tokens[i].lexeme == enumLexeme) {
         i += 2; // skip it from treating enumerations as functions
      }
      else if (tokens[i].type == TOKEN_IDENTIFIER && (tokens[i + 1].type == TOKEN_L_PAREN || tokens[i + 1].type == TOKEN_LABEL)) {
         size_t position = tokens[i].lexeme;
         if (auto it = constants.find(position); it != constants.end()) {
            error(executor.diagnostics, tokens[i].file, tokens[i].line, "%s '%s' redefined", getValueName(it->second.type), getLexeme(executor.cache, position).c_str());
         }

         size_t functionId = executor.functions.size();
         Function function;
         function.isLabel = (tokens[i + 1].type == TOKEN_LABEL);
         function.lexeme = position;

         if (function.isLabel) {
            Value value {VALUE_LABEL};
            value.label = functionId;
            constants[position] = value;
         }
         else {
            Value value {VALUE_FUNCTION};
            value.function = functionId;
            constants[position] = value;
         }
         executor.functions.push_back(function);
      }
   }

   // real parsing
   Function *lastFunction = nullptr;
   bool firstFunction = true;

   for (size_t i = 0; i < size && tokens[i].type != TOKEN_EOF; ++i) {
      // labels
      if (tokens[i].type == TOKEN_IDENTIFIER && tokens[i + 1].type == TOKEN_LABEL) {
         Function &label = executor.functions[constants[tokens[i].lexeme].label];
         label.position = executor.code.size();
         i += 1;
      }
      // enumeration declarations
      else if (tokens[i].type == TOKEN_IDENTIFIER && tokens[i].lexeme == enumLexeme) {
         i += 1;
         if (tokens[i].type != TOKEN_IDENTIFIER) {
            error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected Identifier after enumeration declaration, got %s instead", getTokenName(tokens[i].type));
            continue;
         }

         pilint_t iota = 0;
         size_t line = tokens[i].line, file = tokens[i].file;
         size_t lexeme = tokens[i].lexeme;
         size_t entryCount = 0;
         enumerationMap[{lexeme, 0}] = NULL_VALUE; // token of existance
         i += 1;

         if (tokens[i].type != TOKEN_L_PAREN) {
            error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected Left Parentheses after enumeration Identifier, got %d instead", getTokenName(tokens[i].type));
            continue;
         }

         for (++i; i < size && tokens[i].type != TOKEN_EOF && tokens[i].type != TOKEN_R_PAREN; ++i) {
            if (tokens[i].type != TOKEN_IDENTIFIER) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected Identifier, got %s instead", getTokenName(tokens[i].type));
               break;
            }

            size_t enumerationLexeme = tokens[i].lexeme;
            i += 1;

            if (tokens[i].type == TOKEN_ASSIGN) {
               i += 1;
               Value value = internalParse(executor, tokens, i, {}, constants, enumerationMap);
               if (value.type != VALUE_INTEGER && value.type != VALUE_FLOATING) {
                  error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected a Number after Assign, got %d instead", getTokenName(tokens[i].type));
               }
               else {
                  iota = (value.type == VALUE_INTEGER ? value.integer : value.floating);
               }
               i += 1;
            }
            enumerationMap[{lexeme, enumerationLexeme}] = Value{.type = VALUE_INTEGER, .integer = iota};
            iota += 1;
            entryCount += 1;

            if (tokens[i].type == TOKEN_EOF || tokens[i].type == TOKEN_R_PAREN) {
               break;
            }

            if (tokens[i].type != TOKEN_COMMA) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected Comma after enumeration entry, got %s instead", getTokenName(tokens[i].type));
               i -= 1;
            }
         }

         if (tokens[i].type != TOKEN_R_PAREN) {
            error(executor.diagnostics, file, line, "Unterminated enumeration");
         }

         if (entryCount == 0) {
            error(executor.diagnostics, file, line, "Enumeration '%s' does not contain any entries", getLexeme(executor.cache, lexeme).c_str());
         }
      }
      // structure declarations
      else if (tokens[i].type == TOKEN_IDENTIFIER && tokens[i].lexeme == structLexeme) {
         if (tokens[i + 1].type == TOKEN_IDENTIFIER && tokens[i + 1].lexeme == arrayLexeme) {
            i += 1;
         }
         
         size_t line = tokens[i].line, file = tokens[i].file;
         Function &structure = executor.functions[constants[tokens[i + 1].lexeme].structure];
         structure.nativeFn = executor.structData.size();

         i += 2;
         if (tokens[i].type != TOKEN_L_PAREN) {
            error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected Left Parentheses after structure Identifier, got %d instead", getTokenName(tokens[i].type));
            continue;
         }

         for (++i; i < size && tokens[i].type != TOKEN_EOF && tokens[i].type != TOKEN_R_PAREN; ++i) {
            if (tokens[i].type != TOKEN_IDENTIFIER) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected Identifier, got %s instead", getTokenName(tokens[i].type));
               break;
            }
            executor.structData.emplace_back(structure.position, tokens[i].lexeme, structure.localCount);
            structure.paramCount += 1;
            structure.localCount += 1;

            i += 1;
            if (tokens[i].type == TOKEN_EOF || tokens[i].type == TOKEN_R_PAREN) {
               break;
            }

            if (tokens[i].type != TOKEN_COMMA) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected Comma after Identifier, got %s instead", getTokenName(tokens[i].type));
               i -= 1;
            }
         }

         if (tokens[i].type != TOKEN_R_PAREN) {
            error(executor.diagnostics, file, line, "Unterminated structure");
         }

         if (structure.localCount == 0) {
            error(executor.diagnostics, file, line, "Structure '%s' does not contain any fields", getLexeme(executor.cache, structure.lexeme).c_str());
         }
      }
      // function declarations
      else if (tokens[i].type == TOKEN_IDENTIFIER && tokens[i + 1].type == TOKEN_L_PAREN) {
         if (!firstFunction && (executor.code.empty() || executor.code.back().lexeme != returnLexeme)) {
            executor.code.emplace_back(returnLexeme, tokens[i-1].file, tokens[i-1].line, 0, 0, returnId);
         }
         size_t functionId = constants[tokens[i].lexeme].function;
         size_t start = i;

         Function &function = executor.functions[functionId];
         function.paramCount = 0;

         firstFunction = false;
         functionParamMap.clear();

         for (i += 2; i < size && tokens[i].type != TOKEN_EOF && tokens[i].type != TOKEN_R_PAREN; ++i) {
            if (tokens[i].type != TOKEN_IDENTIFIER) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected Identifier, got %s instead", getTokenName(tokens[i].type));
            }

            if (functionParamMap.find(tokens[i].lexeme) != functionParamMap.end() || constants.find(tokens[i].lexeme) != constants.end()) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Redefined function parameter '%s'", getLexeme(executor.cache, tokens[i].lexeme).c_str());
            }
            function.paramCount += 1;
            functionParamMap[tokens[i].lexeme] = functionParamMap.size();

            i += 1;
            if (tokens[i].type == TOKEN_EOF || tokens[i].type == TOKEN_R_PAREN) {
               break;
            }

            if (tokens[i].type != TOKEN_COMMA) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected Comma after Identifier, got %s instead", getTokenName(tokens[i].type));
               i -= 1;
            }
         }

         if (tokens[i].type != TOKEN_R_PAREN) {
            error(executor.diagnostics, tokens[start].file, tokens[start].line, "Unterminated function parameters");
         }
         function.position = executor.code.size();
         function.localCount = functionParamMap.size();
         lastFunction = &function;

         if (function.lexeme == mainLexeme) {
            executor.main = functionId;
         }
      }
      // const/global declaration
      else if (tokens[i].type == TOKEN_IDENTIFIER && (tokens[i].lexeme == constLexeme || tokens[i].lexeme == globalLexeme)) {
         bool constant = (tokens[i].lexeme == constLexeme);
         do {
            i += 1;
            size_t lexeme = tokens[i].lexeme;
            if (tokens[i].type != TOKEN_IDENTIFIER) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected identifier after const/global keyword, got %s instead", getTokenName(tokens[i].type));
               continue; // might be EOF
            }

            i += 1;
            if (tokens[i].type != TOKEN_ASSIGN && !constant) {
               constants[lexeme] = Value{.type = VALUE_GLOBAL, .global = executor.globals.size()};
               executor.globals.push_back(NULL_VALUE);
               continue;
            }

            if (tokens[i].type != TOKEN_ASSIGN) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected Assign after constant/global declaration identifier, got %s instead", getTokenName(tokens[i].type));
               continue;
            }

            i += 1;
            TokenType type = tokens[i].type;
            if (type == TOKEN_EOF || type == TOKEN_REGISTER || type == TOKEN_RETURN_REGISTER) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected a constant value in the constant/global declaration, got %s instead", getTokenName(tokens[i].type));
               continue;
            }

            if (constants.find(lexeme) != constants.end()) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Identifier '%s' redefined in constant/global declaration", getLexeme(executor.cache, lexeme).c_str());
            }

            if (constant) {
               constants[lexeme] = internalParse(executor, tokens, i, {}, constants, enumerationMap); // functionParamMap handles runtime values, not constants
            }
            else {
               constants[lexeme] = Value{.type = VALUE_GLOBAL, .global = executor.globals.size()};
               executor.globals.push_back(internalParse(executor, tokens, i, {}, constants, enumerationMap));
            }
            i += 1;
         }
         while (tokens[i].type == TOKEN_COMMA);

         if (tokens[i].type != TOKEN_SEMICOLON) {
            error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected a semicolon after constant/global declaration, got %s instead", getTokenName(tokens[i].type));
            i -= 1;
            continue;
         }
      }
      // variable declaration
      else if (lastFunction && tokens[i].type == TOKEN_IDENTIFIER && tokens[i].lexeme == defineLexeme) {
         for (++i; i < size && tokens[i].type != TOKEN_EOF && tokens[i].type != TOKEN_SEMICOLON; ++i) {
            if (tokens[i].type != TOKEN_IDENTIFIER) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected unique Identifier, got %s instead", getTokenName(tokens[i].type));
               continue;
            }

            if (functionParamMap.find(tokens[i].lexeme) != functionParamMap.end() || constants.find(tokens[i].lexeme) != constants.end()) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Redefined variable '%s'", getLexeme(executor.cache, tokens[i].lexeme).c_str());
               continue;
            }
            functionParamMap[tokens[i].lexeme] = functionParamMap.size();

            i += 1;
            if (tokens[i].type == TOKEN_EOF || tokens[i].type == TOKEN_SEMICOLON) {
               break;
            }

            if (tokens[i].type != TOKEN_COMMA) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected Comma after Identifier, got %s instead", getTokenName(tokens[i].type));
               i -= 1;
            }
         }
         lastFunction->localCount = functionParamMap.size();
      }
      // function calls
      else if (tokens[i].type == TOKEN_IDENTIFIER) {
         auto it = constants.find(tokens[i].lexeme);
         if (it == constants.end()) {
            if (tokens[i].type == TOKEN_IDENTIFIER) {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "No such function '%s'", getLexeme(executor.cache, tokens[i].lexeme).c_str());
            }
            else {
               error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected a function call, got %s instead", getTokenName(tokens[i].type));
            }
            // to not spiral errors out of control
            while (i < size && tokens[i].type != TOKEN_EOF && tokens[i].type != TOKEN_SEMICOLON) i += 1;
            continue;
         }

         executor.code.emplace_back(tokens[i].lexeme, tokens[i].file, tokens[i].line, executor.arguments.size(), 0, it->second.function);
         Command &command = executor.code.back();
         bool isCall = (tokens[i].lexeme == callLexeme);
         bool isReturn = (tokens[i].lexeme == returnLexeme);

         for (++i; i < size && tokens[i].type != TOKEN_EOF && tokens[i].type != TOKEN_SEMICOLON; ++i) {
            Value value = internalParse(executor, tokens, i, functionParamMap, constants, enumerationMap);
            if (isCall && value.type == VALUE_FUNCTION) {
               if (command.callee != std::string::npos) {
                  error(executor.diagnostics, command.file, command.line, "call: Cannot call multiple functions in a single call");
               }
               command.callee = command.argCount;
            }

            executor.arguments.push_back(value);
            command.argCount += 1;

            i += 1;
            if (tokens[i].type == TOKEN_SEMICOLON || tokens[i].type == TOKEN_EOF || tokens[i].type != TOKEN_COMMA) {
               break;
            }
         }

         if (tokens[i].type != TOKEN_SEMICOLON) {
            error(executor.diagnostics, tokens[i].file, tokens[i].line, "Expected a semicolon after function call, got %s instead", getTokenName(tokens[i].type));
            // to not spiral errors out of control
            while (i < size && tokens[i].type != TOKEN_EOF && tokens[i].type != TOKEN_SEMICOLON) i += 1;
            continue;
         }

         if (isCall && command.callee == std::string::npos) {
            error(executor.diagnostics, command.file, command.line, "call: Expected function name to call");
         }

         size_t args = command.argCount;
         size_t params = executor.functions[it->second.function].paramCount;
         bool variadic = executor.functions[it->second.function].variadic;

         if ((!variadic && args != params) || (variadic && args < params)) {
            error(executor.diagnostics, command.file, command.line, "Function '%s' expected %s%zu parameters, but received %zu arguments", getLexeme(executor.cache, command.lexeme).c_str(), (variadic ? ">" : ""), params, args);
         }

         if (isCall) {
            size_t idx = executor.arguments[command.argStart + command.callee].function;
            Function &function = executor.functions[idx];
            args = command.argCount - command.callee - 1;
            params = function.paramCount;
            variadic = function.variadic;
            if ((!variadic && args != params) || (variadic && args < params)) {
               error(executor.diagnostics, command.file, command.line, "call: Function '%s' expected %s%zu parameters, but received %zu arguments", getLexeme(executor.cache, function.lexeme).c_str(), (variadic ? ">" : ""), params, args);
            }
         }
         else if (isReturn && args > returnRegisterCount) {
            error(executor.diagnostics, command.file, command.line, "return: Can return at maximum %zu values. Define '@return-reg-size %zu' directive to mitigate. Error", returnRegisterCount, args);
         }
      }
      // something we didn't expect
      else {
         error(executor.diagnostics, tokens[i].file, tokens[i].line, "Unexpected %s", getTokenName(tokens[i].type));
      }
   }
   if (!tokens.empty()) {
      executor.code.emplace_back(returnLexeme, tokens.back().file, tokens.back().line, 0, 0, returnId);
   }

   if (executor.main == std::string::npos) {
      error(executor.diagnostics, 0, 0, "Main program entry point 'main' is not defined");
      return;
   }

   Function &main = executor.functions[executor.main];
   if (main.paramCount != 0 || main.variadic) {
      error(executor.diagnostics, executor.code[main.position].file, executor.code[main.position].line, "Expected function 'main' to not have any parameters nor be variadic");
      return;
   }
}
