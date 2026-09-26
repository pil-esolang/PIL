#include "pil.hpp"
#include <cstring>
#include <filesystem>

void printHelp();
void compile(Executor executor, const std::filesystem::path &file, float &readTime, float &lexTime, float &translatorTime, float &parseTime, bool debugLexer, bool debugCode);

int main(int argc, char *argv[]) {
   bool time = false;
   bool debugCode = false;
   bool debugLexer = false;
   
   for (int i = 1; i < argc; ++i) {
      if (strcmp(argv[i], "--time") == 0) time = true;
      else if (strcmp(argv[i], "--debug-code") == 0) debugCode = true;
      else if (strcmp(argv[i], "--debug-tokens") == 0) debugLexer = true;
      else {
         argv = &argv[i];
         argc -= i;
         break;
      }
   }

   if (argc == 2 && strcmp(argv[0], "run") == 0) {
      std::filesystem::path path (argv[1]);
      if (path.has_extension() && path.extension() == ".pil") {
         Executor executor;
         float readTime, lexTime, translatorTime, parseTime;
         compile(executor, path, readTime, lexTime, translatorTime, parseTime, debugLexer, debugCode);

         measure();
         callMain(executor, SEVERITY_ERROR);
         float runtime = measureEnd();

         logStackTrace(executor, SEVERITY_ERROR);
         logMemoryLeaks(executor);
         debugExecutionTime(time, readTime, lexTime, translatorTime, parseTime, runtime);
      }
      else if (path.has_extension() && path.extension() == ".pilo") {
         Executor executor;
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
      Executor executor;
      float readTime, lexTime, translatorTime, parseTime;
      compile(executor, in, readTime, lexTime, translatorTime, parseTime, debugLexer, debugCode);

      printf("Writing to '%s'...\n", out.string().c_str());
      measure();
      writeToFile(executor, out.string());
      float writeTime = measureEnd();

      log(executor, SEVERITY_ERROR);
      printf("Wrote %zuB to '%s'.\n", std::filesystem::file_size(out), out.string().c_str());
      debugCompilationTime(time, readTime, lexTime, translatorTime, parseTime, writeTime);
   }
   else {
      printHelp();
   }
}

void compile(Executor executor, const std::filesystem::path &file, float &readTime, float &lexTime, float &translatorTime, float &parseTime, bool debugLexer, bool debugCode) {
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

   measure();
   expandSnippets(executor, tokens);
   log(executor, SEVERITY_ERROR);
   translatorTime = measureEnd();

   debugTokens(debugLexer, executor.cache, tokens);

   measure();
   parsePIL(executor, tokens);
   log(executor, SEVERITY_ERROR);
   tokens.clear(); // tokens are no longer in use
   tokens.shrink_to_fit();
   parseTime = measureEnd();
}

void printHelp() {
   printf(
      "Usage:\n"
      "\t./pil [FLAGS...] [COMMAND] [ARGS...]\n"
      "Commands:\n"
      "\trun      [FILE/EXECUTABLE] run a file/executable\n"
      "\tcompile  [FILE] [OUTPUT]   compile a file into output\n"
      "Flags:\n"
      "\t--debug-code    output bytecode and compile/runtime time\n"
      "\t--debug-tokens  output tokens after translation\n"
      "\t--time          show time of each compiler's operation\n"
   );
}
