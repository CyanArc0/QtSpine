#pragma once
#include<QDebug>
#include<QString>
#include<cstdlib>
#include<cstdio>
#include<cassert>

namespace error_handling
{
    inline void warn(const QString& s)
    {
        qWarning().noquote() << s;
    }

    using cstring_t = const char*;
    [[noreturn]] inline void panic(cstring_t s)noexcept
    {
        std::fputs(s, stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
        std::abort();
    }
}