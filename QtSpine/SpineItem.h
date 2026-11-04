#pragma once
#include <QQuickRhiItem>
#include <QQuickRhiItemRenderer>
#include <QUrl>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
#include <unordered_map>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <rhi/qrhi.h>
#include <spine/spine.h>
#include "QtTextureLoader.hpp"
#include "SpineVertex.h"

class SpineItem : public QQuickRhiItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl skeletonSource READ get_skeletonSource WRITE set_skeletonSource NOTIFY skeletonSource_changed)
    Q_PROPERTY(QUrl atlasSource READ get_atlasSource WRITE set_atlasSource NOTIFY atlasSource_changed)
    Q_PROPERTY(QString animationName READ get_animationName WRITE set_animationName NOTIFY animationName_changed)

public:
    SpineItem();

    QUrl get_skeletonSource() const;
    void set_skeletonSource(const QUrl&);
    QUrl get_atlasSource() const;
    void set_atlasSource(const QUrl&);
    QString get_animationName() const;
    void set_animationName(const QString&);

signals:
    void skeletonSource_changed();
    void atlasSource_changed();
    void animationName_changed();

protected:
    static QByteArray readResource(const QString &path);

protected:
    class SpineRenderer : public QQuickRhiItemRenderer
    {
    public:
        explicit SpineRenderer(SpineItem *item);

        void initialize(QRhiCommandBuffer *cb) override;
        void synchronize(QQuickRhiItem *item) override;
        void render(QRhiCommandBuffer *cb) override;

    private:
        std::unique_ptr<QRhiGraphicsPipeline> createPipeline(spine::BlendMode blendMode);
        QRhiTexture *getOrCreateTexture(void *spineTexKey, QRhiResourceUpdateBatch *rub);
        QRhiShaderResourceBindings *getOrCreateSrb(void *texKey, QRhiTexture *texture);

        struct FrameGroup {
            void *texKey;
            spine::BlendMode blendMode;
            std::size_t vertexOffset;
            std::size_t vertexCount;
            std::size_t indexOffset;
            std::size_t indexCount;
        };

        const SpineItem *m_item = nullptr;

        QRhi *m_rhi = nullptr;
        std::unique_ptr<QRhiSampler> m_sampler;
        std::unique_ptr<QRhiBuffer>  m_ubo;
        std::unique_ptr<QRhiShaderResourceBindings> m_dummySrb;

        std::unique_ptr<QRhiGraphicsPipeline> m_pipelineNormal;
        std::unique_ptr<QRhiGraphicsPipeline> m_pipelineAdditive;
        std::unique_ptr<QRhiGraphicsPipeline> m_pipelineMultiply;
        std::unique_ptr<QRhiGraphicsPipeline> m_pipelineScreen;

        std::unordered_map<void*, std::unique_ptr<QRhiTexture>> m_textureCache;
        std::unordered_map<void*, std::unique_ptr<QRhiShaderResourceBindings>> m_srbCache;

        std::vector<SpineVertex> m_vertexData;
        std::vector<uint32_t> m_indexData;
        std::vector<FrameGroup> m_frameGroups;

        spine::SkeletonRenderer m_skeletonRenderer;

        std::unique_ptr<QRhiBuffer> m_vbuf;
        std::unique_ptr<QRhiBuffer> m_ibuf;

        // 纹理预创建标志：仅首帧或资源重载后为 true
        bool m_texturesPending = true;
    };

protected:
    QQuickRhiItemRenderer *createRenderer() override;

private:
    void load_assets();
    void advance();

private:
    QUrl skeletonSource;
    QUrl atlasSource;
    QString animationName;

    QtTextureLoader textureLoader;

    std::unique_ptr<spine::Atlas> atlas_ptr;
    std::unique_ptr<spine::SkeletonData> skeletonData_ptr;
    std::unique_ptr<spine::Skeleton> skeleton_ptr;
    std::unique_ptr<spine::AnimationStateData> animationStateData_ptr;
    std::unique_ptr<spine::AnimationState> animationState_ptr;

    QTimer timer;
    QElapsedTimer times;
    int64_t lastTime = 0;

    bool texturesNeedReset = false;

};