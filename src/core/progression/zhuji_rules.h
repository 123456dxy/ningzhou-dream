#pragma once
#include "core/progression/zhuji_state.h"

namespace nz {
struct Game;
int mfZhujiHealthReward(int Score);
std::string mfZhujiNodeEffect(int NodeIndex);
int mfZhujiNodeSuccessPercent(const ZhujiRun& Run, int NodeIndex);
ZhujiRun mfCreateZhujiRun(ZhujiRoute Route, int PillCount, bool Core,
                         const std::vector<std::string>& Traits, std::uint32_t Seed);
ZhujiQuote mfQuoteZhujiAction(const ZhujiRun& Run, const ZhujiAction& Action);
std::string mfApplyZhujiAction(ZhujiRun& Run, const ZhujiAction& Action);
std::string mfZhujiRunError(const ZhujiRun& Run);
std::vector<int> mfZhujiNeighbors(const ZhujiRun& Run);
bool mfZhujiBlocksActions(const Game& State);
std::string mfStartZhuji(Game& State, ZhujiRoute Route, int PillCount, bool Core);
void mfCompleteZhujiPreparation(Game& State);
void mfSettleZhujiRun(Game& State);
}
