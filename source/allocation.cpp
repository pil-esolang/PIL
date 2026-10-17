#include "pil.hpp"

// map keys can be any value except for arrays, maps and file handles
size_t ValueHash::operator () (Value v) const {
   switch (v.type) {
   case VALUE_INTEGER: return std::hash<pilfloat_t>{}((pilfloat_t)v.integer);
   case VALUE_FLOATING: return std::hash<pilfloat_t>{}(v.floating);
   case VALUE_CHARACTER: return std::hash<char>{}(v.character);
   case VALUE_STRING: return std::hash<std::string>{}(getString(*executor, v.string, 0, 0));
   case VALUE_CSTRING: return std::hash<std::string>{}(getLexeme(executor->cache, v.string));
   // we can use the same hash since we know they won't overlap
   case VALUE_FUNCTION: return std::hash<size_t>{}(v.function) ^ 0x9E3779B9;
   case VALUE_LABEL: return std::hash<size_t>{}(v.label) ^ 0x9E3779B9;
   case VALUE_STRUCT: return std::hash<size_t>{}(v.structure) ^ 0x9E3779B9;
   default: return 0;
   }
}

bool ValueEqual::operator () (Value a, Value b) const {
   if ((a.type == VALUE_INTEGER || a.type == VALUE_FLOATING) && (b.type == VALUE_INTEGER || b.type == VALUE_FLOATING)) {
      pilfloat_t x = (a.type == VALUE_INTEGER) ? (pilfloat_t)a.integer : a.floating;
      pilfloat_t y = (b.type == VALUE_INTEGER) ? (pilfloat_t)b.integer : b.floating;
      return x == y;
   }
   else if (a.type == VALUE_CHARACTER && b.type == VALUE_CHARACTER) {
      return a.character == b.character;
   }
   else if ((a.type == VALUE_STRING || a.type == VALUE_CSTRING) && (b.type == VALUE_STRING || b.type == VALUE_CSTRING)) {
      return getString(*executor, a, 0, 0) == getString(*executor, b, 0, 0);
   }
   else if (a.type == VALUE_COUNT && b.type == VALUE_COUNT) {
      return true;
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
   return false;
}

const std::string &getString(Executor &executor, Value stringValue, size_t file, size_t line) {
   if (stringValue.type == VALUE_CSTRING) {
      return getLexeme(executor.cache, stringValue.string);
   }
   else if (stringValue.type == VALUE_STRING) {
      return getString(executor, stringValue.string, file, line);
   }
   printf("PIL::getString: %s is not a string value.\n", getValueName(stringValue.type));
   exit(EXIT_FAILURE);
}

std::string &getString(Executor &executor, size_t ID, size_t file, size_t line) {
   if (auto it = executor.strings.find(ID); it != executor.strings.end()) {
      return it->second;
   }
   error(executor.diagnostics, file, line, "Invalid string ID %zu. Use after free", ID);
   return executor.strings[ID]; // return something. erroring anyway
}

PILArray &getArray(Executor &executor, size_t ID, size_t file, size_t line) {
   if (auto it = executor.arrays.find(ID); it != executor.arrays.end()) {
      return it->second;
   }
   error(executor.diagnostics, file, line, "Invalid array ID %zu. Use after free", ID);
   return executor.arrays[ID];
}

PILMap &getMap(Executor &executor, size_t ID, size_t file, size_t line) {
   if (auto it = executor.maps.find(ID); it != executor.maps.end()) {
      return it->second;
   }
   error(executor.diagnostics, file, line, "Invalid map ID %zu. Use after free", ID);
   return executor.maps[ID];
}

std::fstream &getFile(Executor &executor, size_t ID, size_t file, size_t line) {
   if (auto it = executor.files.find(ID); it != executor.files.end()) {
      return it->second;
   }
   error(executor.diagnostics, file, line, "Invalid file ID %zu. Use after free", ID);
   return executor.files[ID];
}

size_t allocateString(Executor &executor, const std::string &string) {
   executor.stringIdCounter += 1;
   executor.strings[executor.stringIdCounter] = string;
   return executor.stringIdCounter;
}

size_t allocateArray(Executor &executor, const std::vector<Value> &array) {
   executor.arrayIdCounter += 1;
   executor.arrays[executor.arrayIdCounter].array = array;
   return executor.arrayIdCounter;
}

size_t allocateMap(Executor &executor, const std::unordered_map<Value, Value, ValueHash, ValueEqual> &map) {
   executor.mapIdCounter += 1;
   executor.maps[executor.mapIdCounter] = map;
   return executor.mapIdCounter;
}

size_t allocateFile(Executor &executor, std::fstream &&file) {
   executor.fileIdCounter += 1;
   executor.files[executor.fileIdCounter] = std::move(file);
   return executor.fileIdCounter;
}
