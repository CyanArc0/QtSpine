#pragma once
#include<stddef.h>
// UBO 布局：必须与 spine.vert 里 std140 的 buf 块一致
struct alignas(16) SpineUniforms {
    float matrix[16];   // qt_Matrix（投影 × 模型视图）
    float opacity;      // qt_Opacity
};
static_assert(sizeof(SpineUniforms) == 80, "UBO layout must match the std140 block in spine.vert");
static_assert(offsetof(SpineUniforms, opacity) == 64, "opacity must be at offset 64; do not insert members before it");