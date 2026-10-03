#include "builtin.hpp"
#include "builtinhelpers.hpp"

void builtinFileOpen(const Command &command, Executor &executor) {
   const std::string *path;
   if (!constStringOrError(command, executor, "file-open", path, 0)) return;
   char mode = getChar(command, executor, "file-open", 1);
   std::fstream file;

   if (mode == 'r') file.open(path->c_str(), std::ios::in);
   else if (mode == 'w') file.open(path->c_str(), std::ios::out);
   else if (mode == 'a') file.open(path->c_str(), std::ios::out | std::ios::app);
   else if (mode == 'x') file.open(path->c_str(), std::ios::out | std::ios::in);
   else {
      error(executor.diagnostics, command.file, command.line, "file-open: Unknown mode '%c'. Valid modes are 'r', 'w', 'a' and 'x'", mode);
      return;
   }
   size_t id = allocateFile(executor, std::move(file));
   storeInRegister(executor, command, back(executor, command), Value{.type = VALUE_FILE, .handle = id}, "file-open");
}

void builtinFileClose(const Command &command, Executor &executor) {
   Value a = arg(executor, command, 0);
   Value &handle = resolveVariableByRef(executor, a);
   if (handle.type != VALUE_FILE) {
      error(executor.diagnostics, command.file, command.line, "file-close: Expected File, got %s instead", getValueName(handle.type));
      return;
   }
   if (auto it = executor.files.find(handle.handle); it != executor.files.end()) {
      it->second.file.close();
      executor.files.erase(it);
   }
   handle = NULL_VALUE;
}

void builtinFileRead(const Command &command, Executor &executor) {

}

void builtinFileReadLn(const Command &command, Executor &executor) {

}

void builtinFileReadCh(const Command &command, Executor &executor) {

}

void builtinFileReadBytes(const Command &command, Executor &executor) {

}

void builtinFileEof(const Command &command, Executor &executor) {

}

void builtinFileWrite(const Command &command, Executor &executor) {

}

void builtinFileWriteLn(const Command &command, Executor &executor) {

}

void builtinFileFlush(const Command &command, Executor &executor) {

}

void builtinFileTell(const Command &command, Executor &executor) {

}

void builtinFileSeek(const Command &command, Executor &executor) {

}

void builtinFileSeekRel(const Command &command, Executor &executor) {

}

void builtinFileSeekEnd(const Command &command, Executor &executor) {

}

void builtinPathExists(const Command &command, Executor &executor) {

}

void builtinPathIsFile(const Command &command, Executor &executor) {

}

void builtinPathIsDir(const Command &command, Executor &executor) {

}

void builtinPathSize(const Command &command, Executor &executor) {

}

void builtinPathModifiedTime(const Command &command, Executor &executor) {

}

void builtinPathCreateDir(const Command &command, Executor &executor) {

}

void builtinPathRemove(const Command &command, Executor &executor) {

}

void builtinPathRemoveAll(const Command &command, Executor &executor) {

}

void builtinPathCopy(const Command &command, Executor &executor) {

}

void builtinPathMove(const Command &command, Executor &executor) {

}

void builtinPathJoin(const Command &command, Executor &executor) {

}

void builtinPathFilename(const Command &command, Executor &executor) {

}

void builtinPathStem(const Command &command, Executor &executor) {

}

void builtinPathExtension(const Command &command, Executor &executor) {

}

void builtinPathParent(const Command &command, Executor &executor) {

}

void builtinPathAbsolute(const Command &command, Executor &executor) {

}

void builtinPathNormalize(const Command &command, Executor &executor) {

}

void builtinDirList(const Command &command, Executor &executor) {

}

void builtinDirListRecursive(const Command &command, Executor &executor) {

}

void builtinOsName(const Command &command, Executor &executor) {

}

void builtinOsCwd(const Command &command, Executor &executor) {

}

void builtinOsSetCwd(const Command &command, Executor &executor) {

}

void builtinOsEnvGet(const Command &command, Executor &executor) {

}

void builtinOsEnvSet(const Command &command, Executor &executor) {

}

void builtinOsArgs(const Command &command, Executor &executor) {

}

void builtinOsExec(const Command &command, Executor &executor) {

}

void builtinOsExitCode(const Command &command, Executor &executor) {

}
