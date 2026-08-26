import testNapi from 'libentry.so';
import { KeyCode } from '@kit.InputKit';

@Entry
@Component
struct Index {
  @State versionInput: string = '18.16.0';
  @State runArgsInput: string = '';
  @State consoleOutput: string = '';
  private filesDir: string = '';
  private lastEnterTrigger: number = 0;

  aboutToAppear(): void {
    const hostCtx = this.getUIContext().getHostContext();
    if (hostCtx) {
      this.filesDir = hostCtx.filesDir;
      this.consoleOutput = `[Nodist 控制台就绪]\n安装目录: ${this.filesDir}/nodist_versions\n`;
    } else {
      this.consoleOutput = '[Nodist 控制台就绪]\n获取 filesDir 失败\n';
    }
  }

  private nativeReady(): boolean {
    return testNapi !== undefined && testNapi !== null;
  }

  private appendConsole(line: string): void {
    this.consoleOutput += line + '\n';
  }

  private runAction(label: string, action: () => string): void {
    this.appendConsole(`> ${label}`);
    if (!this.nativeReady()) {
      this.appendConsole('[错误] libentry.so 原生模块未加载\n');
      return;
    }
    try {
      this.appendConsole(action());
    } catch (err) {
      this.appendConsole('[异常] ' + JSON.stringify(err));
    }
    this.appendConsole('');
  }

  // 回车触发防抖:onSubmit 与 onKeyEvent 可能同时命中,避免重复执行
  private enterTrigger(action: () => void): void {
    const now = Date.now();
    if (now - this.lastEnterTrigger < 500) {
      return;
    }
    this.lastEnterTrigger = now;
    action();
  }

  private isEnterDown(event: KeyEvent): boolean {
    // type 0:模拟器/部分输入管线注入的按键事件;KeyType.Down:真实物理键盘
    const isDown = event.type === KeyType.Down || event.type === 0;
    return isDown &&
      (event.keyCode === KeyCode.KEYCODE_ENTER || event.keyCode === KeyCode.KEYCODE_NUMPAD_ENTER);
  }

  private installVersion(): void {
    this.runAction(`nodist + ${this.versionInput}`, () => testNapi.nodistInstall(this.filesDir, this.versionInput));
  }

  private runNode(): void {
    this.runAction(`node ${this.runArgsInput}`, () => testNapi.nodistRun(this.filesDir, this.versionInput, this.runArgsInput));
  }

  build() {
    Column() {
      // 内容容器:手机全宽;PC/平板大屏限制最大宽度并居中(自适应布局)
      Column({ space: 10 }) {
        // ---------- 标题区 ----------
        Text('Nodist 控制台')
          .fontSize(24)
          .fontWeight(FontWeight.Bold)
        Text('Node.js 版本管理器 · HarmonyOS NDK 移植版 v0.9.1')
          .fontSize(13)
          .fontColor(Color.Gray)

        // ---------- 命令区 ----------
        Row({ space: 8 }) {
          TextInput({ text: this.versionInput, placeholder: '版本号' })
            .layoutWeight(1)
            .enterKeyType(EnterKeyType.Go)
            .onChange((value: string) => {
              this.versionInput = value;
            })
            .onSubmit(() => {
              this.enterTrigger(() => this.installVersion());
            })
            .onKeyEvent((event: KeyEvent) => {
              if (this.isEnterDown(event)) {
                this.enterTrigger(() => this.installVersion());
              }
            })
          Button('nodist + 版本')
            .onClick(() => {
              this.installVersion();
            })
        }

        Row({ space: 8 }) {
          Button('nodist -v')
            .onClick(() => {
              this.runAction('nodist -v', () => testNapi.nodistVersion());
            })
          Button('列出已安装')
            .onClick(() => {
              this.runAction('nodist list', () => testNapi.nodistList(this.filesDir));
            })
          Button('NAPI 自检')
            .onClick(() => {
              this.runAction('NAPI 自检: add(2, 3)', () => `add(2, 3) = ${testNapi.add(2, 3)}`);
            })
        }

        Row({ space: 8 }) {
          TextInput({ text: this.runArgsInput, placeholder: 'node 参数,如 --version(回车执行)' })
            .layoutWeight(1)
            .enterKeyType(EnterKeyType.Go)
            .onChange((value: string) => {
              this.runArgsInput = value;
            })
            .onSubmit(() => {
              this.enterTrigger(() => this.runNode());
            })
            .onKeyEvent((event: KeyEvent) => {
              if (this.isEnterDown(event)) {
                this.enterTrigger(() => this.runNode());
              }
            })
          Button('node 运行')
            .onClick(() => {
              this.runNode();
            })
        }

        Divider()

        // ---------- 输出区 ----------
        Text('输出:')
          .fontSize(16)
          .fontWeight(FontWeight.Medium)

        Scroll() {
          Text(this.consoleOutput)
            .fontSize(13)
            .fontColor(Color.Gray)
            .textAlign(TextAlign.Start)
            .width('100%')
            .padding(12)
        }
        .layoutWeight(1)
        .width('100%')
        .align(Alignment.TopStart)
        .scrollBar(BarState.Auto)
        .backgroundColor('#F1F3F5')
        .borderRadius(8)
      }
      .width('100%')
      .height('100%')
      .constraintSize({ maxWidth: 760 })
      .alignSelf(ItemAlign.Center)
      .padding(16)
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#FFFFFF')
  }
}
