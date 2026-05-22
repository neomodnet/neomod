// Copyright (c) 2026, kiwec, All rights reserved.
#include "i18n.h"

#include <utility>  // std::unreachable on gcc
#include "binary_embed.h"
#include "Logging.h"
#include "SString.h"
#include "types.h"

namespace {

// Reference: https://www.gnu.org/software/gettext/manual/html_node/Plural-forms.html
enum PLURAL_FORM {
    ONE_FORM,
    TWO_FORMS_A,
    TWO_FORMS_B,
    THREE_FORMS_A,
    THREE_FORMS_B,
    THREE_FORMS_C,
    THREE_FORMS_D,
    THREE_FORMS_E,
    THREE_FORMS_F,
    THREE_FORMS_G,
    FOUR_FORMS,
    SIX_FORMS,
};
static const std::unordered_map<std::string_view, PLURAL_FORM> plural_forms_table = {
    {"jp", ONE_FORM},       {"vi", ONE_FORM},      {"ko", ONE_FORM},      {"th", ONE_FORM},

    {"en", TWO_FORMS_A},    {"de", TWO_FORMS_A},   {"nl", TWO_FORMS_A},   {"sv", TWO_FORMS_A},   {"da", TWO_FORMS_A},
    {"no", TWO_FORMS_A},    {"fo", TWO_FORMS_A},   {"es", TWO_FORMS_A},   {"pt", TWO_FORMS_A},   {"it", TWO_FORMS_A},
    {"el", TWO_FORMS_A},    {"bg", TWO_FORMS_A},   {"fi", TWO_FORMS_A},   {"et", TWO_FORMS_A},   {"he", TWO_FORMS_A},
    {"id", TWO_FORMS_A},    {"eo", TWO_FORMS_A},   {"hu", TWO_FORMS_A},   {"tr", TWO_FORMS_A},

    {"pt-BR", TWO_FORMS_B}, {"fr", TWO_FORMS_B},

    {"lv", THREE_FORMS_A},

    {"ga", THREE_FORMS_B},

    {"ro", THREE_FORMS_C},

    {"lt", THREE_FORMS_D},

    {"ru", THREE_FORMS_E},  {"uk", THREE_FORMS_E}, {"be", THREE_FORMS_E}, {"sr", THREE_FORMS_E}, {"hr", THREE_FORMS_E},

    {"cs", THREE_FORMS_F},  {"sk", THREE_FORMS_F},

    {"pl", THREE_FORMS_G},

    {"sl", FOUR_FORMS},

    {"ar", SIX_FORMS},
};

// Looks hacky, but better than just using a NULL pointer
const char* translation_fallback = "\0\0\0\0\0\0\0";

std::string current_language{};
PLURAL_FORM current_plural_form{ONE_FORM};
std::array<const char*, TRANSLATABLE_STRINGS.size()> translations;

struct MOHeader {
    u32 magic;
    u32 version;
    u32 nb_strings;
    u32 orig_offset;
    u32 trans_offset;
    u32 hash_table_size;
    u32 hash_table_offset;
};

struct MOString {
    u32 length;
    u32 offset;
};

// Reference: https://www.gnu.org/software/gettext/manual/html_node/MO-Files.html
void load_translation(const char* data) {
    // "data" is assumed to be well-formed, we will not do any size checks!
    // (should be ok because we build and embed the .mo files during compilation)
    const MOHeader* mo = (const MOHeader*)data;
    assert(mo->magic == 0x950412de || mo->magic == 0xde120495);
    assert(mo->version == 0);

    // In case .mo file is outdated or a translation is missing.
    // Shouldn't happen in releases, but it would be annoying to crash during development.
    for(u32 i = 0; i < TRANSLATABLE_STRINGS.size(); i++) {
        translations[i] = translation_fallback;
    }

    const MOString* origs = (const MOString*)(data + mo->orig_offset);
    const MOString* trans = (const MOString*)(data + mo->trans_offset);

    // XXX: since strings are sorted in lexicographical order, we could do binary search to load translations
    //      a bit faster. but surely this is already fast enough not to bother with extra complexity?
    for(u32 i = 0; i < mo->nb_strings; i++) {
        const char* orig = data + origs[i].offset;

        for(int j = 0; j < TRANSLATABLE_STRINGS.size(); j++) {
            if(TRANSLATABLE_STRINGS[j] == orig) {
                translations[j] = data + trans[i].offset;
                break;
            }
        }
    }

    for(u32 i = 0; i < TRANSLATABLE_STRINGS.size(); i++) {
        if(translations[i] == translation_fallback) {
            debugLog("Failed to find translation for '{}'!", TRANSLATABLE_STRINGS[i]);
        }
    }
}

// We could parse the "Plural-Forms" header in the .mo file, but the logic is already exhaustive here
// It's easier to add case for a rare language than to debug a C math parser!
int get_plural(int n) {
    switch(current_plural_form) {
        case ONE_FORM:
            // No plural
            return 0;
        case TWO_FORMS_A:
            // Singular for 1 only
            return n == 1 ? 0 : 1;
        case TWO_FORMS_B:
            // Singular for 0 and 1
            return n > 1 ? 1 : 0;
        case THREE_FORMS_A:
            // Special case for 0
            return n % 10 == 1 && n % 100 != 11 ? 0 : n != 0 ? 1 : 2;
        case THREE_FORMS_B:
            // Special cases for 1 and 2
            return n == 1 ? 0 : n == 2 ? 1 : 2;
        case THREE_FORMS_C:
            // Special case for numbers ending in 00 or [2-9][0-9]
            return n == 1 ? 0 : (n == 0 || (n % 100 > 0 && n % 100 < 20)) ? 1 : 2;
        case THREE_FORMS_D:
            // Special case for numbers ending in 1[2-9]
            return n % 10 == 1 && n % 100 != 11                     ? 0  //
                   : n % 10 >= 2 && (n % 100 < 10 || n % 100 >= 20) ? 1  //
                                                                    : 2;
        case THREE_FORMS_E:
            // Special cases for numbers ending in 1 and 2, 3, 4 except those ending in 1[1-4]
            return n % 10 == 1 && n % 100 != 11                                    ? 0
                   : n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 10 || n % 100 >= 20) ? 1
                                                                                   : 2;
        case THREE_FORMS_F:
            // Special cases for 1 and 2, 3, 4
            return n == 1 ? 0 : (n >= 2 && n <= 4) ? 1 : 2;
        case THREE_FORMS_G:
            // Special case for 1 and some numbers ending in 2, 3, 4
            return n == 1 ? 0 : n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 10 || n % 100 >= 20) ? 1 : 2;
        case FOUR_FORMS:
            // Special case for 1 and all numbers ending in 02, 03, 04
            return n % 100 == 1 ? 0 : n % 100 == 2 ? 1 : n % 100 == 3 || n % 100 == 4 ? 2 : 3;
        case SIX_FORMS:
            // Wtf arabic needs to chill...
            return n == 0 ? 0 : n == 1 ? 1 : n == 2 ? 2 : n % 100 >= 3 && n % 100 <= 10 ? 3 : n % 100 >= 11 ? 4 : 5;
    }

    std::unreachable();
}

}  // namespace

namespace i18n {

void load(std::string_view locale) {
    if(locale == "en"sv) {
        current_language = "";
        return;
    }

    // en_US.UTF-8 -> en
    std::string lang{};
    std::string region{};
    {
        auto foo = SString::split(locale, '.');
        lang = foo[0];

        for(int i = 0; i < lang.length(); i++) {
            if(lang[i] == '_') lang[i] = '-';
        }

        auto bar = SString::split(lang, '-');
        lang = bar[0];
        if(bar.size() > 1) region = bar[1];
    }

    // Edge case for Brazillian Portuguese
    std::string lang_for_plural = lang;
    if(lang == "pt" && region == "BR") lang_for_plural = "pt-BR";

    for(const auto& [identifier, full_language_name] : LANGUAGES) {
        if(identifier != lang) continue;

        std::string foo{"locale_"};
        foo.append(identifier);
        assert(ALL_BINMAP.contains(foo));
        load_translation(ALL_BINMAP.at(foo).data());

        auto it = plural_forms_table.find(lang_for_plural);
        if(it == plural_forms_table.end()) {
            debugLog("Locale '{}' doesn't have registered plural forms!", lang_for_plural);
            current_plural_form = ONE_FORM;
        } else {
            current_plural_form = it->second;
        }

        current_language = lang;
        return;
    }

    current_language = "";
    debugLog("Failed to load locale '{}' (doesn't exist!)", locale);
}

const char* translate(int index, const std::string_view& original) {
    // NOTE: if the compiler is being retarded and stripping null bytes from static strings, gg
    //       technically ub but no other way to do this and keep consteval
    //       unless we rewrite whole codebase to expect string_views everywhere it's string or char*
    if(current_language.empty() || current_language == "en") return original.data();

    if(index == -1) return original.data();
    if(translations[index] == translation_fallback) return original.data();

    return translations[index];
}

const char* translate_plural(int index, const std::string_view& singular, const std::string_view& plural, int n) {
    if(index == -1 || current_language.empty() || current_language == "en") {
        return n == 1 ? singular.data() : plural.data();
    }

    const char* text = translations[index];
    if(text == translation_fallback) {
        return n == 1 ? singular.data() : plural.data();
    }

    // Plural variants are stored consecutively, separated by a NULL byte.
    for(int i = 0; i < get_plural(n); i++) {
        text = text + strlen(text) + 1;
    }

    return text;
}

std::vector<Language> get_available_languages() {
    std::vector<Language> out;
    for(const auto& [identifier, full_language_name] : LANGUAGES) {
        out.push_back(Language{.code = identifier, .name = full_language_name});
    }
    return out;
}

}  // namespace i18n
