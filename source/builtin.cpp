#include "builtin.hpp"
#include "builtinhelpers.hpp"
#include <iomanip>
#include <iostream>
#include <thread>

// output
void builtinFlush(const Command &command, Executor &executor) {
   std::cout << std::flush;
}

void builtinPrintch(const Command &command, Executor &executor) {
   putchar(getChar(command, executor, "printch", 0));
}

void builtinPrint(const Command &command, Executor &executor) {
   print(command, executor, "print", command.file, command.line);
}

void builtinPrintln(const Command &command, Executor &executor) {
   print(command, executor, "printn", command.file, command.line);
   putchar('\n');
}

void builtinPrintf(const Command &command, Executor &executor) {
   printf("%s", format(command, executor, "printf", 0).c_str());
}

void builtinPrintfln(const Command &command, Executor &executor) {
   printf("%s\n", format(command, executor, "printfn", 0).c_str());
}

void builtinRead(const Command &command, Executor &executor) {
   std::string input;
   std::cin >> input;
   storeString(executor, command, input, back(executor, command), "read");
}

void builtinReadln(const Command &command, Executor &executor) {
   std::string input;
   std::getline(std::cin >> std::ws, input);
   storeString(executor, command, input, back(executor, command), "readline");
}

void builtinReadch(const Command &command, Executor &executor) {
   Value value {VALUE_CHARACTER};
   value.character = getCanonicalChar();
   storeInRegister(executor, command, value, "readchar");
}

void builtinSetecho(const Command &command, Executor &executor) {
   setEcho(getBool(executor, command, 0));
}

// control flow
void builtinLe(const Command &command, Executor &executor) {
   comparisonBuiltin(executor, command, "le", COMPARISON_LESS, false, false);
}

void builtinGr(const Command &command, Executor &executor) {
   comparisonBuiltin(executor, command, "gr", COMPARISON_GREATER, false, false);
}

void builtinLeeq(const Command &command, Executor &executor) {
   comparisonBuiltin(executor, command, "leeq", COMPARISON_GREATER, true, false);
}

void builtinGreq(const Command &command, Executor &executor) {
   comparisonBuiltin(executor, command, "greq", COMPARISON_LESS, true, false);
}

void builtinEq(const Command &command, Executor &executor) {
   comparisonBuiltin(executor, command, "eq", COMPARISON_EQUAL, false, true);
}

void builtinNeq(const Command &command, Executor &executor) {
   comparisonBuiltin(executor, command, "neq", COMPARISON_EQUAL, true, true);
}

void builtinOr(const Command &command, Executor &executor) {
   bool cond1 = getBool(executor, command, 0);
   bool cond2 = getBool(executor, command, 1);
   storeBoolean(executor, command, cond1 || cond2, "or");
}

void builtinAnd(const Command &command, Executor &executor) {
   bool cond1 = getBool(executor, command, 0);
   bool cond2 = getBool(executor, command, 1);
   storeBoolean(executor, command, cond1 && cond2, "and");
}

void builtinNot(const Command &command, Executor &executor) {
   storeBoolean(executor, command, !getBool(executor, command, 0), "not");
}

void builtinGoto(const Command &command, Executor &executor) {
   jumpToLabel(executor, resolveVariable(executor, arg(executor, command, 0)), "goto", "1st", command.file, command.line, true);
}

void builtinJmp(const Command &command, Executor &executor) {
   jumpToLabel(executor, resolveVariable(executor, arg(executor, command, 1)), "jmp", "2nd", command.file, command.line, getBool(executor, command, 0));
}

void builtinJmpn(const Command &command, Executor &executor) {
   jumpToLabel(executor, resolveVariable(executor, arg(executor, command, 1)), "jmpn", "2nd", command.file, command.line, !getBool(executor, command, 0));
}

void builtinJmptable(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   bool hasDefault = (command.argCount % 2 == 0);

   for (size_t i = 1; i < command.argCount - hasDefault; i += 2) {
      Value result = resolveVariable(executor, arg(executor, command, i));
      if (valuesEqual(executor, command, value, result, "jmptable")) {
         jumpToLabel(executor, resolveVariable(executor, arg(executor, command, i + 1)), "jmptable", "destination", command.file, command.line, true);
         return;
      }
   }
   if (hasDefault) {
      jumpToLabel(executor, resolveVariable(executor, back(executor, command)), "jmptable", "destination", command.file, command.line, true);
   }
}

void builtinCall(const Command &command, Executor &executor) {
   Function &function = executor.functions[arg(executor, command, command.callee).function];
   call(executor, command, function, command.callee, command.callee, command.argCount - command.callee - 1);
}

void builtinFunccall(const Command &command, Executor &executor) {
   Value f = resolveVariable(executor, arg(executor, command, 0));
   if (f.type != VALUE_FUNCTION) {
      error(executor.diagnostics, command.file, command.line, "func-call: Expected function to call for the 1st argument, got %s instead", getValueName(f.type));
      return;
   }
   Function &function = executor.functions[f.function];
   size_t params = function.paramCount;
   size_t args = command.argCount - 1;
   bool variadic = function.variadic;

   if ((!variadic && args != params) || (variadic && args < params)) {
      error(executor.diagnostics, command.file, command.line, "func-call: Called function expected %s%zu parameters, but received %zu arguments", (variadic ? ">" : ""), params, args);
      return;
   }
   Command copy = command;
   copy.argStart += 1;
   copy.argCount -= 1;
   call(executor, copy, function, -1, std::string::npos, args);
}

void builtinReturn(const Command &command, Executor &executor) {
   if (executor.stackTrace.size() <= 1) {
      executor.exitCalled = true;
      return;
   }
   Trace trace = executor.stackTrace.top();
   executor.pointer = trace.position;
   executor.returnCount = command.argCount;

   for (size_t i = 0; i < executor.returnCount; ++i) {
      Value value = resolveVariable(executor, arg(executor, command, i));
      executor.returnRegisters[i] = value;
   }
   executor.locals.resize(trace.localStart);
   executor.stackTrace.pop();

   // call shenanigans
   if (trace.callArgCount != std::string::npos) {
      if (executor.returnCount != trace.callArgCount) {
         warn(executor.diagnostics, command.file, command.line, "call: Expected %zu return values, but got %zu instead", trace.callArgCount, executor.returnCount);
      }

      size_t count = std::min(executor.returnCount, trace.callArgCount);
      for (size_t i = 0; i < count; ++i) {
         Value reg = executor.arguments[trace.callArgStart + i];
         storeInRegister(executor, command, reg, executor.returnRegisters[i], "call");
      }
   }
}

// error handling
void builtinCatch(const Command &command, Executor &executor) {
   Value f = resolveVariable(executor, arg(executor, command, 1));
   if (f.type != VALUE_FUNCTION) {
      error(executor.diagnostics, command.file, command.line, "catch: Expected function to call for the 2nd argument, got %s instead", getValueName(f.type));
      return;
   }
   Function &function = executor.functions[f.function];
   size_t params = function.paramCount;
   size_t args = command.argCount - 2;
   bool variadic = function.variadic;

   if ((!variadic && args != params) || (variadic && args < params)) {
      error(executor.diagnostics, command.file, command.line, "catch: Called function expected %s%zu parameters, but received %zu arguments", (variadic ? ">" : ""), params, args);
      return;
   }
   Command copy = command;
   copy.argStart += 2;
   copy.argCount -= 2;
   call(executor, copy, function, -1, std::string::npos, args);

   size_t size = executor.diagnostics.diagnostics.size();
   if (size == 0) {
      storeInRegister(executor, command, arg(executor, command, 0), NULL_VALUE, "catch");
   }
   else {
      std::vector<Value> errors (size);
      for (size_t i = 0; i < size; ++i) {
         errors[i].type = VALUE_STRING;
         errors[i].string = allocateString(executor, executor.diagnostics.diagnostics[i].message);
      }
      storeArray(executor, command, errors, arg(executor, command, 0), "catch");
   }
   clear(executor.diagnostics);
}

void builtinAssert(const Command &command, Executor &executor) {
   if (!getBool(executor, command, 0)) {
      Value value = resolveVariable(executor, arg(executor, command, 1));
      if (value.type != VALUE_STRING && value.type != VALUE_CSTRING) {
         error(executor.diagnostics, command.file, command.line, "assert: Expected String as the 2nd argument, got %s instead", getValueName(value.type));
         return;
      }
      error(executor.diagnostics, 0, 0, format(command, executor, "assert", 1).c_str());
   }
}

void builtinWarn(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   if (value.type != VALUE_STRING && value.type != VALUE_CSTRING) {
      error(executor.diagnostics, command.file, command.line, "warn: Expected String as the 1st argument, got %s instead", getValueName(value.type));
      return;
   }
   warn(executor.diagnostics, 0, 0, format(command, executor, "warn", 0).c_str());
}

void builtinError(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   if (value.type != VALUE_STRING && value.type != VALUE_CSTRING) {
      error(executor.diagnostics, command.file, command.line, "error: Expected String as the 1st argument, got %s instead", getValueName(value.type));
      return;
   }
   error(executor.diagnostics, 0, 0, format(command, executor, "error", 0).c_str());
}

void builtinExit(const Command &command, Executor &executor) {
   logMemoryLeaks(executor);
   pilfloat_t code = getNum(executor, command, 0, "exit");
   exit(code);
}

void builtinStackdepth(const Command &command, Executor &executor) {
   storeNumber(executor, command, executor.stackTrace.size(), false, "stack-depth");
}

void builtinStackname(const Command &command, Executor &executor) {
   storeString(executor, command, getLexeme(executor.cache, executor.code[executor.stackTrace.top().position].lexeme), back(executor, command), "stack-name");
}

void builtinStackline(const Command &command, Executor &executor) {
   storeNumber(executor, command, executor.code[executor.stackTrace.top().position].line, false, "stack-line");
}

void builtinStackfile(const Command &command, Executor &executor) {
   storeString(executor, command, getLexeme(executor.cache, executor.code[executor.stackTrace.top().position].file), back(executor, command), "stack-file");
}

void builtinStacktrace(const Command &command, Executor &executor) {
   logStackTrace(executor, SEVERITY_NONE);
}

// types
void builtinTypeof(const Command &command, Executor &executor) {
   ValueType type = resolveVariable(executor, arg(executor, command, 0)).type;
   const char *string;
   switch (type) {
   case VALUE_INTEGER: string = "int"; break;
   case VALUE_FLOATING: string = "float"; break;
   case VALUE_CHARACTER: string = "char"; break;
   case VALUE_STRING: case VALUE_CSTRING: string = "string"; break;
   case VALUE_ARRAY: string = "array"; break;
   case VALUE_MAP: string = "map"; break;
   case VALUE_FILE: string = "file"; break;
   case VALUE_FUNCTION: string = "function"; break;
   case VALUE_LABEL: string = "label"; break;
   case VALUE_STRUCT: string = "struct"; break;
   case VALUE_COUNT: string = "null"; break;
   default:
      printf("PIL::builtinTypeof: Cannot get the type of value %s.\n", getValueName(type));
      exit(EXIT_FAILURE);
   }
   storeString(executor, command, string, back(executor, command), "typeof");
}

void builtinIsnum(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_INTEGER || value.type == VALUE_FLOATING, "is-num");
}

void builtinIsfloat(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_FLOATING, "is-float");
}

void builtinIsint(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_INTEGER, "is-int");
}

void builtinIschar(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_CHARACTER, "is-char");
}

void builtinIsstring(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_STRING || value.type == VALUE_CSTRING, "is-string");
}

void builtinIsdynamicstring(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_STRING, "is-dynamic-string");
}

void builtinIsconstantstring(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_CSTRING, "is-constant-string");
}

void builtinIsarray(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_ARRAY, "is-array");
}

void builtinIsmap(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_MAP, "is-map");
}

void builtinIsfilehandle(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_FILE, "is-file-handle");
}

void builtinIsreg(const Command &command, Executor &executor) {
   Value value = arg(executor, command, 0);
   storeBoolean(executor, command, value.type == VALUE_LOCAL || value.type == VALUE_GLOBAL || value.type == VALUE_REGISTER || value.type == VALUE_RETURN_REGISTER || value.type == VALUE_STRUCT_ACCESS, "is-reg");
}

void builtinIsfunction(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_FUNCTION, "is-function");
}

void builtinIslabel(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_LABEL, "is-label");
}

void builtinIsStruct(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_LABEL, "is-struct");
}

void builtinIsOfStruct(const Command &command, Executor &executor) {
   Value arrayValue = resolveVariable(executor, arg(executor, command, 0));
   if (arrayValue.type != VALUE_ARRAY) {
      error(executor.diagnostics, command.file, command.line, "is-of-struct: Expected Array for the 1st argument, got %s instead", getValueName(arrayValue.type));
      return;
   }

   auto it = executor.arrays.find(arrayValue.array);
   if (it == executor.arrays.end()) {
      error(executor.diagnostics, command.file, command.line, "Invalid Array ID %zu. Use after free", arrayValue.array);
      return;
   }

   Value structValue = resolveVariable(executor, arg(executor, command, 1));
   if (structValue.type != VALUE_STRUCT) {
      error(executor.diagnostics, command.file, command.line, "is-of-struct: Expected Struct for the 2nd argument, got %s instead", getValueName(structValue.type));
      return;
   }
   storeBoolean(executor, command, it->second.structureId == structValue.structure, "is-of-struct");
}

void builtinIsnull(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_COUNT, "is-null");
}

void builtinIsinf(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_FLOATING && std::isinf(value.floating), "is-inf");
}

void builtinIsnan(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   storeBoolean(executor, command, value.type == VALUE_FLOATING && std::isnan(value.floating), "is-nan");
}

void builtinToint(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   Value integer {VALUE_INTEGER};
   switch (value.type) {
   case VALUE_INTEGER: integer.integer = value.integer; break;
   case VALUE_FLOATING: integer.integer = value.floating; break;
   case VALUE_CHARACTER: integer.integer = value.character; break;
   case VALUE_STRING: case VALUE_CSTRING: {
      const std::string &str = (value.type == VALUE_STRING ? getString(executor, value.string, command.file, command.line) : getLexeme(executor.cache, value.string));
      try {
         size_t pos = 0;
         pilint_t result = std::stoll(str, &pos);
         if (pos != str.size() || str.empty() || std::isspace(str.front())) integer.type = VALUE_COUNT;
         else integer.integer = result;
      }
      catch (...) { integer.type = VALUE_COUNT; }
      break;
   }
   default: error(executor.diagnostics, command.file, command.line, "to-int: Cannot convert %s to Integer", getValueName(value.type));
   }
   storeInRegister(executor, command, integer, "to-int");
}

void builtinTofloat(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   Value floating {VALUE_FLOATING};
   switch (value.type) {
   case VALUE_INTEGER: floating.floating = value.integer; break;
   case VALUE_FLOATING: floating.floating = value.floating; break;
   case VALUE_CHARACTER: floating.floating = value.character; break;
   case VALUE_STRING: case VALUE_CSTRING: {
      const std::string &str = (value.type == VALUE_STRING ? getString(executor, value.string, command.file, command.line) : getLexeme(executor.cache, value.string));
      try {
         size_t pos = 0;
         pilfloat_t result = std::stod(str, &pos);
         if (pos != str.size() || str.empty() || std::isspace(str.front())) floating.type = VALUE_COUNT;
         else floating.floating = result;
      }
      catch (...) { floating.type = VALUE_COUNT; }
      break;
   }
   default: error(executor.diagnostics, command.file, command.line, "to-float: Cannot convert %s to Floating", getValueName(value.type));
   }
   storeInRegister(executor, command, floating, "to-float");
}

void builtinTochar(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   Value character {VALUE_CHARACTER};
   switch (value.type) {
   case VALUE_INTEGER: character.character = value.integer; break;
   case VALUE_FLOATING: character.character = value.floating; break;
   case VALUE_CHARACTER: character.character = value.character; break;
   default: error(executor.diagnostics, command.file, command.line, "to-char: Cannot convert %s to Character", getValueName(value.type));
   }
   storeInRegister(executor, command, character, "to-char");
}

// misc.
void builtinTime(const Command &command, Executor &executor) {
   static const auto start = std::chrono::steady_clock::now();
   pilfloat_t ms = std::chrono::duration<pilfloat_t, std::milli>(std::chrono::steady_clock::now() - start).count();
   storeNumber(executor, command, ms, true, "time");
}

void builtinUnixTime(const Command &command, Executor &executor) {
   pilfloat_t epoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
   storeNumber(executor, command, epoch, false, "unix-time");
}

void builtinDate(const Command &command, Executor &executor) {
   Value string = resolveVariable(executor, arg(executor, command, 0));
   if (string.type != VALUE_CSTRING && string.type != VALUE_STRING) {
      error(executor.diagnostics, command.file, command.line, "date: Expected String for the 1st argument, got %s instead", getValueName(string.type));
      return;
   }
   std::string &str = (string.type == VALUE_CSTRING ? getLexeme(executor.cache, string.string) : getString(executor, string.string, command.file, command.line));
   time_t t = std::time(nullptr);
   tm lt = *std::localtime(&t);
   std::ostringstream stream;
   stream << std::put_time(&lt, str.c_str());
   storeString(executor, command, stream.str().c_str(), back(executor, command), "date");
}

void builtinSleep(const Command &command, Executor &executor) {
   pilfloat_t s = getNum(executor, command, 0, "sleep");
   std::this_thread::sleep_for(std::chrono::duration<pilfloat_t>(s));
}

void builtinSwap(const Command &command, Executor &executor) {
   Value &arg0 = executor.arguments[command.argStart + 0];
   Value &arg1 = executor.arguments[command.argStart + 1];

   bool arg0Valid = (arg0.type == VALUE_LOCAL || arg0.type == VALUE_REGISTER || arg0.type == VALUE_RETURN_REGISTER);
   bool arg1Valid = (arg1.type == VALUE_LOCAL || arg1.type == VALUE_REGISTER || arg1.type == VALUE_RETURN_REGISTER);
   if (!arg0Valid) error(executor.diagnostics, command.file, command.line, "swap: Expected 1st argument to be a Register/Variable, got %s instead", getValueName(arg0.type));
   if (!arg1Valid) error(executor.diagnostics, command.file, command.line, "swap: Expected 2nd argument to be a Register/Variable, got %s instead", getValueName(arg1.type));
   if (!arg0Valid || !arg1Valid) return;

   Value &a = resolveVariableByRef(executor, arg0);
   Value &b = resolveVariableByRef(executor, arg1);
   std::swap(a, b);
}

void builtinSet(const Command &command, Executor &executor) {
   storeInRegister(executor, command, resolveVariable(executor, arg(executor, command, 0)), "set");
}

void builtinValTable(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   Value dest = arg(executor, command, 1);
   bool hasDefault = (command.argCount % 2 == 1);

   for (size_t i = 2; i < command.argCount - hasDefault; i += 2) {
      Value result = resolveVariable(executor, arg(executor, command, i));
      if (valuesEqual(executor, command, value, result, "valtable")) {
         storeInRegister(executor, command, dest, resolveVariable(executor, arg(executor, command, i+1)), "valtable");
         return;
      }
   }
   Value defaultValue = (hasDefault ? resolveVariable(executor, back(executor, command)) : NULL_VALUE);
   storeInRegister(executor, command, dest, defaultValue, "valtable");
}

void builtinTableContains(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   Value dest = arg(executor, command, 1);
   for (size_t i = 2; i < command.argCount; ++i) {
      Value result = resolveVariable(executor, arg(executor, command, i));
      if (valuesEqual(executor, command, value, result, "table-contains")) {
         Value value {VALUE_INTEGER};
         value.integer = 1;
         storeInRegister(executor, command, dest, value, "table-contains");
         return;
      }
   }
   Value returnValue {VALUE_INTEGER};
   returnValue.integer = 0;
   storeInRegister(executor, command, dest, returnValue, "table-contains");
}

void builtinRegSize(const Command &command, Executor &executor) {
   storeNumber(executor, command, executor.registers.size(), false, "reg-size");
}

void builtinRegAt(const Command &command, Executor &executor) {
   pilint_t id = getNum(executor, command, 0, "reg-at");
   if (id < 0 || (size_t)id >= executor.registers.size()) {
      error(executor.diagnostics, command.file, command.line, "reg-at: Index %ld is out of bounds", id);
      return;
   }
   storeInRegister(executor, command, executor.registers[id], "reg-at");
}

void builtinRegSet(const Command &command, Executor &executor) {
   pilint_t id = getNum(executor, command, 0, "reg-set");
   if (id < 0 || (size_t)id >= executor.registers.size()) {
      error(executor.diagnostics, command.file, command.line, "reg-set: Index %ld is out of bounds", id);
      return;
   }
   Value reg {.type = VALUE_REGISTER, .reg = (piluint_t)id};
   storeInRegister(executor, command, reg, resolveVariable(executor, arg(executor, command, 1)), "reg-set");
}

void builtinReturnRegSize(const Command &command, Executor &executor) {
   storeNumber(executor, command, executor.returnRegisters.size(), false, "return-reg-size");
}

void builtinReturnRegAt(const Command &command, Executor &executor) {
   pilint_t id = getNum(executor, command, 0, "return-reg-at");
   if (id < 0 || (size_t)id >= executor.returnRegisters.size()) {
      error(executor.diagnostics, command.file, command.line, "return-reg-at: Index %ld is out of bounds", id);
      return;
   }
   storeInRegister(executor, command, executor.returnRegisters[id], "return-reg-at");
}

void builtinReturnRegSet(const Command &command, Executor &executor) {
   pilint_t id = getNum(executor, command, 0, "return-reg-set");
   if (id < 0 || (size_t)id >= executor.returnRegisters.size()) {
      error(executor.diagnostics, command.file, command.line, "return-reg-set: Index %ld is out of bounds", id);
      return;
   }
   Value reg {.type = VALUE_RETURN_REGISTER, .reg = (piluint_t)id};
   storeInRegister(executor, command, reg, resolveVariable(executor, arg(executor, command, 1)), "return-reg-set");
}

void builtinReturnCount(const Command &command, Executor &executor) {
   storeNumber(executor, command, executor.returnCount, false, "return-count");
}

void builtinFuncArity(const Command &command, Executor &executor) {
   Value f = resolveVariable(executor, arg(executor, command, 0));
   if (f.type != VALUE_FUNCTION) {
      error(executor.diagnostics, command.file, command.line, "func-arity: Expected function to call for the 1st argument, got %s instead", getValueName(f.type));
      return;
   }
   storeNumber(executor, command, executor.functions[f.function].paramCount, false, "func-arity");
}

void builtinFuncVariadic(const Command &command, Executor &executor) {
   Value f = resolveVariable(executor, arg(executor, command, 0));
   if (f.type != VALUE_FUNCTION) {
      error(executor.diagnostics, command.file, command.line, "func-variadic: Expected function to call for the 1st argument, got %s instead", getValueName(f.type));
      return;
   }
   storeBoolean(executor, command, executor.functions[f.function].variadic, "func-variadic");
}

void builtinFuncArgMatch(const Command &command, Executor &executor) {
   Value f = resolveVariable(executor, arg(executor, command, 0));
   if (f.type != VALUE_FUNCTION) {
      error(executor.diagnostics, command.file, command.line, "func-arg-match: Expected function to call for the 1st argument, got %s instead", getValueName(f.type));
      return;
   }
   Function &func = executor.functions[f.function];
   piluint_t args = getNum(executor, command, 1, "func-arg-match");
   size_t params = func.paramCount;
   storeBoolean(executor, command, (!func.variadic && args == params) || (func.variadic && args >= params), "func-arg-match");
}

// structures
void builtinMake(const Command &command, Executor &executor) {
   Value structValue = resolveVariable(executor, arg(executor, command, 0));
   if (structValue.type != VALUE_STRUCT) {
      error(executor.diagnostics, command.file, command.line, "make: Expected Structure for the 1st argument, got %s instead", getValueName(structValue.type));
      return;
   }

   Function &structure = executor.functions[structValue.structure];
   if (structure.localCount != command.argCount - 2) {
      error(executor.diagnostics, command.file, command.line, "make: Structure '%s' expected %zu fields, got %zu instead", getLexeme(executor.cache, structure.lexeme).c_str(), structure.localCount, command.argCount);
      return;
   }

   std::vector<Value> values;
   values.reserve(structure.localCount);
   for (size_t i = 2; i < command.argCount; ++i) {
      values.push_back(resolveVariable(executor, arg(executor, command, i)));
   }

   size_t array = allocateArray(executor, values);
   executor.arrays[array].structureId = structure.position;
   storeInRegister(executor, command, arg(executor, command, 1), Value{.type = VALUE_ARRAY, .array = array}, "make");
}

void builtinFree(const Command &command, Executor &executor) {
   for (size_t i = 0; i < command.argCount; ++i) {
      Value a = arg(executor, command, i);
      Value &value = resolveVariableByRef(executor, a);
      if (value.type == VALUE_STRING) {
         executor.strings.erase(value.string);
      }
      else if (value.type == VALUE_ARRAY) {
         executor.arrays.erase(value.array);
      }
      else if (value.type == VALUE_MAP) {
         executor.maps.erase(value.map);
      }
      else {
         error(executor.diagnostics, command.file, command.line, "free: Expected String/Array/Map/Structure, got %s instead", getValueName(value.type));
         continue;
      }
      value = NULL_VALUE;
   }
}

void builtinDeepFree(const Command &command, Executor &executor) {
   std::set<std::pair<size_t, ValueType>> visited;
   for (size_t i = 0; i < command.argCount; ++i) {
      Value a = arg(executor, command, i);
      Value &value = resolveVariableByRef(executor, a);
      if (value.type == VALUE_STRING) {
         executor.strings.erase(value.string);
      }
      else if (value.type == VALUE_ARRAY || value.type == VALUE_MAP) {
         visited.clear();
         deepFree(command, executor, value, visited);
      }
      else {
         error(executor.diagnostics, command.file, command.line, "deep-free: Expected String/Array/Map/Structure, got %s instead", getValueName(value.type));
         continue;
      }
      value = NULL_VALUE;
   }
}

void builtinCopy(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   if (value.type == VALUE_STRING) {
      const std::string *string;
      if (!constStringOrError(command, executor, "copy", string)) return;
      storeString(executor, command, *string, back(executor, command), "copy");
   }
   else if (value.type == VALUE_ARRAY) {
      std::vector<Value> *array;
      if (!arrayOrError(command, executor, "copy", array, 0, true)) return;
      storeArray(executor, command, *array, back(executor, command), "copy");
   }
   else if (value.type == VALUE_MAP) {
      PILMap *map;
      if (!mapOrError(command, executor, "copy", map)) return;
      storeMap(executor, command, *map, back(executor, command), "copy");
   }
   else {
      storeInRegister(executor, command, value, "copy");
   }
}

void builtinDeepCopy(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   if (value.type == VALUE_STRING) {
      const std::string *string;
      if (!constStringOrError(command, executor, "copy", string)) return;
      storeString(executor, command, *string, back(executor, command), "copy");
   }
   else if (value.type == VALUE_ARRAY) {
      if (auto it = executor.arrays.find(value.array); it == executor.arrays.end()) {
         error(executor.diagnostics, command.file, command.line, "Invalid Array ID %zu. Use after free", value.array);
         return;
      }
      std::map<std::pair<size_t, ValueType>, size_t> copied;
      Value result {VALUE_ARRAY};
      result.array = deepCopy(command, executor, value.array, value.type, copied);
      storeInRegister(executor, command, back(executor, command), result, "deep-copy");
   }
   else if (value.type == VALUE_MAP) {
      if (auto it = executor.maps.find(value.map); it == executor.maps.end()) {
         error(executor.diagnostics, command.file, command.line, "Invalid Map ID %zu. Use after free", value.map);
         return;
      }
      std::map<std::pair<size_t, ValueType>, size_t> copied;
      Value result {VALUE_MAP};
      result.map = deepCopy(command, executor, value.map, value.type, copied);
      storeInRegister(executor, command, back(executor, command), result, "deep-copy");
   }
   else {
      storeInRegister(executor, command, value, "copy");
   }
}

void builtinSize(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   if (value.type == VALUE_STRING) {
      const std::string *string;
      if (!constStringOrError(command, executor, "size", string)) return;
      storeNumber(executor, command, string->size(), false, "size");
   }
   else if (value.type == VALUE_ARRAY) {
      std::vector<Value> *array;
      if (!arrayOrError(command, executor, "size", array, 0, true)) return;
      storeNumber(executor, command, array->size(), false, "size");
   }
   else if (value.type == VALUE_MAP) {
      PILMap *map;
      if (!mapOrError(command, executor, "size", map)) return;
      storeNumber(executor, command, map->size(), false, "size");
   }
   else {
      storeNumber(executor, command, 1, false, "size");
   }
}

void builtinAt(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   if (value.type == VALUE_STRING) {
      const std::string *string;
      if (!constStringOrError(command, executor, "at", string)) return;
      pilint_t id = getNum(executor, command, 1, "at");
      if (id < 0 || (size_t)id >= string->size()) {
         error(executor.diagnostics, command.file, command.line, "at: Index %zu for String is out of bounds", id);
         return;
      }
      Value value {VALUE_CHARACTER};
      value.character = (*string)[id];
      storeInRegister(executor, command, value, "at");
   }
   else if (value.type == VALUE_ARRAY) {
      std::vector<Value> *array;
      if (!arrayOrError(command, executor, "at", array, 0, true)) return;
      pilint_t id = getNum(executor, command, 1, "at");
      if (id < 0 || (size_t)id >= array->size()) {
         error(executor.diagnostics, command.file, command.line, "at: Index %zu for Array is out of bounds", id);
         return;
      }
      storeInRegister(executor, command, (*array)[id], "at");
   }
   else if (value.type == VALUE_MAP) {
      PILMap *map;
      if (!mapOrError(command, executor, "at", map)) return;
      Value key = resolveVariable(executor, arg(executor, command, 1));
      if (key.type == VALUE_ARRAY || key.type == VALUE_MAP || key.type == VALUE_FILE) {
         error(executor.diagnostics, command.file, command.line, "at: %s cannot be used as a key in a Map", getValueName(key.type));
         return;
      }
      Value val = NULL_VALUE;
      if (auto it = map->find(key); it != map->end()) {
         val = it->second;
      }
      storeInRegister(executor, command, val, "at");
   }
   else {
      error(executor.diagnostics, command.file, command.line, "at: %s is not indexable", getValueName(value.type));
   }
}

void builtinStore(const Command &command, Executor &executor) {
   Value value = resolveVariable(executor, arg(executor, command, 0));
   if (value.type == VALUE_STRING) {
      std::string *string;
      if (!stringOrError(command, executor, "store", string)) return;
      pilint_t id = getNum(executor, command, 1, "store");
      if (id < 0 || (size_t)id >= string->size()) {
         error(executor.diagnostics, command.file, command.line, "store: Index %zu for String is out of bounds", id);
         return;
      }
      (*string)[id] = getChar(command, executor, "string-set", 2);
   }
   else if (value.type == VALUE_ARRAY) {
      std::vector<Value> *array;
      if (!arrayOrError(command, executor, "store", array, 0, true)) return;
      pilint_t id = getNum(executor, command, 1, "store");
      if (id < 0 || (size_t)id >= array->size()) {
         error(executor.diagnostics, command.file, command.line, "store: Index %zu for Array is out of bounds", id);
         return;
      }
      (*array)[id] = resolveVariable(executor, arg(executor, command, 2));
   }
   else if (value.type == VALUE_MAP) {
      PILMap *map;
      if (!mapOrError(command, executor, "store", map)) return;
      Value key = resolveVariable(executor, arg(executor, command, 1));
      if (key.type == VALUE_ARRAY || key.type == VALUE_MAP || key.type == VALUE_FILE) {
         error(executor.diagnostics, command.file, command.line, "store: %s cannot be used as a key in a Map", getValueName(key.type));
         return;
      }
      (*map)[key] = resolveVariable(executor, arg(executor, command, 2));
   }
   else {
      error(executor.diagnostics, command.file, command.line, "store: %s is not indexable", getValueName(value.type));
   }
}
