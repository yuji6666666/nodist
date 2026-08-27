import { router } from '@kit.ArkUI';

@Entry
@Component
struct Splash {
  aboutToAppear(): void {
    // 启动页停留 1.5 秒后进入主控制台
    setTimeout(() => {
      router.replaceUrl({ url: 'pages/Index' });
    }, 1500);
  }

  build() {
    Column() {
      Image($r('app.media.splash'))
        .width('52%')
        .aspectRatio(1)
        .objectFit(ImageFit.Contain)
    }
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Center)
    .backgroundColor(Color.White)
  }
}
