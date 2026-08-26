#include <algorithm>
#include <cstdlib>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// 辅助函数：去除字符串前后的空格和换行符
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \n\r\t");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \n\r\t");
    return str.substr(first, (last - first + 1));
}

// 版本号白名单校验（与 napi_init.cpp 保持一致）：
// 仅允许 数字/字母/./-/_，必须包含数字，长度 <= 32。
// 防止路径穿越（"../.."）与 shim 脚本内容注入。
bool isValidVersion(const std::string& version) {
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

// 1:1 还原：动态版本决议
std::string resolveVersion() {
    // 优先读取当前工作目录下的 .node-version 文件
    std::ifstream file(".node-version");
    std::string version;
    if (file.is_open()) {
        std::getline(file, version);
        file.close();
        version = trim(version);
        // 去掉 UTF-8 BOM（\xEF\xBB\xBF）与可选的 "v" 前缀
        if (version.size() >= 3 && version.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            version = version.substr(3);
        }
        if (!version.empty() && version[0] == 'v') {
            version = version.substr(1);
        }
        if (isValidVersion(version)) {
            return version;
        }
        std::cerr << "Nodist: 警告：.node-version 内容无效（" << version
                  << "），使用默认版本。" << std::endl;
    }
    // TODO: 1:1 还原中，这里应该读取全局环境变量 NODIST_VERSION，为方便测试暂定 fallback
    return "18.16.0";
}

// 1:1 还原：真实的进程劫持与参数透传
void runShim(int argc, char* argv[]) {
    std::string version = resolveVersion();
    std::string realNodePath = "/data/local/tmp/nodist_versions/v" + version + "/node";

    // 检查真实二进制文件是否存在
    if (access(realNodePath.c_str(), F_OK) != 0) {
        std::cout << "Nodist: 无法执行，版本 v" << version << " 尚未安装。请使用 nodist + " << version << " 进行安装。" << std::endl;
        exit(1);
    }

    // 核心操作：将 argv[0] (原本是假 node 的路径) 替换为真 node 的路径
    // 使用独立缓冲区，避免 std::string 内部缓冲与 const_cast 带来的生命周期/UB 风险
    std::vector<char> realNodePathBuf(realNodePath.begin(), realNodePath.end());
    realNodePathBuf.push_back('\0');
    argv[0] = realNodePathBuf.data();

    // 触发系统级进程替换；execv 成功后当前进程被替换，缓冲区随之消失（无生命周期问题）
    execv(realNodePath.c_str(), argv);

    // 只有当 execv 失败时才会走到这里
    std::cerr << "Nodist: 致命错误，进程替换 (execv) 失败，错误码: " << errno << std::endl;
    exit(1);
}

// 基础的工具菜单
void printHelp() {
    std::cout << "Nodist (鸿蒙适配版) - Node.js 版本管理器\n";
    std::cout << "用法:\n";
    std::cout << "  nodist -v                 查看当前 Nodist 版本\n";
    std::cout << "  nodist + <version>        安装指定版本的 Node.js\n";
    std::cout << "  nodist -h / --help        显示本帮助\n";
}

bool createVersionDirectory(const std::string& version, std::string& outTargetDir) {
    std::string baseDir = "/data/local/tmp/nodist_versions";
    outTargetDir = baseDir + "/v" + version;
    mkdir(baseDir.c_str(), 0777);
    if (mkdir(outTargetDir.c_str(), 0777) == 0 || errno == EEXIST) {
        return true;
    }
    return false;
}

bool simulateDownload(const std::string& version, const std::string& targetDir) {
    std::string exePath = targetDir + "/node";
    std::ofstream outfile(exePath);
    if (outfile.is_open()) {
        outfile << "#!/bin/sh\n";
        // 打印所有接收到的参数，用来验证参数透传是否成功
        outfile << "echo \"[真实 Node.js v" << version << "] 收到执行指令。参数列表: $@\"\n";
        outfile.close();
        chmod(exePath.c_str(), 0777);
        return true;
    }
    return false;
}

int main(int argc, char* argv[]) {
    // 1:1 还原：识别触发源。如果是 node 唤起的，直接进入 Shim 逻辑
    std::string programName = argv[0];
    if (programName.find("node") != std::string::npos && programName.find("nodist") == std::string::npos) {
        runShim(argc, argv);
        return 0; // Shim 成功后不会执行到这里
    }

    // 否则执行 Nodist 本身的管理命令
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    if (args.empty()) {
        printHelp();
        return 0;
    }

    std::string command = args[0];

    if (command == "-v" || command == "--version") {
        std::cout << "v0.9.1 (HarmonyOS NDK Port)" << std::endl;
    } else if (command == "-h" || command == "--help") {
        printHelp();
    } else if (command == "+") {
        if (args.size() < 2) {
            std::cout << "错误: 请指定要安装的版本号。" << std::endl;
        } else {
            std::string targetVersion = trim(args[1]);
            if (!isValidVersion(targetVersion)) {
                std::cout << "错误: 版本号不合法(仅允许 数字/字母/./-/_)。" << std::endl;
                return 1;
            }
            std::string targetDir;
            if (createVersionDirectory(targetVersion, targetDir)) {
                if (simulateDownload(targetVersion, targetDir)) {
                    std::cout << "版本 v" << targetVersion << " 安装完成。" << std::endl;
                } else {
                    std::cout << "错误: 写入 shim 脚本失败。" << std::endl;
                    return 1;
                }
            } else {
                std::cout << "错误: 创建版本目录失败。" << std::endl;
                return 1;
            }
        }
    } else {
        std::cout << "未知命令: " << command << std::endl;
        printHelp();
        return 1;
    }

    return 0;
}
