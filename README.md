# QtSpine

基于 Qt6 的 QML Spine 运行时，在 Qt Quick 中原生渲染骨骼动画。

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
| Qt | **6.8+** | 设计基线；已验证 6.12.0；最低可尝试 6.7（未验证） |
| Spine Runtimes (spine-cpp) | **4.2** | 当前适配版本 |
| C++ | **C++20 及以上** | 最低已验证 C++17 |
| CMake | **3.18+** | `FetchContent_Declare` 的 `SOURCE_SUBDIR` 自 3.18 引入 |

> **关于 Qt 6.7**：`QQuickRhiItem` 自 Qt 6.7 引入，但当时为技术预览。`QtSpine/` 子目录不强制 Qt 版本，可自行尝试，但 API 与 6.8 可能存在差异，**未经验证，不保证可编译或可运行**。

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
        // 初始居中；不建议用 anchors.centerIn，会与 DragHandler 冲突
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2

        // width/height 只决定渲染范围（离屏缓冲大小），一般直接跟随 implicitWidth/Height
        width: implicitWidth
        height: implicitHeight

        // skeletonScale 是模型自身的缩放，会同时反映到 implicitWidth/Height
        skeletonScale: 1.0

        // QML 的 scale 只是对整个 item 做普通变换，不影响布局；两者可以叠加
        scale: 1.0

        skeletonSource: "qrc:/shaonv.json"   // 也支持 .skel
        atlasSource: "qrc:/shaonv.atlas"
        animationName: "a2b2a"

        DragHandler { } // 拖拽处理程序
    }
}
```

### 属性说明

| 属性 | 类型 | 读写 | 默认值 | 说明 |
|---|---|---|---|---|
| `skeletonSource` | `url` | 读写 | `""` | Spine 骨骼文件，支持 `.json` 与 `.skel`；与 `atlasSource` 任一为空时卸载资源并清空画面 |
| `atlasSource` | `url` | 读写 | `""` | Spine atlas 文件；纹理路径相对于该 atlas 所在目录解析；与 `skeletonSource` 任一为空时卸载 |
| `animationName` | `string` | 读写 | `""` | 要播放的动画名，在 `track 0` 上循环；为空时清空 `track 0`（停止播放，保持当前姿态） |
| `skeletonScale` | `qreal` | 读写 | `1.0` | 模型显示缩放；影响 `implicitWidth` / `implicitHeight` 和绘制大小，不影响 `skeletonWidth` / `skeletonHeight`。渲染时负值按 `0` 处理 |
| `skeletonWidth` | `qreal` | 只读 | — | skeleton 资源的原始宽度，不含 `skeletonScale` 和 padding；资源加载/卸载时发出 `skeletonWidthChanged` |
| `skeletonHeight` | `qreal` | 只读 | — | skeleton 资源的原始高度；资源加载/卸载时发出 `skeletonHeightChanged` |
| `leftPadding` | `qreal` | 读写 | `0` | 左侧内边距，参与 `implicitWidth` 与内容区居中的计算 |
| `topPadding` | `qreal` | 读写 | `0` | 上侧内边距，参与 `implicitHeight` 与内容区居中的计算 |
| `rightPadding` | `qreal` | 读写 | `0` | 右侧内边距，参与 `implicitWidth` 与内容区居中的计算 |
| `bottomPadding` | `qreal` | 读写 | `0` | 下侧内边距，参与 `implicitHeight` 与内容区居中的计算 |

`implicitWidth` / `implicitHeight` 由上面的值算出，不需要手动赋值：

```text
implicitWidth  = skeletonWidth  * skeletonScale + leftPadding + rightPadding
implicitHeight = skeletonHeight * skeletonScale + topPadding  + bottomPadding
```

四个 padding 同时定义了"内容区"：模型按 `skeletonScale` 缩放后，在 padding 内缩出来的矩形中居中绘制。

### 继承自 QQuickRhiItem 的属性

| 属性 | 说明 |
|---|---|
| `sampleCount` | MSAA 采样数，默认 `1`；变化时会重建渲染管线 |
| `colorBufferFormat` | 离屏颜色缓冲格式（`RGBA8` / `RGBA16F` / `RGBA32F` / `RGB10A2`）；变化时会重建管线 |
| `mirrorVertically` | 垂直翻转，默认 `false` |
| `alphaBlending` | 预乘 Alpha 合成，`SpineItem` 构造时已置为 `true`，不建议关闭 |

### 行为说明

- `width` / `height` 决定渲染范围（离屏缓冲大小），QML 的 `scale` 只是普通变换，两者互不影响。
- 动画在 `track 0` 上循环播放，切换 `animationName` 会替换当前动画。
- 运行中修改 `skeletonSource` / `atlasSource` 会重新加载；把任一置空会卸载当前资源并让画面变空（不会停在上一帧）。
- 纹理在加载时按 atlas 的 `pma` 标志统一转换为预乘 Alpha，渲染端全程按 PMA 处理。
- Tint Black 的暗色数据来自 skeleton 资源，库不额外提供控制接口。
- 支持 Normal / Additive / Multiply / Screen 四种混合模式，由 Spine 渲染命令自动选择。
- 渲染基于 Qt RHI（`QQuickRhiItem`），后端由 Qt 选择（Vulkan / Metal / D3D11 / OpenGL）。
- 只有设备、render pass 格式或采样数变化时才重建管线，纯尺寸变化不重建；顶点/索引缓冲按 2 倍容量增长复用，着色器只解析一次。

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
