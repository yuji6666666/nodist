#include "napi/native_api.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// ==================== 工具函数 ====================

// 去除字符串首尾空白(与 main.cpp 的 trim 一致)
static std::string Trim(const std::string& str)
{
    size_t first = str.find_first_not_of(" \n\r\t");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \n\r\t");
    return str.substr(first, (last - first + 1));
}

// 从 napi_value 读取 UTF-8 字符串
static std::string GetStringArg(napi_env env, napi_value value)
{
    size_t len = 0;
    if (napi_get_value_string_utf8(env, value, nullptr, 0, &len) != napi_ok) {
        return "";
    }
    if (len == 0) {
        return "";
    }
    std::string result(len, '\0');
    napi_get_value_string_utf8(env, value, &result[0], len + 1, &len);
    return result;
}

// 把 C++ 字符串包装为 napi string
static napi_value CreateString(napi_env env, const std::string& text)
{
    napi_value out = nullptr;
    napi_create_string_utf8(env, text.c_str(), text.size(), &out);
    return out;
}

// 版本号白名单校验:
// 仅允许 数字/字母/./-/_ ,且必须包含数字,长度 <= 32。
// 防止路径穿越(如 "../..")与 shim 脚本内容注入(如 引号/反引号/$(...) )。
static bool IsValidVersion(const std::string& version)
{
    if (version.empty() || version.size() > 32) {
        return false;
    }
    bool hasDigit = false;
    for (char c : version) {
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == '_';
        if (!ok) {
            return false;
        }
        if (c >= '0' && c <= '9') {
            hasDigit = true;
        }
    }
    if (!hasDigit || version == "." || version == "..") {
        return false;
    }
    return true;
}

// ==================== 1:1 还原自 main.cpp 的 Nodist 逻辑 ====================

// createVersionDirectory:创建 <base>/nodist_versions/v<version> 目录
static bool CreateVersionDirectory(const std::string& baseDir, const std::string& version, std::string& outTargetDir)
{
    std::string versionsDir = baseDir + "/nodist_versions";
    outTargetDir = versionsDir + "/v" + version;
    mkdir(versionsDir.c_str(), 0777);
    if (mkdir(outTargetDir.c_str(), 0777) == 0 || errno == EEXIST) {
        return true;
    }
    return false;
}

// simulateDownload:生成 shim 脚本(与 main.cpp 完全一致)
static bool SimulateDownload(const std::string& version, const std::string& targetDir, std::string& outScriptPath)
{
    std::string exePath = targetDir + "/node";
    std::ofstream outfile(exePath);
    if (outfile.is_open()) {
        outfile << "#!/bin/sh\n";
        outfile << "echo \"[真实 Node.js v" << version << "] 收到执行指令。参数列表: $@\"\n";
        outfile.close();
        chmod(exePath.c_str(), 0777);
        outScriptPath = exePath;
        return true;
    }
    return false;
}

// 执行 shim 脚本并捕获 stdout/stderr(fork + pipe + waitpid)
static std::string RunNodeScript(const std::string& scriptPath, const std::vector<std::string>& args)
{
    std::vector<std::string> argvStore;
    argvStore.push_back("/system/bin/sh");
    argvStore.push_back(scriptPath);
    for (const auto& a : args) {
        argvStore.push_back(a);
    }
    std::vector<char*> argv;
    for (auto& s : argvStore) {
        argv.push_back(const_cast<char*>(s.c_str()));
    }
    argv.push_back(nullptr);

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return "[错误] pipe 创建失败: " + std::string(strerror(errno));
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return "[错误] fork 失败: " + std::string(strerror(errno));
    }

    if (pid == 0) {
        // 子进程:stdout/stderr 重定向到管道写端
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execv(argv[0], argv.data());
        // exec 失败:把错误写进管道(stderr 已重定向到管道),方便诊断
        std::string errMsg = "[execv 失败] " + std::string(strerror(errno)) + "\n";
        write(STDERR_FILENO, errMsg.c_str(), errMsg.size());
        _exit(127);
    }

    // 父进程:读取管道内容
    close(pipefd[1]);
    std::string output;
    char buf[512];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        output.append(buf, static_cast<size_t>(n));
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        output += "[exit code " + std::to_string(WEXITSTATUS(status)) + "]\n";
    } else if (WIFSIGNALED(status)) {
        output += "[被信号 " + std::to_string(WTERMSIG(status)) + " 终止]\n";
    }
    return output;
}

// ==================== NAPI 导出函数 ====================

static napi_value Add(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = {nullptr};

    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 2) {
        napi_throw_type_error(env, nullptr, "add 需要两个数字参数");
        return nullptr;
    }

    napi_valuetype valuetype0;
    napi_typeof(env, args[0], &valuetype0);
    napi_valuetype valuetype1;
    napi_typeof(env, args[1], &valuetype1);

    if (valuetype0 != napi_number || valuetype1 != napi_number) {
        napi_throw_type_error(env, nullptr, "add 参数必须是数字");
        return nullptr;
    }

    double value0;
    napi_get_value_double(env, args[0], &value0);

    double value1;
    napi_get_value_double(env, args[1], &value1);

    napi_value sum;
    napi_create_double(env, value0 + value1, &sum);

    return sum;
}

// nodist -v:查看 Nodist 版本
static napi_value NodistVersion(napi_env env, napi_callback_info info)
{
    return CreateString(env, "v0.9.1 (HarmonyOS NDK Port)");
}

// nodist + <version>:安装指定版本(baseDir 由 ArkTS 传入 context.filesDir)
static napi_value NodistInstall(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return CreateString(env, "[错误] 缺少参数: nodistInstall(baseDir, version)");
    }
    std::string baseDir = GetStringArg(env, args[0]);
    std::string version = Trim(GetStringArg(env, args[1]));
    if (version.empty()) {
        return CreateString(env, "[错误] 请指定要安装的版本号。");
    }
    if (!IsValidVersion(version)) {
        return CreateString(env, "[错误] 版本号不合法(仅允许 数字/字母/./-/_)。");
    }

    std::string targetDir;
    if (!CreateVersionDirectory(baseDir, version, targetDir)) {
        return CreateString(env, "[错误] 创建版本目录失败: " + std::string(strerror(errno)));
    }

    std::string scriptPath;
    if (!SimulateDownload(version, targetDir, scriptPath)) {
        return CreateString(env, "[错误] 写入 shim 脚本失败: " + std::string(strerror(errno)));
    }

    std::string out = "版本 v" + version + " 安装完成。\n";
    out += "已生成: " + scriptPath + "\n";
    out += "---- shim 脚本内容 ----\n";
    std::ifstream in(scriptPath);
    std::ostringstream ss;
    ss << in.rdbuf();
    out += ss.str();
    out += "---- 结束 ----\n";
    out += "用 \"node 运行\" 按钮执行该版本。";
    return CreateString(env, out);
}

// 列出已安装版本
static napi_value NodistList(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::string baseDir = (argc >= 1) ? GetStringArg(env, args[0]) : "";
    std::string versionsDir = baseDir + "/nodist_versions";

    DIR* dir = opendir(versionsDir.c_str());
    if (!dir) {
        return CreateString(env, "尚未安装任何版本(目录不存在: " + versionsDir + ")。\n请先使用 \"nodist + <版本>\" 安装。");
    }

    std::vector<std::string> versions;
    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        if (name.rfind("v", 0) == 0) {
            versions.push_back(name);
        }
    }
    closedir(dir);

    std::sort(versions.begin(), versions.end());
    std::string out;
    if (versions.empty()) {
        out = "尚未安装任何版本。请使用 \"nodist + <版本>\" 安装。";
    } else {
        out = "已安装版本:\n";
        for (const auto& v : versions) {
            out += "  " + v + "\n";
        }
        out += "版本决议: 读取 .node-version 文件,缺省回退 18.16.0";
    }
    return CreateString(env, out);
}

// 运行已安装的 node shim:nodistRun(baseDir, version, argsStr)
static napi_value NodistRun(napi_env env, napi_callback_info info)
{
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return CreateString(env, "[错误] 缺少参数: nodistRun(baseDir, version[, args])");
    }
    std::string baseDir = GetStringArg(env, args[0]);
    std::string version = Trim(GetStringArg(env, args[1]));
    std::string argsStr = (argc >= 3) ? GetStringArg(env, args[2]) : "";

    if (version.empty()) {
        return CreateString(env, "[错误] 请指定要运行的版本号。");
    }
    if (!IsValidVersion(version)) {
        return CreateString(env, "[错误] 版本号不合法(仅允许 数字/字母/./-/_)。");
    }

    std::string scriptPath = baseDir + "/nodist_versions/v" + version + "/node";
    if (access(scriptPath.c_str(), F_OK) != 0) {
        return CreateString(env, "Nodist: 无法执行,版本 v" + version + " 尚未安装。\n请使用 \"nodist + " + version + "\" 进行安装。");
    }

    // 按空白切分参数(简单透传演示)
    std::vector<std::string> argsList;
    std::istringstream iss(argsStr);
    std::string token;
    while (iss >> token) {
        argsList.push_back(token);
    }

    std::string output = RunNodeScript(scriptPath, argsList);
    if (output.find("[execv 失败]") != std::string::npos) {
        output += "\n[提示] 应用沙箱禁止 spawn shell 进程(平台限制)。\n";
        output += "请通过 hdc 使用独立二进制执行:\n";
        output += "  hdc shell /data/local/tmp/nodist + " + version + "\n";
        output += "  hdc shell \"cd /data/local/tmp && ./node --version\"\n";
    }
    return CreateString(env, output);
}

// ==================== 模块注册 ====================

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        {"add", nullptr, Add, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"nodistVersion", nullptr, NodistVersion, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"nodistInstall", nullptr, NodistInstall, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"nodistList", nullptr, NodistList, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"nodistRun", nullptr, NodistRun, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module demoModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "entry",
    .nm_priv = ((void*)0),
    .reserved = { 0 },
};

extern "C" __attribute__((constructor)) void RegisterEntryModule(void)
{
    napi_module_register(&demoModule);
}
