#include"SpineRenderer.h"
#include<QByteArray>
#include<QMatrix4x4>
#include<ranges>
#include<cstring>
#include<cstddef>
#include<cstdint>
#include<tuple>
#include<utility>
#include<memory>
#include"SpineVertex.h"
#include"SpineUniforms.h"
#include"SpineShader.hpp"
#include"error_handling.hpp"

using namespace Qt::StringLiterals;

QSGRenderNode::RenderingFlags SpinePlayer::SpineRenderer::flags() const
{
    // 只用 QRhi 录制命令，且只在 z = 0 平面绘制。
    // 不声明 BoundedRectRendering：骨架内容可能超出 item 矩形。
    return NoExternalRendering | DepthAwareRendering;
}

QSGRenderNode::StateFlags SpinePlayer::SpineRenderer::changedStates() const
{
    // render() 里设置了 scissor（viewport 交给场景图管理）。
    // 声明出来，场景图会在后续批次重新应用自己的 viewport / scissor。
    return ScissorState;
}

void SpinePlayer::SpineRenderer::prepare()
{
    QRhiRenderTarget* rt = this->renderTarget();
    QRhiCommandBuffer* cb = this->commandBuffer();
    assert(rt);
    assert(cb);

    initialize(rt);

    // 资源层失败：initialize 已设 failed，等环境变化或 releaseResources() 再试
    if(m_geometryState == State::failed)
        return;

    // 内容为空（空骨架 / 尺寸退化）：这一帧不画，标记待重建
    if(!m_renderCommands) {
        m_geometryState = State::pending;
        return;
    }

    assert(m_sampler);
    assert(m_ubo);
    assert(m_dummySrb);
    assert(m_pipelineNormal);
    assert(m_pipelineAdditive);
    assert(m_pipelineMultiply);
    assert(m_pipelineScreen);

    QRhiResourceUpdateBatch* rub = m_rhi->nextResourceUpdateBatch();
    if(!rub)[[unlikely]]
        error_handling::panic("Spine RHI: failed to allocate resource update batch");

    // 顶点/索引/纹理只在内容真的变过（synchronize 跑过）时重建：
    // 窗口因为其它 item 重绘时，没必要把同样的几何再算一遍、再传一遍。
    if(m_geometryState == State::pending) {
        m_vertexData.clear();
        m_indexData.clear();
        m_frameGroups.clear();
        // buildGeometry：把 render command 合并成"同纹理 + 同混合模式"的批次
        for(spine::RenderCommand* cmd = m_renderCommands; cmd;) {
            if(cmd->numVertices == 0) { cmd = cmd->next; continue; }

            QtTextureLoader::Texture* texKey = static_cast<QtTextureLoader::Texture*>(cmd->texture);
            const spine::BlendMode blend = cmd->blendMode;

            std::size_t groupIndices = 0;
            spine::RenderCommand* groupEnd = cmd;
            while(groupEnd && groupEnd->texture == texKey && groupEnd->blendMode == blend) {
                groupIndices += groupEnd->numIndices;
                groupEnd = groupEnd->next;
            }

            FrameGroup group {
            .texKey      = texKey,
            .blendMode   = blend,
            .indexOffset = m_indexData.size(),
            .indexCount  = groupIndices };

            for(spine::RenderCommand* c = cmd; c != groupEnd; c = c->next) {
                // 每条 command 的 indices 都是从 0 开始的局部索引，要加上顶点基址
                const uint32_t vertexBase = m_vertexData.size();

                for(int32_t i = 0; i < c->numVertices; ++i) {
                    SpineVertex v;
                    v.x = +c->positions[i * 2 + 0] * m_transformScale.x() + m_transformOffset.x();
                    v.y = -c->positions[i * 2 + 1] * m_transformScale.y() + m_transformOffset.y();
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

        if(!m_vertexData.empty()) {
            // 纹理缺就补建：命中缓存则跳过，成本 O(numCommands)
            for(spine::RenderCommand* c = m_renderCommands; c; c = c->next) {
                if(c->numVertices <= 0 || !c->texture) continue;
                auto* key = static_cast<QtTextureLoader::Texture*>(c->texture);
                getOrCreateTexture(key, rub);
            }

            // SRB 也在 pass 之外建好，render() 里只查表
            for(const auto texKey : m_frameGroups | std::views::transform(&FrameGroup::texKey)) {
                auto it = m_textureCache.find(texKey);
                if(it == m_textureCache.end() || !it->second) continue;
                getOrCreateSrb(texKey, it->second.get());
            }

            const uint32_t vbytes = m_vertexData.size() * sizeof(SpineVertex);
            const uint32_t ibytes = m_indexData.size() * sizeof(uint32_t);

            // 容量只增不减，并按 2 倍增长，避免裁剪动画逐帧改变顶点数时反复重建缓冲
            if(!m_vbuf || m_vbuf->size() < vbytes) {
                const uint32_t capacity = m_vbuf ? qMax(vbytes, m_vbuf->size() * 2) : vbytes;
                m_vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, capacity));
                if(!m_vbuf)[[unlikely]]
                    error_handling::panic("Spine RHI: out of memory while creating vertex buffer");
                if(!m_vbuf->create())[[unlikely]] {
                    error_handling::warn(u"Spine RHI: failed to create vertex buffer; rendering disabled"_s);
                    m_vbuf.reset();
                    cb->resourceUpdate(rub);   // rub 里已有纹理/UBO 更新，先提交再退出
                    return;
                }
            }
            if(!m_ibuf || m_ibuf->size() < ibytes) {
                const uint32_t capacity = m_ibuf ? qMax(ibytes, m_ibuf->size() * 2) : ibytes;
                m_ibuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, capacity));
                if(!m_ibuf)[[unlikely]]
                    error_handling::panic("Spine RHI: out of memory while creating index buffer");
                if(!m_ibuf->create())[[unlikely]] {
                    error_handling::warn(u"Spine RHI: failed to create index buffer; rendering disabled"_s);
                    m_ibuf.reset();
                    cb->resourceUpdate(rub);
                    return;
                }
            }

            rub->updateDynamicBuffer(m_vbuf.get(), 0, vbytes, m_vertexData.data());
            rub->updateDynamicBuffer(m_ibuf.get(), 0, ibytes, m_indexData.data());
        }

        m_geometryState = State::valid;
    }

    {
        // UBO 每帧更新：opacity 和 item 变换可能在不触发同步的情况下变化
        const QMatrix4x4 mvp = (*projectionMatrix()) * (*matrix());

        SpineUniforms uniforms{};
        std::memcpy(uniforms.matrix, mvp.constData(), sizeof(uniforms.matrix));
        uniforms.opacity = this->inheritedOpacity();
        rub->updateDynamicBuffer(m_ubo.get(), 0, sizeof(SpineUniforms), &uniforms);
    }

    cb->resourceUpdate(rub);
}

void SpinePlayer::SpineRenderer::render(const RenderState* state)
{
    if(m_frameGroups.empty() || !m_pipelineNormal || !m_vbuf || !m_ibuf || !m_renderCommands)
        return;
    assert(m_pipelineAdditive);
    assert(m_pipelineMultiply);
    assert(m_pipelineScreen);

    QRhiCommandBuffer* cb = this->commandBuffer();
    assert(cb);
    assert(state);

    // pipeline 声明了 UsesScissor，所以每次都要给出裁剪矩形：
    // 场景图会把祖先/item 的裁剪结果通过 state 传进来，没有裁剪时就是整个 render target。
    // 坐标约定两边一致（x/y 为左下角、设备像素），直接透传。
    QRhiScissor scissor;
    const QRect clipRect = state->scissorRect();
    if(state->scissorEnabled() && clipRect.isValid()) {
        scissor = QRhiScissor(clipRect.x(), clipRect.y(), clipRect.width(), clipRect.height());
    } else {
        const QSize rtSize = renderTarget()->pixelSize();
        scissor = QRhiScissor(0, 0, rtSize.width(), rtSize.height());
    }

    // viewport 由场景图按批次设好，这里不动，避免冲掉它设置的 scissor。
    const QRhiCommandBuffer::VertexInput vinput(m_vbuf.get(), 0);

    for(const auto &[texKey, blendMode, indexOffset, indexCount] : m_frameGroups) {
        QRhiGraphicsPipeline* pipeline = nullptr;
        switch(blendMode) {
        default:[[fallthrough]];
        case spine::BlendMode_Normal:   pipeline = m_pipelineNormal.get();   break;
        case spine::BlendMode_Additive: pipeline = m_pipelineAdditive.get(); break;
        case spine::BlendMode_Multiply: pipeline = m_pipelineMultiply.get(); break;
        case spine::BlendMode_Screen:   pipeline = m_pipelineScreen.get();   break;
        }

        auto it = m_srbCache.find(texKey);
        if(it == m_srbCache.end() || !it->second) continue;   // 纹理缺失

        cb->setGraphicsPipeline(pipeline);
        cb->setScissor(scissor);   // 必须在绑定带 UsesScissor 的 pipeline 之后
        cb->setShaderResources(it->second.get());
        cb->setVertexInput(0, 1, &vinput, m_ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(indexCount, 1, indexOffset, 0, 0);
    }
}

void SpinePlayer::SpineRenderer::releaseResources()
{
    // 按"引用方 → 被引用方"倒序释放
    m_pipelineScreen.reset();
    m_pipelineMultiply.reset();
    m_pipelineAdditive.reset();
    m_pipelineNormal.reset();

    m_srbCache.clear();
    m_dummySrb.reset();

    m_textureCache.clear();

    m_ibuf.reset();
    m_vbuf.reset();

    m_ubo.reset();
    m_sampler.reset();

    m_geometryState = State::pending;

    m_fingerprint = {};
    m_rhi = nullptr;
}

void SpinePlayer::SpineRenderer::synchronize(SpinePlayer* item)
{
    assert(item);

    // 内容换了一帧，prepare() 里要重建
    if(m_geometryState == State::valid)
        m_geometryState = State::pending;

    if(item->texturesNeedReset) {
        // 纹理换了：引用旧纹理的 SRB 必须一起清掉
        m_srbCache.clear();
        m_textureCache.clear();
        item->texturesNeedReset = false;
    }

    if(not item->isValid()) {
        m_renderCommands = nullptr;   // 没有骨架 → 这一帧不画
        return;
    }

    m_renderCommands = item->skeletonRenderer.render(*item->skeleton);
    if(!m_renderCommands) return;     // 空骨架（没有可画槽位）

    // 骨架坐标 → item 局部坐标的变换与 contentRect 共用，避免两处算法分叉
    auto result = item->contentTransform();
    if(!result) {
        m_renderCommands = nullptr;   // 尺寸退化
        return;
    }
    std::tie(m_transformScale,m_transformOffset) = std::move(*result);
}

void SpinePlayer::SpineRenderer::initialize(QRhiRenderTarget* rt)
{
    QRhi* rhi = rt->rhi();
    QRhiRenderPassDescriptor* rpd = rt->renderPassDescriptor();
    assert(rhi);
    assert(rpd);

    if(m_rhi != rhi) {
        // 换了设备：旧对象都属于上一个 QRhi
        releaseResources();
        m_rhi = rhi;
    }

    Fingerprint fingerprint = {rt->sampleCount(),rpd->serializedFormat()};
    const bool sameFingerprint = m_fingerprint == fingerprint;

    // 同一环境下已经失败过：不再每帧重试（否则 60Hz 刷日志），
    // 等设备 / render pass 环境变化，或 releaseResources() 之后再试。
    if(m_geometryState == State::failed) {
        if(sameFingerprint)
            return;
        // 环境变化：清除失败状态，允许重试
        m_geometryState = State::pending;
    }
    m_fingerprint = fingerprint;

    // ── sampler / UBO / dummy SRB 与 render target 无关，只在缺失时补建 ──
    // 创建失败不终止进程：告警一次，本 item 空渲染，环境变化后再试。
    if(!m_sampler) {
        m_sampler.reset(m_rhi->newSampler(
            QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
            QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if(!m_sampler)[[unlikely]]
            error_handling::panic("Spine RHI: out of memory while creating sampler");
        if(!m_sampler->create())[[unlikely]] {
            error_handling::warn(u"Spine RHI: failed to create sampler; rendering disabled"_s);
            m_sampler.reset();
            m_geometryState = State::failed;
            return;
        }
    }

    if(!m_ubo) {
        m_ubo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(SpineUniforms)));
        if(!m_ubo)[[unlikely]]
            error_handling::panic("Spine RHI: out of memory while creating uniform buffer");
        if(!m_ubo->create())[[unlikely]] {
            error_handling::warn(u"Spine RHI: failed to create uniform buffer; rendering disabled"_s);
            m_ubo.reset();
            m_geometryState = State::failed;
            return;
        }
    }

    if(!m_dummySrb) {
        // 只是给 pipeline 提供布局，绑定的资源无所谓（见 qrhi.cpp 中
        // QRhiGraphicsPipeline::setShaderResourceBindings 的说明），
        // 真正绘制时换成 getOrCreateSrb() 建的那份。
        m_dummySrb.reset(m_rhi->newShaderResourceBindings());
        if(!m_dummySrb)[[unlikely]]
            error_handling::panic("Spine RHI: out of memory while creating shader resource bindings");

        m_dummySrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, nullptr, nullptr)
        });
        if(!m_dummySrb->create())[[unlikely]] {
            error_handling::warn(u"Spine RHI: failed to create shader resource bindings; rendering disabled"_s);
            m_dummySrb.reset();
            m_geometryState = State::failed;
            return;
        }
    }

    // ── pipeline 与 render pass 格式、采样数绑定；render target 可能变（例如开了 layer）──
    const bool pipelinesReady = m_pipelineNormal && m_pipelineAdditive && m_pipelineMultiply && m_pipelineScreen;
    if(pipelinesReady && sameFingerprint)
        return;

    m_pipelineScreen.reset();
    m_pipelineMultiply.reset();
    m_pipelineAdditive.reset();
    m_pipelineNormal.reset();

    // 链式创建：任何一条失败就停手，四条全部作废。
    // render() 只以 m_pipelineNormal 判断"能不能画"，不存在半残状态。
    m_pipelineNormal = createPipeline(rt, spine::BlendMode_Normal);
    if(m_pipelineNormal)
        m_pipelineAdditive = createPipeline(rt, spine::BlendMode_Additive);
    if(m_pipelineAdditive)
        m_pipelineMultiply = createPipeline(rt, spine::BlendMode_Multiply);
    if(m_pipelineMultiply)
        m_pipelineScreen = createPipeline(rt, spine::BlendMode_Screen);

    if(!m_pipelineScreen) {
        m_pipelineNormal.reset();
        m_pipelineAdditive.reset();
        m_pipelineMultiply.reset();
        m_geometryState = State::failed;
        return;
    }

}

// ─────────────────────────────────────────────────────────────
// createPipeline：失败返回 nullptr（告警一次），由 ensureResources 统一处理
// ─────────────────────────────────────────────────────────────

std::unique_ptr<QRhiGraphicsPipeline> SpinePlayer::SpineRenderer::createPipeline(QRhiRenderTarget* rt, spine::BlendMode blendMode)
{
    assert(rt);
    assert(rt->renderPassDescriptor());
    assert(m_rhi);
    assert(m_dummySrb);

    const QShader &vs = SpineShader::vertexShader();
    const QShader &fs = SpineShader::fragmentShader();
    if(!vs.isValid() || !fs.isValid())
        return nullptr;   // 加载着色器时已经告警过

    auto pipeline = std::unique_ptr<QRhiGraphicsPipeline>(m_rhi->newGraphicsPipeline());
    if(!pipeline)[[unlikely]]
        error_handling::panic("Spine RHI: out of memory while creating graphics pipeline");

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

    // 需要响应场景图的裁剪（QSGRenderNode::RenderState::scissorRect）：
    // 声明 UsesScissor 后，render() 每次都必须给出 scissor 矩形。
    pipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);

    // 混合因子对齐 spine-webgl 实现：
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    switch(blendMode) {
    default:[[fallthrough]];
    case spine::BlendMode_Normal:
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
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
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        break;
    case spine::BlendMode_Screen:
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcColor;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcColor;
        break;
    }
    pipeline->setTargetBlends({ blend });
    pipeline->setSampleCount(rt->sampleCount());
    pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    pipeline->setRenderPassDescriptor(rt->renderPassDescriptor());

    if(!pipeline->create())[[unlikely]] {
        error_handling::warn(u"Spine RHI: failed to create graphics pipeline; rendering disabled"_s);
        return nullptr;
    }

    return pipeline;
}

// ─────────────────────────────────────────────────────────────
// 纹理：指针由 m_textureCache 持有；失败时缓存 nullptr，避免每帧重试
// ─────────────────────────────────────────────────────────────

QRhiTexture* SpinePlayer::SpineRenderer::getOrCreateTexture(QtTextureLoader::Texture* texKey, QRhiResourceUpdateBatch* rub)
{
    assert(texKey);
    assert(rub);

    if(auto it = m_textureCache.find(texKey); it != m_textureCache.end())
        return it->second.get();   // 可能为 nullptr：上次就失败了

    // 输入非法（atlas 里的图片打不开）：跳过引用它的批次
    if(texKey->isNull())[[unlikely]] {
        error_handling::warn(u"Spine RHI: atlas image is null; the batch referencing it is skipped"_s);
        m_textureCache[texKey] = nullptr;
        return nullptr;
    }

    QRhiTexture* texture = m_rhi->newTexture(QRhiTexture::RGBA8, texKey->size(), 1,
        QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips);
    if(!texture)[[unlikely]]
        error_handling::panic("Spine RHI: out of memory while creating texture");

    if(!texture->create())[[unlikely]] {
        error_handling::warn(u"Spine RHI: failed to create atlas texture; the batch referencing it is skipped"_s);
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

QRhiShaderResourceBindings* SpinePlayer::SpineRenderer::getOrCreateSrb(QtTextureLoader::Texture* texKey, QRhiTexture* texture)
{
    assert(texKey);
    assert(texture);
    assert(m_ubo);
    assert(m_sampler);

    if(auto it = m_srbCache.find(texKey); it != m_srbCache.end())
        return it->second.get();   // 可能为 nullptr：上次就失败了

    QRhiShaderResourceBindings* srb = m_rhi->newShaderResourceBindings();
    if(!srb)[[unlikely]]
        error_handling::panic("Spine RHI: out of memory while creating shader resource bindings");

    srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, texture, m_sampler.get())
    });
    if(!srb->create())[[unlikely]] {
        error_handling::warn(u"Spine RHI: failed to create shader resource bindings; the batch referencing it is skipped"_s);
        delete srb;
        m_srbCache[texKey] = nullptr;
        return nullptr;
    }

    m_srbCache.emplace(texKey, std::unique_ptr<QRhiShaderResourceBindings>(srb));
    return srb;
}