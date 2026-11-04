#version 440

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec4 vDarkColor;

layout(location = 0) out vec4 fragColor;

// 注意：若你没有重写 updateSampledImage() 把纹理绑到 unit 1，
// Qt Quick 默认绑 unit 0，这里必须改成 binding = 0（或不写 binding）。
layout(binding = 1) uniform sampler2D tex;

void main() {
    vec4 texColor = texture(tex, vUV);   // PMA 纹理

    // Spine tint black：纹理越透明（a 越小）dark 权重越大
    // 再乘 vColor.a 保证整体 opacity 淡出时 dark 一起淡出（维持 PMA 上界）
    float darkWeight = vDarkColor.a * (1.0 - texColor.a) * vColor.a;

    fragColor.a   = texColor.a * vColor.a;
    fragColor.rgb = texColor.rgb * vColor.rgb        // 主色（PMA，已是预乘乘积）
                  + vDarkColor.rgb * darkWeight;     // 暗部贡献
}