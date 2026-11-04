#include "SpineItem.h"
#include <QMatrix4x4>
#include <QDebug>

SpineItem::SpineRenderer::SpineRenderer(SpineItem *item)
    : m_item(item) { }

void SpineItem::SpineRenderer::initialize(QRhiCommandBuffer *cb)
{
    QRhi *currentRhi = rhi();
    // 如果 RHI 没变，且核心资源已在，直接返回。
    if (m_rhi == currentRhi && m_sampler && m_ubo && m_dummySrb
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

    //qDeleteAll(m_srbCache);
    m_srbCache.clear();
    //qDeleteAll(m_textureCache);
    m_textureCache.clear();
    m_texturesPending = true;

    m_vbuf.reset();
    m_ibuf.reset();

    // 此时已无任何对象引用 ubo/sampler，安全释放
    m_ubo.reset();
    m_sampler.reset();

    // ── 第 2 步：按"被引用方 → 引用方"顺序重建，任一步失败都早返回 ──
    m_sampler.reset(m_rhi->newSampler(
        QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    if (!m_sampler || !m_sampler->create()) [[unlikely]] {
        qWarning() << "Spine RHI: failed to create sampler";
        m_sampler.reset();
        return;
    }

    m_ubo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80));
    if (!m_ubo || !m_ubo->create()) [[unlikely]] {
        qWarning() << "Spine RHI: failed to create UBO";
        m_ubo.reset();
        return;
    }

    m_dummySrb.reset(m_rhi->newShaderResourceBindings());
    if (!m_dummySrb) [[unlikely]] {
        qWarning() << "Spine RHI: newShaderResourceBindings returned null";
        return;
    }
    m_dummySrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage, nullptr, nullptr)
    });
    if (!m_dummySrb->create()) [[unlikely]] {
        qWarning() << "Spine RHI: failed to create dummy SRB";
        m_dummySrb.reset();
        return;
    }

    m_pipelineNormal   = createPipeline(spine::BlendMode_Normal);
    m_pipelineAdditive = createPipeline(spine::BlendMode_Additive);
    m_pipelineMultiply = createPipeline(spine::BlendMode_Multiply);
    m_pipelineScreen   = createPipeline(spine::BlendMode_Screen);

    if (!m_pipelineNormal || !m_pipelineAdditive || !m_pipelineMultiply || !m_pipelineScreen) [[unlikely]] {
        qWarning() << "Spine RHI: one or more pipelines failed to create";
    }
}

// ─────────────────────────────────────────────────────────────
// createPipeline：返回 unique_ptr，失败返回 nullptr
// ─────────────────────────────────────────────────────────────

std::unique_ptr<QRhiGraphicsPipeline> SpineItem::SpineRenderer::createPipeline(spine::BlendMode blendMode)
{
    QByteArray vsData = readResource(QStringLiteral(":/shaders/spine.vert.qsb"));
    QByteArray fsData = readResource(QStringLiteral(":/shaders/spine.frag.qsb"));
    if (vsData.isEmpty() || fsData.isEmpty()) [[unlikely]] {
        qWarning() << "Spine RHI: shader resource empty";
        return nullptr;
    }
    QShader vs = QShader::fromSerialized(vsData);
    QShader fs = QShader::fromSerialized(fsData);
    if (!vs.isValid() || !fs.isValid()) [[unlikely]] {
        qWarning() << "Spine RHI: invalid shader data";
        return nullptr;
    }

    auto pipeline = std::unique_ptr<QRhiGraphicsPipeline>(m_rhi->newGraphicsPipeline());

    QRhiVertexInputLayout layout;
    layout.setBindings({
        { sizeof(SpineVertex) }
    });
    layout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2,     offsetof(SpineVertex, x) },
        { 0, 1, QRhiVertexInputAttribute::Float2,     offsetof(SpineVertex, u) },
        { 0, 2, QRhiVertexInputAttribute::UNormByte4, offsetof(SpineVertex, rgba) },
        { 0, 3, QRhiVertexInputAttribute::UNormByte4, offsetof(SpineVertex, dark_rgba) },
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

    QRhiRenderTarget *rt = renderTarget();
    if (!rt) [[unlikely]] {
        qWarning() << "Spine RHI: renderTarget() is null, cannot create pipeline";
        return nullptr;
    }
    pipeline->setRenderPassDescriptor(rt->renderPassDescriptor());

    if (!pipeline->create()) [[unlikely]] {
        qWarning() << "Spine RHI: pipeline create failed, blend =" << blendMode;
        return nullptr;
    }
    return pipeline;
}

// ─────────────────────────────────────────────────────────────
// 纹理：返回裸指针（由 m_textureCache 持有）
// ─────────────────────────────────────────────────────────────

QRhiTexture *SpineItem::SpineRenderer::getOrCreateTexture(void *spineTexKey, QRhiResourceUpdateBatch *rub)
{
    if (!spineTexKey || !rub) return nullptr;

    auto it = m_textureCache.find(spineTexKey);
    if (it != m_textureCache.end()) return it->second.get();

    // ✅ 直接拿已处理好的 QImage
    QImage *image = static_cast<QtTextureLoader::Texture*>(spineTexKey);
    if (!image || image->isNull()) [[unlikely]] {
        qWarning() << "Spine RHI: QImage is null";
        return nullptr;
    }

    const QRhiTexture::Flags flags = QRhiTexture::UsedAsTransferSource | QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips;
    QRhiTexture *texture = m_rhi->newTexture(QRhiTexture::RGBA8, image->size(), 1, flags);
    if (!texture) [[unlikely]] {
        qWarning() << "Spine RHI: newTexture returned null";
        return nullptr;
    }
    if (!texture->create()) [[unlikely]] {
        qWarning() << "Spine RHI: texture->create() failed" << image->size();
        delete texture;
        return nullptr;
    }

    rub->uploadTexture(texture, *image);
    rub->generateMips(texture);
    m_textureCache.emplace(spineTexKey, std::unique_ptr<QRhiTexture>(texture));
    return texture;
}

// ─────────────────────────────────────────────────────────────
// SRB：返回裸指针（由 m_srbCache 持有）
// ─────────────────────────────────────────────────────────────

QRhiShaderResourceBindings *SpineItem::SpineRenderer::getOrCreateSrb(void *texKey, QRhiTexture *texture)
{
    auto it = m_srbCache.find(texKey);
    if (it != m_srbCache.end()) {
        return it->second.get();
    }

    QRhiShaderResourceBindings *srb = m_rhi->newShaderResourceBindings();
    if (!srb) [[unlikely]] {
        qWarning() << "Spine RHI: newShaderResourceBindings returned null";
        return nullptr;
    }
    srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, texture, m_sampler.get())
    });
    if (!srb->create()) [[unlikely]] {
        qWarning() << "Spine RHI: failed to create SRB";
        delete srb;
        return nullptr;
    }
    m_srbCache.emplace(texKey, std::unique_ptr<QRhiShaderResourceBindings>(srb));
    return srb;
}

void SpineItem::SpineRenderer::synchronize(QQuickRhiItem *item)
{
    SpineItem *spineItem = static_cast<SpineItem*>(item);
    if (!spineItem) return;

    if (spineItem->texturesNeedReset) {
        // 只清缓存，不动 sampler/ubo/pipeline（它们不依赖纹理）
        //qDeleteAll(m_srbCache);
        m_srbCache.clear();
        //qDeleteAll(m_textureCache);
        m_textureCache.clear();
        spineItem->texturesNeedReset = false;
        m_texturesPending = true;
    }
}

void SpineItem::SpineRenderer::render(QRhiCommandBuffer *cb)
{
    if (!m_item || !m_item->skeleton_ptr || !m_item->atlas_ptr) return;
    if (!m_pipelineNormal || !m_sampler || !m_ubo) return;

    QRhiRenderTarget *rt = renderTarget();
    if (!rt) [[unlikely]] { qWarning() << "Spine RHI: renderTarget() is null"; return; }

    spine::RenderCommand *commands = m_skeletonRenderer.render(*m_item->skeleton_ptr);
    if (!commands) return;

    const QRectF bounds = m_item->boundingRect();
    const float skelX = m_item->skeletonData_ptr->getX();
    const float skelY = m_item->skeletonData_ptr->getY();
    const float skelW = m_item->skeletonData_ptr->getWidth()  > 0 ? m_item->skeletonData_ptr->getWidth()  : 1.0f;
    const float skelH = m_item->skeletonData_ptr->getHeight() > 0 ? m_item->skeletonData_ptr->getHeight() : 1.0f;
    const float scaleX  = 1.0f;//bounds.width()  / skelW;
    const float scaleY  = 1.0f;//bounds.height() / skelH;
    const float offsetX = bounds.center().x() - skelW / 2;
    const float offsetY = bounds.center().y() + skelH / 2;

    m_vertexData.clear();
    m_indexData.clear();
    m_frameGroups.clear();

    spine::RenderCommand *cmd = commands;
    while (cmd) {
        if (cmd->numVertices == 0) { cmd = cmd->next; continue; }

        void *texKey = cmd->texture;
        spine::BlendMode blend = cmd->blendMode;

        std::size_t groupVerts = 0, groupIndices = 0;
        spine::RenderCommand *groupEnd = cmd;
        while (groupEnd && groupEnd->texture == texKey && groupEnd->blendMode == blend) {
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

        for (spine::RenderCommand *c = cmd; c != groupEnd; c = c->next) {
            const std::size_t cmdVertexStart = m_vertexData.size();

            for (int32_t i = 0; i < c->numVertices; ++i) {
                SpineVertex v;
                v.x = offsetX + (c->positions[i * 2 + 0] - skelX) * scaleX;
                v.y = offsetY - (c->positions[i * 2 + 1] - skelY) * scaleY;
                v.u = c->uvs[i * 2 + 0];
                v.v = c->uvs[i * 2 + 1];
                v.rgba = c->colors[i];
                v.dark_rgba = c->darkColors ? c->darkColors[i] : 0;
                m_vertexData.push_back(v);
            }

            for (int32_t i = 0; i < c->numIndices; ++i) {
                m_indexData.push_back(static_cast<uint32_t>(c->indices[i] + cmdVertexStart));
            }
        }

        m_frameGroups.push_back(fg);
        cmd = groupEnd;
    }

    if (m_vertexData.empty()) return;

    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
    if (!rub) [[unlikely]] { qWarning() << "Spine RHI: rub is null"; return; }

    // 纹理仅在首帧或资源重载后预创建，缓存命中后跳过
    if (m_texturesPending) {
        for (spine::RenderCommand *c = commands; c; c = c->next) {
            if (c->numVertices > 0 && c->texture) {
                getOrCreateTexture(c->texture, rub);
            }
        }
        m_texturesPending = false;
    }

    {
        QMatrix4x4 proj;
        proj.ortho(0.0f, float(m_item->width()),float(m_item->height()), 0.0f, -1.0f, 1.0f);
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

    const uint32_t vbytes = m_vertexData.size() * sizeof(SpineVertex);
    const uint32_t ibytes = m_indexData.size() * sizeof(uint32_t);

    if (!m_vbuf || m_vbuf->size() < vbytes) {
        m_vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, vbytes));
        if (!m_vbuf || !m_vbuf->create()) [[unlikely]] {
            qWarning() << "Spine RHI: vbuf create failed";
            m_vbuf.reset();
            return;
        }
    }
    if (!m_ibuf || m_ibuf->size() < ibytes) {
        m_ibuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, ibytes));
        if (!m_ibuf || !m_ibuf->create()) [[unlikely]] {
            qWarning() << "Spine RHI: ibuf create failed";
            m_ibuf.reset();
            return;
        }
    }

    rub->updateDynamicBuffer(m_vbuf.get(), 0, vbytes, std::as_const(m_vertexData).data());
    rub->updateDynamicBuffer(m_ibuf.get(), 0, ibytes, std::as_const(m_indexData).data());

    cb->resourceUpdate(rub);

    const QColor clearColor = QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f);
    const QRhiDepthStencilClearValue dsClear{ 1.0f, 0 };
    cb->beginPass(rt, clearColor, dsClear);

    const QSize rtSize = rt->pixelSize();
    cb->setViewport({ 0, 0, float(rtSize.width()), float(rtSize.height()) });

    for (const FrameGroup &fg : std::as_const(m_frameGroups)) {
        QRhiGraphicsPipeline *pipeline = nullptr;
        switch (fg.blendMode) {
        case spine::BlendMode_Normal: pipeline = m_pipelineNormal.get();   break;
        case spine::BlendMode_Additive: pipeline = m_pipelineAdditive.get(); break;
        case spine::BlendMode_Multiply: pipeline = m_pipelineMultiply.get(); break;
        case spine::BlendMode_Screen: pipeline = m_pipelineScreen.get();   break;
        default: pipeline = m_pipelineNormal.get();  break;
        }
        if (!pipeline) continue;

        auto it = m_textureCache.find(fg.texKey);
        QRhiTexture *texture = (it != m_textureCache.end()) ? it->second.get() : nullptr;
        if (!texture) continue;

        QRhiShaderResourceBindings *srb = getOrCreateSrb(fg.texKey, texture);
        if (!srb) continue;

        cb->setGraphicsPipeline(pipeline);
        cb->setShaderResources(srb);

        const QRhiCommandBuffer::VertexInput vinput(m_vbuf.get(), 0);
        cb->setVertexInput(0, 1, &vinput, m_ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(fg.indexCount, 1, fg.indexOffset, 0, 0);
    }

    cb->endPass();
}