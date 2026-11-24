#pragma once
#include"SpinePlayer.h"
#include<QSGRenderNode>
#include<QVector2D>
#include<QVector>
#include<memory>
#include<unordered_map>
#include<vector>
#include<cstddef>
#include<cstdint>
#include<utility>
#include<rhi/qrhi.h>
#include<spine/spine.h>
#include"QtTextureLoader.hpp"
#include"SpineVertex.h"

class SpinePlayer::SpineRenderer : public QSGRenderNode
{
    friend QSGNode* SpinePlayer::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updateInfo);
public:
    virtual RenderingFlags flags() const override;
    virtual StateFlags changedStates() const override;

    virtual void prepare() override;                            // 更新缓冲、纹理、pipeline
    virtual void render(const RenderState* state) override;     // 只记录 draw call
    virtual void releaseResources() override;                   // 释放所有 QRhi 资源

protected:
    void synchronize(SpinePlayer* item); // 由 updatePaintNode() 在同步阶段调用（此时 GUI 线程被阻塞）
    void initialize(QRhiRenderTarget* rt);

    std::unique_ptr<QRhiGraphicsPipeline> createPipeline(QRhiRenderTarget* rt, spine::BlendMode blendMode);
    QRhiTexture* acquireTexture(QtTextureLoader::Texture* texKey, QRhiResourceUpdateBatch* rub);
    QRhiShaderResourceBindings* acquireSrb(QtTextureLoader::Texture* texKey, QRhiTexture* texture);

private:
    struct FrameGroup {
        QtTextureLoader::Texture* texKey;
        spine::BlendMode blendMode;
        std::size_t indexOffset;
        std::size_t indexCount;
    };
    enum class State : int8_t {Failed = -1, Pending = 0, Valid = +1};
    using Fingerprint = std::pair<int,QVector<quint32>>;

    QRhi* m_rhi = nullptr;

    State m_geometryState = State::Pending;

    Fingerprint m_fingerprint;

    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiBuffer> m_ubo;

    std::unique_ptr<QRhiBuffer> m_vbuf;
    std::unique_ptr<QRhiBuffer> m_ibuf;

    std::unique_ptr<QRhiShaderResourceBindings> m_dummySrb;
    std::unordered_map<QtTextureLoader::Texture*, std::unique_ptr<QRhiTexture>> m_textureCache;
    std::unordered_map<QtTextureLoader::Texture*, std::unique_ptr<QRhiShaderResourceBindings>> m_srbCache;

    std::unique_ptr<QRhiGraphicsPipeline> m_pipelineNormal;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipelineAdditive;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipelineMultiply;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipelineScreen;

    spine::RenderCommand* m_renderCommands = nullptr;
    QVector2D m_transformScale;
    QVector2D m_transformOffset;

    std::vector<SpineVertex> m_vertexData;
    std::vector<uint32_t> m_indexData;
    std::vector<FrameGroup> m_frameGroups;
};