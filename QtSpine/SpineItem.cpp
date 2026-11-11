#include"SpineItem.h"
#include<QByteArray>
#include<QFile>
#include<QQmlFile>
#include<QIODevice>
#include<algorithm>
#include<cmath>
#include"error_handling.hpp"

SpineItem::SpineItem(QQuickItem* parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);//允许自定义场景图内容

    connect(&timer, &QTimer::timeout, this, &SpineItem::advance);
    timer.setInterval(1000 / 60);
    timer.start();
    times.start();
}

void SpineItem::reportError(const QString &message)
{
    // 同一个错误只上报一次：animationName / skin 可能是绑定，
    // 绑定每帧求值的话会把日志和 QML 弹窗都刷爆。
    if(message == lastError) return;
    lastError = message;

    qWarning().noquote() << "Spine:" << message;

    // 延迟到下一轮事件循环：QML 的 onErrorOccurred 是在属性赋值之后才连上的，
    // 立即 emit 的话声明式写法会漏掉这个信号。
    QMetaObject::invokeMethod(this, [this, message] { emit errorOccurred(message); },
                              Qt::QueuedConnection);
}

QUrl SpineItem::get_skeletonSource()const { return skeletonSource; }

void SpineItem::set_skeletonSource(const QUrl &new_skeletonSource)
{
    if(skeletonSource == new_skeletonSource) return;
    skeletonSource = new_skeletonSource;
    emit skeletonSourceChanged();

    if(!skeletonSource.isEmpty() && !atlasSource.isEmpty())
        loadAssets();
    else clearAssets();
}

QUrl SpineItem::get_atlasSource()const { return atlasSource; }

void SpineItem::set_atlasSource(const QUrl &new_atlasSource)
{
    if(atlasSource == new_atlasSource) return;
    atlasSource = new_atlasSource;
    emit atlasSourceChanged();

    if(!skeletonSource.isEmpty() && !atlasSource.isEmpty())
        loadAssets();
    else clearAssets();
}

QString SpineItem::get_animationName()const { return animationName; }

void SpineItem::set_animationName(const QString &new_animationName)
{
    if(animationName == new_animationName) return;
    animationName = new_animationName;
    emit animationNameChanged();

    if(skeletonData_ptr && animationState_ptr)
        setAnimation();
}

bool SpineItem::get_paused()const { return paused; }

void SpineItem::set_paused(bool value)
{
    if(paused == value) return;
    paused = value;

    if(paused) {
        timer.stop();                 // 暂停时不空转
    } else {
        time = times.elapsed();       // 暂停的时长不计入这一帧的 delta
        timer.start();
    }

    emit pausedChanged();
}

qreal SpineItem::get_timeScale()const { return timeScale; }

void SpineItem::set_timeScale(qreal value)
{
    if(qFuzzyCompare(timeScale, value)) return;
    timeScale = value;

    if(animationState_ptr)
        animationState_ptr->setTimeScale(static_cast<float>(timeScale));

    emit timeScaleChanged();
}

bool SpineItem::get_loop()const { return loop; }

void SpineItem::set_loop(bool value)
{
    if(loop == value) return;
    loop = value;

    // 立即作用到 track 0 上正在播放的动画
    if(animationState_ptr) {
        if(spine::TrackEntry *entry = animationState_ptr->getCurrent(0))
            entry->setLoop(loop);
    }

    emit loopChanged();
}

qreal SpineItem::get_mixDuration()const { return mixDuration; }

void SpineItem::set_mixDuration(qreal seconds)
{
    if(qFuzzyCompare(mixDuration, seconds)) return;
    mixDuration = seconds;

    // 只影响之后的切换：spine 在 setAnimation/addAnimation 时才读这个值
    if(animationStateData_ptr)
        animationStateData_ptr->setDefaultMix(static_cast<float>(mixDuration));

    emit mixDurationChanged();
}

void SpineItem::play()
{
    set_paused(false);
}

void SpineItem::pause()
{
    set_paused(true);
}

bool SpineItem::playAnimation(const QString &name)
{
    return playAnimation(name, 0, loop);
}

bool SpineItem::playAnimation(const QString &name, int track, bool loop)
{
    if(!animationState_ptr || !skeletonData_ptr || track < 0) return false;

    spine::Animation *animation = skeletonData_ptr->findAnimation(spine::String(name.toUtf8().constData()));
    if(!animation)[[unlikely]] {
        reportError(QStringLiteral("animation not found: %1 (available: %2)")
                        .arg(name, get_animationNames().join(QStringLiteral(", "))));
        return false;
    }

    animationState_ptr->setAnimation(static_cast<size_t>(track), animation, loop);
    return true;
}

bool SpineItem::addAnimation(const QString &name, qreal delay, bool loop)
{
    if(!animationState_ptr || !skeletonData_ptr) return false;

    spine::Animation *animation = skeletonData_ptr->findAnimation(spine::String(name.toUtf8().constData()));
    if(!animation)[[unlikely]] {
        reportError(QStringLiteral("animation not found: %1 (available: %2)")
                        .arg(name, get_animationNames().join(QStringLiteral(", "))));
        return false;
    }

    animationState_ptr->addAnimation(0, animation, loop, static_cast<float>(delay));
    return true;
}

void SpineItem::reload()
{
    if(skeletonSource.isEmpty() || atlasSource.isEmpty()) return;
    loadAssets();
}

QString SpineItem::get_skin()const { return skin; }

void SpineItem::set_skin(const QString &name)
{
    if(skin == name) return;
    skin = name;

    if(skeleton_ptr) {
        const spine::String skinName(skin.toUtf8().constData());
        if(skin.isEmpty() || skeletonData_ptr->findSkin(skinName)) {
            // spine 会把新皮肤的附件重新挂到槽位上，不会重置骨骼姿态
            skeleton_ptr->setSkin(skinName);
            update();
            emit contentRectChanged();   // 换了附件，包围盒可能变
        } else {
            reportError(QStringLiteral("skin not found: %1 (available: %2)")
                            .arg(skin, get_skinNames().join(QStringLiteral(", "))));
        }
    }

    emit skinChanged();
}

QStringList SpineItem::get_skinNames()const
{
    QStringList names;
    if(!skeletonData_ptr) return names;

    spine::Vector<spine::Skin *> &skins = skeletonData_ptr->getSkins();
    for(size_t i = 0, n = skins.size(); i < n; ++i) {
        const spine::String &name = skins[i]->getName();
        names.append(QString::fromUtf8(name.buffer(), static_cast<int>(name.length())));
    }
    return names;
}

QStringList SpineItem::get_animationNames()const
{
    QStringList names;
    if(!skeletonData_ptr) return names;

    spine::Vector<spine::Animation *> &animations = skeletonData_ptr->getAnimations();
    for(size_t i = 0, n = animations.size(); i < n; ++i) {
        const spine::String &name = animations[i]->getName();
        names.append(QString::fromUtf8(name.buffer(), static_cast<int>(name.length())));
    }
    return names;
}

QColor SpineItem::get_skeletonColor()const { return skeletonColor; }

void SpineItem::set_skeletonColor(const QColor &color)
{
    if(skeletonColor == color) return;
    skeletonColor = color;

    if(skeleton_ptr) {
        skeleton_ptr->getColor() = spine::Color(
            static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
            static_cast<float>(color.blueF()), static_cast<float>(color.alphaF()));
        update();
    }

    emit skeletonColorChanged();
}

bool SpineItem::contentTransform(float &scale, float &tx, float &ty) const
{
    if(!skeletonData_ptr) return false;

    const float w = static_cast<float>(width());
    const float h = static_cast<float>(height());
    if(w <= 0.0f || h <= 0.0f) return false;

    // 骨架包围盒居中于 item
    const float anchorX = w * 0.5f;
    const float anchorY = h * 0.5f;

    // 负值会把模型翻面，clamp 掉
    const float s = std::max(0.0f, static_cast<float>(skeletonScale));

    spine::SkeletonData *data = skeletonData_ptr.get();
    scale = s;
    tx = anchorX - data->getWidth() * s * 0.5f - data->getX() * s;
    ty = anchorY + data->getHeight() * s * 0.5f + data->getY() * s;
    return true;
}

QRectF SpineItem::get_contentRect()const
{
    float scale = 1.0f, tx = 0.0f, ty = 0.0f;
    if(!contentTransform(scale, tx, ty)) return QRectF();

    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    spine::Vector<float> vertices;   // getBounds 的暂存缓冲
    skeleton_ptr->getBounds(x, y, w, h, vertices);

    // 没有可见附件时 getBounds 会留下 ±FLT_MAX，算出来是 ±inf
    if(!std::isfinite(w) || !std::isfinite(h) || w < 0.0f || h < 0.0f)
        return QRectF();

    // spine 的 y 轴朝上，item 的 y 轴朝下：把 (minX, minY, w, h) 映射成 item 矩形
    const qreal left = x * scale + tx;
    const qreal top = -(y + h) * scale + ty;
    return QRectF(left, top, w * scale, h * scale);
}

qreal SpineItem::get_currentTime()const
{
    if(!animationState_ptr) return 0.0;
    if(spine::TrackEntry *entry = animationState_ptr->getCurrent(0))
        return static_cast<qreal>(entry->getTrackTime());
    return 0.0;
}

void SpineItem::set_currentTime(qreal seconds)
{
    if(!animationState_ptr || !skeleton_ptr) return;

    spine::TrackEntry *entry = animationState_ptr->getCurrent(0);
    if(!entry) return;

    const float t = static_cast<float>(std::max(0.0, seconds));
    entry->setTrackTime(t);
    entry->setAnimationLast(t);   // 把"上次应用时间"也挪过来，避免补发区间内的 spine 事件

    // 立即把新姿态应用一次，暂停时也能看到 seek 的结果
    animationState_ptr->apply(*skeleton_ptr);
    skeleton_ptr->updateWorldTransform(spine::Physics_Update);

    update();
    emit currentTimeChanged();
    emit contentRectChanged();   // 姿态变了，包围盒也可能变
}

qreal SpineItem::get_skeletonScale()const { return skeletonScale; }

void SpineItem::set_skeletonScale(qreal newSkeletonScale)
{
    if(qFuzzyCompare(skeletonScale, newSkeletonScale)) return;
    skeletonScale = newSkeletonScale;
    emit skeletonScaleChanged();

    if(!skeletonData_ptr) return;
    setImplicitWidth(get_skeletonWidth()*skeletonScale);
    setImplicitHeight(get_skeletonHeight()*skeletonScale);
}

qreal SpineItem::get_skeletonWidth()const
{
    if(!skeletonData_ptr) return 0;
    return skeletonData_ptr->getWidth();
}

qreal SpineItem::get_skeletonHeight()const
{
    if(!skeletonData_ptr) return 0;
    return skeletonData_ptr->getHeight();
}

QSGNode* SpineItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    auto* node = static_cast<SpineNode*>(oldNode);
    if(!node)
        node = new SpineNode;

    node->synchronize(this);
    return node;
}

void SpineItem::clearAssets()
{
    // 还没有加载过任何东西：什么都不用做
    if(!atlas_ptr && !skeletonData_ptr && !skeleton_ptr && !animationStateData_ptr && !animationState_ptr) return;

    // 与 loadAssets 相同：按依赖倒序释放
    animationState_ptr.reset();
    animationStateData_ptr.reset();
    skeleton_ptr.reset();
    skeletonData_ptr.reset();
    atlas_ptr.reset();

    texturesNeedReset = true;   // 让渲染端丢掉纹理 / SRB 缓存
    lastError.clear();
    emit skeletonWidthChanged();
    emit skeletonHeightChanged();
    emit skinNamesChanged();
    emit animationNamesChanged();
    emit contentRectChanged();
    setImplicitWidth(0);
    setImplicitHeight(0);
    update();                   // 关键：请求重绘，render() 才能把上一帧清掉
}

void SpineItem::loadAssets()
{
    assert(!atlasSource.isEmpty());
    assert(!skeletonSource.isEmpty());

    // 读取所有文件到内存
    QFile atlasFile(QQmlFile::urlToLocalFileOrQrc(atlasSource));
    if(!atlasFile.open(QIODevice::ReadOnly))[[unlikely]] {
        reportError(QStringLiteral("cannot open atlas: %1").arg(atlasSource.toString()));
        return;
    }
    const QByteArray atlasFileData = atlasFile.readAll();
    atlasFile.close();
    //QByteArray atlasFileData = readResource(QQmlFile::urlToLocalFileOrQrc(atlasSource));

    QFile skeletonFile(QQmlFile::urlToLocalFileOrQrc(skeletonSource));
    if(!skeletonFile.open(QIODevice::ReadOnly))[[unlikely]] {
        reportError(QStringLiteral("cannot open skeleton: %1").arg(skeletonSource.toString()));
        return;
    }
    const QByteArray skeletonFileData = skeletonFile.readAll();
    skeletonFile.close();
    //QByteArray skeletonFileData = readResource(QQmlFile::urlToLocalFileOrQrc(skeletonSource));

    // 按依赖倒序释放旧资源，接管新资源
    animationState_ptr.reset();
    animationStateData_ptr.reset();
    skeleton_ptr.reset();
    skeletonData_ptr.reset();
    atlas_ptr.reset();

    // 加载骨骼数据（根据后缀自动选择 JSON 或二进制）
    const QString atlasDir = QQmlFile::urlToLocalFileOrQrc(atlasSource.adjusted(QUrl::RemoveFilename));
    atlas_ptr = std::make_unique<spine::Atlas>(atlasFileData.constData(), atlasFileData.size(), atlasDir.toUtf8().constData(), &textureLoader);

    spine::AtlasAttachmentLoader attachmentLoader(atlas_ptr.get());
    if(skeletonSource.fileName().endsWith(".skel", Qt::CaseInsensitive)) {
        spine::SkeletonBinary binary(&attachmentLoader);
        skeletonData_ptr = std::unique_ptr<spine::SkeletonData>(binary.readSkeletonData(reinterpret_cast<const unsigned char*>(skeletonFileData.constData()), skeletonFileData.size()));
    } else {
        spine::SkeletonJson json(&attachmentLoader);
        skeletonData_ptr = std::unique_ptr<spine::SkeletonData>(json.readSkeletonData(skeletonFileData.constData()));
    }
    if(!skeletonData_ptr)[[unlikely]] {
        atlas_ptr.reset();

        // 旧资源已在上面释放，这里把状态收敛到"没有骨架"，并请求一次重绘，
        // 让这一帧什么都不画，而不是停在上一次的画面上。
        texturesNeedReset = true;
        emit skeletonWidthChanged();
        emit skeletonHeightChanged();
        emit skinNamesChanged();
        emit animationNamesChanged();
        emit contentRectChanged();
        setImplicitWidth(0);
        setImplicitHeight(0);
        update();
        reportError(QStringLiteral("failed to parse skeleton data: %1").arg(skeletonSource.toString()));
        return;
    }

    // 创建 Skeleton 与 AnimationState
    skeleton_ptr = std::make_unique<spine::Skeleton>(skeletonData_ptr.get());
    animationStateData_ptr = std::make_unique<spine::AnimationStateData>(skeletonData_ptr.get());
    animationStateData_ptr->setDefaultMix(static_cast<float>(mixDuration));
    animationState_ptr = std::make_unique<spine::AnimationState>(animationStateData_ptr.get());
    animationState_ptr->setListener(&listener);
    animationState_ptr->setTimeScale(static_cast<float>(timeScale));

    // 套用 QML 侧设置过的皮肤与整体着色
    if(!skin.isEmpty()) {
        const spine::String skinName(skin.toUtf8().constData());
        if(skeletonData_ptr->findSkin(skinName))
            skeleton_ptr->setSkin(skinName);
        else
            reportError(QStringLiteral("skin not found: %1 (available: %2)")
                            .arg(skin, get_skinNames().join(QStringLiteral(", "))));
    }
    skeleton_ptr->getColor() = spine::Color(
        static_cast<float>(skeletonColor.redF()), static_cast<float>(skeletonColor.greenF()),
        static_cast<float>(skeletonColor.blueF()), static_cast<float>(skeletonColor.alphaF()));

    // 设置默认动画（如果已指定）
    setAnimation();

    emit skinNamesChanged();
    emit animationNamesChanged();

    emit skeletonWidthChanged();
    emit skeletonHeightChanged();
    setImplicitWidth(skeletonData_ptr->getWidth()*skeletonScale);
    setImplicitHeight(skeletonData_ptr->getHeight()*skeletonScale);

    texturesNeedReset = true;
    time = times.elapsed();
    lastError.clear();   // 加载成功：下一次同样的错误可以重新上报
    update();
}

void SpineItem::setAnimation()
{
    assert(skeletonData_ptr);
    assert(animationState_ptr);

    if(!animationName.isEmpty()) {
        spine::Animation* animation = skeletonData_ptr->findAnimation(spine::String(animationName.toUtf8().constData()));
        if(!animation)[[unlikely]] {
            reportError(QStringLiteral("animation not found: %1 (available: %2)")
                            .arg(animationName, get_animationNames().join(QStringLiteral(", "))));
            animationState_ptr->clearTrack(0);
            return;
        }
        animationState_ptr->setAnimation(0, animation, loop);
    } else animationState_ptr->clearTrack(0);
}

void SpineItem::advance()
{
    if(paused || !animationState_ptr || !skeleton_ptr) return;

    const qint64 now = times.elapsed();
    float delta = (now - time) / 1000.0f;
    time = now;

    if(delta > 1.0f/30.0f) delta = 1.0f/30.0f;

    animationState_ptr->update(delta);
    animationState_ptr->apply(*skeleton_ptr);
    skeleton_ptr->update(delta);
    skeleton_ptr->updateWorldTransform(spine::Physics_Update);

    update();

    // 姿态和播放进度都变了：绑定在 currentTime / contentRect 上的 QML 需要重新求值
    emit currentTimeChanged();
    emit contentRectChanged();
}

// ─────────────────────────────────────────────────────────────
// spine 回调 → Qt 信号（在 advance() 里、也就是 GUI 线程被调用）
// ─────────────────────────────────────────────────────────────

void SpineItem::AnimationListener::callback(spine::AnimationState*, spine::EventType type,
                                            spine::TrackEntry* entry, spine::Event* event)
{
    if(!m_item || !entry) return;

    if(type == spine::EventType_Complete) {
        // spine 对循环动画每完成一圈也会发 complete，这里只上报"非循环动作真正结束"
        if(!entry->getLoop())
            emit m_item->finished(entry->getTrackIndex());

    } else if(type == spine::EventType_Event && event) {
        const spine::String &name = event->getData().getName();
        const spine::String &stringValue = event->getStringValue();

        emit m_item->eventFired(
            QString::fromUtf8(name.buffer(), static_cast<int>(name.length())),
            event->getIntValue(),
            event->getFloatValue(),
            QString::fromUtf8(stringValue.buffer(), static_cast<int>(stringValue.length())));
    }
}