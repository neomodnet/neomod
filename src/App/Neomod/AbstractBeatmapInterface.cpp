#include "AbstractBeatmapInterface.h"

#include "Osu.h"
#include "GameRules.h"
#include "LegacyReplay.h"
#include "DatabaseBeatmap.h"
#include "score.h"

f32 AbstractBeatmapInterface::getHitWindow300() const {
    return GameRules::mapDifficultyRange(this->getOD(), GameRules::MIN_HITWINDOW_300, GameRules::MID_HITWINDOW_300,
                                         GameRules::MAX_HITWINDOW_300);
}

f32 AbstractBeatmapInterface::getRawHitWindow300() const {
    return GameRules::mapDifficultyRange(this->getRawOD(), GameRules::MIN_HITWINDOW_300, GameRules::MID_HITWINDOW_300,
                                         GameRules::MAX_HITWINDOW_300);
}

f32 AbstractBeatmapInterface::getHitWindow100() const {
    return GameRules::mapDifficultyRange(this->getOD(), GameRules::MIN_HITWINDOW_100, GameRules::MID_HITWINDOW_100,
                                         GameRules::MAX_HITWINDOW_100);
}

f32 AbstractBeatmapInterface::getHitWindow50() const {
    return GameRules::mapDifficultyRange(this->getOD(), GameRules::MIN_HITWINDOW_50, GameRules::MID_HITWINDOW_50,
                                         GameRules::MAX_HITWINDOW_50);
}

f32 AbstractBeatmapInterface::getApproachRateForSpeedMultiplier() const {
    return GameRules::mapDifficultyRangeInv((f32)this->getApproachTime() * (1.0f / this->getSpeedMultiplier()),
                                            GameRules::getMinApproachTime(), GameRules::getMidApproachTime(),
                                            GameRules::getMaxApproachTime());
}

f32 AbstractBeatmapInterface::getRawARForSpeedMultiplier() const {
    return GameRules::mapDifficultyRangeInv((f32)this->getRawApproachTime() * (1.0f / this->getSpeedMultiplier()),
                                            GameRules::getMinApproachTime(), GameRules::getMidApproachTime(),
                                            GameRules::getMaxApproachTime());
}

f32 AbstractBeatmapInterface::getConstantApproachRateForSpeedMultiplier() const {
    return GameRules::mapDifficultyRangeInv((f32)this->getRawApproachTime() * this->getSpeedMultiplier(),
                                            GameRules::getMinApproachTime(), GameRules::getMidApproachTime(),
                                            GameRules::getMaxApproachTime());
}

f32 AbstractBeatmapInterface::getOverallDifficultyForSpeedMultiplier() const {
    return GameRules::mapDifficultyRangeInv((f32)this->getHitWindow300() * (1.0f / this->getSpeedMultiplier()),
                                            GameRules::MIN_HITWINDOW_300, GameRules::MID_HITWINDOW_300,
                                            GameRules::MAX_HITWINDOW_300);
}

f32 AbstractBeatmapInterface::getRawODForSpeedMultiplier() const {
    return GameRules::mapDifficultyRangeInv((f32)this->getRawHitWindow300() * (1.0f / this->getSpeedMultiplier()),
                                            GameRules::MIN_HITWINDOW_300, GameRules::MID_HITWINDOW_300,
                                            GameRules::MAX_HITWINDOW_300);
}

f32 AbstractBeatmapInterface::getConstantOverallDifficultyForSpeedMultiplier() const {
    return GameRules::mapDifficultyRangeInv((f32)this->getRawHitWindow300() * this->getSpeedMultiplier(),
                                            GameRules::MIN_HITWINDOW_300, GameRules::MID_HITWINDOW_300,
                                            GameRules::MAX_HITWINDOW_300);
}

const Replay::Mods &AbstractBeatmapInterface::getMods() const { return osu->getScore()->mods; }
LegacyFlags AbstractBeatmapInterface::getModsLegacy() const { return osu->getScore()->getModsLegacy(); }

i32 AbstractBeatmapInterface::getPVS() const {
    // this is an approximation with generous boundaries, it doesn't need to be exact (just good enough to filter 10000
    // hitobjects down to a few hundred or so) it will be used in both positive and negative directions (previous and
    // future hitobjects) to speed up loops which iterate over all hitobjects
    return this->fCachedApproachTimeForUpdate + GameRules::getFadeInTime() + (i32)GameRules::HITWINDOW_MISS +
           1500;  // sanity
}

u32 AbstractBeatmapInterface::getScoreV1DifficultyMultiplier() const {
    // NOTE: We intentionally get CS/HP/OD from beatmap data, not "real" CS/HP/OD
    //       Since this multiplier is only used for ScoreV1
    u32 breakTimeMS = this->getBreakDurationTotal();
    f32 drainLength =
        std::max(this->getLengthPlayable() - std::min(breakTimeMS, this->getLengthPlayable()), (u32)1000) / 1000;
    return std::round((this->beatmap->getCS() + this->beatmap->getHP() + this->beatmap->getOD() +
                       std::clamp<f32>((f32)this->beatmap->getNumObjects() / drainLength * 8.0f, 0.0f, 16.0f)) /
                      38.0f * 5.0f);
}

LiveHitResult AbstractBeatmapInterface::getHitResult(i32 delta) const {
    // "stable-like" hit windows, see https://github.com/ppy/osu/pull/33882
    const f32 window300 = std::floor(this->getHitWindow300()) - 0.5f;
    const f32 window100 = std::floor(this->getHitWindow100()) - 0.5f;
    const f32 window50 = std::floor(this->getHitWindow50()) - 0.5f;
    const f32 fDelta = std::abs((f32)delta);

    // We are 400ms away from the hitobject, don't count this as a miss
    if(fDelta > GameRules::HITWINDOW_MISS) {
        return LiveHitResult::HIT_NULL;
    }

    const auto modFlags = this->getMods().flags;

    // mod_halfwindow only allows early hits
    // mod_halfwindow_allow_300s also allows "late" perfect hits
    if(flags::has<ModFlags::HalfWindow>(modFlags) && delta > 0) {
        if(fDelta > window300 || !flags::has<ModFlags::HalfWindowAllow300s>(modFlags)) {
            return LiveHitResult::HIT_MISS;
        }
    }

    if(fDelta < window300) return LiveHitResult::HIT_300;
    if(fDelta < window100 && !(flags::has<ModFlags::No100s>(modFlags) || flags::has<ModFlags::Ming3012>(modFlags)))
        return LiveHitResult::HIT_100;
    if(fDelta < window50 && !(flags::has<ModFlags::No100s>(modFlags) || flags::has<ModFlags::No50s>(modFlags)))
        return LiveHitResult::HIT_50;
    return LiveHitResult::HIT_MISS;
}

bool AbstractBeatmapInterface::isClickHeld() const { return this->getKeys() & ~LegacyReplay::Smoke; }
