#version 440

// 顶点布局必须与 SpineNode.cpp 中的 QRhiVertexInputLayout 一致：
//   0: aPos        Float2       offset  0
//   1: aUV         Float2       offset  8
//   2: aColor      UNormByte4   offset 16
//   3: aDarkColor  UNormByte4   offset 20
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aDarkColor;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;      // 预乘后的 light tint
layout(location = 2) out vec4 vDarkColor;  // 只使用 rgb

// 与 C++ 端 80 字节 UBO 对应：mat4(64) + float(4) + 12 字节 padding
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

void main() {
    gl_Position = qt_Matrix * vec4(aPos, 0.0, 1.0);
    vUV = aUV;

    // Spine 4.2 的顶点色按 0xAARRGGBB 打包（见 SkeletonRenderer.cpp）。
    // 小端机器上 UNormByte4 读到的字节序是 (B, G, R, A)，
    // 必须用 .bgra 还原成 (R, G, B, A)，否则红色和蓝色会颠倒。
    vec4 c = aColor.bgra;

    // Qt Quick 使用预乘 alpha：这里预乘一次（含 item opacity）。
    c.a *= qt_Opacity;
    c.rgb *= c.a;
    vColor = c;

    // dark tint 不预乘，只乘一次 opacity。
    // 不要在这里再乘 vColor.a，否则片元里会把 opacity 应用两次。
    vec4 d = aDarkColor.bgra;
    d.rgb *= qt_Opacity;
    vDarkColor = d;
}
