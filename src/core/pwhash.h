// 道号密码：PBKDF2-HMAC-SHA256（自写，无第三方库）
#pragma once
#include <string>
namespace nz::pw {
std::string sha256(const std::string& data);                 // 32 字节原始摘要
std::string pbkdf2(const std::string& pass, const std::string& salt, int iter, size_t len = 32);
std::string toHex(const std::string& raw);
// 生成存储串：pbkdf2-sha256$迭代$盐hex$摘要hex
std::string makeRecord(const std::string& pass);
bool verifyRecord(const std::string& record, const std::string& pass);
}
