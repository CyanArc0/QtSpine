#include"SpineItem.h"
#include"SpineNode.h"
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
#include"error_handling.hpp"

using namespace Qt::StringLiterals;

SpineItem::SpineItem(QQuickItem* parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);//允许自定义场景图内容

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
    else clearAssets();
    update();
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
    update();
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

QStringList SpineItem::get_animationNames()const
{
    QStringList names;
    if(!skeletonData_ptr) return names;

    spine::Vector<spine::Animation*>& animations = skeletonData_ptr->getAnimations();
    for(const spine::String &name : std::span(animations.buffer(), animations.size()) | std::views::transform(&spine::Animation::getName)) {
        names.append(QString::fromUtf8(name.buffer(), name.length()));
    }
    return names;
}

QRectF SpineItem::get_contentRect()const
{
    if(!skeleton_ptr) return QRectF();
    auto[x,y,w,h] = [this] {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        spine::Vector<float> vertices;   // getBounds 的暂存缓冲
        skeleton_ptr->getBounds(x, y, w, h, vertices);
        return std::tuple{x,y,w,h};
    }();
    // 没有可见附件时 getBounds 会留下 ±FLT_MAX，算出来是 ±inf
    if(!std::isfinite(w) || !std::isfinite(h) || w < 0.0f || h < 0.0f)
        return QRectF();

    auto o = contentTransform();
    if(!o) return QRectF();
    auto&&[s,t] = *o;
    const auto[sx,sy] = s;
    const auto[tx,ty] = t;

    // spine 的 y 轴朝上，item 的 y 轴朝下：把 (minX, minY, w, h) 映射成 item 矩形
    const qreal left = +x * sx + tx;
    const qreal top = -(y + h) * sy + ty;
    return QRectF(left, top, w * sx, h * sy);
}

bool SpineItem::playAnimation(const QString &name)
{
    if(!animationState_ptr || !skeletonData_ptr) return false;

    spine::Animation *animation = skeletonData_ptr->findAnimation(spine::String(name.toUtf8().constData()));
    if(!animation)[[unlikely]] {
        error_handling::warn(u"animation not found: "_s+name+u" (available: "_s+'['+get_animationNames().join(',')+']'+')');
        return false;
    }

    animationState_ptr->setAnimation(0, animation, true);
    return true;
}

QSGNode* SpineItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    SpineNode* node = static_cast<SpineNode*>(oldNode);
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
    emit skeletonWidthChanged();
    emit skeletonHeightChanged();
    emit animationNamesChanged();
    emit contentRectChanged();
    setImplicitWidth(0);
    setImplicitHeight(0);
}

bool SpineItem::loadAssets()
{
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
        error_handling::warn(u"failed to parse skeleton data: "_s+skeletonSource.toString());
        clearAssets();
        return false;
    }

    // 创建 Skeleton 与 AnimationState
    skeleton_ptr = std::make_unique<spine::Skeleton>(skeletonData_ptr.get());
    animationStateData_ptr = std::make_unique<spine::AnimationStateData>(skeletonData_ptr.get());
    animationState_ptr = std::make_unique<spine::AnimationState>(animationStateData_ptr.get());
    animationState_ptr->apply(*skeleton_ptr);
    skeleton_ptr->updateWorldTransform(spine::Physics_Update);

    emit skeletonWidthChanged();
    emit skeletonHeightChanged();
    emit animationNamesChanged();
    emit contentRectChanged();
    setImplicitWidth(skeletonData_ptr->getWidth());
    setImplicitHeight(skeletonData_ptr->getHeight());

    texturesNeedReset = true;
    time = times.elapsed();

    assert(atlas_ptr);
    assert(skeletonData_ptr);
    assert(skeleton_ptr);
    assert(animationStateData_ptr);
    assert(animationState_ptr);
    return true;
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

    emit contentRectChanged();
}

std::optional<std::pair<QVector2D,QVector2D>> SpineItem::contentTransform() const
{
    if(!skeletonData_ptr) return std::nullopt;

    const float w = this->width();
    const float h = this->height();
    if(w <= 0.0f || h <= 0.0f) return std::nullopt;

    // 骨架包围盒居中于 item
    const float anchorX = w * 0.5f;
    const float anchorY = h * 0.5f;

    const float s = 1.0f;

    const float tx = anchorX - skeletonData_ptr->getWidth() * s * 0.5f - skeletonData_ptr->getX() * s;
    const float ty = anchorY + skeletonData_ptr->getHeight() * s * 0.5f + skeletonData_ptr->getY() * s;
    return std::make_pair(QVector2D{1.0f,1.0f},QVector2D{tx,ty});
}