// NPC 姓名生成自测：对照 JS 版 socNPCName。
#include "harness.h"

#include "core/society.h"

using namespace nz;

namespace nztest {

void runNameTests() {
    std::printf("\n-- NPC 姓名（对照 JS 版 socNPCName）--\n");

    // 期望值来自 tools/name-check.mjs：JS 版实际输出
    static const char* want12[12] = {
        "苏清川", "秦清白", "齐云尘", "宁怀衡", "陆怀辞", "叶映均",
        "莫照影", "上官闻秋", "柳闻书", "宋长迟", "徐明水", "慕容明华",
    };
    bool allMatch = true;
    for (int i = 0; i < 12; ++i) {
        if (socNPCName(i) != want12[i]) {
            allMatch = false;
            std::printf("        第 %d 个：得到「%s」，期望「%s」\n",
                        i, socNPCName(i).c_str(), want12[i]);
        }
    }
    check(allMatch, "前 12 个名字与 JS 版一致");
    checkStr(socNPCName(100), "苏知晏", "第 100 个名字为「苏知晏」");
    checkStr(socNPCName(1000), "苏苍声", "第 1000 个名字为「苏苍声」");
    checkStr(socNPCName(2000), "苏雨华", "第 2000 个名字为「苏雨华」");

    // 同一个序号必须稳定得到同一个名字（否则存档读回来会变人）
    checkStr(socNPCName(777), socNPCName(777), "同序号名字稳定");

    // 姓名不应为空
    bool nonEmpty = true;
    for (int i = 0; i < 500; ++i)
        if (socNPCName(i).empty()) nonEmpty = false;
    check(nonEmpty, "前 500 个名字都不为空");
}

} // namespace nztest
