#include"SpineItem.h"
#include<QByteArray>
#include<QColor>
#include<QMatrix4x4>
#include<QResource>
#include<algorithm>
#include<atomic>
#include<cstring>
#include<cstddef>
#include<cstdint>
#include<rhi/qshader.h>
#include"error_handling.hpp"

// ─────────────────────────────────────────────────────────────
// 内部工具
// ─────────────────────────────────────────────────────────────

namespace {

// UBO 布局：必须与 spine.vert 里 std140 的 buf 块一致
struct SpineUniforms {
    float matrix[16];   // qt_Matrix
    float opacity;      // qt_Opacity
    float pad[3];       // std140：整个块对齐到 16 字节
};
static_assert(sizeof(SpineUniforms) == 80, "UBO layout must match the std140 block in spine.vert");
constexpr quint32 UniformsSize = static_cast<quint32>(sizeof(SpineUniforms));

// 失败时返回非法 QShader；调用方的 static 缓存保证只告警一次
QShader loadShader(const QString &path)
{
    const QResource resource(path);
    if(!resource.isValid() || resource.size() == 0)[[unlikely]] {
        qWarning() << "Spine RHI: shader resource" << path << "is missing or empty; rendering disabled";
        return QShader();
    }

    const QByteArray data(reinterpret_cast<const char*>(resource.data()), resource.size());
    QShader shader = QShader::fromSerialized(data);
    if(!shader.isValid())[[unlikely]] {
        qWarning() << "Spine RHI: shader" << path << "is not a valid QShader; rendering disabled";
        return QShader();
    }
    return shader;
}

// 着色器只解析一次：pipeline 会因为 render target 变化被反复重建
const QShader &vertexShader()
{
    static const QShader shader = loadShader(QStringLiteral(":/shaders/spine.vert.qsb"));
    return shader;
}

const QShader &fragmentShader()
{
    static const QShader shader = loadShader(QStringLiteral(":/shaders/spine.frag.qsb"));
    return shader;
}

} // namespace

// ─────────────────────────────────────────────────────────────
// initialize：只有"设备 / render pass 格式 / 采样数"三者都没变时才复用
// ─────────────────────────────────────────────────────────────

void SpineItem::SpineRenderer::initialize(QRhiCommandBuffer*)
{
    QRhi* currentRhi = rhi();
    QRhiRenderTarget* rt = renderTarget();
    Q_ASSERT(currentRhi && rt);

    QRhiRenderPassDescriptor* rpd = rt->renderPassDescriptor();
    Q_ASSERT(rpd);

    // 问 pipeline 自己拿它创建时的 RPD 与采样数：不额外保存状态，也不用去碰
    // 可能已被 Qt reset 掉的 RPD 对象（这里只比较指针值，不解引用）。
    const QRhiGraphicsPipeline* reference = m_pipelineNormal.get();
    const bool pipelineReady = reference
        && reference->renderPassDescriptor() == rpd
        && reference->sampleCount() == rt->sampleCount();
    const bool baseReady = m_sampler && m_ubo && m_dummySrb;

    if(m_rhi == currentRhi && pipelineReady && baseReady)
        return;

    if(m_rhi != currentRhi) {
        // 换了设备：旧对象都属于上一个 QRhi，按"引用方 → 被引用方"倒序拆除
        m_pipelineScreen.reset();
        m_pipelineMultiply.reset();
        m_pipelineAdditive.reset();
        m_pipelineNormal.reset();
        m_dummySrb.reset();

        m_srbCache.clear();
        m_textureCache.clear();

        m_ibuf.reset();
        m_vbuf.reset();

        m_ubo.reset();
        m_sampler.reset();

        m_rhi = currentRhi;
    }

    // ── sampler / UBO / dummy SRB 与 render target 无关，只在缺失时补建 ──
    // 创建失败不终止进程：告警一次，本 item 空渲染，下次 sync 再试。
    if(!m_sampler) {
        m_sampler.reset(m_rhi->newSampler(
            QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
            QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if(!m_sampler)[[unlikely]]
            qFatal("Spine RHI: out of memory while creating sampler");
        if(!m_sampler->create())[[unlikely]] {
            qWarning("Spine RHI: failed to create sampler; rendering disabled");
            m_sampler.reset();
            return;
        }
    }

    if(!m_ubo) {
        m_ubo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, UniformsSize));
        if(!m_ubo)[[unlikely]]
            qFatal("Spine RHI: out of memory while creating uniform buffer");
        if(!m_ubo->create())[[unlikely]] {
            qWarning("Spine RHI: failed to create uniform buffer; rendering disabled");
            m_ubo.reset();
            return;
        }
    }

    if(!m_dummySrb) {
        // 只是给 pipeline 提供布局，绑定的资源无所谓（见 qrhi.cpp:7481 的说明），
        // 真正绘制时换成 getOrCreateSrb() 建的那份。
        m_dummySrb.reset(m_rhi->newShaderResourceBindings());
        if(!m_dummySrb)[[unlikely]]
            qFatal("Spine RHI: out of memory while creating shader resource bindings");

        m_dummySrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, nullptr, nullptr)
        });
        if(!m_dummySrb->create())[[unlikely]] {
            qWarning("Spine RHI: failed to create shader resource bindings; rendering disabled");
            m_dummySrb.reset();
            return;
        }
    }

    // ── pipeline 绑定了 RPD 与采样数，格式变了一定要重建 ──
    m_pipelineScreen.reset();
    m_pipelineMultiply.reset();
    m_pipelineAdditive.reset();
    m_pipelineNormal.reset();

    // 链式创建：任何一条失败就停手，四条全部作废。
    // render() 只以 m_pipelineNormal 判断"能不能画"，不存在半残状态。
    m_pipelineNormal = createPipeline(spine::BlendMode_Normal);
    if(m_pipelineNormal)
        m_pipelineAdditive = createPipeline(spine::BlendMode_Additive);
    if(m_pipelineAdditive)
        m_pipelineMultiply = createPipeline(spine::BlendMode_Multiply);
    if(m_pipelineMultiply)
        m_pipelineScreen = createPipeline(spine::BlendMode_Screen);

    if(!m_pipelineScreen) {
        m_pipelineNormal.reset();
        m_pipelineAdditive.reset();
        m_pipelineMultiply.reset();
    }
}

// ─────────────────────────────────────────────────────────────
// createPipeline：失败返回 nullptr（告警一次），由 initialize 统一处理
// ─────────────────────────────────────────────────────────────

std::unique_ptr<QRhiGraphicsPipeline> SpineItem::SpineRenderer::createPipeline(spine::BlendMode blendMode)
{
    QRhiRenderTarget* rt = renderTarget();
    Q_ASSERT(rt && rt->renderPassDescriptor());
    Q_ASSERT(m_rhi && m_dummySrb);

    const QShader &vs = vertexShader();
    const QShader &fs = fragmentShader();
    if(!vs.isValid() || !fs.isValid())
        return nullptr;   // 加载着色器时已经告警过

    auto pipeline = std::unique_ptr<QRhiGraphicsPipeline>(m_rhi->newGraphicsPipeline());
    if(!pipeline)[[unlikely]]
        qFatal("Spine RHI: out of memory while creating graphics pipeline");

    // 顶点布局必须与 SpineVertex 和 spine.vert 的 layout(location) 一致
    QRhiVertexInputLayout layout;
    layout.setBindings({ QRhiVertexInputBinding{ sizeof(SpineVertex) } });
    layout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2,     offsetof(SpineVertex, x) },
        { 0, 1, QRhiVertexInputAttribute::Float2,     offsetof(SpineVertex, u) },
        { 0, 2, QRhiVertexInputAttribute::UNormByte4, offsetof(SpineVertex, argb) },
        { 0, 3, QRhiVertexInputAttribute::UNormByte4, offsetof(SpineVertex, dark_argb) },
    });
    pipeline->setVertexInputLayout(layout);

    pipeline->setShaderStages({
        { QRhiShaderStage::Vertex,   vs },
        { QRhiShaderStage::Fragment, fs },
    });

    pipeline->setShaderResourceBindings(m_dummySrb.get());

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    switch(blendMode) {
    case spine::BlendMode_Additive:
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::One;
        blend.srcAlpha = QRhiGraphicsPipeline::Zero;
        blend.dstAlpha = QRhiGraphicsPipeline::One;
        break;
    case spine::BlendMode_Multiply:
        blend.srcColor = QRhiGraphicsPipeline::DstColor;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;   // 对齐 GLFW/TS
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        break;
    case spine::BlendMode_Screen:
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcColor;
        blend.srcAlpha = QRhiGraphicsPipeline::OneMinusSrcColor;   // 对齐 GLFW/TS
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcColor;
        break;
    default:
        // Normal：预乘 alpha 混合，src = One 正好是 QRhi 的默认值
        break;
    }
    pipeline->setTargetBlends({ blend });
    pipeline->setSampleCount(rt->sampleCount());
    pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    pipeline->setRenderPassDescriptor(rt->renderPassDescriptor());

    if(!pipeline->create())[[unlikely]] {
        qWarning("Spine RHI: failed to create graphics pipeline; rendering disabled");
        return nullptr;
    }

    return pipeline;
}

// ─────────────────────────────────────────────────────────────
// 纹理：指针由 m_textureCache 持有；失败时缓存 nullptr，避免每帧重试
// ─────────────────────────────────────────────────────────────

QRhiTexture* SpineItem::SpineRenderer::getOrCreateTexture(QtTextureLoader::Texture* texKey, QRhiResourceUpdateBatch* rub)
{
    Q_ASSERT(texKey && rub);

    auto it = m_textureCache.find(texKey);
    if(it != m_textureCache.end())
        return it->second.get();   // 可能为 nullptr：上次就失败了

    // 输入非法（atlas 里的图片打不开）：跳过引用它的批次
    if(texKey->isNull())[[unlikely]] {
        qWarning("Spine RHI: atlas image is null; the batch referencing it is skipped");
        m_textureCache[texKey] = nullptr;
        return nullptr;
    }

    QRhiTexture* texture = m_rhi->newTexture(QRhiTexture::RGBA8, texKey->size(), 1,
        QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips);
    if(!texture)[[unlikely]]
        qFatal("Spine RHI: out of memory while creating %dx%d texture", texKey->width(), texKey->height());

    if(!texture->create())[[unlikely]] {
        qWarning("Spine RHI: failed to create atlas texture; the batch referencing it is skipped");
        delete texture;
        m_textureCache[texKey] = nullptr;
        return nullptr;
    }

    rub->uploadTexture(texture, *texKey);
    rub->generateMips(texture);

    m_textureCache.emplace(texKey, std::unique_ptr<QRhiTexture>(texture));
    return texture;
}

// ─────────────────────────────────────────────────────────────
// SRB：指针由 m_srbCache 持有；失败时缓存 nullptr，避免每帧重试
// ─────────────────────────────────────────────────────────────

QRhiShaderResourceBindings* SpineItem::SpineRenderer::getOrCreateSrb(QtTextureLoader::Texture* texKey, QRhiTexture* texture)
{
    Q_ASSERT(texKey && texture && m_ubo && m_sampler);

    auto it = m_srbCache.find(texKey);
    if(it != m_srbCache.end())
        return it->second.get();   // 可能为 nullptr：上次就失败了

    QRhiShaderResourceBindings* srb = m_rhi->newShaderResourceBindings();
    if(!srb)[[unlikely]]
        qFatal("Spine RHI: out of memory while creating shader resource bindings");

    srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, texture, m_sampler.get())
    });
    if(!srb->create())[[unlikely]] {
        qWarning("Spine RHI: failed to create shader resource bindings; the batch referencing it is skipped");
        delete srb;
        m_srbCache[texKey] = nullptr;
        return nullptr;
    }

    m_srbCache.emplace(texKey, std::unique_ptr<QRhiShaderResourceBindings>(srb));
    return srb;
}

// ─────────────────────────────────────────────────────────────
// synchronize：GUI 线程被阻塞，这里把这一帧要用的数据全部拷进渲染端
// ─────────────────────────────────────────────────────────────

void SpineItem::SpineRenderer::synchronize(QQuickRhiItem* item)
{
    Q_ASSERT(item);
    SpineItem* spineItem = static_cast<SpineItem*>(item);

    if(spineItem->texturesNeedReset) {
        // 纹理换了：SRB 引用旧纹理，必须一起清掉
        m_srbCache.clear();
        m_textureCache.clear();
        spineItem->texturesNeedReset = false;
    }

    if(!spineItem->skeleton_ptr || !spineItem->skeletonData_ptr) {
        m_renderCommands = nullptr;   // 没有骨架 → render() 只清屏
        return;
    }

    m_renderCommands = spineItem->skeletonRenderer.render(*spineItem->skeleton_ptr);
    if(!m_renderCommands) return;     // 空骨架（没有可画槽位）→ 同样只清屏

    m_projectionSize = { static_cast<float>(spineItem->width()), static_cast<float>(spineItem->height()) };
    if(m_projectionSize.x() <= 0.0f || m_projectionSize.y() <= 0.0f) {
        m_renderCommands = nullptr;   // 尺寸退化时 ortho 会除零，按"没有内容"处理
        return;
    }

    // 内容区（padding 内缩）的中心作为骨架的锚点
    const float padL = static_cast<float>(spineItem->leftPadding);
    const float padT = static_cast<float>(spineItem->topPadding);
    const float padR = static_cast<float>(spineItem->rightPadding);
    const float padB = static_cast<float>(spineItem->bottomPadding);

    const float anchorX = padL + (m_projectionSize.x() - padL - padR) * 0.5f;
    const float anchorY = padT + (m_projectionSize.y() - padT - padB) * 0.5f;

    // 负值会把模型翻面，clamp 掉
    const float s = std::max(0.0f, static_cast<float>(spineItem->skeletonScale));

    spine::SkeletonData* data = spineItem->skeletonData_ptr.get();
    const float skelX = data->getX();
    const float skelY = data->getY();

    // 骨架左上角对齐到内容区中心；y 轴翻转在顶点阶段用负号完成
    m_vertexScale = s;
    m_vertexTranslate = {
        anchorX - data->getWidth() * s * 0.5f - skelX * s,
        anchorY + data->getHeight() * s * 0.5f + skelY * s
    };
}

// ─────────────────────────────────────────────────────────────
// render：每条路径都要恰好一次 beginPass/endPass
// ─────────────────────────────────────────────────────────────

void SpineItem::SpineRenderer::render(QRhiCommandBuffer* cb)
{
    QRhiRenderTarget* rt = renderTarget();
    Q_ASSERT(rt);

    const QColor clearColor = QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f);
    const QRhiDepthStencilClearValue dsClear{ 1.0f, 0 };

    // 离屏颜色纹理由本 item 独占，Qt 不会替我们清屏：
    // 没有可画内容时也必须进一次 pass，否则画面会停在上一帧。
    const auto clearOnly = [&] {
        cb->beginPass(rt, clearColor, dsClear);
        cb->endPass();
    };

    // initialize() 没能建起资源（着色器缺失、后端创建失败……）：空渲染
    if(!m_pipelineNormal) {
        clearOnly();
        return;
    }

    m_vertexData.clear();
    m_indexData.clear();
    m_frameGroups.clear();

    // ── 把 render command 合并成"同纹理 + 同混合模式"的批次 ──
    for(spine::RenderCommand* cmd = m_renderCommands; cmd;) {
        if(cmd->numVertices == 0) { cmd = cmd->next; continue; }

        QtTextureLoader::Texture* texKey = static_cast<QtTextureLoader::Texture*>(cmd->texture);
        const spine::BlendMode blend = cmd->blendMode;

        std::size_t groupVerts = 0;
        std::size_t groupIndices = 0;
        spine::RenderCommand* groupEnd = cmd;
        while(groupEnd && groupEnd->texture == texKey && groupEnd->blendMode == blend) {
            groupVerts += groupEnd->numVertices;
            groupIndices += groupEnd->numIndices;
            groupEnd = groupEnd->next;
        }

        FrameGroup group;
        group.texKey       = texKey;
        group.blendMode    = blend;
        group.vertexOffset = m_vertexData.size();
        group.vertexCount  = groupVerts;
        group.indexOffset  = m_indexData.size();
        group.indexCount   = groupIndices;

        for(spine::RenderCommand* c = cmd; c != groupEnd; c = c->next) {
            // 每条 command 的 indices 都是从 0 开始的局部索引，要加上顶点基址
            const uint32_t vertexBase = static_cast<uint32_t>(m_vertexData.size());

            for(int32_t i = 0; i < c->numVertices; ++i) {
                SpineVertex v;
                v.x = c->positions[i * 2 + 0] * m_vertexScale + m_vertexTranslate.x();
                v.y = -c->positions[i * 2 + 1] * m_vertexScale + m_vertexTranslate.y();
                v.u = c->uvs[i * 2 + 0];
                v.v = c->uvs[i * 2 + 1];
                v.argb = c->colors[i];
                v.dark_argb = c->darkColors[i];   // SkeletonRenderer 总是分配 darkColors
                m_vertexData.push_back(v);
            }

            for(int32_t i = 0; i < c->numIndices; ++i)
                m_indexData.push_back(vertexBase + c->indices[i]);
        }

        m_frameGroups.push_back(group);
        cmd = groupEnd;
    }

    if(m_vertexData.empty()) {
        clearOnly();
        return;
    }

    QRhiResourceUpdateBatch* rub = m_rhi->nextResourceUpdateBatch();
    if(!rub)[[unlikely]]
        qFatal("Spine RHI: out of memory while allocating resource update batch");

    // 纹理缺就补建：命中缓存则跳过，成本 O(numCommands)
    for(spine::RenderCommand* c = m_renderCommands; c; c = c->next) {
        if(c->numVertices <= 0 || !c->texture) continue;
        auto* key = static_cast<QtTextureLoader::Texture*>(c->texture);
        if(m_textureCache.find(key) == m_textureCache.end())
            getOrCreateTexture(key, rub);
    }

    {
        QMatrix4x4 proj = m_rhi->clipSpaceCorrMatrix();
        proj.ortho(0.0f, m_projectionSize.x(), m_projectionSize.y(), 0.0f, -1.0f, 1.0f);

        SpineUniforms uniforms{};
        std::memcpy(uniforms.matrix, proj.constData(), sizeof(uniforms.matrix));
        uniforms.opacity = 1.0f;   // item 的 opacity 由场景图在合成时应用
        rub->updateDynamicBuffer(m_ubo.get(), 0, UniformsSize, &uniforms);
    }

    const quint32 vbytes = static_cast<quint32>(m_vertexData.size() * sizeof(SpineVertex));
    const quint32 ibytes = static_cast<quint32>(m_indexData.size() * sizeof(uint32_t));

    // 容量只增不减，并按 2 倍增长，避免裁剪动画逐帧改变顶点数时反复重建缓冲
    if(!m_vbuf || m_vbuf->size() < vbytes) {
        const quint32 capacity = m_vbuf ? qMax(vbytes, m_vbuf->size() * 2) : vbytes;
        m_vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, capacity));
        if(!m_vbuf)[[unlikely]]
            qFatal("Spine RHI: out of memory while creating vertex buffer");
        if(!m_vbuf->create())[[unlikely]] {
            qWarning("Spine RHI: failed to create vertex buffer; rendering disabled");
            m_vbuf.reset();
            cb->resourceUpdate(rub);   // rub 里已有纹理/UBO 更新，先提交掉
            clearOnly();
            return;
        }
    }
    if(!m_ibuf || m_ibuf->size() < ibytes) {
        const quint32 capacity = m_ibuf ? qMax(ibytes, m_ibuf->size() * 2) : ibytes;
        m_ibuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, capacity));
        if(!m_ibuf)[[unlikely]]
            qFatal("Spine RHI: out of memory while creating index buffer");
        if(!m_ibuf->create())[[unlikely]] {
            qWarning("Spine RHI: failed to create index buffer; rendering disabled");
            m_ibuf.reset();
            cb->resourceUpdate(rub);
            clearOnly();
            return;
        }
    }

    rub->updateDynamicBuffer(m_vbuf.get(), 0, vbytes, m_vertexData.data());
    rub->updateDynamicBuffer(m_ibuf.get(), 0, ibytes, m_indexData.data());
    cb->resourceUpdate(rub);

    cb->beginPass(rt, clearColor, dsClear);

    const QSize rtSize = rt->pixelSize();
    cb->setViewport(QRhiViewport(0.0f, 0.0f, float(rtSize.width()), float(rtSize.height())));

    const QRhiCommandBuffer::VertexInput vinput(m_vbuf.get(), 0);

    for(const FrameGroup &group : m_frameGroups) {
        QRhiGraphicsPipeline* pipeline = nullptr;
        switch(group.blendMode) {
        case spine::BlendMode_Additive: pipeline = m_pipelineAdditive.get(); break;
        case spine::BlendMode_Multiply: pipeline = m_pipelineMultiply.get(); break;
        case spine::BlendMode_Screen:   pipeline = m_pipelineScreen.get();   break;
        default:                        pipeline = m_pipelineNormal.get();   break;
        }

        QRhiTexture* texture = nullptr;
        if(auto it = m_textureCache.find(group.texKey); it != m_textureCache.end())
            texture = it->second.get();
        if(!texture) continue;   // 纹理建不起来（加载时已告警），跳过该批次

        QRhiShaderResourceBindings* srb = getOrCreateSrb(group.texKey, texture);
        if(!srb) continue;

        cb->setGraphicsPipeline(pipeline);
        cb->setShaderResources(srb);
        cb->setVertexInput(0, 1, &vinput, m_ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(quint32(group.indexCount), 1, quint32(group.indexOffset), 0, 0);
    }

    cb->endPass();
}
