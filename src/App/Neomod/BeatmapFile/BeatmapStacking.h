#pragma once
// Copyright (c) 2020, PG & 2026, WH, All rights reserved.
#include "types.h"
#include "Vectors.h"
#include "GameRules.h"

#include <algorithm>
#include <functional>
#include <type_traits>

namespace neomod {
class HitObject;
namespace DiffCalc {
class DifficultyHitObject;
}
}  // namespace neomod

namespace neomod::Primitives {

template <typename T>
concept HitObjectContainer = std::is_same_v<T, DiffCalc::DifficultyHitObject> || std::is_same_v<T, HitObject>;

template <HitObjectContainer C>
using ObjectGetter = std::function<C *(uSz)>;

// osu!'s stacking of objects sorted by time whose stacks are all 0 (maps before v6 stack the old way)
template <HitObjectContainer C>
void calculateStacks(const ObjectGetter<C> &getObj, uSz numObjects, float AR, int beatmapVersion, float stackLeniency) {
    constexpr float STACK_LENIENCE = 3.f;
    const float approachTime = GameRules::getApproachTimeForStacking(AR);

    if(beatmapVersion > 5) {
        // peppy's algorithm
        // https://gist.github.com/peppy/1167470

        for(sSz i = static_cast<sSz>(numObjects) - 1; i >= 0; i--) {
            sSz n = i;

            auto *objectI = getObj(i);

            bool isSpinner = objectI->isSpinner();

            if(objectI->getStack() != 0 || isSpinner) continue;

            bool isHitCircle = objectI->isCircle();
            bool isSlider = objectI->isSlider();

            if(isHitCircle) {
                while(--n >= 0) {
                    auto *objectN = getObj(n);

                    bool isSpinnerN = objectN->isSpinner();

                    if(isSpinnerN) continue;

                    if((f32)objectI->getClickTime() - (approachTime * stackLeniency) > (f32)(objectN->getEndTime()))
                        break;

                    vec2 objectNEndPosition = objectN->getOriginalRawPosAt(objectN->getEndTime());
                    if(objectN->getDuration() != 0 &&
                       vec::length(objectNEndPosition - objectI->getOriginalRawPosAt(objectI->getClickTime())) <
                           STACK_LENIENCE) {
                        int offset = objectI->getStack() - objectN->getStack() + 1;
                        for(sSz j = n + 1; j <= i; j++) {
                            auto *objectJ = getObj(j);
                            if(vec::length((objectNEndPosition -
                                            objectJ->getOriginalRawPosAt(objectJ->getClickTime()))) < STACK_LENIENCE)
                                objectJ->setStack(objectJ->getStack() - offset);
                        }

                        break;
                    }

                    if(vec::length((objectN->getOriginalRawPosAt(objectN->getClickTime()) -
                                    objectI->getOriginalRawPosAt(objectI->getClickTime()))) < STACK_LENIENCE) {
                        objectN->setStack(objectI->getStack() + 1);
                        objectI = objectN;
                    }
                }
            } else if(isSlider) {
                while(--n >= 0) {
                    auto *objectN = getObj(n);

                    bool isSpinnerN = objectN->isSpinner();

                    if(isSpinnerN) continue;

                    if((f32)objectI->getClickTime() - (approachTime * stackLeniency) > (f32)objectN->getClickTime())
                        break;

                    if(vec::length(
                           ((objectN->getDuration() != 0 ? objectN->getOriginalRawPosAt(objectN->getEndTime())
                                                         : objectN->getOriginalRawPosAt(objectN->getClickTime())) -
                            objectI->getOriginalRawPosAt(objectI->getClickTime()))) < STACK_LENIENCE) {
                        objectN->setStack(objectI->getStack() + 1);
                        objectI = objectN;
                    }
                }
            }
        }
    } else  // getSelectedDifficulty()->version < 6
    {
        // old stacking algorithm for old beatmaps
        // https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Beatmaps/BeatmapProcessor.cs

        for(int i = 0; i < numObjects; i++) {
            auto *currHitObject = getObj(i);

            const bool isSlider = currHitObject->isSlider();

            if(currHitObject->getStack() != 0 && !isSlider) continue;

            i32 startTime = currHitObject->getEndTime();
            int sliderStack = 0;

            for(int j = i + 1; j < numObjects; j++) {
                auto *objectJ = getObj(j);

                if((f32)objectJ->getClickTime() - (approachTime * stackLeniency) > (f32)startTime) break;

                // "The start position of the hitobject, or the position at the end of the path if the hitobject is a
                // slider"
                const vec2 position2 = isSlider ? currHitObject->getOriginalRawPosAt(currHitObject->getEndTime())
                                                : currHitObject->getOriginalRawPosAt(currHitObject->getClickTime());

                if(vec::length((objectJ->getOriginalRawPosAt(objectJ->getClickTime()) -
                                currHitObject->getOriginalRawPosAt(currHitObject->getClickTime()))) < 3) {
                    currHitObject->setStack(currHitObject->getStack() + 1);
                    startTime = objectJ->getEndTime();
                } else if(vec::length((objectJ->getOriginalRawPosAt(objectJ->getClickTime()) - position2)) < 3) {
                    // "Case for sliders - bump notes down and right, rather than up and left."
                    sliderStack++;
                    objectJ->setStack(objectJ->getStack() - sliderStack);
                    startTime = objectJ->getEndTime();
                }
            }
        }
    }

    return;
}

// objects (sorted by time) fall into runs that stack only among themselves: calculateStacks() over one run alone gives
// it the stacks it gets over all objects. returns where the run after the one at `start` starts: the first object that
// starts further after every earlier end in the run than objects stack across (numObjects if there's none). only the
// objects from `start` on are looked at
template <HitObjectContainer C>
uSz nextStackingRun(const ObjectGetter<C> &getObj, uSz numObjects, uSz start, float AR, float stackLeniency) {
    const float window = GameRules::getApproachTimeForStacking(AR) * stackLeniency;
    i32 latestEnd = getObj(start)->getEndTime();
    for(uSz i = start + 1; i < numObjects; i++) {
        const C *object = getObj(i);
        if((f32)object->getClickTime() - window > (f32)latestEnd) return i;
        latestEnd = std::max(latestEnd, object->getEndTime());
    }
    return numObjects;
}

}  // namespace neomod::Primitives
