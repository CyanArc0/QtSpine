#version 440

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;      // 预乘后的 light tint
layout(location = 2) in vec4 vDarkColor;  // 未预乘的 dark tint（只用 rgb）

layout(location = 0) out vec4 fragColor;

// 与 SpineRenderer.cpp 中 sampledTexture(1, ...) 对应
layout(binding = 1) uniform sampler2D tex;

void main() {
    // QtTextureLoader 保证纹理是预乘 alpha（PMA）
    vec4 texColor = texture(tex, vUV);

    // 最终 alpha：纹理 alpha × 顶点 alpha（顶点 alpha 已含 item opacity）
    float a = texColor.a * vColor.a;

    // light：PMA 纹理 × PMA 顶点色
    vec3 light = texColor.rgb * vColor.rgb;

    // Spine tint black：纹理里越黑的部分越偏向 dark。
    // 对 PMA 纹理，texColor.a - texColor.rgb == texColor.a * (1 - c)，
    // c 是未预乘的颜色；因此权重来自“黑度”，不是透明度 (1 - texColor.a)。
    vec3 dark = (texColor.a - texColor.rgb) * vDarkColor.rgb;

    fragColor = vec4(light + dark, a);
}
