#pragma once
#include<QString>
#include<spine/SpineString.h>

template<typename T>
T qspine_cast(const auto&);

template<>
inline QString qspine_cast(const spine::String& s)
{
    return QString::fromUtf8(s.buffer(),s.length());
}

template<>
inline spine::String qspine_cast(const QString& s)
{
    return spine::String(s.toUtf8().constData());
}