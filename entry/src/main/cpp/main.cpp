#include <iostream>
#include <string>
#include <vector>
#include <sys/stat.h> 
#include <unistd.h>   
#include <errno.h>    
#include <fstream>
#include <algorithm>

// 辅助函数：去除字符串前后的空格和换行符
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \n\r\t");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \n\r\t");
    return str.substr(first, (last - first + 1));
}

// 1:1 还原：动态版本决议
std::string resolveVersion() {
    // 优先读取当前工作目录下的 .node-version 文件
    std::ifstream file(".node-version");
    std::string version;
    if (file.is_open()) {
        std::getline(file, version);
        file.close();
        return trim(version);
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
    // 其他参数 (argv[1], argv[2]...) 保持完全不动，实现 100% 透传
    argv[0] = (char*)realNodePath.c_str();

    // 触发系统级进程替换
    execv(realNodePath.c_str(), argv);

    // 只有当 execv 失败时才会走到这里
    std::cerr << "Nodist: 致命错误，进程替换 (execv) 失败，错误码: " << errno << std::endl;
    exit(1);
}

// 基础的工具菜单 (保持原样)
void printHelp() {
    std::cout << "Nodist (鸿蒙适配版) - Node.js 版本管理器\n";
    std::cout << "用法:\n";
    std::cout << "  nodist -v                 查看当前 Nodist 版本\n";
    std::cout << "  nodist + <version>        安装指定版本的 Node.js\n";
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
    } 
    else if (command == "+") {
        if (args.size() < 2) {
            std::cout << "错误: 请指定要安装的版本号。" << std::endl;
        } else {
            std::string targetVersion = args[1];
            std::string targetDir;
            if (createVersionDirectory(targetVersion, targetDir)) {
                simulateDownload(targetVersion, targetDir);
                std::cout << "版本 v" << targetVersion << " 安装完成。" << std::endl;
            }
        }
    }

    return 0;
}