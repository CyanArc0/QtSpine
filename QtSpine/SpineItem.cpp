#include "SpineItem.h"
#include <QByteArray>
#include <QFile>
#include <QQmlFile>
#include <QIODevice>
#include <QDebug>

QByteArray SpineItem::readResource(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) [[unlikely]] {
        qWarning() << "Spine RHI: cannot open resource" << path;
        return {};
    }
    return f.readAll();
}

SpineItem::SpineItem(QQuickRhiItem *parent) : QQuickRhiItem(parent)
{
    setFlag(ItemHasContents, true);//允许自定义场景图内容
    setAlphaBlending(true);//背景透明

    connect(&timer, &QTimer::timeout, this, &SpineItem::advance);
    timer.setInterval(16);
    timer.start();
    times.start();
}

QUrl SpineItem::get_skeletonSource() const { return skeletonSource; }

void SpineItem::set_skeletonSource(const QUrl &new_skeletonSource)
{
    if (skeletonSource == new_skeletonSource) return;
    skeletonSource = new_skeletonSource;
    emit skeletonSource_changed();
    loadAssets();
}

QUrl SpineItem::get_atlasSource() const { return atlasSource; }

void SpineItem::set_atlasSource(const QUrl &new_atlasSource)
{
    if (atlasSource == new_atlasSource) return;
    atlasSource = new_atlasSource;
    emit atlasSource_changed();
    loadAssets();
}

QString SpineItem::get_animationName() const { return animationName; }

void SpineItem::set_animationName(const QString &new_animationName)
{
    if (animationName == new_animationName) return;
    animationName = new_animationName;
    emit animationName_changed();
    if (!skeletonData_ptr || !animationState_ptr) return;
    if (!animationName.isEmpty()) {
        spine::Animation* animation = skeletonData_ptr->findAnimation(
            spine::String(animationName.toUtf8().constData()));
        if (!animation) [[unlikely]] {
            qWarning() << "Spine: animation not found:" << animationName;
            animationState_ptr->clearTrack(0);
            return;
        }
        animationState_ptr->setAnimation(0, animation, true);
    } else {
        animationState_ptr->clearTrack(0);
    }
}

void SpineItem::loadAssets()
{
    if (skeletonSource.isEmpty() || atlasSource.isEmpty()) return;

    // 读取所有文件到内存
    QFile atlasFile(QQmlFile::urlToLocalFileOrQrc(atlasSource));
    if (!atlasFile.open(QIODevice::ReadOnly)) [[unlikely]] {
        qWarning() << "Spine: cannot open atlas" << atlasSource;
        return;
    }
    QByteArray atlasFileData = atlasFile.readAll();
    atlasFile.close();
    //QByteArray atlasFileData = readResource(QQmlFile::urlToLocalFileOrQrc(atlasSource));

    QFile skeletonFile(QQmlFile::urlToLocalFileOrQrc(skeletonSource));
    if (!skeletonFile.open(QIODevice::ReadOnly)) [[unlikely]] {
        qWarning() << "Spine: cannot open skeleton" << skeletonSource;
        return;
    }
    QByteArray skeletonFileData = skeletonFile.readAll();
    skeletonFile.close();
    //QByteArray skeletonFileData = readResource(QQmlFile::urlToLocalFileOrQrc(skeletonSource));

    // 按依赖倒序释放旧资源，接管新资源
    animationState_ptr.reset();
    animationStateData_ptr.reset();
    skeleton_ptr.reset();
    skeletonData_ptr.reset();
    atlas_ptr.reset();

    // 加载骨骼数据（根据后缀自动选择 JSON 或二进制）
    QString atlasDir = QQmlFile::urlToLocalFileOrQrc(atlasSource.adjusted(QUrl::RemoveFilename));
    atlas_ptr = std::make_unique<spine::Atlas>(
        atlasFileData.constData(), atlasFileData.size(),
        atlasDir.toUtf8().constData(), &textureLoader);

    spine::AtlasAttachmentLoader attachmentLoader(atlas_ptr.get());
    if (skeletonSource.fileName().endsWith(".skel", Qt::CaseInsensitive)) {
        spine::SkeletonBinary binary(&attachmentLoader);
        skeletonData_ptr = std::unique_ptr<spine::SkeletonData>(binary.readSkeletonData(reinterpret_cast<const unsigned char*>(skeletonFileData.constData()), skeletonFileData.size()));
        if (!skeletonData_ptr) [[unlikely]] {
            qWarning() << "Spine: failed to read skeleton data from .skel";
            return;
        }
    } else {
        spine::SkeletonJson json(&attachmentLoader);
        skeletonData_ptr = std::unique_ptr<spine::SkeletonData>(json.readSkeletonData(skeletonFileData.constData()));
        if (!skeletonData_ptr) [[unlikely]] {
            qWarning() << "Spine: failed to read skeleton data from .json";
            return;
        }
    }

    // 创建 Skeleton 与 AnimationState
    skeleton_ptr = std::make_unique<spine::Skeleton>(skeletonData_ptr.get());
    animationStateData_ptr = std::make_unique<spine::AnimationStateData>(skeletonData_ptr.get());
    animationState_ptr = std::make_unique<spine::AnimationState>(animationStateData_ptr.get());

    // 设置默认动画（如果已指定）
    if (!animationName.isEmpty()) {
        spine::Animation* animation = skeletonData_ptr->findAnimation(
            spine::String(animationName.toUtf8().constData()));
        if (!animation) [[unlikely]] {
            qWarning() << "Spine: animation not found:" << animationName;
        } else {
            animationState_ptr->setAnimation(0, animation, true);
        }
    }

    setImplicitWidth(skeletonData_ptr->getWidth());
    setImplicitHeight(skeletonData_ptr->getHeight());

    texturesNeedReset = true;
    lastTime = times.elapsed();
    update();
}

void SpineItem::advance()
{
    if (!animationState_ptr || !skeleton_ptr) return;

    const qint64 now = times.elapsed();
    float delta = (now - lastTime) / 1000.0f;
    lastTime = now;

    animationState_ptr->update(delta);
    animationState_ptr->apply(*skeleton_ptr);
    skeleton_ptr->update(delta);
    skeleton_ptr->updateWorldTransform(spine::Physics_Update);

    update();
}

QQuickRhiItemRenderer *SpineItem::createRenderer()
{
    return new SpineRenderer;
}