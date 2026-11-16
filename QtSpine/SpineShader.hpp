#pragma once
#include<QString>
#include<QResource>
#include<QByteArray>
#include<rhi/qshader.h>
#include"error_handling.hpp"

namespace SpineShader
{
    using namespace Qt::StringLiterals;
    // 失败时返回非法 QShader；调用方的 static 缓存保证只告警一次
    inline QShader loadShader(const QString& path)
    {
        const QResource resource(path);
        if(!resource.isValid() || resource.size() == 0)[[unlikely]] {
            error_handling::warn(u"Spine RHI: shader resource"_s + path + u"is missing or empty; rendering disabled"_s);
            return QShader();
        }

        const QByteArray data(reinterpret_cast<const char*>(resource.data()), resource.size());
        QShader shader = QShader::fromSerialized(data);
        if(!shader.isValid())[[unlikely]] {
            error_handling::warn(u"Spine RHI: shader"_s + path + u"is not a valid QShader; rendering disabled"_s);
            return QShader();
        }
        return shader;
    }

    // 着色器只解析一次：pipeline 会因为 render target 变化被反复重建
    inline const QShader& vertexShader()
    {
        static const QShader shader = loadShader(u":/shaders/spine.vert.qsb"_s);
        return shader;
    }

    inline const QShader& fragmentShader()
    {
        static const QShader shader = loadShader(u":/shaders/spine.frag.qsb"_s);
        return shader;
    }
}