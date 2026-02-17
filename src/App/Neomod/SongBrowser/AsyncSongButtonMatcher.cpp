// Copyright (c) 2016 PG, All rights reserved.
#include "AsyncSongButtonMatcher.h"

#include "SString.h"

#include "SongButton.h"
#include "DatabaseBeatmap.h"

namespace {
static bool searchMatcher(const DatabaseBeatmap *databaseBeatmap, const std::vector<std::string> &searchStringTokens,
                          float speed);
}

AsyncSongButtonMatcher::AsyncSongButtonMatcher() : Resource(APPDEFINED) {}

void AsyncSongButtonMatcher::setSongButtonsAndSearchString(const std::vector<SongButton *> &songButtons,
                                                           const std::string &searchString,
                                                           const std::string &hardcodedSearchString,
                                                           float currentSpeedMultiplier) {
    this->fSpeedMultiplier = currentSpeedMultiplier;
    this->vSongButtons = songButtons;
    this->sSearchString.clear();

    UString uSearch;
    const UString uHardcodedSearch{hardcodedSearchString};

    if(!uHardcodedSearch.isEmpty()) {
        uSearch.append(hardcodedSearchString);
        uSearch.append(u' ');
    }

    uSearch.append(searchString);
    // do case-insensitive searches
    uSearch.lowerCase();

    this->sSearchString = uSearch.utf8View();
    this->sHardcodedSearchString = uHardcodedSearch.utf8View();
}

void AsyncSongButtonMatcher::initAsync() {
    if(this->bDead.load(std::memory_order_acquire)) {
        this->setAsyncReady(true);
        return;
    }
    const float speed = this->fSpeedMultiplier;

    // flag matches across entire database
    const std::vector<std::string> searchStringTokens =
        SString::split<std::string>(this->sSearchString, ' ');  // make a copy
    for(auto &songButton : this->vSongButtons) {
        // FIXME: this is unsafe, children could be getting sorted while we do this
        std::vector<SongButton *> children = songButton->getChildren();
        if(children.size() > 0) {
            for(auto c : children) {
                const bool match = searchMatcher(c->getDatabaseBeatmap(), searchStringTokens, speed);
                c->setIsSearchMatch(match);
            }
        } else {
            const bool match = searchMatcher(songButton->getDatabaseBeatmap(), searchStringTokens, speed);
            songButton->setIsSearchMatch(match);
        }

        // cancellation point
        if(this->bDead.load(std::memory_order_acquire)) break;
    }

    this->setAsyncReady(true);
}

namespace {
enum operatorId : uint8_t { EQ, LT, GT, LE, GE, NE };

struct Operator {
    std::string_view str;
    operatorId id;
};

inline constexpr std::initializer_list<Operator> operators = {
    {.str = "<=", .id = LE}, {.str = ">=", .id = GE}, {.str = "<", .id = LT}, {.str = ">", .id = GT},
    {.str = "!=", .id = NE}, {.str = "==", .id = EQ}, {.str = "=", .id = EQ},
};

enum keywordId : uint8_t {
    AR,
    CS,
    OD,
    HP,
    BPM,
    OPM,
    CPM,
    SPM,
    OBJECTS,
    CIRCLES,
    SLIDERS,
    SPINNERS,
    LENGTH,
    STARS,
    CREATOR
};

struct Keyword {
    std::string_view str;
    keywordId id;
};

inline constexpr std::initializer_list<Keyword> keywords = {{.str = "ar", .id = AR},
                                                            {.str = "cs", .id = CS},
                                                            {.str = "od", .id = OD},
                                                            {.str = "hp", .id = HP},
                                                            {.str = "bpm", .id = BPM},
                                                            {.str = "opm", .id = OPM},
                                                            {.str = "cpm", .id = CPM},
                                                            {.str = "spm", .id = SPM},
                                                            {.str = "object", .id = OBJECTS},
                                                            {.str = "objects", .id = OBJECTS},
                                                            {.str = "circle", .id = CIRCLES},
                                                            {.str = "circles", .id = CIRCLES},
                                                            {.str = "slider", .id = SLIDERS},
                                                            {.str = "sliders", .id = SLIDERS},
                                                            {.str = "spinner", .id = SPINNERS},
                                                            {.str = "spinners", .id = SPINNERS},
                                                            {.str = "length", .id = LENGTH},
                                                            {.str = "len", .id = LENGTH},
                                                            {.str = "stars", .id = STARS},
                                                            {.str = "star", .id = STARS},
                                                            {.str = "creator", .id = CREATOR}};

static inline bool findSubstringInDiff(const DatabaseBeatmap *diff, std::string_view searchString) {
    return (SString::contains_ncase(diff->getTitleLatin(), searchString)) ||
           (SString::contains_ncase(diff->getArtistLatin(), searchString)) ||
           (!diff->getTitleUnicode().empty() && diff->getTitleUnicode().contains(searchString)) ||
           (!diff->getArtistUnicode().empty() && diff->getArtistUnicode().contains(searchString)) ||
           (SString::contains_ncase(diff->getCreator(), searchString)) ||
           (SString::contains_ncase(diff->getDifficultyName(), searchString)) ||
           (SString::contains_ncase(diff->getSource(), searchString)) ||
           (SString::contains_ncase(diff->getTags(), searchString)) ||
           (diff->getID() > 0 && SString::contains_ncase(std::to_string(diff->getID()), searchString)) ||
           (diff->getSetID() > 0 && SString::contains_ncase(std::to_string(diff->getSetID()), searchString));
};

static bool searchMatcher(const DatabaseBeatmap *databaseBeatmap, const std::vector<std::string> &searchStringTokens,
                          float speed) {
    if(databaseBeatmap == nullptr) return false;

    const auto diffs = [&bdiffs = databaseBeatmap->getDifficulties(),
                        databaseBeatmap]() -> std::vector<const DatabaseBeatmap *> {
        std::vector<const DatabaseBeatmap *> ret;
        if(bdiffs.empty()) {
            // standalone set
            ret.push_back(databaseBeatmap);
        } else {
            for(const auto &diff : bdiffs) {
                ret.push_back(diff.get());
            }
        }
        return ret;
    }();

    // TODO: optimize this dumpster fire. can at least cache the parsed tokens and literal strings array instead of
    // parsing every single damn time

    // intelligent search parser
    // all strings which are not expressions get appended with spaces between, then checked with one call to
    // findSubstringInDiff() the rest is interpreted NOTE: this code is quite shitty. the order of the operators
    // array does matter, because find() is used to detect their presence (and '=' would then break '<=' etc.)

    // split search string into tokens
    // parse over all difficulties
    bool expressionMatches = false;  // if any diff matched all expressions
    std::vector<std::string> literalSearchStrings;
    for(const auto *diff : diffs) {
        bool expressionsMatch = true;  // if the current search string (meaning only the expressions in this case)
                                       // matches the current difficulty

        for(const auto &searchStringToken : searchStringTokens) {
            // debugLog("token[{:d}] = {:s}", i, tokens[i].toUtf8());
            //  determine token type, interpret expression
            bool expression = false;
            for(const auto &[op_str, op_id] : operators) {
                if(searchStringToken.find(op_str) != std::string::npos) {
                    // split expression into left and right parts (only accept singular expressions, things like
                    // "0<bpm<1" will not work with this)
                    // debugLog("splitting by string {:s}", operators[o].first.toUtf8());
                    std::vector<std::string_view> values{SString::split(searchStringToken, op_str)};
                    if(values.size() == 2 && values[0].length() > 0 && values[1].length() > 0) {
                        const std::string_view lvalue = values[0];
                        const std::string_view rstring = values[1];

                        const auto rvaluePercentIndex = rstring.find('%');
                        const bool rvalueIsPercent = (rvaluePercentIndex != std::string::npos);
                        const float rvalue = [&rstring, rvaluePercentIndex]() -> float {
                            float rvalue_tmp{0.f};
                            const std::string_view rstring_sub = rvaluePercentIndex == std::string::npos
                                                                     ? rstring
                                                                     : rstring.substr(0, rvaluePercentIndex);

                            auto [ptr, ec] = std::from_chars(rstring_sub.data(),
                                                             rstring_sub.data() + rstring_sub.size(), rvalue_tmp);
                            if(ec != std::errc()) return 0.f;
                            return rvalue_tmp;
                        }();  // this must always be a number (at least, assume it is)

                        // find lvalue keyword in array (only continue if keyword exists)
                        for(const auto &[kw_str, kw_id] : keywords) {
                            if(kw_str == lvalue) {
                                expression = true;

                                // we now have a valid expression: the keyword, the operator and the value

                                // solve keyword
                                float compareValue = 5.0f;
                                std::string compareString{};
                                switch(kw_id) {
                                    case AR:
                                        compareValue = diff->getAR();
                                        break;
                                    case CS:
                                        compareValue = diff->getCS();
                                        break;
                                    case OD:
                                        compareValue = diff->getOD();
                                        break;
                                    case HP:
                                        compareValue = diff->getHP();
                                        break;
                                    case BPM:
                                        compareValue = diff->getMostCommonBPM();
                                        break;
                                    case OPM:
                                        compareValue =
                                            (diff->getLengthMS() > 0 ? ((float)diff->getNumObjects() /
                                                                        (float)(diff->getLengthMS() / 1000.0f / 60.0f))
                                                                     : 0.0f) *
                                            speed;
                                        break;
                                    case CPM:
                                        compareValue =
                                            (diff->getLengthMS() > 0 ? ((float)diff->getNumCircles() /
                                                                        (float)(diff->getLengthMS() / 1000.0f / 60.0f))
                                                                     : 0.0f) *
                                            speed;
                                        break;
                                    case SPM:
                                        compareValue =
                                            (diff->getLengthMS() > 0 ? ((float)diff->getNumSliders() /
                                                                        (float)(diff->getLengthMS() / 1000.0f / 60.0f))
                                                                     : 0.0f) *
                                            speed;
                                        break;
                                    case OBJECTS:
                                        compareValue = diff->getNumObjects();
                                        break;
                                    case CIRCLES:
                                        compareValue =
                                            (rvalueIsPercent
                                                 ? ((float)diff->getNumCircles() / (float)diff->getNumObjects()) *
                                                       100.0f
                                                 : diff->getNumCircles());
                                        break;
                                    case SLIDERS:
                                        compareValue =
                                            (rvalueIsPercent
                                                 ? ((float)diff->getNumSliders() / (float)diff->getNumObjects()) *
                                                       100.0f
                                                 : diff->getNumSliders());
                                        break;
                                    case SPINNERS:
                                        compareValue =
                                            (rvalueIsPercent
                                                 ? ((float)diff->getNumSpinners() / (float)diff->getNumObjects()) *
                                                       100.0f
                                                 : diff->getNumSpinners());
                                        break;
                                    case LENGTH:
                                        compareValue = diff->getLengthMS() / 1000.0f;
                                        break;
                                    case STARS:
                                        compareValue =
                                            std::round(diff->getStarRating(StarPrecalc::active_idx) * 100.0f) /
                                            100.0f;  // round to 2 decimal places
                                        break;
                                    case CREATOR:
                                        compareString = SString::to_lower(diff->getCreator());
                                        break;
                                }

                                // solve operator
                                bool matches = false;
                                switch(op_id) {
                                    case LE:
                                        if(compareValue <= rvalue) matches = true;
                                        break;
                                    case GE:
                                        if(compareValue >= rvalue) matches = true;
                                        break;
                                    case LT:
                                        if(compareValue < rvalue) matches = true;
                                        break;
                                    case GT:
                                        if(compareValue > rvalue) matches = true;
                                        break;
                                    case NE:
                                        if(compareValue != rvalue) matches = true;
                                        break;
                                    case EQ:
                                        if(compareValue == rvalue ||
                                           (!compareString.empty() && compareString == SString::to_lower(rstring)))
                                            matches = true;
                                        break;
                                }

                                // debugLog("comparing {:f} {:s} {:f} (operatorId = {:d}) = {:d}", compareValue,
                                // operators[o].first.toUtf8(), rvalue, (int)operators[o].second, (int)matches);

                                if(!matches)  // if a single expression doesn't match, then the whole diff doesn't match
                                    expressionsMatch = false;

                                break;
                            }
                        }
                    }

                    break;
                }
            }

            // if this is not an expression, add the token to the literalSearchStrings array
            if(!expression) {
                // only add it if it doesn't exist yet
                // this check is only necessary due to multiple redundant parser executions (one per diff!)
                bool exists = false;
                for(const auto &literalSearchString : literalSearchStrings) {
                    if(literalSearchString == searchStringToken) {
                        exists = true;
                        break;
                    }
                }

                if(!exists) {
                    std::string litAdd{searchStringToken};
                    SString::trim_inplace(litAdd);
                    if(!SString::is_wspace_only(litAdd)) literalSearchStrings.push_back(litAdd);
                }
            }
        }

        if(expressionsMatch)  // as soon as one difficulty matches all expressions, we are done here
        {
            expressionMatches = true;
            break;
        }
    }

    // if no diff matched any expression, then we can already stop here
    if(!expressionMatches) return false;

    bool hasAnyValidLiteralSearchString = false;
    for(const auto &literalSearchString : literalSearchStrings) {
        if(literalSearchString.length() > 0) {
            hasAnyValidLiteralSearchString = true;
            break;
        }
    }

    // early return here for literal match/contains
    if(hasAnyValidLiteralSearchString) {
        for(const auto *diff : diffs) {
            bool atLeastOneFullMatch = true;

            for(const auto &literalSearchString : literalSearchStrings) {
                if(!findSubstringInDiff(diff, literalSearchString)) atLeastOneFullMatch = false;
            }

            // as soon as one diff matches all strings, we are done
            if(atLeastOneFullMatch) return true;
        }

        // expression may have matched, but literal didn't match, so the entire beatmap doesn't match
        return false;
    }

    return expressionMatches;
}

}  // namespace
