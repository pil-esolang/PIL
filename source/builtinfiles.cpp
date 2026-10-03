#include "builtin.hpp"
#include "builtinhelpers.hpp"
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <array>
#include <chrono>

#ifdef _WIN32
#define PIL_POPEN _popen
#define PIL_PCLOSE _pclose
#else
#include <sys/wait.h>
#define PIL_POPEN popen
#define PIL_PCLOSE pclose
#endif

void builtinFileOpen(const Command &command, Executor &executor) {
   const std::string *path;
   if (!hasPermission(command, executor, "file-open", FILE_LOCK) || !constStringOrError(command, executor, "file-open", path, 0)) return;

   std::error_code ec;
   if (!std::filesystem::is_regular_file(*path, ec)) {
      storeInRegister(executor, command, NULL_VALUE, "file-open");
      return;
   }
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
   storeInRegister(executor, command, Value{.type = VALUE_FILE, .handle = id}, "file-open");
}

void builtinFileClose(const Command &command, Executor &executor) {
   if (!hasPermission(command, executor, "file-close", FILE_LOCK)) return;
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
   std::fstream *file;
   if (!hasPermission(command, executor, "file-read", FILE_LOCK) || !fileHandleOrError(command, executor, "file-read", file)) return;
   std::string content((std::istreambuf_iterator<char>(*file)), std::istreambuf_iterator<char>());
   storeString(executor, command, content, back(executor, command), "file-read");
}

void builtinFileReadLn(const Command &command, Executor &executor) {
   std::fstream *file;
   if (!hasPermission(command, executor, "file-readln", FILE_LOCK) || !fileHandleOrError(command, executor, "file-readln", file)) return;
   std::string line;
   if (!std::getline(*file, line)) {
      storeInRegister(executor, command, NULL_VALUE, "file-readln");
      return;
   }
   storeString(executor, command, line, back(executor, command), "file-readln");
}

void builtinFileReadCh(const Command &command, Executor &executor) {
   std::fstream *file;
   if (!hasPermission(command, executor, "file-readch", FILE_LOCK) || !fileHandleOrError(command, executor, "file-readch", file)) return;
   char c;
   if (!file->get(c)) {
      storeInRegister(executor, command, NULL_VALUE, "file-readch");
      return;
   }
   storeInRegister(executor, command, Value{.type = VALUE_CHARACTER, .character = c}, "file-readch");
}

void builtinFileReadBytes(const Command &command, Executor &executor) {
   std::fstream *file;
   if (!hasPermission(command, executor, "file-read-bytes", FILE_LOCK) || !fileHandleOrError(command, executor, "file-read-bytes", file)) return;
   pilfloat_t nRaw = getNum(executor, command, 1, "file-read-bytes");
   if (nRaw < 0) {
      error(executor.diagnostics, command.file, command.line, "file-read-bytes: Byte count cannot be negative");
      return;
   }
   size_t n = nRaw;
   std::string buffer(n, '\0');
   file->read(buffer.data(), (std::streamsize)n);
   buffer.resize((size_t)file->gcount());
   storeString(executor, command, buffer, back(executor, command), "file-read-bytes");
}

void builtinFileEof(const Command &command, Executor &executor) {
   std::fstream *file;
   if (!hasPermission(command, executor, "file-eof", FILE_LOCK) || !fileHandleOrError(command, executor, "file-eof", file)) return;
   storeBoolean(executor, command, file->peek() == std::char_traits<char>::eof(), "file-eof");
}

void builtinFileWrite(const Command &command, Executor &executor) {
   std::fstream *file;
   if (!hasPermission(command, executor, "file-write", FILE_LOCK) || !fileHandleOrError(command, executor, "file-write", file)) return;
   Value value = resolveVariable(executor, arg(executor, command, 1));
   *file << toString(executor, value, "file-write", command.file, command.line);
}

void builtinFileWriteLn(const Command &command, Executor &executor) {
   std::fstream *file;
   if (!hasPermission(command, executor, "file-writeln", FILE_LOCK) || !fileHandleOrError(command, executor, "file-writeln", file)) return;
   Value value = resolveVariable(executor, arg(executor, command, 1));
   *file << toString(executor, value, "file-writeln", command.file, command.line) << '\n';
}

void builtinFileFlush(const Command &command, Executor &executor) {
   std::fstream *file;
   if (!hasPermission(command, executor, "file-flush", FILE_LOCK) || !fileHandleOrError(command, executor, "file-flush", file)) return;
   file->flush();
}

void builtinFileTell(const Command &command, Executor &executor) {
   std::fstream *file;
   if (!hasPermission(command, executor, "file-tell", FILE_LOCK) || !fileHandleOrError(command, executor, "file-tell", file)) return;
   auto pos = file->tellg();
   if (pos < 0) {
      error(executor.diagnostics, command.file, command.line, "file-tell: Stream position is invalid (stream may be in a failed state)");
      return;
   }
   storeNumber(executor, command, (pilfloat_t)(long long)pos, false, "file-tell");
}

void builtinFileSeek(const Command &command, Executor &executor) {
   std::fstream *file;
   if (!hasPermission(command, executor, "file-seek", FILE_LOCK) || !fileHandleOrError(command, executor, "file-seek", file)) return;
   pilfloat_t pos = getNum(executor, command, 1, "file-seek");
   file->clear();
   file->seekg((long long)pos, std::ios::beg);
}

void builtinFileSeekRel(const Command &command, Executor &executor) {
   std::fstream *file;
   if (!hasPermission(command, executor, "file-seek-rel", FILE_LOCK) || !fileHandleOrError(command, executor, "file-seek-rel", file)) return;
   pilfloat_t offset = getNum(executor, command, 1, "file-seek-rel");
   file->clear();
   file->seekg((long long)offset, std::ios::cur);
}

void builtinFileSeekEnd(const Command &command, Executor &executor) {
   std::fstream *file;
   if (!hasPermission(command, executor, "file-seek-end", FILE_LOCK) || !fileHandleOrError(command, executor, "file-seek-end", file)) return;
   pilfloat_t offset = getNum(executor, command, 1, "file-seek-end");
   file->clear();
   file->seekg((long long)offset, std::ios::end);
}

void builtinPathExists(const Command &command, Executor &executor) {
   const std::string *path;
   if (!hasPermission(command, executor, "path-exists", FILE_LOCK) || !constStringOrError(command, executor, "path-exists", path)) return;
   std::error_code ec;
   bool result = std::filesystem::exists(*path, ec);
   storeBoolean(executor, command, !ec && result, "path-exists");
}

void builtinPathIsFile(const Command &command, Executor &executor) {
   const std::string *path;
   if (!hasPermission(command, executor, "path-is-file", FILE_LOCK) || !constStringOrError(command, executor, "path-is-file", path)) return;
   std::error_code ec;
   bool result = std::filesystem::is_regular_file(*path, ec);
   storeBoolean(executor, command, !ec && result, "path-is-file");
}

void builtinPathIsDir(const Command &command, Executor &executor) {
   const std::string *path;
   if (!hasPermission(command, executor, "path-is-dir", FILE_LOCK) || !constStringOrError(command, executor, "path-is-dir", path)) return;
   std::error_code ec;
   bool result = std::filesystem::is_directory(*path, ec);
   storeBoolean(executor, command, !ec && result, "path-is-dir");
}

void builtinPathSize(const Command &command, Executor &executor) {
   const std::string *path;
   if (!hasPermission(command, executor, "path-size", FILE_LOCK) || !constStringOrError(command, executor, "path-size", path)) return;
   std::error_code ec;
   if (std::filesystem::is_directory(*path, ec)) {
      storeInRegister(executor, command, NULL_VALUE, "path-size");
      return;
   }
   uintmax_t size = std::filesystem::file_size(*path, ec);
   if (ec) {
      error(executor.diagnostics, command.file, command.line, "path-size: %s", ec.message().c_str());
      return;
   }
   storeNumber(executor, command, (pilfloat_t)size, false, "path-size");
}

void builtinPathModifiedTime(const Command &command, Executor &executor) {
   const std::string *path;
   if (!hasPermission(command, executor, "path-modified-time", FILE_LOCK) || !constStringOrError(command, executor, "path-modified-time", path)) return;
   std::error_code ec;
   std::filesystem::file_time_type ftime = std::filesystem::last_write_time(*path, ec);
   if (ec) {
      error(executor.diagnostics, command.file, command.line, "path-modified-time: %s", ec.message().c_str());
      return;
   }
   auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
   long long unixTime = std::chrono::duration_cast<std::chrono::seconds>(systemTime.time_since_epoch()).count();
   storeNumber(executor, command, (pilfloat_t)unixTime, false, "path-modified-time");
}

void builtinPathCreateDir(const Command &command, Executor &executor) {
   const std::string *path;
   if (!hasPermission(command, executor, "path-create-dir", FILE_LOCK) || !constStringOrError(command, executor, "path-create-dir", path)) return;
   std::error_code ec;
   std::filesystem::create_directories(*path, ec);
   if (ec) {
      error(executor.diagnostics, command.file, command.line, "path-create-dir: %s", ec.message().c_str());
   }
}

void builtinPathRemove(const Command &command, Executor &executor) {
   const std::string *path;
   if (!hasPermission(command, executor, "path-remove", FILE_LOCK) || !constStringOrError(command, executor, "path-remove", path)) return;
   std::error_code ec;
   std::filesystem::remove(*path, ec);
   if (ec) {
      error(executor.diagnostics, command.file, command.line, "path-remove: %s", ec.message().c_str());
   }
}

void builtinPathRemoveAll(const Command &command, Executor &executor) {
   const std::string *path;
   if (!hasPermission(command, executor, "path-remove-all", FILE_LOCK) || !constStringOrError(command, executor, "path-remove-all", path)) return;
   std::error_code ec;
   std::filesystem::remove_all(*path, ec);
   if (ec) {
      error(executor.diagnostics, command.file, command.line, "path-remove-all: %s", ec.message().c_str());
   }
}

void builtinPathCopy(const Command &command, Executor &executor) {
   const std::string *src, *dst;
   if (!hasPermission(command, executor, "path-copy", FILE_LOCK) || !constStringOrError(command, executor, "path-copy", src, 0) || !constStringOrError(command, executor, "path-copy", dst, 1)) return;
   std::error_code ec;
   std::filesystem::copy(*src, *dst, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
   if (ec) {
      error(executor.diagnostics, command.file, command.line, "path-copy: %s", ec.message().c_str());
   }
}

void builtinPathMove(const Command &command, Executor &executor) {
   const std::string *src, *dst;
   if (!hasPermission(command, executor, "path-move", FILE_LOCK) || !constStringOrError(command, executor, "path-move", src, 0) || !constStringOrError(command, executor, "path-move", dst, 1)) return;

   std::error_code ec;
   std::filesystem::rename(*src, *dst, ec);
   if (!ec) return;

   std::error_code copyEc;
   std::filesystem::copy(*src, *dst, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, copyEc);
   if (copyEc) {
      error(executor.diagnostics, command.file, command.line, "path-move: %s", copyEc.message().c_str());
      return;
   }
   std::error_code removeEc;
   std::filesystem::remove_all(*src, removeEc);
   if (removeEc) {
      error(executor.diagnostics, command.file, command.line, "path-move: Copied to destination but failed to remove source: %s", removeEc.message().c_str());
   }
}

void builtinPathJoin(const Command &command, Executor &executor) {
   const std::string *a, *b;
   if (!constStringOrError(command, executor, "path-join", a, 0) || !constStringOrError(command, executor, "path-join", b, 1)) return;
   std::filesystem::path result = std::filesystem::path(*a) / std::filesystem::path(*b);
   storeString(executor, command, result.string(), back(executor, command), "path-join");
}

void builtinPathFilename(const Command &command, Executor &executor) {
   const std::string *path;
   if (!constStringOrError(command, executor, "path-filename", path)) return;
   storeString(executor, command, std::filesystem::path(*path).filename().string(), back(executor, command), "path-filename");
}

void builtinPathStem(const Command &command, Executor &executor) {
   const std::string *path;
   if (!constStringOrError(command, executor, "path-stem", path)) return;
   storeString(executor, command, std::filesystem::path(*path).stem().string(), back(executor, command), "path-stem");
}

void builtinPathExtension(const Command &command, Executor &executor) {
   const std::string *path;
   if (!constStringOrError(command, executor, "path-extension", path)) return;
   storeString(executor, command, std::filesystem::path(*path).extension().string(), back(executor, command), "path-extension");
}

void builtinPathParent(const Command &command, Executor &executor) {
   const std::string *path;
   if (!constStringOrError(command, executor, "path-parent", path)) return;
   storeString(executor, command, std::filesystem::path(*path).parent_path().string(), back(executor, command), "path-parent");
}

void builtinPathAbsolute(const Command &command, Executor &executor) {
   const std::string *path;
   if (!constStringOrError(command, executor, "path-absolute", path)) return;
   std::error_code ec;
   std::filesystem::path result = std::filesystem::absolute(*path, ec);
   if (ec) {
      error(executor.diagnostics, command.file, command.line, "path-absolute: %s", ec.message().c_str());
      return;
   }
   storeString(executor, command, result.string(), back(executor, command), "path-absolute");
}

void builtinPathNormalize(const Command &command, Executor &executor) {
   const std::string *path;
   if (!constStringOrError(command, executor, "path-normalize", path)) return;
   storeString(executor, command, std::filesystem::path(*path).lexically_normal().string(), back(executor, command), "path-normalize");
}

void builtinDirList(const Command &command, Executor &executor) {
   const std::string *path;
   if (!hasPermission(command, executor, "dir-list", FILE_LOCK) || !constStringOrError(command, executor, "dir-list", path)) return;

   std::error_code ec;
   std::filesystem::directory_iterator it(*path, ec);
   if (ec) {
      error(executor.diagnostics, command.file, command.line, "dir-list: %s", ec.message().c_str());
      return;
   }

   std::vector<Value> entries;
   std::filesystem::directory_iterator end;
   for (; it != end; it.increment(ec)) {
      if (ec) break;
      Value v {VALUE_STRING};
      v.string = allocateString(executor, it->path().filename().string());
      entries.push_back(v);
   }
   storeArray(executor, command, entries, back(executor, command), "dir-list");
}

void builtinDirListRecursive(const Command &command, Executor &executor) {
   const std::string *path;
   if (!hasPermission(command, executor, "dir-list-recursive", FILE_LOCK) || !constStringOrError(command, executor, "dir-list-recursive", path)) return;

   std::error_code ec;
   std::filesystem::recursive_directory_iterator it(*path, ec);
   if (ec) {
      error(executor.diagnostics, command.file, command.line, "dir-list-recursive: %s", ec.message().c_str());
      return;
   }

   std::filesystem::path base(*path);
   std::vector<Value> entries;
   std::filesystem::recursive_directory_iterator end;
   for (; it != end; it.increment(ec)) {
      if (ec) break;
      std::error_code relEc;
      std::filesystem::path relative = std::filesystem::relative(it->path(), base, relEc);
      Value v {VALUE_STRING};
      v.string = allocateString(executor, relEc ? it->path().string() : relative.string());
      entries.push_back(v);
   }
   storeArray(executor, command, entries, back(executor, command), "dir-list-recursive");
}

void builtinOsName(const Command &command, Executor &executor) {
#if defined(_WIN32)
   constexpr const char *name = "windows";
#elif defined(__APPLE__)
   constexpr const char *name = "macos";
#elif defined(__linux__)
   constexpr const char *name = "linux";
#else
   constexpr const char *name = "unknown";
#endif
   storeString(executor, command, name, back(executor, command), "os-name");
}

void builtinOsCwd(const Command &command, Executor &executor) {
   if (!hasPermission(command, executor, "os-cwd", FILE_LOCK)) return;
   std::error_code ec;
   std::filesystem::path cwd = std::filesystem::current_path(ec);
   if (ec) {
      error(executor.diagnostics, command.file, command.line, "os-cwd: %s", ec.message().c_str());
      return;
   }
   storeString(executor, command, cwd.string(), back(executor, command), "os-cwd");
}

void builtinOsSetCwd(const Command &command, Executor &executor) {
   const std::string *path;
   if (!hasPermission(command, executor, "os-set-cwd", FILE_LOCK) || !constStringOrError(command, executor, "os-set-cwd", path)) return;
   std::error_code ec;
   std::filesystem::current_path(*path, ec);
   if (ec) {
      error(executor.diagnostics, command.file, command.line, "os-set-cwd: %s", ec.message().c_str());
   }
}

void builtinOsEnvGet(const Command &command, Executor &executor) {
   const std::string *name;
   if (!hasPermission(command, executor, "os-env-get", ENV_LOCK) || !constStringOrError(command, executor, "os-env-get", name)) return;
   const char *value = std::getenv(name->c_str());
   if (!value) {
      storeInRegister(executor, command, NULL_VALUE, "os-env-get");
      return;
   }
   storeString(executor, command, std::string(value), back(executor, command), "os-env-get");
}

void builtinOsEnvSet(const Command &command, Executor &executor) {
   const std::string *name;
   if (!hasPermission(command, executor, "os-env-set", ENV_LOCK) || !constStringOrError(command, executor, "os-env-set", name, 0)) return;
   Value value = resolveVariable(executor, arg(executor, command, 1));
   std::string valueStr = toString(executor, value, "os-env-set", command.file, command.line);
#ifdef _WIN32
   _putenv_s(name->c_str(), valueStr.c_str());
#else
   setenv(name->c_str(), valueStr.c_str(), 1);
#endif
}

void builtinOsExec(const Command &command, Executor &executor) {
   const std::string *cmd;
   if (!hasPermission(command, executor, "os-exec", EXEC_LOCK) || !constStringOrError(command, executor, "os-exec", cmd)) return;

   FILE *pipe = PIL_POPEN(cmd->c_str(), "r");
   if (!pipe) {
      error(executor.diagnostics, command.file, command.line, "os-exec: Failed to start process");
      return;
   }

   std::string output;
   std::array<char, 4096> buffer;
   size_t n;
   while ((n = fread(buffer.data(), 1, buffer.size(), pipe)) > 0) {
      output.append(buffer.data(), n);
   }

   int status = PIL_PCLOSE(pipe);
#ifdef _WIN32
   executor.lastExecExitCode = status;
#else
   executor.lastExecExitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif

   storeString(executor, command, output, back(executor, command), "os-exec");
}

void builtinOsExitCode(const Command &command, Executor &executor) {
   storeNumber(executor, command, (pilfloat_t)executor.lastExecExitCode, false, "os-exit-code");
}
