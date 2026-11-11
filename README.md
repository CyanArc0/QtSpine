# QtSpine

基于 Qt6 的 QML Spine 运行时，在 Qt Quick 中原生渲染骨骼动画。

渲染直接录制在 Qt Quick 自己的 render pass 里（`QSGRenderNode`），不经过离屏纹理，因此混合语义、缩放清晰度和 `opacity` 行为都与 spine 官方运行时一致。

## 预览

在线演示（WebAssembly 构建）：<https://CyanArc0.github.io/Columbina/>

<table width="100%">
<tr>
<td align="center" valign="middle" width="60%">
<img src="preview/Windows.gif" width="100%"><br>
Windows
</td>
<td align="center" valign="middle" width="30%">
<img src="preview/Android.jpg" width="100%"><br>
Android
</td>
</tr>
</table>

本地构建仓库根目录的 QtSpineExample 示例工程，可查看完整效果。

## 版本支持

| 组件 | 版本 | 说明 |
|---|---|---|
| Qt | **6.8+** | 设计基线；已验证 6.12.0 |
| Spine Runtimes (spine-cpp) | **4.2** | 当前适配版本 |
| C++ | **C++20 及以上** | 最低已验证 C++17 |
| CMake | **3.18+** | `FetchContent_Declare` 的 `SOURCE_SUBDIR` 自 3.18 引入 |

> **关于 Qt 版本**：渲染基于 `QSGRenderNode`，其中 `renderTarget()` / `commandBuffer()` / `QRhiRenderPassDescriptor::serializedFormat()` 等接口自 Qt 6.6 起提供，因此理论最低版本为 6.6，但只在 6.8+ 上验证过。

> **关于 C++17**：`[[unlikely]]` 是 C++20 属性。C++17 标准规定，实现未识别的属性应被忽略（[dcl.attr.grammar]/6），因此不会因其产生编译错误；实现可以给出属性警告。

## 通过 CMake 引入

> 首次配置时，CMake 会通过 FetchContent 拉取并构建 `spine-cpp`，耗时可能较长（视网络和机器而定）。之后的构建会复用缓存，不再重复下载。

库本体位于仓库的 `QtSpine/` 子目录，自包含。仓库根目录是示例工程，使用库时**不需要**复制示例代码和资源。

### 方式一：复制 `QtSpine/` 子目录（推荐）

把 `QtSpine/` 复制到项目中。

在默认 `CMakeLists.txt` 中插入两处：

```cmake
add_subdirectory(QtSpine)

# ...

target_link_libraries(appYourProject PRIVATE QtSpine)
```

### 方式二：FetchContent 拉取整个仓库

如果不想手动复制目录，也可以让 CMake 从 GitHub 拉取本仓库。

```cmake
include(FetchContent)
FetchContent_Declare(
    QtSpine
    GIT_REPOSITORY https://github.com/CyanArc0/QtSpine.git
    GIT_TAG main
    SOURCE_SUBDIR QtSpine
)
FetchContent_MakeAvailable(QtSpine)

# ...

target_link_libraries(appYourProject PRIVATE QtSpine)
```

### 关于异常与 RTTI（推荐）

spine-cpp 默认禁用异常和 RTTI。为保证头文件宏与库二进制一致、并让跨平台行为统一，**推荐**在引入 QtSpine 时一并引入仓库提供的 `disable-Exceptions.cmake` 与 `disable-RTTI.cmake`。

```cmake
include(disable-Exceptions.cmake)
include(disable-RTTI.cmake)
```

不引入通常也能编译，但部分平台可能编译失败。

### 插入位置说明

下面以 Qt Creator 默认生成的 Qt Quick 工程为基准。
```cmake
cmake_minimum_required(VERSION 3.18)

project(YourProject VERSION 1.0 LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Quick)

# ① 推荐：在引入 QtSpine 之前禁用异常与 RTTI
include(disable-Exceptions.cmake)
include(disable-RTTI.cmake)

qt_standard_project_setup(REQUIRES 6.8)

# ② 引入 QtSpine：方式一，本地子目录
add_subdirectory(QtSpine)

# ② 引入 QtSpine：方式二，FetchContent 拉取（与上一行二选一）
# include(FetchContent)
# FetchContent_Declare(
#     QtSpine
#     GIT_REPOSITORY https://github.com/CyanArc0/QtSpine.git
#     GIT_TAG main
#     SOURCE_SUBDIR QtSpine
# )
# FetchContent_MakeAvailable(QtSpine)

qt_add_executable(appYourProject
    main.cpp
)

qt_add_qml_module(appYourProject
    URI YourProject
    QML_FILES
        Main.qml
)

target_link_libraries(appYourProject
    PRIVATE Qt6::Quick
    QtSpine # ③ 无论用哪种引入方式，都需要链接 QtSpine
)
```

## 在 QML 中使用

CMake 引入后，QML 中即可 `import QtSpine` 并使用 `SpineItem`。

```qml
import QtQuick
import QtSpine

Window {
    width: 1920
    height: 1080
    visible: true

    SpineItem {
        id: spine
        anchors.centerIn: parent

        skeletonSource: "qrc:/shaonv.json"   // 也支持 .skel
        atlasSource: "qrc:/shaonv.atlas"
        animationName: "a2b2a"

        skeletonScale: 1.0
        scale: 1.0            // QML 的 scale 只是普通变换，不影响布局

        DragHandler { }       // 拖拽处理程序
    }
}
```

### 属性说明

| 属性 | 类型 | 读写 | 默认值 | 说明 |
|---|---|---|---|---|
| `skeletonSource` | `url` | 读写 | `""` | Spine 骨骼文件，支持 `.json` 与 `.skel`；与 `atlasSource` 任一为空时卸载资源并清空画面 |
| `atlasSource` | `url` | 读写 | `""` | Spine atlas 文件；纹理路径相对于该 atlas 所在目录解析；与 `skeletonSource` 任一为空时卸载 |
| `animationName` | `string` | 读写 | `""` | 在 `track 0` 上播放的动画名，循环与否由 `loop` 决定；为空时清空 `track 0`（停止播放，保持当前姿态） |
| `paused` | `bool` | 读写 | `false` | 暂停后不再推进动画，也不请求重绘；恢复时不会把暂停时长计入 delta |
| `timeScale` | `qreal` | 读写 | `1.0` | 播放速度倍率，作用在 `AnimationState` 上 |
| `loop` | `bool` | 读写 | `true` | `animationName` / `playAnimation(name)` 的循环开关；修改时立即作用到 `track 0` 当前动画 |
| `mixDuration` | `qreal` | 读写 | `0.0` | 切换动画时的混入时长（秒），`0` 为直接切换。只影响之后的切换，已在进行的混合不受影响 |
| `skin` | `string` | 读写 | `""` | 皮肤名；空串为默认皮肤。资源加载前设置会在加载后自动套用，找不到时告警并保持默认 |
| `skinNames` | `list<string>` | 只读 | — | 资源里的皮肤名列表；加载/卸载时发 `skinNamesChanged` |
| `animationNames` | `list<string>` | 只读 | — | 资源里的动画名列表；加载/卸载时发 `animationNamesChanged` |
| `skeletonColor` | `color` | 读写 | `white` | 整体着色，等价于 `Skeleton::setColor`（会乘进顶点色） |
| `contentRect` | `rect` | 只读 | — | 当前姿态在 **item 局部坐标**里的包围盒；每帧推进后发 `contentRectChanged` |
| `currentTime` | `qreal` | 读写 | `0` | `track 0` 的播放时间（秒）；写入即 seek 并立刻应用一帧姿态（暂停时也生效） |
| `skeletonScale` | `qreal` | 读写 | `1.0` | 模型显示缩放；影响 `implicitWidth` / `implicitHeight` 和绘制大小，不影响 `skeletonWidth` / `skeletonHeight`。负值按 `0` 处理 |
| `skeletonWidth` | `qreal` | 只读 | — | skeleton 资源的原始宽度，不含 `skeletonScale` |
| `skeletonHeight` | `qreal` | 只读 | — | skeleton 资源的原始高度 |

`implicitWidth` / `implicitHeight` 由资源尺寸算出，不需要手动赋值：

```text
implicitWidth  = skeletonWidth  * skeletonScale
implicitHeight = skeletonHeight * skeletonScale
```

模型按 `skeletonScale` 缩放后**居中于 item**；需要外边距时用外层 `Item` 或显式 `width`/`height` 表达。渲染不经过离屏纹理，**内容超出 item 矩形不会被裁掉**，只有自身或祖先的 `clip` 才裁剪。

### 方法与信号

| 名字 | 说明 |
|---|---|
| `play()` / `pause()` | 等价于 `paused = false / true` |
| `playAnimation(name)` | 在 `track 0` 上播放，`loop` 取属性值 |
| `playAnimation(name, track, loop)` | 在指定轨道播放；找不到同名动画时只告警，不影响当前播放 |
| `addAnimation(name, delay, loop)` | 在 `track 0` 当前动画之后排队，`delay` 秒后开始（单位秒） |
| `finished(track)` | 非循环动画播放完毕。spine 对循环动画每完成一圈也会发 `complete`，这里已过滤 |
| `eventFired(name, intValue, floatValue, stringValue)` | spine 事件时间轴上的事件（脚步、攻击判定等） |
| `errorOccurred(message)` | 所有可恢复错误：加载/解析失败、找不到动画名、找不到皮肤名。`message` 是可直接展示给人看的文案（含出错对象和候选列表），QML 用它决定弹窗还是记日志。同一条错误只上报一次；信号延迟到下一轮事件循环发出，所以声明式的 `onErrorOccurred` 也能收到 |

`finished` / `eventFired` 是在 `advance()` 内部（GUI 线程）发出的，可以直接在处理器里切换动画：

```qml
SpineItem {
    id: spine
    skeletonSource: "qrc:/hero.json"
    atlasSource: "qrc:/hero.atlas"
    mixDuration: 0.2

    onFinished: (track) => spine.playAnimation("idle", 0, true)
    onEventFired: (name, intValue, floatValue, stringValue) => console.log(name, intValue)
    onErrorOccurred: (message) => console.warn("spine:", message)   // 也可以弹窗
}
```

### 行为说明

- 动画由 60 Hz 定时器推进，单帧 delta 上限 `1/30` 秒（掉帧时动画会变慢而不是跳帧）。
- `skeletonSource` / `atlasSource` 任一置空都会卸载资源，画面立即变空，不会停在上一帧。
- 纹理按 atlas 的 `pma` 标志统一转成预乘 Alpha，渲染端全程按 PMA 处理。
- 支持 Normal / Additive / Multiply / Screen 四种混合模式，混合因子对齐 spine 官方 GL/WebGL 实现，且混合目标就是场景里真实的背景。
- `opacity` 通过场景图的 `inheritedOpacity()` 乘进顶点色；`clip` 通过 scissor 生效。
- 只有设备、render pass 格式或采样数变化时才重建管线；纯尺寸变化不重建；着色器只解析一次。
- 顶点/索引缓冲按 2 倍容量增长复用，裁剪动画逐帧改变顶点数时不会反复重建。

### 已知限制

- **不要在 `SpineItem` 或它的祖先上开 `layer.enabled` / `ShaderEffect`**：一旦被 Qt 渲染进 FBO，就又变成离屏合成，Additive / Multiply / Screen 会退化。这是离屏合成本身的限制，与 QtSpine 无关。
- atlas 里的 `filter` / `repeat` 字段目前被忽略：采样器统一为 `Linear` + mipmap + `ClampToEdge`（与 spine 官方 GL 示例一致）。像素画资源若出现模糊，请在导出时放大 atlas。
- Tint Black 的暗色项不做顶点 alpha 预乘（与 spine-cpp 官方 GL 示例一致；spine-ts 会乘）。
- 动画时间轴目前只暴露 `track 0` 的读写（`currentTime`、`animationName`），其它轨道只能通过 `playAnimation(name, track, loop)` 播放。

## 许可证与合规

QtSpine 以 **LGPL v3** 发布。完整授权文本由 [`COPYING`](./COPYING)（GNU GPL v3）与 [`COPYING.LESSER`](./COPYING.LESSER)（GNU LGPL v3）共同构成。

使用和分发时需注意三层独立义务：

1. **QtSpine 自身（LGPL v3）**：修改 QtSpine 源代码后，修改部分需以 LGPL v3 开源；未修改使用则专有代码无需开源。
2. **Qt（LGPL v3）**：QtSpine 当前构建为静态库。若你静态链接 Qt，需要向接收方提供可重新链接的材料（如目标文件）；改为动态链接 Qt 可以显著降低合规成本。
3. **spine-cpp（独立许可）**：**使用 QtSpine 需要你自行持有 Spine 许可证。** 如果向不持有 Spine 许可证的人分发包含 spine-cpp 的构建产物，分发者需要在分发时持有相应许可证。

第三方组件与版权声明：

- **Qt 6** — LGPL v3，Copyright (C) The Qt Company Ltd. and other contributors。许可证全文见 <https://www.gnu.org/licenses/lgpl-3.0.html>
- **Spine Runtimes (spine-cpp) 4.2** — Spine Runtimes License Agreement，Copyright (C) Esoteric Software LLC。条款见 <http://esotericsoftware.com/spine-runtimes-license>

**使用 QtSpine 构建的软件，最终用户需要自行持有有效的 Spine 许可证。**

## 致谢

- [Esoteric Software Spine Runtimes](https://github.com/esotericsoftware/spine-runtimes)
- [Qt](https://www.qt.io/)
- [UWillno/spine-qt](https://github.com/UWillno/spine-qt) — 早期实现参考
