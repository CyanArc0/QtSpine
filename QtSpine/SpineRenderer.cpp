#include"SpineItem.h"
#include<QMatrix4x4>
#include<QResource>
#include<QColor>
#include<cstring>
#include<utility>
#include<algorithm>
#include"error_handling.hpp"

void SpineItem::SpineRenderer::initialize(QRhiCommandBuffer* cb)
{
    QRhi* currentRhi = this->rhi();
    // 如果 RHI 没变，且核心资源已在，直接返回。
    if(m_rhi == currentRhi && m_sampler && m_ubo && m_dummySrb
        && m_pipelineNormal && m_pipelineAdditive && m_pipelineMultiply && m_pipelineScreen) {
        return;
    }
    m_rhi = currentRhi;

    // ── 第 1 步：按"引用方 → 被引用方"顺序，拆掉上一次（可能重入）留下的资源 ──
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

    // ── 第 2 步：按"被引用方 → 引用方"顺序重建，任一步失败都早返回 ──
    m_sampler.reset(m_rhi->newSampler(
        QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    if(!m_sampler || !m_sampler->create())[[unlikely]] {
        qWarning() << "Spine RHI: failed to create sampler";
        m_sampler.reset();
        return;
    }

    m_ubo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80));
    if(!m_ubo || !m_ubo->create())[[unlikely]] {
        qWarning() << "Spine RHI: failed to create UBO";
        m_ubo.reset();
        return;
    }

    m_dummySrb.reset(m_rhi->newShaderResourceBindings());
    if(!m_dummySrb)[[unlikely]] {
        qWarning() << "Spine RHI: newShaderResourceBindings returned null";
        return;
    }
    m_dummySrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, nullptr, nullptr)
    });
    if(!m_dummySrb->create())[[unlikely]] {
        qWarning() << "Spine RHI: failed to create dummy SRB";
        m_dummySrb.reset();
        return;
    }

    m_pipelineNormal   = createPipeline(spine::BlendMode_Normal);
    m_pipelineAdditive = createPipeline(spine::BlendMode_Additive);
    m_pipelineMultiply = createPipeline(spine::BlendMode_Multiply);
    m_pipelineScreen   = createPipeline(spine::BlendMode_Screen);

    if(!m_pipelineNormal || !m_pipelineAdditive || !m_pipelineMultiply || !m_pipelineScreen)[[unlikely]] {
        qWarning() << "Spine RHI: one or more pipelines failed to create";
    }
}

// ─────────────────────────────────────────────────────────────
// createPipeline：返回 unique_ptr，失败返回 nullptr
// ─────────────────────────────────────────────────────────────
std::unique_ptr<QRhiGraphicsPipeline> SpineItem::SpineRenderer::createPipeline(spine::BlendMode blendMode)
{
    const QResource vsResource(QStringLiteral(":/shaders/spine.vert.qsb"));
    const QResource fsResource(QStringLiteral(":/shaders/spine.frag.qsb"));
    const QByteArray vsData(reinterpret_cast<const char*>(vsResource.data()), vsResource.size());
    const QByteArray fsData(reinterpret_cast<const char*>(fsResource.data()), fsResource.size());
    if(vsData.isEmpty() || fsData.isEmpty())[[unlikely]] {
        qWarning() << "Spine RHI: shader resource empty";
        return nullptr;
    }
    QShader vs = QShader::fromSerialized(vsData);
    QShader fs = QShader::fromSerialized(fsData);
    if(!vs.isValid() || !fs.isValid())[[unlikely]] {
        qWarning() << "Spine RHI: invalid shader data";
        return nullptr;
    }

    auto pipeline = std::unique_ptr<QRhiGraphicsPipeline>(m_rhi->newGraphicsPipeline());

    QRhiVertexInputLayout layout;
    layout.setBindings({QRhiVertexInputBinding{sizeof(SpineVertex)}});
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
    switch (blendMode) {
    case spine::BlendMode_Normal:
        break;
    case spine::BlendMode_Additive:
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::One;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::One;
        break;
    case spine::BlendMode_Multiply:
        blend.srcColor = QRhiGraphicsPipeline::DstColor;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        break;
    case spine::BlendMode_Screen:
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcColor;
        break;
    default:
        break;
    }
    pipeline->setTargetBlends({ blend });
    pipeline->setTopology(QRhiGraphicsPipeline::Triangles);

    QRhiRenderTarget* rt = renderTarget();
    if(!rt)[[unlikely]] {
        qWarning() << "Spine RHI: renderTarget() is null, cannot create pipeline";
        return nullptr;
    }
    pipeline->setRenderPassDescriptor(rt->renderPassDescriptor());

    if(!pipeline->create())[[unlikely]] {
        qWarning() << "Spine RHI: pipeline create failed, blend =" << blendMode;
        return nullptr;
    }
    return pipeline;
}

// ─────────────────────────────────────────────────────────────
// 纹理：返回裸指针（由 m_textureCache 持有）
// ─────────────────────────────────────────────────────────────

QRhiTexture* SpineItem::SpineRenderer::getOrCreateTexture(QtTextureLoader::Texture* spineTexKey, QRhiResourceUpdateBatch* rub)
{
    if(!spineTexKey || !rub) return nullptr;

    auto it = m_textureCache.find(spineTexKey);
    if(it != m_textureCache.end()) return it->second.get();

    // ✅ 直接拿已处理好的 QImage
    QImage* image = static_cast<QtTextureLoader::Texture*>(spineTexKey);
    if(image->isNull())[[unlikely]] {
        qWarning() << "Spine RHI: QImage is null";
        return nullptr;
    }

    const QRhiTexture::Flags flags = QRhiTexture::UsedAsTransferSource | QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips;
    QRhiTexture* texture = m_rhi->newTexture(QRhiTexture::RGBA8, image->size(), 1, flags);
    if(!texture)[[unlikely]] {
        qWarning() << "Spine RHI: newTexture returned null";
        return nullptr;
    }
    if(!texture->create())[[unlikely]] {
        qWarning() << "Spine RHI: texture->create() failed" << image->size();
        delete texture;
        return nullptr;
    }

    rub->uploadTexture(texture,* image);
    rub->generateMips(texture);
    m_textureCache.emplace(spineTexKey, std::unique_ptr<QRhiTexture>(texture));
    return texture;
}

// ─────────────────────────────────────────────────────────────
// SRB：返回裸指针（由 m_srbCache 持有）
// ─────────────────────────────────────────────────────────────

QRhiShaderResourceBindings* SpineItem::SpineRenderer::getOrCreateSrb(QtTextureLoader::Texture* texKey, QRhiTexture* texture)
{
    auto it = m_srbCache.find(texKey);
    if(it != m_srbCache.end()) {
        return it->second.get();
    }

    QRhiShaderResourceBindings* srb = m_rhi->newShaderResourceBindings();
    if(!srb)[[unlikely]] {
        qWarning() << "Spine RHI: newShaderResourceBindings returned null";
        return nullptr;
    }
    srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, texture, m_sampler.get())
    });
    if(!srb->create())[[unlikely]] {
        qWarning() << "Spine RHI: failed to create SRB";
        delete srb;
        return nullptr;
    }
    m_srbCache.emplace(texKey, std::unique_ptr<QRhiShaderResourceBindings>(srb));
    return srb;
}

void SpineItem::SpineRenderer::synchronize(QQuickRhiItem* item)
{
    SpineItem* spineItem = static_cast<SpineItem*>(item);

    if(spineItem->texturesNeedReset) {
        // 只清缓存，不动 sampler/ubo/pipeline（它们不依赖纹理）
        m_srbCache.clear();
        m_textureCache.clear();
        spineItem->texturesNeedReset = false;
    }

    // 骨架不存在 → 清空，render 直接早退
    if(!spineItem || !spineItem->skeleton_ptr || !spineItem->skeletonData_ptr) {
        m_renderCommands = nullptr;
        return;
    }

    m_renderCommands = spineItem->skeletonRenderer.render(*spineItem->skeleton_ptr);
    if(!m_renderCommands) return;

    // ── 1. FBO 尺寸（投影矩阵用） ──
    m_projectionSize = {static_cast<float>(spineItem->width()), static_cast<float>(spineItem->height())};
    if(m_projectionSize.x() <= 0 || m_projectionSize.y() <= 0) {
        m_renderCommands = nullptr;
        return;
    }

    // ── 2. 内容区与锚点（padding 内缩后的中心） ──
    const float padL = static_cast<float>(spineItem->leftPadding);
    const float padT = static_cast<float>(spineItem->topPadding);
    const float padR = static_cast<float>(spineItem->rightPadding);
    const float padB = static_cast<float>(spineItem->bottomPadding);

    const float contentW = m_projectionSize.x() - padL - padR;
    const float contentH = m_projectionSize.y() - padT - padB;
    const float anchorX  = padL + contentW * 0.5f;
    const float anchorY  = padT + contentH * 0.5f;

    // ── 3. 缩放（clamp 防负值翻转） ──
    const float s = std::max(0.0f, static_cast<float>(spineItem->skeletonScale));

    // ── 4. 骨架 setup-pose 参数 ──
    const float skelX = spineItem->skeletonData_ptr->getX();
    const float skelY = spineItem->skeletonData_ptr->getY();
    const float skelW = spineItem->skeletonData_ptr->getWidth();
    const float skelH = spineItem->skeletonData_ptr->getHeight();

    // ── 5. 模型左上角（含居中）→ 合并 skelX/skelY → tx/ty ──
    // 防御：skelW/H 为 0 时居中项为 0，不产生 NaN（乘法不除零）
    const float originX = anchorX - skelW * s * 0.5f;
    const float originY = anchorY + skelH * s * 0.5f;

    m_vertexScale = s;
    m_vertexTranslate = {originX - skelX * s, originY + skelY * s};
}

void SpineItem::SpineRenderer::render(QRhiCommandBuffer* cb)
{
    QRhiRenderTarget* rt = renderTarget();
    if(!rt)[[unlikely]] { qWarning() << "Spine RHI: renderTarget() is null"; return; }

    const QColor clearColor = QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f);
    const QRhiDepthStencilClearValue dsClear{ 1.0f, 0 };

    // 离屏颜色纹理由本 item 独占，Qt 不会替我们清屏。
    // 因此即使这一帧没有任何可画内容，也要进一次 pass 把上一帧擦掉，
    // 否则骨架被卸载 / 加载失败后，画面会一直停在最后一帧。
    const auto clearOnly = [cb, rt, clearColor, dsClear] {
        cb->beginPass(rt, clearColor, dsClear);
        cb->endPass();
    };

    m_vertexData.clear();
    m_indexData.clear();
    m_frameGroups.clear();

    spine::RenderCommand* cmd = m_renderCommands;
    while(cmd) {
        if(cmd->numVertices == 0) { cmd = cmd->next; continue; }

        QtTextureLoader::Texture* texKey = static_cast<QtTextureLoader::Texture*>(cmd->texture);
        spine::BlendMode blend = cmd->blendMode;

        std::size_t groupVerts = 0, groupIndices = 0;
        spine::RenderCommand* groupEnd = cmd;
        while(groupEnd && groupEnd->texture == texKey && groupEnd->blendMode == blend) {
            groupVerts   += groupEnd->numVertices;
            groupIndices += groupEnd->numIndices;
            groupEnd = groupEnd->next;
        }

        FrameGroup fg;
        fg.texKey       = texKey;
        fg.blendMode    = blend;
        fg.vertexOffset = m_vertexData.size();
        fg.vertexCount  = groupVerts;
        fg.indexOffset  = m_indexData.size();
        fg.indexCount   = groupIndices;

        for(spine::RenderCommand* c = cmd; c != groupEnd; c = c->next) {
            const std::size_t cmdVertexStart = m_vertexData.size();

            for(int32_t i = 0; i < c->numVertices; ++i) {
                SpineVertex v;
                v.x = +c->positions[i * 2 + 0] * m_vertexScale + m_vertexTranslate.x();
                v.y = -c->positions[i * 2 + 1] * m_vertexScale + m_vertexTranslate.y();
                v.u = c->uvs[i * 2 + 0];
                v.v = c->uvs[i * 2 + 1];
                v.argb = c->colors[i];
                v.dark_argb = c->darkColors ? c->darkColors[i] : 0;
                m_vertexData.push_back(v);
            }

            for(int32_t i = 0; i < c->numIndices; ++i) {
                m_indexData.push_back(static_cast<uint32_t>(c->indices[i] + cmdVertexStart));
            }
        }

        m_frameGroups.push_back(fg);
        cmd = groupEnd;
    }

    // 骨架已卸载、管线不可用或没有顶点：清屏后返回，不要留下上一帧的内容
    if(!m_renderCommands || !m_pipelineNormal || !m_sampler || !m_ubo || m_vertexData.empty()) {
        clearOnly();
        return;
    }

    QRhiResourceUpdateBatch* rub = m_rhi->nextResourceUpdateBatch();
    if(!rub)[[unlikely]] { qWarning() << "Spine RHI: rub is null"; clearOnly(); return; }

    // 纹理缺就补建：每帧扫一遍，缓存命中则跳过，成本 O(numCommands)
    for(auto* c = m_renderCommands; c; c = c->next) {
        if(c->numVertices > 0 && c->texture
            && m_textureCache.find(static_cast<QtTextureLoader::Texture*>(c->texture)) == m_textureCache.end())
            getOrCreateTexture(static_cast<QtTextureLoader::Texture*>(c->texture), rub);
    }

    {
        QMatrix4x4 proj = m_rhi->clipSpaceCorrMatrix();
        proj.ortho(0.0f, m_projectionSize.x(), m_projectionSize.y(), 0.0f, -1.0f, 1.0f);
        struct {
            float m[16];
            float opacity;
            float pad[3];
        } ubuf;
        std::memcpy(ubuf.m, proj.constData(), sizeof(ubuf.m));
        ubuf.opacity = 1.0f;
        ubuf.pad[0] = ubuf.pad[1] = ubuf.pad[2] = 0.0f;
        rub->updateDynamicBuffer(m_ubo.get(), 0, sizeof(ubuf), &ubuf);
    }

    const std::size_t vbytes = m_vertexData.size() * sizeof(SpineVertex);
    const std::size_t ibytes = m_indexData.size() * sizeof(uint32_t);

    if(!m_vbuf || m_vbuf->size() < vbytes) {
        m_vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, vbytes));
        if(!m_vbuf || !m_vbuf->create())[[unlikely]] {
            qWarning() << "Spine RHI: vbuf create failed";
            m_vbuf.reset();
            cb->resourceUpdate(rub); // rub 里已有纹理/UBO 更新，先提交再清屏，避免批次悬空
            clearOnly();
            return;
        }
    }
    if(!m_ibuf || m_ibuf->size() < ibytes) {
        m_ibuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, ibytes));
        if(!m_ibuf || !m_ibuf->create())[[unlikely]] {
            qWarning() << "Spine RHI: ibuf create failed";
            m_ibuf.reset();
            cb->resourceUpdate(rub);
            clearOnly();
            return;
        }
    }

    rub->updateDynamicBuffer(m_vbuf.get(), 0, vbytes, std::as_const(m_vertexData).data());
    rub->updateDynamicBuffer(m_ibuf.get(), 0, ibytes, std::as_const(m_indexData).data());

    cb->resourceUpdate(rub);

    cb->beginPass(rt, clearColor, dsClear);

    const QSize rtSize = rt->pixelSize();
    cb->setViewport({ 0, 0, float(rtSize.width()), float(rtSize.height()) });

    for(const FrameGroup &fg : std::as_const(m_frameGroups)) {
        QRhiGraphicsPipeline* pipeline = nullptr;
        switch (fg.blendMode) {
        case spine::BlendMode_Normal: pipeline = m_pipelineNormal.get();   break;
        case spine::BlendMode_Additive: pipeline = m_pipelineAdditive.get(); break;
        case spine::BlendMode_Multiply: pipeline = m_pipelineMultiply.get(); break;
        case spine::BlendMode_Screen: pipeline = m_pipelineScreen.get();   break;
        default: pipeline = m_pipelineNormal.get();  break;
        }
        if(!pipeline) continue;

        auto it = m_textureCache.find(fg.texKey);
        QRhiTexture* texture = (it != m_textureCache.end()) ? it->second.get() : nullptr;
        if(!texture) continue;

        QRhiShaderResourceBindings* srb = getOrCreateSrb(fg.texKey, texture);
        if(!srb) continue;

        cb->setGraphicsPipeline(pipeline);
        cb->setShaderResources(srb);

        const QRhiCommandBuffer::VertexInput vinput(m_vbuf.get(), 0);
        cb->setVertexInput(0, 1, &vinput, m_ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(fg.indexCount, 1, fg.indexOffset, 0, 0);
    }

    cb->endPass();
}