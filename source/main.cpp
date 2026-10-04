#include "pil.hpp"
#include <cstring>
#include <filesystem>

void printHelp();
void compile(Executor &executor, const std::filesystem::path &file, float &readTime, float &lexTime, float &parseTime, bool debugLexer, bool debugCode);

int main(int argc, char *argv[]) {
   Executor executor;
   bool time = false;
   bool debugCode = false;
   bool debugLexer = false;
   
   for (int i = 1; i < argc; ++i) {
      if (strcmp(argv[i], "--time") == 0) time = true;
      else if (strcmp(argv[i], "--debug-code") == 0) debugCode = true;
      else if (strcmp(argv[i], "--debug-tokens") == 0) debugLexer = true;
      else if (strcmp(argv[i], "--allow-fileio") == 0) executor.allowFileio = true;
      else if (strcmp(argv[i], "--allow-env") == 0) executor.allowEnv = true;
      else if (strcmp(argv[i], "--allow-exec") == 0) executor.allowExec = true;
      else if (strcmp(argv[i], "--silence-leaks") == 0) executor.silenceLeaks = true;
      else {
         argv = &argv[i];
         argc -= i;
         break;
      }
   }

   if (argc == 2 && strcmp(argv[0], "run") == 0) {
      std::filesystem::path path (argv[1]);
      if (path.has_extension() && path.extension() == ".pil") {
         float readTime, lexTime, parseTime;
         compile(executor, path, readTime, lexTime, parseTime, debugLexer, debugCode);

         measure();
         callMain(executor, SEVERITY_ERROR);
         float runtime = measureEnd();

         logStackTrace(executor, SEVERITY_ERROR);
         logMemoryLeaks(executor);
         debugExecutionTime(time, readTime, lexTime, parseTime, runtime);
      }
      else if (path.has_extension() && path.extension() == ".pilo") {
         measure();
         readCachedBytecode(executor, path.string());
         log(executor, SEVERITY_ERROR);
         float readTime = measureEnd();

         measure();
         callMain(executor, SEVERITY_ERROR);
         float runtime = measureEnd();

         logStackTrace(executor, SEVERITY_ERROR);
         logMemoryLeaks(executor);
         debugCacheExecutionTime(time, readTime, runtime);
      }
      else {
         printf("PIL::run: Expected second argument to be either a '.pil' or .'pilo' file.\n");
         printHelp();
         exit(EXIT_FAILURE);
      }
   }
   else if (argc == 3 && strcmp(argv[0], "compile") == 0) {
      std::filesystem::path in (argv[1]);
      std::filesystem::path out (argv[2]);
      if (!in.has_extension() || in.extension() != ".pil" || !out.has_extension() || out.extension() != ".pilo") {
         printf("PIL::compile: Expected second argument to be a '.pil' file and the third to have the '.pilo' extension.\n");
         printHelp();
         exit(EXIT_FAILURE);
      }
      float readTime, lexTime, parseTime;
      compile(executor, in, readTime, lexTime, parseTime, debugLexer, debugCode);

      printf("Writing to '%s'...\n", out.string().c_str());
      measure();
      writeToFile(executor, out.string());
      float writeTime = measureEnd();

      log(executor, SEVERITY_ERROR);
      printf("Wrote %zuB to '%s'.\n", std::filesystem::file_size(out), out.string().c_str());
      debugCompilationTime(time, readTime, lexTime, parseTime, writeTime);
   }
   else {
      printHelp();
   }
}

void compile(Executor &executor, const std::filesystem::path &file, float &readTime, float &lexTime, float &parseTime, bool debugLexer, bool debugCode) {
   std::vector<Token> tokens;
   PILFile fileData;

   measure();
   readPIL(executor.diagnostics, executor.cache, file.string(), fileData, 0, 0);
   log(executor, SEVERITY_ERROR);
   readTime = measureEnd();

   measure();
   lexPILFile(executor.diagnostics, executor.cache, fileData, tokens);
   log(executor, SEVERITY_ERROR);
   fileData.code.clear(); // free up memory for the includes, which will read more files
   fileData.code.shrink_to_fit();
   lexTime = measureEnd();

   measure();
   translatePIL(executor, fileData.lexeme, tokens);
   log(executor, SEVERITY_ERROR);
   readTime += measureEnd();

   debugTokens(debugLexer, executor.cache, tokens);

   measure();
   parsePIL(executor, tokens);
   log(executor, SEVERITY_ERROR);
   tokens.clear(); // tokens are no longer in use
   tokens.shrink_to_fit();
   parseTime = measureEnd();

   debugBytecode(debugCode, executor);
}

void printHelp() {
   printf(
      "Usage:\n"
      "\t./pil [FLAGS...] [COMMAND] [ARGS...]\n"
      "Commands:\n"
      "\trun      [FILE/EXECUTABLE] run a file/executable\n"
      "\tcompile  [FILE] [OUTPUT]   compile a file into output\n"
      "Flags:\n"
      "\t--debug-code      output bytecode and compile/runtime time\n"
      "\t--debug-tokens    output tokens after translation\n"
      "\t--time            show time of each compiler's operation\n"
      "\t--allow-fileio    allow file I/O built-ins\n"
      "\t--allow-env       allow OS environment variable built-ins\n"
      "\t--allow-exec      allow 'os-exec' built-in\n"
      "\t--silence-leaks   silence all memory leaks\n"
   );
}
