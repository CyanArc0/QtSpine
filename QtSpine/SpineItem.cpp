#include"SpineItem.h"
#include<QByteArray>
#include<QFile>
#include<QQmlFile>
#include<QIODevice>
#include"error_handling.hpp"

SpineItem::SpineItem(QQuickRhiItem* parent) : QQuickRhiItem(parent)
{
    setFlag(ItemHasContents, true);//允许自定义场景图内容
    setAlphaBlending(true);//背景透明

    connect(&timer, &QTimer::timeout, this, &SpineItem::advance);
    timer.setInterval(1000 / 60);
    timer.start();
    times.start();
}

QUrl SpineItem::get_skeletonSource()const { return skeletonSource; }

void SpineItem::set_skeletonSource(const QUrl &new_skeletonSource)
{
    if(skeletonSource == new_skeletonSource) return;
    skeletonSource = new_skeletonSource;
    emit skeletonSourceChanged();

    if(!skeletonSource.isEmpty() && !atlasSource.isEmpty())
        loadAssets();
    else clear();
}

QUrl SpineItem::get_atlasSource()const { return atlasSource; }

void SpineItem::set_atlasSource(const QUrl &new_atlasSource)
{
    if(atlasSource == new_atlasSource) return;
    atlasSource = new_atlasSource;
    emit atlasSourceChanged();

    if(!skeletonSource.isEmpty() && !atlasSource.isEmpty())
        loadAssets();
    else clear();
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

qreal SpineItem::get_leftPadding()const { return leftPadding; }

void SpineItem::set_leftPadding(qreal newLeftPadding)
{
    if(qFuzzyCompare(leftPadding, newLeftPadding)) return;
    leftPadding = newLeftPadding;
    emit leftPaddingChanged();

    if(!skeletonData_ptr) return;
    setImplicitWidth(get_skeletonWidth()*skeletonScale+leftPadding+rightPadding);
}

qreal SpineItem::get_topPadding()const { return topPadding; }

void SpineItem::set_topPadding(qreal newTopPadding)
{
    if(qFuzzyCompare(topPadding, newTopPadding)) return;
    topPadding = newTopPadding;
    emit topPaddingChanged();

    if(!skeletonData_ptr) return;
    setImplicitHeight(get_skeletonHeight()*skeletonScale+topPadding+bottomPadding);
}

qreal SpineItem::get_rightPadding()const { return rightPadding; }

void SpineItem::set_rightPadding(qreal newRightPadding)
{
    if(qFuzzyCompare(rightPadding, newRightPadding)) return;
    rightPadding = newRightPadding;
    emit rightPaddingChanged();

    if(!skeletonData_ptr) return;
    setImplicitWidth(get_skeletonWidth()*skeletonScale+leftPadding+rightPadding);
}

qreal SpineItem::get_bottomPadding()const { return bottomPadding; }

void SpineItem::set_bottomPadding(qreal newBottomPadding)
{
    if(qFuzzyCompare(bottomPadding, newBottomPadding)) return;
    bottomPadding = newBottomPadding;
    emit bottomPaddingChanged();

    if(!skeletonData_ptr) return;
    setImplicitHeight(get_skeletonHeight()*skeletonScale+topPadding+bottomPadding);
}

qreal SpineItem::get_skeletonScale()const { return skeletonScale; }

void SpineItem::set_skeletonScale(qreal newSkeletonScale)
{
    if(qFuzzyCompare(skeletonScale, newSkeletonScale)) return;
    skeletonScale = newSkeletonScale;
    emit skeletonScaleChanged();

    if(!skeletonData_ptr) return;
    setImplicitWidth(get_skeletonWidth()*skeletonScale+leftPadding+rightPadding);
    setImplicitHeight(get_skeletonHeight()*skeletonScale+topPadding+bottomPadding);
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

QQuickRhiItemRenderer* SpineItem::createRenderer()
{
    return new SpineRenderer;
}

void SpineItem::clear()
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
    emit skeletonWidthChanged();
    emit skeletonHeightChanged();
    setImplicitWidth(leftPadding + rightPadding);
    setImplicitHeight(topPadding + bottomPadding);
    update();                   // 关键：请求重绘，render() 才能把上一帧清掉
}

void SpineItem::loadAssets()
{
    assert(!atlasSource.isEmpty());
    assert(!skeletonSource.isEmpty());

    // 读取所有文件到内存
    QFile atlasFile(QQmlFile::urlToLocalFileOrQrc(atlasSource));
    if(!atlasFile.open(QIODevice::ReadOnly))[[unlikely]] {
        qWarning() << "Spine: cannot open atlas" << atlasSource;
        return;
    }
    const QByteArray atlasFileData = atlasFile.readAll();
    atlasFile.close();
    //QByteArray atlasFileData = readResource(QQmlFile::urlToLocalFileOrQrc(atlasSource));

    QFile skeletonFile(QQmlFile::urlToLocalFileOrQrc(skeletonSource));
    if(!skeletonFile.open(QIODevice::ReadOnly))[[unlikely]] {
        qWarning() << "Spine: cannot open skeleton" << skeletonSource;
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
        qWarning() << "Spine: failed to read skeleton data" << skeletonSource;
        atlas_ptr.reset();

        // 旧资源已在上面释放，这里把状态收敛到"没有骨架"，并请求一次重绘，
        // 让 render() 把离屏纹理清空，而不是停在上一次的画面上。
        texturesNeedReset = true;
        emit skeletonWidthChanged();
        emit skeletonHeightChanged();
        setImplicitWidth(leftPadding + rightPadding);
        setImplicitHeight(topPadding + bottomPadding);
        update();
        return;
    }

    // 创建 Skeleton 与 AnimationState
    skeleton_ptr = std::make_unique<spine::Skeleton>(skeletonData_ptr.get());
    animationStateData_ptr = std::make_unique<spine::AnimationStateData>(skeletonData_ptr.get());
    animationState_ptr = std::make_unique<spine::AnimationState>(animationStateData_ptr.get());

    // 设置默认动画（如果已指定）
    setAnimation();

    emit skeletonWidthChanged();
    emit skeletonHeightChanged();
    setImplicitWidth(skeletonData_ptr->getWidth()*skeletonScale+leftPadding+rightPadding);
    setImplicitHeight(skeletonData_ptr->getHeight()*skeletonScale+topPadding+bottomPadding);

    texturesNeedReset = true;
    time = times.elapsed();
    update();
}

void SpineItem::setAnimation()
{
    assert(skeletonData_ptr);
    assert(animationState_ptr);

    if(!animationName.isEmpty()) {
        spine::Animation* animation = skeletonData_ptr->findAnimation(spine::String(animationName.toUtf8().constData()));
        if(!animation)[[unlikely]] {
            qWarning() << "Spine: animation not found:" << animationName;
            animationState_ptr->clearTrack(0);
            return;
        }
        animationState_ptr->setAnimation(0, animation, true);
    } else animationState_ptr->clearTrack(0);
}

void SpineItem::advance()
{
    if(!animationState_ptr || !skeleton_ptr) return;

    const qint64 now = times.elapsed();
    float delta = (now - time) / 1000.0f;
    time = now;

    if(delta > 1.0f/30.0f) delta = 1.0f/30.0f;

    animationState_ptr->update(delta);
    animationState_ptr->apply(*skeleton_ptr);
    skeleton_ptr->update(delta);
    skeleton_ptr->updateWorldTransform(spine::Physics_Update);

    update();
}