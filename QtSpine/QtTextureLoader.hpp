#pragma once
#include<spine/TextureLoader.h>
#include<spine/Atlas.h>
#include<spine/SpineString.h>
#include<QImage>
#include<QString>
#include"qspine_cast.hpp"
#include"error_handling.hpp"

class QtTextureLoader : public spine::TextureLoader
{
public:
    struct Texture : QImage
    {
        struct SamplerState {
            spine::TextureFilter minFilter = spine::TextureFilter_Linear;
            spine::TextureFilter magFilter = spine::TextureFilter_Linear;
            spine::TextureWrap uWrap = spine::TextureWrap_ClampToEdge;
            spine::TextureWrap vWrap = spine::TextureWrap_ClampToEdge;
            friend auto operator<=>(SamplerState,SamplerState) = default;
        };
        SamplerState samplerState;
        explicit Texture(QImage image) : QImage(std::move(image)) {}
        Texture(QImage image, SamplerState state) : QImage(std::move(image)), samplerState(state) {}
    };

    virtual void load(spine::AtlasPage& page, const spine::String& path)override
    {
        using namespace Qt::StringLiterals;
        // Load texture from path
        const QString filepath = qspine_cast<QString>(path);
        QImage image(filepath);
        if(image.isNull())[[unlikely]] {
            error_handling::warn(u"Spine: failed to load texture "_s + filepath);
            page.texture = nullptr;
            return;
        }

        // ✅ 根据 AtlasPage::pma 决定是否预乘，仅在这里做一次
        if(page.pma) {
            // 源已预乘：只重排字节序，不执行预乘
            image = image.convertToFormat(QImage::Format_RGBA8888);
            // 然后只改格式标记，像素值一字节不改
            image.reinterpretAsFormat(QImage::Format_RGBA8888_Premultiplied);
        } else {
            // 源非预乘：convertToFormat 会正确执行一次预乘
            image = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        }

        // Store the texture in the page
        Texture* texture = new Texture(std::move(image));
        texture->samplerState.minFilter = page.minFilter;
        texture->samplerState.magFilter = page.magFilter;
        texture->samplerState.uWrap = page.uWrap;
        texture->samplerState.vWrap = page.vWrap;
        page.texture = texture;

        // Set texture dimensions (required)
        page.width  = texture->width();
        page.height = texture->height();
    }

    virtual void unload(void* texture)override
    {
        // Unload the texture
        delete static_cast<Texture*>(texture);
    }
};