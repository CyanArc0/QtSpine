#pragma once
#include<QString>
#include<QResource>
#include<QByteArray>
#include<rhi/qshader.h>
#include"error_handling.hpp"

namespace SpineShader
{
    // 失败时返回非法 QShader；调用方的 static 缓存保证只告警一次
    inline QShader loadShader(const QString& path)
    {
        const QResource resource(path);
        if(!resource.isValid() || resource.size() == 0)[[unlikely]] {
            qWarning() << "Spine RHI: shader resource" << path << "is missing or empty; rendering disabled";
            return QShader();
        }

        const QByteArray data(reinterpret_cast<const char*>(resource.data()), resource.size());
        QShader shader = QShader::fromSerialized(data);
        if(!shader.isValid())[[unlikely]] {
            qWarning() << "Spine RHI: shader" << path << "is not a valid QShader; rendering disabled";
            return QShader();
        }
        return shader;
    }

    // 着色器只解析一次：pipeline 会因为 render target 变化被反复重建
    inline const QShader& vertexShader()
    {
        static const QShader shader = loadShader(QStringLiteral(":/shaders/spine.vert.qsb"));
        return shader;
    }

    inline const QShader& fragmentShader()
    {
        static const QShader shader = loadShader(QStringLiteral(":/shaders/spine.frag.qsb"));
        return shader;
    }
}