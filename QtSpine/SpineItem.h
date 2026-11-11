#pragma once
#include<QQuickItem>
#include<QSGRenderNode>
#include<QUrl>
#include<QString>
#include<QStringList>
#include<QColor>
#include<QRectF>
#include<QTimer>
#include<QElapsedTimer>
#include<QVector2D>
#include<QVector>
#include<memory>
#include<unordered_map>
#include<vector>
#include<cstddef>
#include<cstdint>
#include<rhi/qrhi.h>
#include<spine/spine.h>
#include"QtTextureLoader.hpp"
#include"SpineVertex.h"

class SpineItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QUrl skeletonSource READ get_skeletonSource WRITE set_skeletonSource NOTIFY skeletonSourceChanged)
    Q_PROPERTY(QUrl atlasSource READ get_atlasSource WRITE set_atlasSource NOTIFY atlasSourceChanged)
    Q_PROPERTY(QString animationName READ get_animationName WRITE set_animationName NOTIFY animationNameChanged)

    // 播放控制
    Q_PROPERTY(bool paused READ get_paused WRITE set_paused NOTIFY pausedChanged)
    Q_PROPERTY(qreal timeScale READ get_timeScale WRITE set_timeScale NOTIFY timeScaleChanged)
    Q_PROPERTY(bool loop READ get_loop WRITE set_loop NOTIFY loopChanged)
    // 切换动画时的混入时长（秒），0 表示直接切换
    Q_PROPERTY(qreal mixDuration READ get_mixDuration WRITE set_mixDuration NOTIFY mixDurationChanged)

    // 内容信息 / 播放信息
    Q_PROPERTY(QString skin READ get_skin WRITE set_skin NOTIFY skinChanged)
    Q_PROPERTY(QStringList skinNames READ get_skinNames NOTIFY skinNamesChanged)
    Q_PROPERTY(QStringList animationNames READ get_animationNames NOTIFY animationNamesChanged)
    Q_PROPERTY(QColor skeletonColor READ get_skeletonColor WRITE set_skeletonColor NOTIFY skeletonColorChanged)
    Q_PROPERTY(QRectF contentRect READ get_contentRect NOTIFY contentRectChanged)
    Q_PROPERTY(qreal currentTime READ get_currentTime WRITE set_currentTime NOTIFY currentTimeChanged)

    Q_PROPERTY(qreal skeletonScale READ get_skeletonScale WRITE set_skeletonScale NOTIFY skeletonScaleChanged)
    Q_PROPERTY(qreal skeletonWidth READ get_skeletonWidth NOTIFY skeletonWidthChanged)
    Q_PROPERTY(qreal skeletonHeight READ get_skeletonHeight NOTIFY skeletonHeightChanged)

signals:
    void skeletonSourceChanged();
    void atlasSourceChanged();
    void animationNameChanged();

    void pausedChanged();
    void timeScaleChanged();
    void loopChanged();
    void mixDurationChanged();

    void skinChanged();
    void skinNamesChanged();
    void animationNamesChanged();
    void skeletonColorChanged();
    // 每帧推进后都会发一次：包围盒随姿态变化，绑定在它上面的 QML 需要重新求值
    void contentRectChanged();
    void currentTimeChanged();

    // 非循环动画播放完毕（循环动画每圈结束也会收到 spine 的 complete，这里会过滤掉）
    void finished(int track);
    // spine 事件时间轴上的事件：name 是事件名，intValue/floatValue/stringValue 是它的负载
    void eventFired(const QString &name, int intValue, qreal floatValue, const QString &stringValue);
    // 所有可恢复错误统一走这里（加载失败、找不到动画/皮肤……）。
    // 同一个错误只上报一次；信号延迟到下一轮事件循环发出，
    // 这样 QML 里声明式的 onErrorOccurred 也能收到（属性赋值早于处理器连接）。
    void errorOccurred(const QString &message);

    void skeletonScaleChanged();
    void skeletonWidthChanged();
    void skeletonHeightChanged();

public:
    explicit SpineItem(QQuickItem* parent = nullptr);

    QUrl get_skeletonSource()const;
    void set_skeletonSource(const QUrl&);
    QUrl get_atlasSource()const;
    void set_atlasSource(const QUrl&);
    QString get_animationName()const;
    void set_animationName(const QString&);

    bool get_paused()const;
    void set_paused(bool value);
    qreal get_timeScale()const;
    void set_timeScale(qreal value);
    bool get_loop()const;
    void set_loop(bool value);
    qreal get_mixDuration()const;
    void set_mixDuration(qreal seconds);

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    // 在指定轨道上播放动画。返回 false 表示没播成（资源未就绪、轨道号非法或找不到同名动画，
    // 找不到时会通过 errorOccurred 上报，不影响当前播放）
    Q_INVOKABLE bool playAnimation(const QString &name);
    Q_INVOKABLE bool playAnimation(const QString &name, int track, bool loop);
    // 在当前动画之后排队播放（track 0）；delay 单位秒。返回值同上
    Q_INVOKABLE bool addAnimation(const QString &name, qreal delay, bool loop);

    // 重新加载当前 source（加载失败后重试、或资源在磁盘上更新后用；setter 对相同 URL 不会重新加载）
    Q_INVOKABLE void reload();

    QString get_skin()const;
    void set_skin(const QString &name);
    QStringList get_skinNames()const;
    QStringList get_animationNames()const;

    QColor get_skeletonColor()const;
    void set_skeletonColor(const QColor &color);

    // 当前姿态在 item 局部坐标系里的包围盒
    QRectF get_contentRect()const;

    qreal get_currentTime()const;
    void set_currentTime(qreal seconds);

    qreal get_skeletonScale()const;
    void set_skeletonScale(qreal newSkeletonScale);
    qreal get_skeletonWidth()const;
    qreal get_skeletonHeight()const;

protected:
    // 直接画进 Qt Quick 当前 render pass 的节点。
    // 这样 dst（混合目标）就是场景里已经画好的背景，Normal / Additive / Multiply / Screen
    // 的混合语义与 spine 官方运行时一致；QQuickRhiItem 是离屏纹理，做不到这一点。
    class SpineNode : public QSGRenderNode
    {
    public:
        void prepare() override;                            // 更新缓冲、纹理、pipeline
        void render(const RenderState* state) override;     // 只记录 draw call
        void releaseResources() override;                   // 释放所有 QRhi 资源
        RenderingFlags flags() const override;
        StateFlags changedStates() const override;

        // 由 updatePaintNode() 在同步阶段调用（此时 GUI 线程被阻塞）
        void synchronize(SpineItem* item);

    private:
        void ensureResources(QRhiRenderTarget* rt);
        void buildGeometry();
        void releaseGpuResources();

        std::unique_ptr<QRhiGraphicsPipeline> createPipeline(QRhiRenderTarget* rt, spine::BlendMode blendMode);
        QRhiTexture* getOrCreateTexture(QtTextureLoader::Texture* texKey, QRhiResourceUpdateBatch* rub);
        QRhiShaderResourceBindings* getOrCreateSrb(QtTextureLoader::Texture* texKey, QRhiTexture* texture);

        struct FrameGroup {
            QtTextureLoader::Texture* texKey;
            spine::BlendMode blendMode;
            std::size_t indexOffset;
            std::size_t indexCount;
        };

        QRhi* m_rhi = nullptr;
        QVector<quint32> m_rpdFormat;   // 建 pipeline 时 render pass 的格式指纹
        int m_sampleCount = 1;

        // 资源创建失败后不再每帧重试（否则 60Hz 刷日志），
        // 只有当设备 / render pass 环境变了才重新尝试。
        bool m_creationFailed = false;
        QVector<quint32> m_failedFormat;
        int m_failedSampleCount = -1;

        // 只有在 synchronize() 更新过内容时才重建/上传几何
        bool m_geometryDirty = true;

        std::unique_ptr<QRhiSampler> m_sampler;
        std::unique_ptr<QRhiBuffer>  m_ubo;

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

        float m_vertexScale = 1.0;
        QVector2D m_vertexTranslate;

        std::vector<SpineVertex> m_vertexData;
        std::vector<uint32_t> m_indexData;
        std::vector<FrameGroup> m_frameGroups;
    };

    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updateInfo) override;

private:
    // 把 spine 的回调转成 Qt 信号。AnimationState 只保存裸指针，
    // 所以这个对象必须比它活得久 —— 它是 SpineItem 的成员，满足这一点。
    class AnimationListener : public spine::AnimationStateListenerObject
    {
    public:
        explicit AnimationListener(SpineItem* item) : m_item(item) {}
        void callback(spine::AnimationState* state, spine::EventType type,
                      spine::TrackEntry* entry, spine::Event* event) override;

    private:
        SpineItem* m_item = nullptr;
    };

    void clearAssets();
    void loadAssets();
    void setAnimation();
    void advance();

    // 统一的错误上报：日志 + errorOccurred 信号，同一条错误只报一次
    void reportError(const QString &message);

    // 渲染端与 contentRect 共用的一套变换：骨架坐标 → item 局部坐标
    // 返回 false 表示当前没有可用的骨架/尺寸
    bool contentTransform(float &scale, float &tx, float &ty) const;

private:
    QUrl skeletonSource;
    QUrl atlasSource;
    QString animationName;
    QString skin;
    QColor skeletonColor = QColor(Qt::white);
    QString lastError;   // 上一次上报过的错误，用于去重

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

    bool paused = false;
    qreal timeScale = 1.0;
    bool loop = true;
    qreal mixDuration = 0.0;

    AnimationListener listener{this};

    bool texturesNeedReset = false;
};
