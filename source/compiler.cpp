#include "pil.hpp"
#include <fstream>

typedef unsigned short file_string_size_t;

// write cache bytecode
void writeToFile(Executor &executor, const std::string &out) {
   std::ofstream file (out, std::ios::binary);
   if (!file.is_open()) {
      error(executor.diagnostics, 0, 0, "Cannot write to file '%s'", out.c_str());
      return;
   }
   file.write(reinterpret_cast<const char*>(&FILE_VERSION), sizeof(FILE_VERSION));
   file.write(reinterpret_cast<const char*>(&executor.main), sizeof(executor.main));

   size_t lexemeCount = executor.cache.lexemes.size();
   file.write(reinterpret_cast<const char*>(&lexemeCount), sizeof(lexemeCount));

   for (const std::string &lexeme: executor.cache.lexemes) {
      file_string_size_t size = lexeme.size();
      file.write(reinterpret_cast<const char*>(&size), sizeof(size));
      file.write(reinterpret_cast<const char*>(lexeme.data()), size);
   }

   size_t structFieldCount = executor.structFields.size();
   file.write(reinterpret_cast<const char*>(&structFieldCount), sizeof(structFieldCount));
   file.write(reinterpret_cast<const char*>(executor.structFields.data()), sizeof(uint32_t) * structFieldCount);

   size_t structChainCount = executor.structChains.size();
   file.write(reinterpret_cast<const char*>(&structChainCount), sizeof(structChainCount));
   file.write(reinterpret_cast<const char*>(executor.structChains.data()), sizeof(StructChainLink) * structChainCount);

   size_t functionCount = executor.functions.size();
   file.write(reinterpret_cast<const char*>(&functionCount), sizeof(functionCount));
   file.write(reinterpret_cast<const char*>(executor.functions.data()), sizeof(Function) * functionCount);

   size_t argumentCount = executor.arguments.size();
   file.write(reinterpret_cast<const char*>(&argumentCount), sizeof(argumentCount));
   file.write(reinterpret_cast<const char*>(executor.arguments.data()), sizeof(Value) * argumentCount);

   size_t commandCount = executor.code.size();
   file.write(reinterpret_cast<const char*>(&commandCount), sizeof(commandCount));
   file.write(reinterpret_cast<const char*>(executor.code.data()), sizeof(Command) * commandCount);
}

// read cached bytecode
void readCachedBytecode(Executor &executor, const std::string &in) {
   std::ifstream file (in, std::ios::binary);
   if (!file.is_open()) {
      error(executor.diagnostics, 0, 0, "Cannot read file '%s'", in.c_str());
      return;
   }

   size_t version;
   file.read(reinterpret_cast<char*>(&version), sizeof(version));
   file.read(reinterpret_cast<char*>(&executor.main), sizeof(executor.main));

   if (version != FILE_VERSION) {
      error(executor.diagnostics, 0, 0, "File '%s' is using an outdated version %zu. The newest version is %zu. Recompile the project", in.c_str(), version, FILE_VERSION);
      return;
   }

   size_t lexemeCount;
   file.read(reinterpret_cast<char*>(&lexemeCount), sizeof(lexemeCount));
   executor.cache.lexemes.resize(lexemeCount);

   for (size_t i = 0; i < lexemeCount; ++i) {
      file_string_size_t size;
      file.read(reinterpret_cast<char*>(&size), sizeof(size));
      executor.cache.lexemes[i].resize(size);
      file.read(reinterpret_cast<char*>(executor.cache.lexemes[i].data()), size);
   }

   size_t structFieldCount;
   file.read(reinterpret_cast<char*>(&structFieldCount), sizeof(structFieldCount));
   executor.structFields.resize(structFieldCount);
   file.read(reinterpret_cast<char*>(executor.structFields.data()), sizeof(uint32_t) * structFieldCount);

   size_t structChainCount;
   file.read(reinterpret_cast<char*>(&structChainCount), sizeof(structChainCount));
   executor.structChains.resize(structChainCount);
   file.read(reinterpret_cast<char*>(executor.structChains.data()), sizeof(StructChainLink) * structChainCount);

   size_t functionCount;
   file.read(reinterpret_cast<char*>(&functionCount), sizeof(functionCount));
   executor.functions.resize(functionCount);
   file.read(reinterpret_cast<char*>(executor.functions.data()), sizeof(Function) * functionCount);
   
   size_t argumentCount;
   file.read(reinterpret_cast<char*>(&argumentCount), sizeof(argumentCount));
   executor.arguments.resize(argumentCount);
   file.read(reinterpret_cast<char*>(executor.arguments.data()), sizeof(Value) * argumentCount);
   
   size_t commandCount;
   file.read(reinterpret_cast<char*>(&commandCount), sizeof(commandCount));
   executor.code.resize(commandCount);
   file.read(reinterpret_cast<char*>(executor.code.data()), sizeof(Command) * commandCount);
}
