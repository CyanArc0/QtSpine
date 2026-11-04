#pragma once
#include <spine/TextureLoader.h>
#include <spine/Atlas.h>
#include <QImage>
#include <QString>
#include <QDebug>

class QtTextureLoader : public spine::TextureLoader
{
public:
    using Texture = QImage;

    virtual void load(spine::AtlasPage& page, const spine::String& path)override
    {
        // Load texture from path
        const QString filepath = QString::fromUtf8(path.buffer(), path.length());
        QImage image(filepath);
        if (image.isNull()) {
            qWarning() << "Spine: failed to load texture" << filepath;
            page.texture = nullptr;
            return;
        }

        // ✅ 根据 AtlasPage::pma 决定是否预乘，仅在这里做一次
        if (page.pma) {
            // 源已预乘：只重排字节序（B,G,R,A → R,G,B,A），不执行预乘
            image = image.convertToFormat(QImage::Format_RGBA8888);
            // 然后只改格式标记，像素值一字节不改
            image = QImage(image.constBits(), image.width(), image.height(),
                           image.bytesPerLine(),
                           QImage::Format_RGBA8888_Premultiplied).copy();
        } else {
            // 源非预乘：convertToFormat 会正确执行一次预乘
            image = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        }

        // Store the texture in the page
        page.texture = new QImage(std::move(image));

        // Set texture dimensions (required)
        page.width  = static_cast<Texture*>(page.texture)->width();
        page.height = static_cast<Texture*>(page.texture)->height();
    }

    virtual void unload(void* texture)override
    {
        // Unload the texture
        delete static_cast<Texture*>(texture);
    }
};