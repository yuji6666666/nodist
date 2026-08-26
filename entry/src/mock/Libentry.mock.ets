const NativeMock: Record<string, Object> = {
  'add': (a: number, b: number) => {
    return a + b;
  },
  'nodistVersion': () => {
    return 'v0.9.1 (HarmonyOS NDK Port)';
  },
  'nodistInstall': (baseDir: string, version: string) => {
    return `版本 v${version} 安装完成。(mock)\n目标目录: ${baseDir}/nodist_versions/v${version}/node`;
  },
  'nodistList': (baseDir: string) => {
    return `已安装版本:\n  暂无(mock)\n目录: ${baseDir}/nodist_versions`;
  },
  'nodistRun': (baseDir: string, version: string, args: string) => {
    return `[真实 Node.js v${version}](mock) 收到执行指令。参数列表: ${args}`;
  },
};

export default NativeMock;
