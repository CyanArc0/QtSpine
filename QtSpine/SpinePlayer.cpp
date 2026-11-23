#include"SpinePlayer.h"
#include"SpineRenderer.h"
#include<QByteArray>
#include<QFile>
#include<QQmlFile>
#include<QIODevice>
#include<cmath>
#include<ranges>
#include<span>
#include<tuple>
#include<memory>
#include<optional>
#include<utility>
#include"qspine_cast.hpp"
#include"error_handling.hpp"

using namespace Qt::StringLiterals;

SpinePlayer::SpinePlayer(QQuickItem* parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);

    config = std::make_unique<Config>();

    connect(&timer, &QTimer::timeout, this, &SpinePlayer::advance);
    timer.setInterval(1000 / 60);
    timer.start();
    times.start();
}

void SpinePlayer::componentComplete()
{
    QQuickItem::componentComplete();

    assert(config);
    skeletonSource = std::move(config->skeletonSource);
    atlasSource = std::move(config->atlasSource);
    loadAssets();
    if(!config->skinName.isEmpty())
        setSkin(config->skinName);
    if(!config->animationName.isEmpty())
        playAnimation(config->animationName,config->loop);
    update();

    config.reset();
}

QUrl SpinePlayer::get_skeletonSource()const
{
    if(!isComponentComplete()) {
        assert(config);
        return config->skeletonSource;
    }

    return skeletonSource;
}

void SpinePlayer::set_skeletonSource(const QUrl &new_skeletonSource)
{
    if(!isComponentComplete()) {
        assert(config);
        config->skeletonSource = new_skeletonSource;
        emit skeletonSourceChanged();
        return;
    }

    assert(false);
}

QUrl SpinePlayer::get_atlasSource()const
{
    if(!isComponentComplete()) {
        assert(config);
        return config->atlasSource;
    }

    return atlasSource;
}

void SpinePlayer::set_atlasSource(const QUrl &new_atlasSource)
{
    if(!isComponentComplete()) {
        assert(config);
        config->atlasSource = new_atlasSource;
        emit atlasSourceChanged();
        return;
    }

    assert(false);
}

QString SpinePlayer::get_skinName() const
{
    if(!isComponentComplete()) {
        assert(config);
        return config->skinName;
    }

    if(not isValid()) return u""_s;

    assert(skeleton);
    spine::Skin* skin = skeleton->getSkin();
    if(!skin) return u""_s;
    return qspine_cast<QString>(skin->getName());
}

void SpinePlayer::set_skinName(const QString &new_skinName)
{
    if(!isComponentComplete()) {
        assert(config);
        config->skinName = new_skinName;
        emit skinNameChanged();
        return;
    }

    setSkin(new_skinName);
}

QString SpinePlayer::get_animationName() const
{
    if(!isComponentComplete()) {
        assert(config);
        return config->animationName;
    }

    if(not isValid()) return u""_s;

    assert(animationState);
    spine::TrackEntry* track = animationState->getCurrent(0);
    if(!track) return u""_s;
    spine::Animation* animation = track->getAnimation();
    if(!animation) return u""_s;
    return qspine_cast<QString>(animation->getName());
}

void SpinePlayer::set_animationName(const QString &new_animationName)
{
    if(!isComponentComplete()) {
        assert(config);
        config->animationName = new_animationName;
        emit animationNameChanged();
        return;
    }

    assert(false);
}

bool SpinePlayer::get_loop() const
{
    if(!isComponentComplete()) {
        assert(config);
        return config->loop;
    }

    assert(animationState);
    spine::TrackEntry* track = animationState->getCurrent(0);
    if(!track) return false;
    return track->getLoop();
}

void SpinePlayer::set_loop(bool new_loop)
{
    if(!isComponentComplete()) {
        assert(config);
        config->loop = new_loop;
        emit loopChanged();
        return;
    }

    assert(false);
}

QStringList SpinePlayer::get_skinNames() const
{
    if(not isValid()) return {};

    QStringList names;
    assert(skeletonData);
    spine::Vector<spine::Skin*>& skins = skeletonData->getSkins();
    for(const spine::String& name : std::span(skins.buffer(), skins.size()) | std::views::transform(&spine::Skin::getName)) {
        names.append(qspine_cast<QString>(name));
    }
    return names;
}

QStringList SpinePlayer::get_animationNames()const
{
    if(not isValid()) return {};

    QStringList names;
    assert(skeletonData);
    spine::Vector<spine::Animation*>& animations = skeletonData->getAnimations();
    for(const spine::String& name : std::span(animations.buffer(), animations.size()) | std::views::transform(&spine::Animation::getName)) {
        names.append(qspine_cast<QString>(name));
    }
    return names;
}

qreal SpinePlayer::get_skeletonWidth()const
{
    if(not isValid()) return 0;
    assert(skeletonData);
    return skeletonData->getWidth();
}

qreal SpinePlayer::get_skeletonHeight()const
{
    if(not isValid()) return 0;
    assert(skeletonData);
    return skeletonData->getHeight();
}

QRectF SpinePlayer::get_contentRect()const
{
    if(not isValid()) return QRectF();

    auto[x,y,w,h] = [this] {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        spine::Vector<float> vertices;   // getBounds 的暂存缓冲
        assert(skeleton);
        skeleton->getBounds(x, y, w, h, vertices);
        return std::tuple{x,y,w,h};
    }();
    // 没有可见附件时 getBounds 会留下 ±FLT_MAX，算出来是 ±inf
    if(!std::isfinite(w) || !std::isfinite(h) || w < 0.0f || h < 0.0f)
        return QRectF();

    auto o = contentTransform();
    if(!o) return QRectF();
    const auto[s,t] = *o;
    // spine 的 y 轴朝上，item 的 y 轴朝下：把 (minX, minY, w, h) 映射成 item 矩形
    const qreal left = +x * s.x() + t.x();
    const qreal top = -(y + h) * s.y() + t.y();
    return QRectF(left, top, w * s.x(), h * s.y());
}

bool SpinePlayer::playAnimation(const QString &animationName, bool loop, int trackIndex)
{
    return setAnimation(trackIndex, animationName, loop);
}

bool SpinePlayer::setAnimation(int trackIndex, const QString &animationName, bool loop)
{
    if(not isValid()) return false;

    assert(skeletonData);
    spine::Animation *animation = skeletonData->findAnimation(qspine_cast<spine::String>(animationName));
    if(!animation)[[unlikely]] {
        error_handling::warn(u"animation not found: "_s+animationName+u" ;available: "_s+'['+get_animationNames().join(',')+']');
        return false;
    }
    assert(animationState);
    animationState->setAnimation(trackIndex, animation, loop);
    return true;
}

void SpinePlayer::setSkin(const QString& skinName)
{
    if(not isValid()) return;

    assert(skeletonData);
    spine::Skin* skin = skeletonData->findSkin(qspine_cast<spine::String>(skinName));
    if(!skin)[[unlikely]] {
        error_handling::warn(u"skin not found: "_s+skinName+u" ;available: "_s+'['+get_skinNames().join(',')+']');
        return;
    }

    assert(skeleton);
    skeleton->setSkin(skin);
    emit skinNameChanged();

    emit contentRectChanged();   // 换了附件，包围盒可能变
}

QSGNode* SpinePlayer::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    SpineRenderer* node = static_cast<SpineRenderer*>(oldNode);
    if(!node)
        node = new SpineRenderer;

    node->synchronize(this);
    return node;
}

void SpinePlayer::clearAssets()
{
    // 还没有加载过任何东西：什么都不用做
    if(!atlas && !skeletonData && !skeleton && !animationStateData && !animationState) return;

    // 与 loadAssets 相同：按依赖倒序释放
    animationState.reset();
    animationStateData.reset();
    skeleton.reset();
    skeletonData.reset();
    atlas.reset();

    texturesNeedReset = true;   // 让渲染端丢掉纹理 / SRB 缓存
    emit skeletonWidthChanged();
    emit skeletonHeightChanged();
    emit skinNamesChanged();
    emit animationNamesChanged();
    emit contentRectChanged();
    setImplicitWidth(0);
    setImplicitHeight(0);
}

bool SpinePlayer::loadAssets()
{
    // 按依赖倒序释放旧资源，接管新资源
    animationState.reset();
    animationStateData.reset();
    skeleton.reset();
    skeletonData.reset();
    atlas.reset();

    // 读取所有文件到内存
    QFile atlasFile(QQmlFile::urlToLocalFileOrQrc(atlasSource));
    if(!atlasFile.open(QIODevice::ReadOnly))[[unlikely]] {
        error_handling::warn(u"cannot open atlas: "_s+atlasSource.toString());
        return false;
    }
    const QByteArray atlasFileData = atlasFile.readAll();
    atlasFile.close();

    QFile skeletonFile(QQmlFile::urlToLocalFileOrQrc(skeletonSource));
    if(!skeletonFile.open(QIODevice::ReadOnly))[[unlikely]] {
        error_handling::warn(u"cannot open skeleton: "_s+skeletonSource.toString());
        return false;
    }
    const QByteArray skeletonFileData = skeletonFile.readAll();
    skeletonFile.close();

    // 加载骨骼数据（根据后缀自动选择 JSON 或二进制）
    const QString atlasDir = QQmlFile::urlToLocalFileOrQrc(atlasSource.adjusted(QUrl::RemoveFilename));
    atlas = std::make_unique<spine::Atlas>(atlasFileData.constData(), atlasFileData.size(), atlasDir.toUtf8().constData(), &textureLoader);

    spine::AtlasAttachmentLoader attachmentLoader(atlas.get());
    if(skeletonSource.fileName().endsWith(".json", Qt::CaseInsensitive)) {
        spine::SkeletonJson json(&attachmentLoader);
        skeletonData = std::unique_ptr<spine::SkeletonData>(json.readSkeletonData(skeletonFileData.constData()));
    } else {
        spine::SkeletonBinary binary(&attachmentLoader);
        skeletonData = std::unique_ptr<spine::SkeletonData>(binary.readSkeletonData(reinterpret_cast<const unsigned char*>(skeletonFileData.constData()), skeletonFileData.size()));
    }
    if(!skeletonData)[[unlikely]] {
        error_handling::warn(u"failed to parse skeleton data: "_s+skeletonSource.toString());
        clearAssets();
        return false;
    }

    // 创建 Skeleton 与 AnimationState
    skeleton = std::make_unique<spine::Skeleton>(skeletonData.get());
    animationStateData = std::make_unique<spine::AnimationStateData>(skeletonData.get());
    animationState = std::make_unique<spine::AnimationState>(animationStateData.get());
    animationState->apply(*skeleton);
    skeleton->updateWorldTransform(spine::Physics_Update);

    emit skeletonWidthChanged();
    emit skeletonHeightChanged();
    emit skinNamesChanged();
    emit animationNamesChanged();
    emit contentRectChanged();
    setImplicitWidth(skeletonData->getWidth());
    setImplicitHeight(skeletonData->getHeight());

    texturesNeedReset = true;
    time = times.elapsed();

    assert(atlas);
    assert(skeletonData);
    assert(skeleton);
    assert(animationStateData);
    assert(animationState);
    return true;
}

bool SpinePlayer::isValid()const
{
    if(atlas) {
        assert(skeletonData);
        assert(skeleton);
        assert(animationStateData);
        assert(animationState);
        return true;
    }
    else return false;
}

std::optional<std::pair<QVector2D,QVector2D>> SpinePlayer::contentTransform() const
{
    if(not isValid()) return std::nullopt;

    const float w = this->width();
    const float h = this->height();
    if(w <= 0.0f || h <= 0.0f) return std::nullopt;

    // 骨架包围盒居中于 item
    const float anchorX = w * 0.5f;
    const float anchorY = h * 0.5f;

    const float s = 1.0f;

    assert(skeletonData);
    const float tx = anchorX - skeletonData->getWidth() * s * 0.5f - skeletonData->getX() * s;
    const float ty = anchorY + skeletonData->getHeight() * s * 0.5f + skeletonData->getY() * s;
    return std::make_pair(QVector2D{1.0f,1.0f},QVector2D{tx,ty});
}

void SpinePlayer::advance()
{
    if(not isValid()) return;

    const qint64 now = times.elapsed();
    assert(now>=0);
    float delta = (now - time) / 1000.0f;
    time = now;

    if(delta > 1.0f/30.0f) delta = 1.0f/30.0f;

    assert(animationState);
    assert(skeleton);
    animationState->update(delta);
    animationState->apply(*skeleton);
    skeleton->update(delta);
    skeleton->updateWorldTransform(spine::Physics_Update);

    update();

    emit contentRectChanged();
}