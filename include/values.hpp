#pragma once
#include <cstdint>

typedef void (*NativeFunction)(const struct Command&, struct Executor&);
typedef int64_t pilint_t;
typedef uint64_t piluint_t;
typedef double pilfloat_t;

enum ValueType: char {
   VALUE_INTEGER, VALUE_FLOATING, VALUE_CHARACTER, VALUE_CSTRING, VALUE_STRING, VALUE_FUNCTION, VALUE_LABEL,
   VALUE_LOCAL, VALUE_REGISTER, VALUE_RETURN_REGISTER, VALUE_ARRAY, VALUE_MAP, VALUE_COUNT
};

constexpr const char *valueTypeStrings[VALUE_COUNT + 1] = {
   "Integer", "Floating", "Character", "Constant String", "String", "Function", "Label", "Local Variable",
   "Register", "Return Register", "Array", "Map", "Invalid Value"
};

constexpr const char *getValueName(ValueType value) {
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
      size_t reg; // reused for registers and return registers
      size_t function;
      size_t label;
   };
};

struct Function {
   bool native = false;
   bool variadic = false;
   bool isLabel = false;
   size_t position;
   size_t lexeme;
   size_t localCount;
   size_t nativeFn;
   size_t paramCount;
};

constexpr Value NULL_VALUE = {VALUE_COUNT};
