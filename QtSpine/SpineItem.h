#pragma once
#include<QQuickItem>
#include<QUrl>
#include<QString>
#include<QStringList>
#include<QRectF>
#include<QTimer>
#include<QElapsedTimer>
#include<QVector2D>
#include<memory>
#include<cstddef>
#include<cstdint>
#include<optional>
#include<utility>
#include<spine/spine.h>
#include"QtTextureLoader.hpp"

class SpineItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QUrl skeletonSource READ get_skeletonSource WRITE set_skeletonSource NOTIFY skeletonSourceChanged)
    Q_PROPERTY(QUrl atlasSource READ get_atlasSource WRITE set_atlasSource NOTIFY atlasSourceChanged)

    Q_PROPERTY(qreal skeletonWidth READ get_skeletonWidth NOTIFY skeletonWidthChanged)
    Q_PROPERTY(qreal skeletonHeight READ get_skeletonHeight NOTIFY skeletonHeightChanged)

    Q_PROPERTY(QStringList animationNames READ get_animationNames NOTIFY animationNamesChanged)

    Q_PROPERTY(QRectF contentRect READ get_contentRect NOTIFY contentRectChanged)

signals:
    void skeletonSourceChanged();
    void atlasSourceChanged();

    void skeletonWidthChanged();
    void skeletonHeightChanged();

    void animationNamesChanged();

    void contentRectChanged();

public:
    explicit SpineItem(QQuickItem* parent = nullptr);

    QUrl get_skeletonSource()const;
    void set_skeletonSource(const QUrl&);
    QUrl get_atlasSource()const;
    void set_atlasSource(const QUrl&);

    qreal get_skeletonWidth()const;
    qreal get_skeletonHeight()const;

    QStringList get_animationNames()const;

    QRectF get_contentRect()const;

    Q_INVOKABLE bool playAnimation(const QString &name);

protected:
    class SpineNode;

    virtual QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updateInfo) override;

private:
    void clearAssets();
    bool loadAssets();

    void advance();

    std::optional<std::pair<QVector2D,QVector2D>> contentTransform() const;

private:
    QUrl skeletonSource;
    QUrl atlasSource;

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
