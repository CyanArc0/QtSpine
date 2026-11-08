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
    Q_PROPERTY(QUrl skeletonSource READ get_skeletonSource WRITE set_skeletonSource NOTIFY skeletonSourceChanged)
    Q_PROPERTY(QUrl atlasSource READ get_atlasSource WRITE set_atlasSource NOTIFY atlasSourceChanged)
    Q_PROPERTY(QString animationName READ get_animationName WRITE set_animationName NOTIFY animationNameChanged)

    Q_PROPERTY(qreal leftPadding   MEMBER leftPadding NOTIFY leftPaddingChanged)
    Q_PROPERTY(qreal topPadding    MEMBER topPadding NOTIFY topPaddingChanged)
    Q_PROPERTY(qreal rightPadding  MEMBER rightPadding NOTIFY rightPaddingChanged)
    Q_PROPERTY(qreal bottomPadding MEMBER bottomPadding NOTIFY bottomPaddingChanged)

    Q_PROPERTY(qreal skeletonScale MEMBER skeletonScale NOTIFY skeletonScaleChanged)

public:
    explicit SpineItem(QQuickRhiItem *parent = nullptr);

    QUrl get_skeletonSource() const;
    void set_skeletonSource(const QUrl&);
    QUrl get_atlasSource() const;
    void set_atlasSource(const QUrl&);
    QString get_animationName() const;
    void set_animationName(const QString&);

signals:
    void skeletonSourceChanged();
    void atlasSourceChanged();
    void animationNameChanged();

    void leftPaddingChanged();
    void topPaddingChanged();
    void rightPaddingChanged();
    void bottomPaddingChanged();

    void skeletonScaleChanged();

protected:
    static QByteArray readResource(const QString &path);

protected:
    class SpineRenderer : public QQuickRhiItemRenderer
    {
    public:
        void initialize(QRhiCommandBuffer *cb) override;
        void synchronize(QQuickRhiItem *item) override;
        void render(QRhiCommandBuffer *cb) override;

    private:
        std::unique_ptr<QRhiGraphicsPipeline> createPipeline(spine::BlendMode blendMode);
        QRhiTexture *getOrCreateTexture(QtTextureLoader::Texture *spineTexKey, QRhiResourceUpdateBatch *rub);
        QRhiShaderResourceBindings *getOrCreateSrb(QtTextureLoader::Texture *texKey, QRhiTexture *texture);

        struct FrameGroup {
            QtTextureLoader::Texture *texKey;
            spine::BlendMode blendMode;
            std::size_t vertexOffset;
            std::size_t vertexCount;
            std::size_t indexOffset;
            std::size_t indexCount;
        };

        QRhi *m_rhi = nullptr;

        std::unique_ptr<QRhiSampler> m_sampler;
        std::unique_ptr<QRhiBuffer>  m_ubo;

        std::unique_ptr<QRhiBuffer> m_vbuf;
        std::unique_ptr<QRhiBuffer> m_ibuf;

        std::unordered_map<QtTextureLoader::Texture*, std::unique_ptr<QRhiTexture>> m_textureCache;
        std::unique_ptr<QRhiShaderResourceBindings> m_dummySrb;

        std::unordered_map<QtTextureLoader::Texture*, std::unique_ptr<QRhiShaderResourceBindings>> m_srbCache;
        std::unique_ptr<QRhiGraphicsPipeline> m_pipelineNormal;
        std::unique_ptr<QRhiGraphicsPipeline> m_pipelineAdditive;
        std::unique_ptr<QRhiGraphicsPipeline> m_pipelineMultiply;
        std::unique_ptr<QRhiGraphicsPipeline> m_pipelineScreen;

        spine::RenderCommand *m_renderCommands = nullptr;

        float m_vertexScale = 1.0;
        QVector2D m_vertexTranslate;
        QVector2D m_projectionSize;

        std::vector<SpineVertex> m_vertexData;
        std::vector<uint32_t> m_indexData;
        std::vector<FrameGroup> m_frameGroups;
    };

protected:
    QQuickRhiItemRenderer *createRenderer() override;

private:
    void loadAssets();
    void advance();

private:
    QUrl skeletonSource;
    QUrl atlasSource;
    QString animationName;

    qreal leftPadding = 0;
    qreal topPadding = 0;
    qreal rightPadding = 0;
    qreal bottomPadding = 0;
    qreal skeletonScale = 1.0;

    QtTextureLoader textureLoader;

    std::unique_ptr<spine::Atlas> atlas_ptr;
    std::unique_ptr<spine::SkeletonData> skeletonData_ptr;
    std::unique_ptr<spine::Skeleton> skeleton_ptr;
    std::unique_ptr<spine::AnimationStateData> animationStateData_ptr;
    std::unique_ptr<spine::AnimationState> animationState_ptr;

    spine::SkeletonRenderer skeletonRenderer;

    QTimer timer;
    QElapsedTimer times;
    int64_t time = 0;

    bool texturesNeedReset = false;
};