#version 440

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aDarkColor;   // Tint Black
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec4 vDarkColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

void main() {
    gl_Position = qt_Matrix * vec4(aPos, 0.0, 1.0);
    vUV = aUV;

    // 顶点色预乘（PMA）
    vec4 c = aColor;
    c.a *= qt_Opacity;
    c.rgb *= c.a;
    vColor = c;

    // 暗色只影响 RGB，不预乘
    vec4 d = aDarkColor;
    d.rgb *= qt_Opacity;
    vDarkColor = d;
}