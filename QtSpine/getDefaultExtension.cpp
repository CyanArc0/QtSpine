#include <spine/Extension.h>

// 必须在全局命名空间提供此工厂函数
spine::SpineExtension* spine::getDefaultExtension()
{
    return new spine::DefaultSpineExtension();
}