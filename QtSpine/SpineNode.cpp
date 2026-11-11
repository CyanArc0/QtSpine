#include"SpineItem.h"
#include<QByteArray>
#include<QMatrix4x4>
#include<QResource>
#include<algorithm>
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
    float matrix[16];   // qt_Matrix（投影 × 模型视图）
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
// synchronize：GUI 线程被阻塞，把这一帧要用的数据拷进渲染端
// ─────────────────────────────────────────────────────────────

void SpineItem::SpineNode::synchronize(SpineItem* item)
{
    Q_ASSERT(item);

    m_geometryDirty = true;   // 内容换了一帧，prepare() 里要重建

    if(item->texturesNeedReset) {
        // 纹理换了：引用旧纹理的 SRB 必须一起清掉
        m_srbCache.clear();
        m_textureCache.clear();
        item->texturesNeedReset = false;
    }

    if(!item->skeleton_ptr || !item->skeletonData_ptr) {
        m_renderCommands = nullptr;   // 没有骨架 → 这一帧不画
        return;
    }

    m_renderCommands = item->skeletonRenderer.render(*item->skeleton_ptr);
    if(!m_renderCommands) return;     // 空骨架（没有可画槽位）

    // 骨架坐标 → item 局部坐标的变换与 contentRect 共用，避免两处算法分叉
    float scale = 1.0f, tx = 0.0f, ty = 0.0f;
    if(!item->contentTransform(scale, tx, ty)) {
        m_renderCommands = nullptr;   // 尺寸退化
        return;
    }

    m_vertexScale = scale;
    m_vertexTranslate = { tx, ty };
}

// ─────────────────────────────────────────────────────────────
// prepare：在 render pass 开始之前完成所有资源更新
// ─────────────────────────────────────────────────────────────

void SpineItem::SpineNode::prepare()
{
    QRhiRenderTarget* rt = renderTarget();
    QRhiCommandBuffer* cb = commandBuffer();
    Q_ASSERT(rt && cb);
    Q_ASSERT(rt->renderPassDescriptor());

    ensureResources(rt);

    // 资源未就绪（创建失败）或没有内容：这一帧什么都不画
    if(!m_renderCommands || !m_pipelineNormal || !m_sampler || !m_ubo || !m_dummySrb) {
        m_geometryDirty = true;   // 等资源恢复后重新构建
        return;
    }

    QRhiResourceUpdateBatch* rub = m_rhi->nextResourceUpdateBatch();
    if(!rub)[[unlikely]]
        qFatal("Spine RHI: failed to allocate resource update batch");

    // 顶点/索引/纹理只在内容真的变过（synchronize 跑过）时重建：
    // 窗口因为其它 item 重绘时，没必要把同样的几何再算一遍、再传一遍。
    if(m_geometryDirty) {
        m_vertexData.clear();
        m_indexData.clear();
        m_frameGroups.clear();
        buildGeometry();

        if(!m_vertexData.empty()) {
            // 纹理缺就补建：命中缓存则跳过，成本 O(numCommands)
            for(spine::RenderCommand* c = m_renderCommands; c; c = c->next) {
                if(c->numVertices <= 0 || !c->texture) continue;
                auto* key = static_cast<QtTextureLoader::Texture*>(c->texture);
                getOrCreateTexture(key, rub);
            }

            // SRB 也在 pass 之外建好，render() 里只查表
            for(const FrameGroup &group : m_frameGroups) {
                auto it = m_textureCache.find(group.texKey);
                if(it == m_textureCache.end() || !it->second) continue;
                getOrCreateSrb(group.texKey, it->second.get());
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
                    cb->resourceUpdate(rub);   // rub 里已有纹理/UBO 更新，先提交再退出
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
                    return;
                }
            }

            rub->updateDynamicBuffer(m_vbuf.get(), 0, vbytes, m_vertexData.data());
            rub->updateDynamicBuffer(m_ibuf.get(), 0, ibytes, m_indexData.data());
        }

        m_geometryDirty = false;
    }

    {
        // UBO 每帧更新：opacity 和 item 变换可能在不触发同步的情况下变化
        const QMatrix4x4 mvp = (*projectionMatrix()) * (*matrix());

        SpineUniforms uniforms{};
        std::memcpy(uniforms.matrix, mvp.constData(), sizeof(uniforms.matrix));
        uniforms.opacity = static_cast<float>(inheritedOpacity());
        rub->updateDynamicBuffer(m_ubo.get(), 0, UniformsSize, &uniforms);
    }

    cb->resourceUpdate(rub);
}

// ─────────────────────────────────────────────────────────────
// buildGeometry：把 render command 合并成"同纹理 + 同混合模式"的批次
// ─────────────────────────────────────────────────────────────

void SpineItem::SpineNode::buildGeometry()
{
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

        FrameGroup group;
        group.texKey      = texKey;
        group.blendMode   = blend;
        group.indexOffset = m_indexData.size();
        group.indexCount  = groupIndices;

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
}

// ─────────────────────────────────────────────────────────────
// render：在 Qt Quick 当前 render pass 里直接记录绘制命令。
// 不 beginPass / 不 endPass / 不清屏 —— pass 与清屏由场景图负责。
// 因此 dst 就是已经画好的背景，混合语义与 spine 官方运行时一致。
// ─────────────────────────────────────────────────────────────

void SpineItem::SpineNode::render(const RenderState* state)
{
    if(m_frameGroups.empty() || !m_pipelineNormal || !m_vbuf || !m_ibuf)
        return;

    QRhiCommandBuffer* cb = commandBuffer();
    Q_ASSERT(cb && state);

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

    for(const FrameGroup &group : m_frameGroups) {
        QRhiGraphicsPipeline* pipeline = nullptr;
        switch(group.blendMode) {
        case spine::BlendMode_Additive: pipeline = m_pipelineAdditive.get(); break;
        case spine::BlendMode_Multiply: pipeline = m_pipelineMultiply.get(); break;
        case spine::BlendMode_Screen:   pipeline = m_pipelineScreen.get();   break;
        default:                        pipeline = m_pipelineNormal.get();   break;
        }

        auto it = m_srbCache.find(group.texKey);
        if(it == m_srbCache.end() || !it->second) continue;   // 纹理缺失

        cb->setGraphicsPipeline(pipeline);
        cb->setScissor(scissor);   // 必须在绑定带 UsesScissor 的 pipeline 之后
        cb->setShaderResources(it->second.get());
        cb->setVertexInput(0, 1, &vinput, m_ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(static_cast<quint32>(group.indexCount), 1,
            static_cast<quint32>(group.indexOffset), 0, 0);
    }
}

// ─────────────────────────────────────────────────────────────
// 资源管理
// ─────────────────────────────────────────────────────────────

void SpineItem::SpineNode::releaseGpuResources()
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

    m_geometryDirty = true;    // 缓冲都释放了，下次 prepare 必须重建
    m_creationFailed = false;  // 重新给一次创建机会
}

void SpineItem::SpineNode::releaseResources()
{
    releaseGpuResources();

    m_rhi = nullptr;
    m_rpdFormat.clear();
    m_sampleCount = 1;
}

void SpineItem::SpineNode::ensureResources(QRhiRenderTarget* rt)
{
    QRhi* rhi = rt->rhi();
    QRhiRenderPassDescriptor* rpd = rt->renderPassDescriptor();
    Q_ASSERT(rhi && rpd);

    const QVector<quint32> format = rpd->serializedFormat();
    const int samples = rt->sampleCount();

    if(m_rhi != rhi) {
        // 换了设备：旧对象都属于上一个 QRhi
        releaseGpuResources();
        m_rhi = rhi;
        m_rpdFormat.clear();
        m_sampleCount = 1;
    }

    // 同一环境下已经失败过：不再每帧重试（否则 60Hz 刷日志），
    // 等设备 / render pass 环境变化，或 releaseResources() 之后再试。
    if(m_creationFailed && m_failedFormat == format && m_failedSampleCount == samples)
        return;
    m_creationFailed = false;

    const auto recordFailure = [&] {
        m_creationFailed = true;
        m_failedFormat = format;
        m_failedSampleCount = samples;
    };

    // ── sampler / UBO / dummy SRB 与 render target 无关，只在缺失时补建 ──
    // 创建失败不终止进程：告警一次，本 item 空渲染，环境变化后再试。
    if(!m_sampler) {
        m_sampler.reset(m_rhi->newSampler(
            QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
            QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if(!m_sampler)[[unlikely]]
            qFatal("Spine RHI: out of memory while creating sampler");
        if(!m_sampler->create())[[unlikely]] {
            qWarning("Spine RHI: failed to create sampler; rendering disabled");
            m_sampler.reset();
            recordFailure();
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
            recordFailure();
            return;
        }
    }

    if(!m_dummySrb) {
        // 只是给 pipeline 提供布局，绑定的资源无所谓（见 qrhi.cpp 中
        // QRhiGraphicsPipeline::setShaderResourceBindings 的说明），
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
            recordFailure();
            return;
        }
    }

    // ── pipeline 与 render pass 格式、采样数绑定；render target 可能变（例如开了 layer）──
    const bool pipelinesReady = m_pipelineNormal && m_pipelineAdditive
                             && m_pipelineMultiply && m_pipelineScreen;

    if(pipelinesReady && m_rpdFormat == format && m_sampleCount == samples)
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
        recordFailure();
        return;
    }

    m_rpdFormat = format;
    m_sampleCount = samples;
}

// ─────────────────────────────────────────────────────────────
// createPipeline：失败返回 nullptr（告警一次），由 ensureResources 统一处理
// ─────────────────────────────────────────────────────────────

std::unique_ptr<QRhiGraphicsPipeline> SpineItem::SpineNode::createPipeline(QRhiRenderTarget* rt, spine::BlendMode blendMode)
{
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

    // 需要响应场景图的裁剪（QSGRenderNode::RenderState::scissorRect）：
    // 声明 UsesScissor 后，render() 每次都必须给出 scissor 矩形。
    pipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);

    // 混合因子对齐 spine 官方 GL/WebGL 实现：
    // spine-glfw.cpp 的 blend_modes[] + glBlendFuncSeparate(srcColor, dstColor, srcAlpha, dstColor)
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    switch(blendMode) {
    case spine::BlendMode_Additive:
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::One;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::One;
        break;
    case spine::BlendMode_Multiply:
        blend.srcColor = QRhiGraphicsPipeline::DstColor;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        break;
    case spine::BlendMode_Screen:
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcColor;
        blend.srcAlpha = QRhiGraphicsPipeline::OneMinusSrcColor;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcColor;
        break;
    default:
        // Normal：预乘 alpha 混合，One / OneMinusSrcAlpha 正好是 QRhi 的默认值
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

QRhiTexture* SpineItem::SpineNode::getOrCreateTexture(QtTextureLoader::Texture* texKey, QRhiResourceUpdateBatch* rub)
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

QRhiShaderResourceBindings* SpineItem::SpineNode::getOrCreateSrb(QtTextureLoader::Texture* texKey, QRhiTexture* texture)
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
// 节点属性
// ─────────────────────────────────────────────────────────────

QSGRenderNode::RenderingFlags SpineItem::SpineNode::flags() const
{
    // 只用 QRhi 录制命令，且只在 z = 0 平面绘制。
    // 不声明 BoundedRectRendering：骨架内容可能超出 item 矩形。
    return NoExternalRendering | DepthAwareRendering;
}

QSGRenderNode::StateFlags SpineItem::SpineNode::changedStates() const
{
    // render() 里设置了 scissor（viewport 交给场景图管理）。
    // 声明出来，场景图会在后续批次重新应用自己的 viewport / scissor。
    return ScissorState;
}
