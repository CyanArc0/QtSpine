#pragma once
#include<QString>
#include<QResource>
#include<QByteArray>
#include<rhi/qshader.h>
#include"error_handling.hpp"

struct SpineShader
{
private:
    // 失败时返回非法 QShader；调用方的 static 缓存保证只告警一次
    static QShader loadShader(const QString& path)
    {
        using namespace Qt::StringLiterals;
        const QResource resource(path);
        if(!resource.isValid() || resource.size() == 0)[[unlikely]] {
            error_handling::warn(u"Spine RHI: shader resource "_s + path + u" is missing or empty; rendering disabled"_s);
            return QShader();
        }

        const QByteArray data(reinterpret_cast<const char*>(resource.data()), resource.size());
        QShader shader = QShader::fromSerialized(data);
        if(!shader.isValid())[[unlikely]] {
            error_handling::warn(u"Spine RHI: shader "_s + path + u" is not a valid QShader; rendering disabled"_s);
            return QShader();
        }
        return shader;
    }

public:
    // 着色器只解析一次：pipeline 会因为 render target 变化被反复重建
    static const QShader& vertexShader()
    {
        using namespace Qt::StringLiterals;
        static const QShader shader = loadShader(u":/shaders/spine.vert.qsb"_s);
        return shader;
    }

    static const QShader& fragmentShader()
    {
        using namespace Qt::StringLiterals;
        static const QShader shader = loadShader(u":/shaders/spine.frag.qsb"_s);
        return shader;
    }
};