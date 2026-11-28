#pragma once
#include<spine/Atlas.h>
#include<rhi/qrhi.h>

inline namespace SpineSampler
{
    inline constexpr QRhiSampler::Filter toMagFilter(spine::TextureFilter magFilter)noexcept
    {
        switch(magFilter)
        {
        case spine::TextureFilter_Nearest:
        case spine::TextureFilter_MipMapNearestNearest:
        case spine::TextureFilter_MipMapNearestLinear:
            return QRhiSampler::Nearest;
        case spine::TextureFilter_Linear:
        case spine::TextureFilter_MipMapLinearLinear:
        case spine::TextureFilter_MipMapLinearNearest:
        case spine::TextureFilter_MipMap:
        case spine::TextureFilter_Unknown:
        default:
            return QRhiSampler::Linear;
        }
    }

    inline constexpr QRhiSampler::Filter toMinFilter(spine::TextureFilter minFilter)noexcept
    {
        switch(minFilter) {
        case spine::TextureFilter_Nearest:
        case spine::TextureFilter_MipMapNearestNearest:
        case spine::TextureFilter_MipMapNearestLinear:
            return QRhiSampler::Nearest;
        case spine::TextureFilter_Linear:
        case spine::TextureFilter_MipMapLinearLinear:
        case spine::TextureFilter_MipMapLinearNearest:
        case spine::TextureFilter_MipMap:
        case spine::TextureFilter_Unknown:
        default:
            return QRhiSampler::Linear;
        }
    }

    // None = 不采样 mipmap，纹理也不必分配 mip 链
    inline constexpr QRhiSampler::Filter toMipmapMode(spine::TextureFilter minFilter)noexcept
    {
        switch(minFilter) {
        case spine::TextureFilter_MipMapNearestNearest:
        case spine::TextureFilter_MipMapLinearNearest:
            return QRhiSampler::Nearest;
        case spine::TextureFilter_MipMapLinearLinear:
        case spine::TextureFilter_MipMapNearestLinear:
        case spine::TextureFilter_MipMap:
            return QRhiSampler::Linear;
        case spine::TextureFilter_Nearest:
        case spine::TextureFilter_Linear:
        case spine::TextureFilter_Unknown:
        default:
            return QRhiSampler::None;
        }
    }

    inline constexpr QRhiSampler::AddressMode toAddressMode(spine::TextureWrap wrap)noexcept
    {
        switch(wrap)
        {
        case spine::TextureWrap_Repeat:
            return QRhiSampler::Repeat;
        case spine::TextureWrap_MirroredRepeat:
            return QRhiSampler::Mirror;
        case spine::TextureWrap_ClampToEdge:
        default:
            return QRhiSampler::ClampToEdge;
        }
    }
}
