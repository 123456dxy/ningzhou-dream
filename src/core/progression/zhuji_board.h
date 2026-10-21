#pragma once
#include <span>
#include <string_view>

namespace nz {
struct ZhujiNode {
    const char* Id;
    const char* Label;
    int X;
    int Y;
    const char* Kind;
    const char* GateId;
    int Cost;
    int SuccessPercent;
    int Points;
    int MaxAttempts;
};
struct ZhujiEdge {
    const char* From;
    const char* To;
    const char* UnlockedBy;
};
std::span<const ZhujiNode> mfZhujiNodes();
std::span<const ZhujiEdge> mfZhujiEdges();
int mfZhujiNodeIndex(std::string_view Id);
}
