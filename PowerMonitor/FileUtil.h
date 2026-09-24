// FileUtil.h : 跨平台文件读写工具
#pragma once
#include <string>

namespace FileUtil
{
    // 读取整个文件到 content，成功返回 true
    bool readFile(const std::string& utf8_path, std::string& content);

    // 原子写：先写 <path>.tmp，再替换为目标文件，成功返回 true
    bool writeFileAtomic(const std::string& utf8_path, const std::string& content);

    // 文件是否存在
    bool exists(const std::string& utf8_path);
}
