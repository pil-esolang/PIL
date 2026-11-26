#pragma once
#include <cstddef>
#include <cstdint>

typedef void (*NativeFunction)(const struct Command&, struct Executor&);
typedef int64_t pilint_t;
typedef uint64_t piluint_t;
typedef double pilfloat_t;

enum ValueType: char {
   VALUE_INTEGER, VALUE_FLOATING, VALUE_CHARACTER, VALUE_CSTRING, VALUE_STRING, VALUE_FUNCTION,
   VALUE_LABEL, VALUE_LOCAL, VALUE_ARRAY, VALUE_MAP, VALUE_FILE, VALUE_STRUCT, VALUE_STRUCT_ACCESS,
   VALUE_GLOBAL, VALUE_COUNT
};

static constexpr const char *valueTypeStrings[VALUE_COUNT + 1] = {
   "Integer", "Floating", "Character", "Constant String", "String", "Function", "Label",
   "Local Variable", "Array", "Map", "File", "Structure", "Structure Access",
   "Global Variable", "Invalid Value"
};

static constexpr const char *getValueName(ValueType value) {
   if (value < 0 || value >= VALUE_COUNT) {
      return valueTypeStrings[VALUE_COUNT];
   }
   return valueTypeStrings[value];
}

// please keep it 16 bytes. this heavily affects performance. 7 bytes are free after type.
struct Value {
   ValueType type;
   union {
      pilint_t integer;
      pilfloat_t floating;
      char character;
      size_t string; // reused for strings and cstrings
      size_t array;
      size_t map;
      size_t identifier;
      size_t local;
      size_t function;
      size_t label;
      size_t handle;
      size_t structure;
      size_t chain;
      size_t global;
   };
};

struct Function {
   bool native = false;
   bool variadic = false;
   bool isLabel = false;
   bool isStructureArray = false;
   size_t position = 0;
   size_t lexeme = 0;
   size_t localCount = 0;
   size_t nativeFn = 0;
   size_t paramCount = 0;
};

static constexpr Value NULL_VALUE = {VALUE_COUNT};

enum BuiltinFlags {
   VARIADIC = 1 << 0,
   RESERVED = 1 << 1,
   FILE_LOCK = 1 << 2,
   EXEC_LOCK = 1 << 3,
   ENV_LOCK = 1 << 4,
};

struct BuiltinDef {
   const char *name;
   NativeFunction fn;
   size_t params;
   int flags;
};
