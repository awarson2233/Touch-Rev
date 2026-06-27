#pragma once

#include "StringUtils.h"
#include <string>
#include <fstream>
#include <sstream>

namespace touchrev::common
{
inline std::wstring LoadTextFileUtf8(const std::wstring& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file)
    {
        return {};
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return ToWideFromUtf8(stream.str());
}
}
