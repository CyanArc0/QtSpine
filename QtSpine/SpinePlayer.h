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

class SpinePlayer : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QUrl skeletonSource READ get_skeletonSource WRITE set_skeletonSource NOTIFY skeletonSourceChanged)
    Q_PROPERTY(QUrl atlasSource READ get_atlasSource WRITE set_atlasSource NOTIFY atlasSourceChanged)

    Q_PROPERTY(QString skinName READ get_skinName WRITE set_skinName NOTIFY skinNameChanged)
    Q_PROPERTY(QString animationName READ get_animationName WRITE set_animationName NOTIFY animationNameChanged)
    Q_PROPERTY(bool loop READ get_loop WRITE set_loop NOTIFY loopChanged)

    Q_PROPERTY(QStringList skinNames READ get_skinNames NOTIFY skinNamesChanged)
    Q_PROPERTY(QStringList animationNames READ get_animationNames NOTIFY animationNamesChanged)

    Q_PROPERTY(qreal skeletonWidth READ get_skeletonWidth NOTIFY skeletonWidthChanged)
    Q_PROPERTY(qreal skeletonHeight READ get_skeletonHeight NOTIFY skeletonHeightChanged)

    Q_PROPERTY(QRectF contentRect READ get_contentRect NOTIFY contentRectChanged)

signals:
    void skeletonSourceChanged();
    void atlasSourceChanged();

    void skinNameChanged();
    void animationNameChanged();
    void loopChanged();

    void skinNamesChanged();
    void animationNamesChanged();

    void skeletonWidthChanged();
    void skeletonHeightChanged();

    void contentRectChanged();

public:
    QUrl get_skeletonSource()const;
    void set_skeletonSource(const QUrl&);
    QUrl get_atlasSource()const;
    void set_atlasSource(const QUrl&);

    QString get_skinName()const;
    void set_skinName(const QString&);
    QString get_animationName()const;
    void set_animationName(const QString&);
    bool get_loop()const;
    void set_loop(bool);

    QStringList get_skinNames() const;
    QStringList get_animationNames()const;

    qreal get_skeletonWidth()const;
    qreal get_skeletonHeight()const;

    QRectF get_contentRect()const;

public:
    Q_INVOKABLE bool playAnimation(const QString &animationName, bool loop = true, int trackIndex = 0);
    Q_INVOKABLE bool setAnimation(int trackIndex, const QString& animationName, bool loop);

public:
    explicit SpinePlayer(QQuickItem* parent = nullptr);

    virtual void componentComplete()override;

protected:
    class SpineRenderer;

    virtual QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updateInfo) override;

private:
    void clearAssets();
    bool loadAssets();

    void setSkin(const QString& skinName);

    bool isValid()const;

    std::optional<std::pair<QVector2D,QVector2D>> contentTransform() const;

    void advance();

private:
    struct Config
    {
        QUrl skeletonSource;
        QUrl atlasSource;
        QString skinName;
        QString animationName;
        bool loop = false;
    };
    std::unique_ptr<Config> config;

    QUrl skeletonSource;
    QUrl atlasSource;

    QtTextureLoader textureLoader;

    std::unique_ptr<spine::Atlas> atlas;
    std::unique_ptr<spine::SkeletonData> skeletonData;
    std::unique_ptr<spine::Skeleton> skeleton;
    std::unique_ptr<spine::AnimationStateData> animationStateData;
    std::unique_ptr<spine::AnimationState> animationState;

    spine::SkeletonRenderer skeletonRenderer;

    QTimer timer;
    QElapsedTimer times;
    uint64_t time = 0;

    bool texturesNeedReset = false;
};
