#pragma once
#include "pil.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <random>
#include <unordered_set>

// types
inline bool isValueString(Value value) {
   return value.type == VALUE_STRING || value.type == VALUE_CSTRING;
}

inline bool isValueNumber(Value value) {
   return value.type == VALUE_INTEGER || value.type == VALUE_FLOATING;
}

inline bool isValueContainer(Value value) {
   return value.type == VALUE_LOCAL || value.type == VALUE_GLOBAL || value.type == VALUE_STRUCT_ACCESS;
}

inline bool isValueNonComparable(Value value) {
   return value.type == VALUE_MAP || value.type == VALUE_FILE;
}

inline bool isValueNonHashable(Value value) {
   return value.type == VALUE_MAP || value.type == VALUE_FILE || value.type == VALUE_ARRAY;
}

// generic helpers
inline Value &resolveVariableByRef(Executor &executor, Value &value) {
   if (value.type == VALUE_LOCAL) {
      return executor.locals[executor.stackTrace.top().localStart + value.local];
   }
   else if (value.type == VALUE_GLOBAL) {
      return executor.globals[value.global];
   }
   else if (value.type == VALUE_STRUCT_ACCESS) {
      StructChainLink &link = executor.structChains[value.chain];
      Value *v;
      if (link.parentIsChain) {
         Value parent {.type = VALUE_STRUCT_ACCESS, .chain = link.parent};
         v = &resolveVariableByRef(executor, parent);
      }
      else {
         v = &resolveVariableByRef(executor, executor.locals[executor.stackTrace.top().localStart + link.parent]);
      }

      if (v->type != VALUE_ARRAY) {
         error(executor.diagnostics, executor.code[executor.pointer].file, executor.code[executor.pointer].line, "Tried to access field of non-struct type. Expected Array, got %s instead", getValueName(v->type));
         return *v;
      }
      auto it = executor.arrays.find(v->array);
      if (it == executor.arrays.end()) {
         error(executor.diagnostics, executor.code[executor.pointer].file, executor.code[executor.pointer].line, "Structure use after free");
         return *v;
      }

      if (it->second.structureId == 0) {
         error(executor.diagnostics, executor.code[executor.pointer].file, executor.code[executor.pointer].line, "Tried to access regular Array instead of structure Array using dot operator");
         return *v;
      }

      const Function &structure = executor.functions[it->second.structureId];
      for (size_t i = structure.nativeFn; i < structure.nativeFn + structure.paramCount; ++i) {
         const StructData &data = executor.structData[i];
         if (link.field == data.lexeme && it->second.structureId == data.structId) {
            return it->second.array[data.physicalId];
         }
      }
      error(executor.diagnostics, executor.code[executor.pointer].file, executor.code[executor.pointer].line, "No such member '%s' for structure '%s'", getLexeme(executor.cache, link.field).c_str(), getLexeme(executor.cache, executor.functions[it->second.structureId].lexeme).c_str());
      return *v;
   }
   else {
      return value;
   }
}

inline Value resolveVariable(Executor &executor, Value value) {
   return resolveVariableByRef(executor, value);
}

inline Value arg(const Executor &executor, const Command &command, size_t i) {
   return executor.arguments[command.argStart + i];
}

inline Value back(const Executor &executor, const Command &command) {
   return executor.arguments[command.argStart + command.argCount - 1];
}

inline Value &resolveArgByRef(Executor &executor, const Command &command, size_t i) {
   return resolveVariableByRef(executor, executor.arguments[command.argStart + i]);
}

inline Value resolveArg(Executor &executor, const Command &command, size_t i) {
   return resolveVariable(executor, executor.arguments[command.argStart + i]);
}

inline Value resolveBack(Executor &executor, const Command &command) {
   return resolveVariable(executor, executor.arguments[command.argStart + command.argCount - 1]);
}

inline void storeInRegister(Executor &executor, const Command &command, Value reg, Value value, const char *function) {
   if (reg.type == VALUE_LOCAL) {
      executor.locals[executor.stackTrace.top().localStart + reg.local] = value;
   }
   else if (reg.type == VALUE_GLOBAL) {
      executor.globals[reg.global] = value;
   }
   else if (reg.type == VALUE_STRUCT_ACCESS) {
      StructChainLink &link = executor.structChains[reg.chain];
      Value *v;
      if (link.parentIsChain) {
         Value parent {.type = VALUE_STRUCT_ACCESS, .chain = link.parent};
         v = &resolveVariableByRef(executor, parent);
      }
      else {
         v = &resolveVariableByRef(executor, executor.locals[executor.stackTrace.top().localStart + link.parent]);
      }

      if (v->type != VALUE_ARRAY) {
         error(executor.diagnostics, executor.code[executor.pointer].file, executor.code[executor.pointer].line, "Tried to access field of non-struct type. Expected Array, got %s instead", getValueName(v->type));
         return;
      }
      auto it = executor.arrays.find(v->array);
      if (it == executor.arrays.end()) {
         error(executor.diagnostics, executor.code[executor.pointer].file, executor.code[executor.pointer].line, "Structure use after free");
         return;
      }

      if (it->second.structureId == 0) {
         error(executor.diagnostics, executor.code[executor.pointer].file, executor.code[executor.pointer].line, "Tried to access regular Array instead of structure Array using dot operator");
         return;
      }

      const Function &structure = executor.functions[it->second.structureId];
      for (size_t i = structure.nativeFn; i < structure.nativeFn + structure.paramCount; ++i) {
         const StructData &data = executor.structData[i];
         if (link.field == data.lexeme && it->second.structureId == data.structId) {
            it->second.array[data.physicalId] = value;
            return;
         }
      }
      error(executor.diagnostics, executor.code[executor.pointer].file, executor.code[executor.pointer].line, "No such member '%s' for structure '%s'", getLexeme(executor.cache, link.field).c_str(), getLexeme(executor.cache, executor.functions[it->second.structureId].lexeme).c_str());
   }
   else {
      error(executor.diagnostics, command.file, command.line, "%s: Expected Register/Variable for the destination argument, got %s instead", function, getValueName(reg.type));
   }
}

inline void storeInRegister(Executor &executor, const Command &command, Value value, const char *function) {
   storeInRegister(executor, command, back(executor, command), value, function);
}

// getters
inline pilfloat_t getNum(Executor &executor, const Command &command, size_t i, const char *function, bool *floating = nullptr) {
   Value value = resolveArg(executor, command, i);
   if (!isValueNumber(value)) {
      error(executor.diagnostics, command.file, command.line, "%s: Expected Number, got %s instead", function, getValueName(value.type));
      return 0.0;
   }
   if (floating && value.type == VALUE_FLOATING) *floating = true;
   return (value.type == VALUE_INTEGER ? (pilfloat_t)value.integer : value.floating);
}

inline bool isTruthy(Executor &executor, const Command &command, Value v) {
   switch (v.type) {
   case VALUE_INTEGER: return v.integer != 0;
   case VALUE_FLOATING: return v.floating != 0.0;
   case VALUE_CHARACTER: return v.character != 0;
   case VALUE_CSTRING: return !getLexeme(executor.cache, v.string).empty();
   case VALUE_STRING: return !getString(executor, v.string, command.file, command.line).empty();
   case VALUE_ARRAY: return !getArray(executor, v.array, command.file, command.line).array.empty();
   case VALUE_MAP: return !getMap(executor, v.map, command.file, command.line).empty();
   case VALUE_FILE: return true;
   case VALUE_FUNCTION: return true;
   case VALUE_LABEL: return true;
   case VALUE_STRUCT: return true;
   case VALUE_COUNT: return false;
   default: // should not happen
      printf("PIL::isTruthy: Value %s cannot be checked for truthiness.\n", getValueName(v.type));
      exit(EXIT_FAILURE);
   }
}

inline bool getBool(Executor &executor, const Command &command, size_t i) {
   return isTruthy(executor, command, resolveArg(executor, command, i));
}

inline char getChar(const Command &command, Executor &executor, const char *function, size_t i) {
   Value value = resolveArg(executor, command, i);
   if (value.type != VALUE_CHARACTER) {
      error(executor.diagnostics, command.file, command.line, "%s: Expected Character, got %s instead", function, getValueName(value.type));
      return char{};
   }
   return value.character;
}

inline bool stringOrError(const Command &command, Executor &executor, const char *function, std::string *&out, size_t i = 0) {
   Value string = resolveArg(executor, command, i);
   if (string.type != VALUE_STRING) {
      error(executor.diagnostics, command.file, command.line, "%s: Expected String, got %s instead", function, getValueName(string.type));
      return false;
   }
   if (auto it = executor.strings.find(string.string); it != executor.strings.end()) {
      out = &it->second;
      return true;
   }
   error(executor.diagnostics, command.file, command.line, "Invalid String ID %zu. Use after free", string.string);
   return false;
}

inline bool constStringOrError(const Command &command, Executor &executor, const char *function, const std::string *&stringOut, size_t i = 0) {
   Value value = resolveArg(executor, command, i);
   if (value.type == VALUE_CSTRING) {
      stringOut = &getLexeme(executor.cache, value.string);
      return true;
   }

   if (value.type != VALUE_STRING) {
      error(executor.diagnostics, command.file, command.line, "%s: Expected String or Character, got %s instead", function, getValueName(value.type));
      return false;
   }

   if (auto it = executor.strings.find(value.string); it != executor.strings.end()) {
      stringOut = &it->second;
      return true;
   }
   error(executor.diagnostics, command.file, command.line, "Invalid String ID %zu. Use after free", value.string);
   return false;
}

inline bool constStringOrCharOrError(const Command &command, Executor &executor, const char *function, const std::string *&stringOut, const char *&charOut, size_t i = 0) {
   Value &value = resolveVariableByRef(executor, executor.arguments[command.argStart + i]); // need that ref here for charOut
   if (value.type == VALUE_CHARACTER) {
      charOut = &value.character;
      return true;
   }

   if (value.type == VALUE_CSTRING) {
      stringOut = &getLexeme(executor.cache, value.string);
      return true;
   }

   if (value.type != VALUE_STRING) {
      error(executor.diagnostics, command.file, command.line, "%s: Expected String or Character, got %s instead", function, getValueName(value.type));
      return false;
   }

   if (auto it = executor.strings.find(value.string); it != executor.strings.end()) {
      stringOut = &it->second;
      return true;
   }
   error(executor.diagnostics, command.file, command.line, "Invalid String ID %zu. Use after free", value.string);
   return false;
}

inline bool arrayOrError(const Command &command, Executor &executor, const char *function, std::vector<Value> *&out, size_t i = 0, bool allowStructs = false) {
   Value array = resolveArg(executor, command, i);
   if (array.type != VALUE_ARRAY) {
      error(executor.diagnostics, command.file, command.line, "%s: Expected Array, got %s instead", function, getValueName(array.type));
      return false;
   }
   if (auto it = executor.arrays.find(array.array); it != executor.arrays.end()) {
      if (!allowStructs && it->second.structureId != 0 && !executor.functions[it->second.structureId].isStructureArray) {
         error(executor.diagnostics, command.file, command.line, "%s: Expected regular Array, got structure Array instead. If this is intended, define structure '%s' as 'struct array'", function, getLexeme(executor.cache, executor.functions[it->second.structureId].lexeme).c_str());
         return false;
      }
      
      out = &it->second.array;
      return true;
   }
   error(executor.diagnostics, command.file, command.line, "Invalid Array ID %zu. Use after free", array.array);
   return false;
}

inline bool mapOrError(const Command &command, Executor &executor, const char *function, PILMap *&out, size_t i = 0) {
   Value map = resolveArg(executor, command, i);
   if (map.type != VALUE_MAP) {
      error(executor.diagnostics, command.file, command.line, "%s: Expected Map, got %s instead", function, getValueName(map.type));
      return false;
   }
   if (auto it = executor.maps.find(map.map); it != executor.maps.end()) {
      out = &it->second;
      return true;
   }
   error(executor.diagnostics, command.file, command.line, "Invalid Map ID %zu. Use after free", map.map);
   return false;
}

inline bool fileHandleOrError(const Command &command, Executor &executor, const char *function, std::fstream *&out, size_t i = 0) {
   Value handle = resolveArg(executor, command, i);
   if (handle.type != VALUE_FILE) {
      error(executor.diagnostics, command.file, command.line, "%s: Expected File, got %s instead", function, getValueName(handle.type));
      return false;
   }
   if (auto it = executor.files.find(handle.handle); it != executor.files.end()) {
      out = &it->second;
      return true;
   }
   error(executor.diagnostics, command.file, command.line, "Invalid File ID %zu. Use after free", handle.handle);
   return false;
}

// setters
inline void storeNumber(Executor &executor, const Command &command, pilfloat_t number, bool floating, const char *function) {
   Value value {floating ? VALUE_FLOATING : VALUE_INTEGER};
   if (floating) value.floating = number;
   else value.integer = number;
   storeInRegister(executor, command, value, function);
}

inline void storeBoolean(Executor &executor, const Command &command, bool result, const char *function) {
   Value value {.type = VALUE_INTEGER, .integer = (result ? 1 : 0)};
   storeInRegister(executor, command, value, function);
}

inline void storeBoolean(Executor &executor, const Command &command, Value &reg, bool result, const char *function) {
   Value value {.type = VALUE_INTEGER, .integer = (result ? 1 : 0)};
   storeInRegister(executor, command, reg, value, function);
}

inline void storeString(Executor &executor, const Command &command, const std::string &string, Value reg, const char *function) {
   Value value {.type = VALUE_STRING, .string = allocateString(executor, string)};
   storeInRegister(executor, command, reg, value, function);
}

inline void storeArray(Executor &executor, const Command &command, const std::vector<Value> &array, Value reg, const char *function) {
   Value value {.type = VALUE_ARRAY, .array = allocateArray(executor, array)};
   storeInRegister(executor, command, reg, value, function);
}

inline void storeMap(Executor &executor, const Command &command, const PILMap &map, Value reg, const char *function) {
   Value value {.type = VALUE_MAP, .map = allocateMap(executor, map)};
   storeInRegister(executor, command, reg, value, function);
}

// comparison
inline bool arraysEqual(Executor &executor, Value arr1, Value arr2, size_t file, size_t line, const char *function, std::set<std::pair<size_t, size_t>> &active) {
   auto key = std::minmax(arr1.array, arr2.array);
   if (!active.insert(key).second) return true;

   const std::vector<Value> &array1 = getArray(executor, arr1.array, file, line).array;
   const std::vector<Value> &array2 = getArray(executor, arr2.array, file, line).array;
   if (array1.size() != array2.size()) return false;

   for (size_t i = 0; i < array1.size(); ++i) {
      Value v1 = array1[i];
      Value v2 = array2[i];
      if (isValueString(v1) && isValueString(v2) && getString(executor, v1, file, line) != getString(executor, v2, file, line)) {
         return false;
      }
      else if (isValueNonComparable(v1) || isValueNonComparable(v2)) {
         error(executor.diagnostics, file, line, "%s: Cannot compare %s to %s", function, getValueName(v1.type), getValueName(v2.type));
         return false;
      }
      else if ((v1.type != v2.type) || (v1.type == VALUE_INTEGER && v1.integer != v2.integer) || (v1.type == VALUE_FLOATING && v1.floating != v2.floating)
            || (v1.type == VALUE_CHARACTER && v1.character != v2.character) || (v1.type == VALUE_FUNCTION && v1.function != v2.function)
            || (v1.type == VALUE_LABEL && v1.label != v2.label) || (v1.type == VALUE_STRUCT && v1.structure != v2.structure)
            || (v1.type == VALUE_ARRAY && !arraysEqual(executor, v1, v2, file, line, function, active))) {
         return false;
      }
   }
   return true;
}

enum Comparison: char {
   COMPARISON_LESS, COMPARISON_GREATER, COMPARISON_EQUAL, COMPARISON_NOT_EQUAL
};

inline Comparison compareTwoValues(Executor &executor, Value a, Value b, size_t file, size_t line, bool softie, const char *function) {
   if (isValueNumber(a) && isValueNumber(b)) {
      pilfloat_t x = (a.type == VALUE_INTEGER) ? (pilfloat_t)a.integer : a.floating;
      pilfloat_t y = (b.type == VALUE_INTEGER) ? (pilfloat_t)b.integer : b.floating;
      return (x < y ? COMPARISON_LESS : x > y ? COMPARISON_GREATER : COMPARISON_EQUAL);
   }
   else if (a.type == VALUE_CHARACTER && b.type == VALUE_CHARACTER) {
      return (a.character < b.character ? COMPARISON_LESS : a.character > b.character ? COMPARISON_GREATER : COMPARISON_EQUAL);
   }
   else if (isValueString(a) && isValueString(b)) {
      int c = getString(executor, a, file, line).compare(getString(executor, b, file, line));
      return (c < 0 ? COMPARISON_LESS : c > 0 ? COMPARISON_GREATER : COMPARISON_EQUAL);
   }
   // only check equality for following types
   else if (a.type == VALUE_ARRAY && b.type == VALUE_ARRAY && softie) {
      std::set<std::pair<size_t, size_t>> active;
      return (arraysEqual(executor, a, b, file, line, function, active) ? COMPARISON_EQUAL : COMPARISON_NOT_EQUAL);
   }
   else if (a.type == VALUE_COUNT && b.type == VALUE_COUNT && softie) {
      return COMPARISON_EQUAL;
   }
   else if ((a.type == VALUE_COUNT || b.type == VALUE_COUNT) && softie) {
      return COMPARISON_NOT_EQUAL;
   }
   else if (a.type == VALUE_FUNCTION && b.type == VALUE_FUNCTION && softie) {
      return (a.function == b.function ? COMPARISON_EQUAL : COMPARISON_NOT_EQUAL);
   }
   else if (a.type == VALUE_LABEL && b.type == VALUE_LABEL && softie) {
      return (a.label == b.label ? COMPARISON_EQUAL : COMPARISON_NOT_EQUAL);
   }
   else if (a.type == VALUE_STRUCT && b.type == VALUE_STRUCT && softie) {
      return (a.structure == b.structure ? COMPARISON_EQUAL : COMPARISON_NOT_EQUAL);
   }
   else if (!softie) {
      error(executor.diagnostics, file, line, "%s: Cannot compare %s to %s", function, getValueName(a.type), getValueName(b.type));
      return COMPARISON_NOT_EQUAL;
   }
   return COMPARISON_NOT_EQUAL;
}

inline void comparisonBuiltin(Executor &executor, const Command &command, const char *function, Comparison expected, bool reverse, bool softie) {
   Comparison result = compareTwoValues(executor, resolveArg(executor, command, 0), resolveArg(executor, command, 1), command.file, command.line, softie, function);
   storeBoolean(executor, command, (result == expected) != reverse, function);
}

inline bool valuesEqual(Executor &executor, const Command &command, Value a, Value b, const char *function) {
   if (isValueNumber(a) && isValueNumber(b)) {
      pilfloat_t x = (a.type == VALUE_INTEGER) ? (pilfloat_t)a.integer : a.floating;
      pilfloat_t y = (b.type == VALUE_INTEGER) ? (pilfloat_t)b.integer : b.floating;
      return x == y;
   }
   else if (a.type == VALUE_CHARACTER && b.type == VALUE_CHARACTER) {
      return a.character == b.character;
   }
   else if (isValueString(a) && isValueString(b)) {
      return getString(executor, a, command.file, command.line) == getString(executor, b, command.file, command.line);
   }
   else if (a.type == VALUE_ARRAY && b.type == VALUE_ARRAY) {
      std::set<std::pair<size_t, size_t>> active;
      return arraysEqual(executor, a, b, command.file, command.line, function, active);
   }
   else if (a.type == VALUE_FUNCTION && b.type == VALUE_FUNCTION) {
      return a.function == b.function;
   }
   else if (a.type == VALUE_LABEL && b.type == VALUE_LABEL) {
      return a.label == b.label;
   }
   else if (a.type == VALUE_STRUCT && b.type == VALUE_STRUCT) {
      return a.structure == b.structure;
   }
   else if (isValueNonComparable(a) || isValueNonComparable(b)) {
      error(executor.diagnostics, command.file, command.line, "%s: Cannot compare %s to %s", function, getValueName(a.type), getValueName(b.type));
      return false;
   }
   return a.type == VALUE_COUNT && b.type == VALUE_COUNT;
}

// domain specific helpers
inline bool validateCallbackFunction(Executor &executor, const Command &command, size_t i, const char *function, size_t argCount, Function *&f) {
   Value value = resolveArg(executor, command, i);
   if (value.type != VALUE_FUNCTION) {
      error(executor.diagnostics, command.file, command.line, "%s: Expected Function, got %s instead", function, getValueName(value.type));
      return false;
   }

   f = &executor.functions[value.function];
   if (f->variadic || f->paramCount != argCount) {
      error(executor.diagnostics, command.file, command.line, "%s: Expected input function to take in exactly %zu parameters and to not be variadic", function, argCount);
      return false;
   }
   return true;
}

inline void jumpToLabel(Executor &executor, Value value, const char *function, size_t file, size_t line, bool condition) {
   if (value.type != VALUE_LABEL) {
      error(executor.diagnostics, file, line, "%s: Expected Label to jump to, got %s instead", function, getValueName(value.type));
      return;
   }
   if (condition) {
      executor.pointer = executor.functions[value.label].position - 1;
   }
}

inline std::string toStringParseTime(Executor &executor, Value value) {
   switch (value.type) {
   case VALUE_INTEGER: return std::to_string(value.integer);
   case VALUE_FLOATING: return std::to_string(value.floating);
   case VALUE_CHARACTER: return std::string(1, value.character);
   case VALUE_CSTRING: return getLexeme(executor.cache, value.string);
   default: return "(null)";
   }
}

inline std::string toStringImpl(Executor &executor, Value value, const char *function, size_t file, size_t line, std::unordered_set<size_t> &active) {
   value = resolveVariable(executor, value);
   switch (value.type) {
   case VALUE_INTEGER: return std::to_string(value.integer);
   case VALUE_FLOATING: return std::to_string(value.floating);
   case VALUE_CHARACTER: return std::string(1, value.character);
   case VALUE_CSTRING: return getLexeme(executor.cache, value.string);
   case VALUE_STRING: return getString(executor, value.string, file, line);
   case VALUE_FUNCTION: return getLexeme(executor.cache, executor.functions[value.function].lexeme);
   case VALUE_LABEL: return getLexeme(executor.cache, executor.functions[value.label].lexeme);
   case VALUE_STRUCT: return getLexeme(executor.cache, executor.functions[value.structure].lexeme);
   case VALUE_ARRAY: {
      if (!active.insert(value.array).second) {
         return "...";
      }
      std::string result;
      std::vector<Value> &array = getArray(executor, value.array, file, line).array;
      size_t size = array.size();
      result.reserve(2 + 4 * array.size());
      result += '[';
      for (size_t i = 0; i < size; ++i) {
         result += toStringImpl(executor, array[i], function, file, line, active);
         if (i + 1 < size) result += ',';
      }
      result += ']';
      active.erase(value.array);
      return result;
   }
   case VALUE_MAP: case VALUE_FILE:
      error(executor.diagnostics, file, line, "%s: Cannot convert %s to String", getValueName(value.type), function);
      return "(null)";
   default: return "(null)";
   }
}

inline std::string toString(Executor &executor, Value value, const char *function, size_t file, size_t line) {
   std::unordered_set<size_t> active;
   return toStringImpl(executor, value, function, file, line, active);
}

inline std::string format(const Command &command, Executor &executor, const char *function, size_t offset) {
   Value string = resolveArg(executor, command, offset);
   if (!isValueString(string)) {
      error(executor.diagnostics, command.file, command.line, "%s: Expected String for the 1st argument, got %s instead", function, getValueName(string.type));
      return "";
   }
   std::string result = string.type == VALUE_CSTRING ? getLexeme(executor.cache, string.string) : getString(executor, string.string, command.file, command.line);
   size_t pos = 0;

   for (size_t i = offset + 1; i < command.argCount; ++i) {
      pos = result.find("{}", pos);
      result = (pos != std::string::npos ? result.replace(pos, 2, toString(executor, arg(executor, command, i), function, command.file, command.line)) : result);
   }
   return result;
}

inline void printValue(Executor &executor, Value a, size_t file, size_t line, std::set<std::pair<size_t, ValueType>> &active) {
   switch (a.type) {
   case VALUE_INTEGER: printf(LONG_FMT, a.integer); break;
   case VALUE_FLOATING: printf("%.3F", a.floating); break;
   case VALUE_CHARACTER: printf("%c", a.character); break;
   case VALUE_CSTRING: printf("%s", getLexeme(executor.cache, a.string).c_str()); break;
   case VALUE_STRING: printf("%s", getString(executor, a.string, file, line).c_str()); break;
   case VALUE_FUNCTION: printf("%s", getLexeme(executor.cache, executor.functions[a.function].lexeme).c_str()); break;
   case VALUE_LABEL: printf("%s", getLexeme(executor.cache, executor.functions[a.label].lexeme).c_str()); break;
   case VALUE_STRUCT: printf("%s", getLexeme(executor.cache, executor.functions[a.structure].lexeme).c_str()); break;
   case VALUE_ARRAY: {
      if (!active.insert({a.array, a.type}).second) {
         printf("...");
         break;
      }
      putchar('[');
      std::vector<Value> &array = getArray(executor, a.array, file, line).array;
      size_t size = array.size();
      for (size_t i = 0; i < size; ++i) {
         printValue(executor, array[i], file, line, active);
         if (i + 1 < size) putchar(',');
      }
      putchar(']');
      active.erase({a.array, a.type});
      break;
   }
   case VALUE_MAP: {
      if (!active.insert({a.map, a.type}).second) {
         printf("...");
         break;
      }
      putchar('[');
      PILMap &map = getMap(executor, a.map, file, line);
      for (auto it = map.begin(); it != map.end(); ++it) {
         printValue(executor, it->first, file, line, active);
         putchar(':');
         putchar(' ');
         printValue(executor, it->second, file, line, active);
         if (std::distance(it, map.end()) > 1) putchar(',');
      }
      putchar(']');
      active.erase({a.map, a.type});
      break;
   }
   case VALUE_FILE: printf("(file handle %zu)", a.handle); break;
   default: printf("(null)");
   }
}

inline void print(const Command &command, Executor &executor, const char *function, size_t file, size_t line) {
   std::set<std::pair<size_t, ValueType>> active;
   for (size_t i = 0; i < command.argCount; ++i) {
      Value a = resolveArg(executor, command, i);
      active.clear();
      printValue(executor, a, file, line, active);
   }
}

inline size_t deepCopy(const Command &command, Executor &executor, size_t originalId, ValueType type, std::map<std::pair<size_t, ValueType>, size_t> &copied) {
   if (auto it = copied.find({originalId, type}); it != copied.end()) {
      return it->second;
   }

   auto copyString = [&](Value &v) {
      size_t originalStringId = v.string;
      auto it = copied.find({originalStringId, v.type});
      if (it == copied.end()) {
         v.string = allocateString(executor, getString(executor, v.string, command.file, command.line));
         copied[{originalStringId, v.type}] = v.string;
      }
      else {
         v.string = it->second;
      }
   };

   if (type == VALUE_ARRAY) {
      size_t newId = allocateArray(executor, {});
      copied[{originalId, type}] = newId;

      std::vector<Value> copy = getArray(executor, originalId, command.file, command.line).array;
      for (Value &value: copy) {
         if (value.type == VALUE_ARRAY) {
            value.array = deepCopy(command, executor, value.array, value.type, copied);
         }
         else if (value.type == VALUE_MAP) {
            value.map = deepCopy(command, executor, value.map, value.type, copied);
         }
         else if (value.type == VALUE_STRING) {
            copyString(value);
         }
      }
      getArray(executor, newId, command.file, command.line).array = std::move(copy);
      return newId;
   }
   else {
      size_t newId = allocateMap(executor, {});
      copied[{originalId, type}] = newId;

      PILMap &original = getMap(executor, originalId, command.file, command.line);
      PILMap copy {original.size(), ValueHash{&executor}, ValueEqual{&executor}};

      for (auto &[key, value]: original) {
         Value newValue = value;
         if (newValue.type == VALUE_ARRAY) {
            newValue.array = deepCopy(command, executor, newValue.array, newValue.type, copied);
         }
         else if (newValue.type == VALUE_MAP) {
            newValue.map = deepCopy(command, executor, newValue.map, newValue.type, copied);
         }
         else if (newValue.type == VALUE_STRING) {
            copyString(value);
         }

         Value newKey = key;
         if (key.type == VALUE_STRING) {
            copyString(newKey);
         }
         copy[newKey] = newValue;
      }
      getMap(executor, newId, command.file, command.line) = std::move(copy);
      return newId;
   }
}

inline void deepFree(const Command &command, Executor &executor, Value &value, std::set<std::pair<size_t, ValueType>> &visited) {
   if (value.type == VALUE_MAP) {
      if (!visited.insert({value.map, value.type}).second) return;
      if (auto it = executor.maps.find(value.map); it != executor.maps.end()) {
         PILMap &map = it->second;
         for (auto &[key, value]: map) {
            if (value.type == VALUE_ARRAY || value.type == VALUE_MAP) {
               deepFree(command, executor, value, visited);
            }
            else if (value.type == VALUE_STRING) {
               executor.strings.erase(value.string);
               value = NULL_VALUE;
            }

            if (key.type == VALUE_STRING) {
               executor.strings.erase(key.string);
            }
         }
         executor.maps.erase(it);
         value = NULL_VALUE;
      }
   }
   else if (value.type == VALUE_ARRAY) {
      if (!visited.insert({value.array, value.type}).second) return;
      if (auto it = executor.arrays.find(value.array); it != executor.arrays.end()) {
         std::vector<Value> &arr = it->second.array;
         for (Value &value: arr) {
            if (value.type == VALUE_ARRAY || value.type == VALUE_MAP) {
               deepFree(command, executor, value, visited);
            }
            else if (value.type == VALUE_STRING) {
               executor.strings.erase(value.string);
               value = NULL_VALUE;
            }
         }
         executor.arrays.erase(it);
         value = NULL_VALUE;
      }
   }
}

inline bool hasPermission(const Command &command, Executor &executor, const char *function, int lock) {
   if ((lock & FILE_LOCK) && !executor.allowFileio) {
      error(executor.diagnostics, command.file, command.line, "You don't have permission to run '%s' (no file I/O access)", function);
      return false;
   }

   if ((lock & ENV_LOCK) && !executor.allowEnv) {
      error(executor.diagnostics, command.file, command.line, "You don't have permission to run '%s' (no environment access)", function);
      return false;
   }

   if ((lock & EXEC_LOCK) && !executor.allowExec) {
      error(executor.diagnostics, command.file, command.line, "You don't have permission to run '%s' (no OS access)", function);
      return false;
   }
   return true;
}

inline std::mt19937 &RNG() {
   static std::mt19937 rng {std::random_device{}()};
   return rng;
}

void setEcho(bool on);
char getCanonicalChar();
