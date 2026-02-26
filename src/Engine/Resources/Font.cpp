//========== Copyright (c) 2015, PG & 2025, WH, All rights reserved. ============//
//
// Purpose:		freetype font wrapper with unicode support
//
// $NoKeywords: $fnt
//===============================================================================//

#include "Font.h"

#include "ConVar.h"
#include "Engine.h"
#include "File.h"
#include "FixedSizeArray.h"
#include "FontTypeMap.h"
#include "Mouse.h"
#include "ResourceManager.h"
#include "SString.h"
#include "VertexArrayObject.h"
#include "TextureAtlas.h"
#include "Logging.h"
#include "Environment.h"
#include "SyncMutex.h"
#include "Image.h"
#include "Hashing.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <utility>
#include <cassert>
#include <map>
#include <memory>

#include <freetype/freetype.h>
#include <freetype/ftbitmap.h>
#include <freetype/ftglyph.h>
#include <freetype/ftoutln.h>
#include <freetype/fttrigon.h>
#include <ft2build.h>

// TODO: use fontconfig on linux?
#ifdef _WIN32
#include "WinDebloatDefs.h"
#include <shlobj.h>
#include <windows.h>
#endif

namespace {  // static namespace

// constants for atlas generation and rendering
constexpr const float ATLAS_OCCUPANCY_TARGET{0.75f};  // target atlas occupancy before resize

// how much larger to make atlas for dynamic region
// we initially pack the ASCII characters + initial characters into a region,
// then dynamically loaded glyphs are placed in the remaining space in fixed-size slots (not packed)
// this maximizes the amount of fallback glyphs we can have loaded at once for a fixed amount of memory usage
constexpr float ATLAS_SIZE_MULTIPLIER{4.0f};

constexpr const size_t MIN_ATLAS_SIZE{256};
constexpr const size_t MAX_ATLAS_SIZE{4096};

constexpr const char16_t UNKNOWN_CHAR{u'?'};  // ASCII '?'

constexpr const size_t VERTS_PER_VAO{Env::cfg(REND::GLES32 | REND::DX11 | REND::SDLGPU) ? 6 : 4};

// this is still a very conservative amount of memory
constexpr const size_t CACHED_STRINGS_PER_FONT{4096};
size_t stringToCacheIndex(std::u16string_view u16str) {
    return std::hash<std::u16string_view>{}(u16str) % CACHED_STRINGS_PER_FONT;
}

// other shared-across-instances things
struct FallbackFont {
    UString fontPath;
    FT_Face face;
    bool isSystemFont;
};

// global shared freetype resources
FT_Library s_sharedFtLibrary{nullptr};
std::vector<FallbackFont> s_sharedFallbackFonts;
Hash::flat::set<char16_t> s_sharedFallbackFaceBlacklist;

bool s_sharedFtLibraryInitialized{false};
bool s_sharedFallbacksInitialized{false};

Sync::shared_mutex s_sharedResourcesMutex;

// face size tracking to avoid redundant setFaceSize calls
struct LastSizedFTFace {
    FT_Face face{nullptr};
    int size{0};
    int dpi{0};
} s_lastSizedFace{};

}  // namespace

// implementation details for each McFont object
struct McFontImpl final {
    NOCOPY_NOMOVE(McFontImpl);
    McFont *m_parent;

   public:
    // Internal data structures and state
    struct GLYPH_METRICS {
        FT_Face face;  // source font face (nullptr if glyph not supported)
        unsigned int uvPixelsX, uvPixelsY;
        unsigned int sizePixelsX, sizePixelsY;
        int left, top, width, rows;
        float advance_x;
        bool inAtlas;  // whether UV coordinates are valid (glyph is rendered in atlas)
    };

    // texture atlas dynamic slot management
    struct DynamicSlot {
        int x, y;            // position in atlas
        char16_t character;  // character in this slot (0 if empty)
        uint64_t lastUsed;   // for LRU eviction
        bool occupied;
    };

    struct VerTexMetCacheEntry {
        std::u16string string;
        void clear() {
            string.clear();
            verts.clear();
            texcoords.clear();
            metrics.clear();
        }

        void resize(size_t numVerts, size_t numMetrics) {
            verts.resize(numVerts);
            texcoords.resize(numVerts);
            metrics.resize(numMetrics);
        }

        [[nodiscard]] size_t numVerts() const { return verts.size(); }
        [[nodiscard]] size_t numMetrics() const { return metrics.size(); }

        std::vector<vec3> &getVerts() { return verts; }
        std::vector<vec2> &getTexcoords() { return texcoords; }
        std::vector<const GLYPH_METRICS *> &getMetrics() { return metrics; }

        [[nodiscard]] const std::vector<vec3> &getVerts() const { return verts; }
        [[nodiscard]] const std::vector<vec2> &getTexcoords() const { return texcoords; }
        [[nodiscard]] const std::vector<const GLYPH_METRICS *> &getMetrics() const { return metrics; }

       private:
        std::vector<vec3> verts{};
        std::vector<vec2> texcoords{};
        std::vector<const GLYPH_METRICS *> metrics{};
    };

    std::vector<char16_t> m_vInitialGlyphs;
    std::unordered_map<char16_t, std::unique_ptr<GLYPH_METRICS>> m_mGlyphMetrics;

    std::unique_ptr<VertexArrayObject> m_vao;

    std::unique_ptr<TextureAtlas> m_textureAtlas{nullptr};

    // string caching
    std::vector<VerTexMetCacheEntry> m_vStringCache{CACHED_STRINGS_PER_FONT};
    // for strings too short or too long to bother with caching
    VerTexMetCacheEntry m_tempStringBuffer;

    // per-instance freetype resources (only primary font face)
    FT_Face m_ftFace;  // primary font face

    int m_iFontSize;
    int m_iFontDPI;
    float m_fHeight;
    GLYPH_METRICS m_errorGlyph;

    // dynamic atlas management
    int m_staticRegionHeight;  // height of statically packed region
    int m_dynamicRegionY;      // Y coordinate where dynamic region starts
    int m_slotsPerRow;         // number of slots per row in dynamic region
    std::vector<DynamicSlot> m_dynamicSlots;
    std::unordered_map<char16_t, int> m_dynamicSlotMap;  // character -> slot index for O(1) lookup
    uint64_t m_currentAtlasTime;                         // for LRU tracking
    bool m_bAtlasNeedsReload;                            // flag to batch atlas reloads

    bool m_bFreeTypeInitialized;
    bool m_bAntialiasing;
    bool m_bHeightManuallySet{false};

    // should we look for fallbacks for this specific font face
    bool m_bTryFindFallbacks;

   public:
    McFontImpl() = delete;
    McFontImpl(McFont *parent, int fontSize, bool antialiasing, int fontDPI);
    McFontImpl(McFont *parent, const std::span<const char16_t> &characters, int fontSize, bool antialiasing,
               int fontDPI);

    ~McFontImpl() { destroy(); }

    void init();
    void initAsync();
    void destroy();

    // Main string drawing functions
    void drawString(const UString &text, std::optional<TextShadow> shadow = std::nullopt);

    // Setters
    inline void setSize(int fontSize) { m_iFontSize = fontSize; }
    inline void setDPI(int dpi) { m_iFontDPI = dpi; }
    inline void setHeight(float height) {
        m_fHeight = height;
        m_bHeightManuallySet = true;
    }

    // Getters
    [[nodiscard]] inline int getSize() const { return m_iFontSize; }
    [[nodiscard]] inline int getDPI() const { return m_iFontDPI; }
    [[nodiscard]] inline float getHeight() const { return m_fHeight; }

    [[nodiscard]] float getGlyphWidth(char16_t character) const;
    [[nodiscard]] float getGlyphHeight(char16_t character) const;
    [[nodiscard]] float getStringWidth(const UString &text) const;
    [[nodiscard]] float getStringHeight(const UString &text) const;

    // Public util
    [[nodiscard]] std::vector<UString> wrap(const UString &text, f64 max_width) const;

   private:
    // Shared ctor helper
    void constructor(const std::span<const char16_t> &characters, int fontSize, bool antialiasing, int fontDPI);

    // Internal helper methods below here
    bool initializeFreeType();

    // atlas management methods
    int allocateDynamicSlot(char16_t ch);

    void markSlotUsed(char16_t ch);

    void initializeDynamicRegion(int atlasSize);

    // consolidated glyph processing methods

    [[nodiscard]] const GLYPH_METRICS &getGlyphMetrics(char16_t ch) const;

    // if existingFace is not nullptr, we had metrics already for this glyph (evicted)
    bool loadGlyphDynamic(char16_t ch, FT_Face existingFace);
    bool loadGlyphMetrics(char16_t ch);

    std::unique_ptr<u8[]> createExpandedBitmapData(const FT_Bitmap &bitmap);

    // loads glyph from face and converts to bitmap. Returns nullptr on failure.
    // if storeMetrics is true, stores metrics in m_mGlyphMetrics[ch].
    // caller is responsible for calling FT_Done_Glyph on returned glyph.
    FT_BitmapGlyph loadBitmapGlyph(char16_t ch, FT_Face face, bool storeMetrics);

    // renders bitmap to atlas at specified position. updates UV coords and sets inAtlas.
    void renderBitmapToAtlas(char16_t ch, int x, int y, const FT_Bitmap &bitmap, bool isDynamicSlot);

    // for initial glyphs, packed
    bool initializeAtlas();

    // fallback font management
    FT_Face getFontFaceForGlyph(char16_t ch);

    // returns end vertex from startVertex
    size_t buildGlyphGeometry(std::vector<vec3> &vertsOut, std::vector<vec2> &texcoordsOut, const GLYPH_METRICS &gm,
                              float advanceX, size_t startVertex);

    // returns final vertices ready to be drawn in the out parameter
    void buildStringGeometry(const UString &text, std::vector<vec3> &vertsOut, std::vector<vec2> &texcoordsOut,
                             size_t maxGlyphs, std::vector<const GLYPH_METRICS *> &gmOut);

    static std::unique_ptr<Channel[]> unpackMonoBitmap(const FT_Bitmap &bitmap);

    // helper to set font size on any face for this font instance
    void setFaceSize(FT_Face face);

    [[nodiscard]] inline int getDynSlotSize() const { return 2 * m_iFontSize; }
};

////////////////////////////////////////////////////////////////////////////////////
// Public passthroughs start
////////////////////////////////////////////////////////////////////////////////////

McFont::McFont(std::string filepath, int fontSize, bool antialiasing, int fontDPI)
    : Resource(FONT, std::move(filepath)), pImpl(this, fontSize, antialiasing, fontDPI) {}

McFont::McFont(std::string filepath, const std::span<const char16_t> &characters, int fontSize, bool antialiasing,
               int fontDPI)
    : Resource(FONT, std::move(filepath)), pImpl(this, characters, fontSize, antialiasing, fontDPI) {}

McFont::~McFont() { destroy(); }

void McFont::init() { pImpl->init(); }
void McFont::initAsync() { pImpl->initAsync(); }
void McFont::destroy() { pImpl->destroy(); }

void McFont::setSize(int fontSize) { pImpl->setSize(fontSize); }
void McFont::setDPI(int dpi) { pImpl->setDPI(dpi); }
void McFont::setHeight(float height) { pImpl->setHeight(height); }

int McFont::getSize() const { return pImpl->getSize(); }
int McFont::getDPI() const { return pImpl->getDPI(); }
float McFont::getHeight() const { return pImpl->getHeight(); }

void McFont::drawString(const UString &text, std::optional<TextShadow> shadow) {
    return pImpl->drawString(text, shadow);
}

float McFont::getGlyphWidth(char16_t character) const { return pImpl->getGlyphWidth(character); }
float McFont::getGlyphHeight(char16_t character) const { return pImpl->getGlyphHeight(character); }
float McFont::getStringWidth(const UString &text) const { return pImpl->getStringWidth(text); }
float McFont::getStringHeight(const UString &text) const { return pImpl->getStringHeight(text); }
std::vector<UString> McFont::wrap(const UString &text, f64 max_width) const { return pImpl->wrap(text, max_width); }

////////////////////////////////////////////////////////////////////////////////////
// Public passthroughs end, implementation begins
////////////////////////////////////////////////////////////////////////////////////

McFontImpl::McFontImpl(McFont *parent, int fontSize, bool antialiasing, int fontDPI)
    : m_parent(parent),
      m_vao(g->createVertexArrayObject(
          (Env::cfg(REND::GLES32 | REND::DX11 | REND::SDLGPU) ? DrawPrimitive::TRIANGLES : DrawPrimitive::QUADS),
          DrawUsageType::DYNAMIC, false)) {
    std::array<char16_t, 96> characters;
    // initialize with basic ASCII, load the rest as needed
    for(int i = 32; i < 128; i++) {
        characters[i - 32] = static_cast<char16_t>(i);
    }
    m_bTryFindFallbacks = true;
    constructor(characters, fontSize, antialiasing, fontDPI);
}

McFontImpl::McFontImpl(McFont *parent, const std::span<const char16_t> &characters, int fontSize, bool antialiasing,
                       int fontDPI)
    : m_parent(parent),
      m_vao(g->createVertexArrayObject(
          (Env::cfg(REND::GLES32 | REND::DX11 | REND::SDLGPU) ? DrawPrimitive::TRIANGLES : DrawPrimitive::QUADS),
          DrawUsageType::DYNAMIC, false)) {
    // don't try to find fallbacks if we had an explicitly-passed character set on construction
    m_bTryFindFallbacks = false;
    constructor(characters, fontSize, antialiasing, fontDPI);
}

void McFontImpl::constructor(const std::span<const char16_t> &characters, int fontSize, bool antialiasing,
                             int fontDPI) {
    m_iFontSize = fontSize;
    m_bAntialiasing = antialiasing;
    m_iFontDPI = fontDPI;
    m_fHeight = 1.0f;

    // per-instance freetype initialization state
    m_ftFace = nullptr;
    m_bFreeTypeInitialized = false;

    // initialize dynamic atlas management
    m_staticRegionHeight = 0;
    m_dynamicRegionY = 0;
    m_slotsPerRow = 0;
    m_currentAtlasTime = 0;
    m_bAtlasNeedsReload = false;

    // setup error glyph
    m_errorGlyph = {.face = nullptr,
                    .uvPixelsX = 10,
                    .uvPixelsY = 1,
                    .sizePixelsX = 1,
                    .sizePixelsY = 0,
                    .left = 0,
                    .top = 10,
                    .width = 10,
                    .rows = 1,
                    .advance_x = 0,
                    .inAtlas = true};

    // pre-allocate space for initial glyphs
    m_vInitialGlyphs.reserve(characters.size());
    for(char16_t character : characters) {
        m_vInitialGlyphs.push_back(character);
    }
}

void McFontImpl::init() {
    if(!m_parent->isAsyncReady()) return;  // failed

    // finalize atlas texture
    resourceManager->loadResource(m_textureAtlas.get());

    m_parent->setReady(true);
}

void McFontImpl::initAsync() {
    debugLog("Loading font: {:s}", m_parent->sFilePath);
    assert(s_sharedFtLibraryInitialized);
    assert(s_sharedFallbacksInitialized);

    if(!initializeFreeType()) return;

    // set font size for this instance's primary face
    setFaceSize(m_ftFace);

    m_mGlyphMetrics.reserve(m_vInitialGlyphs.size());
    // load metrics for all initial glyphs
    for(char16_t ch : m_vInitialGlyphs) {
        loadGlyphMetrics(ch);
    }

    // create+pack initial atlas and render all glyphs
    if(!initializeAtlas()) return;

    // precalculate average/max ASCII glyph height (unless it was already set manually)
    if(!m_bHeightManuallySet && m_fHeight > 0.f) {
        m_fHeight = 0.0f;
        for(int i = 32; i < 128; i++) {
            const int curHeight = getGlyphMetrics(static_cast<char16_t>(i)).top;
            m_fHeight = std::max(m_fHeight, static_cast<float>(curHeight));
        }
    }

    m_parent->setAsyncReady(true);
}

void McFontImpl::destroy() {
    // only clean up per-instance resources (primary font face and atlas)
    // shared resources are cleaned up separately via cleanupSharedResources()

    if(m_bFreeTypeInitialized) {
        if(m_ftFace) {
            FT_Done_Face(m_ftFace);
            m_ftFace = nullptr;
        }
        m_bFreeTypeInitialized = false;
    }
    m_vStringCache.clear();
    m_vStringCache.resize(CACHED_STRINGS_PER_FONT);

    m_mGlyphMetrics.clear();
    m_dynamicSlots.clear();
    m_dynamicSlotMap.clear();

    if(!m_bHeightManuallySet) {
        m_fHeight = 1.0f;
    }

    m_bAtlasNeedsReload = false;
}

void McFontImpl::drawString(const UString &text, std::optional<TextShadow> shadow) {
    if(!m_parent->isReady()) return;

    if(text.length() == 0 || text.length() > cv::r_drawstring_max_string_length.getInt()) return;

    m_vao->clear();

    // cache entire strings' vertex/texcoord representations,
    // and only do the minimal work necessary if needing to re-upload them to the texture atlas
    const bool useCache = text.length() >= 8 && text.length() <= 384;  // arbitrary limits
    VerTexMetCacheEntry &buffer = useCache ? m_vStringCache[stringToCacheIndex(text.u16View())] : m_tempStringBuffer;
    if(useCache && buffer.string == text.u16View()) {
        for(int i = -1; const GLYPH_METRICS *gm : buffer.getMetrics()) {
            ++i;
            char16_t ch = text[i];
            if(ch >= 128) {
                if(!gm->inAtlas) {
                    loadGlyphDynamic(ch, gm->face);
                }
                if(gm->face != m_ftFace) {
                    markSlotUsed(ch);
                }
            } else if(ch >= 32) {
                assert(gm->inAtlas);
            }
        }

        if(m_bAtlasNeedsReload) {
            size_t currentVertex = 0;

            float advanceX = 0.0f;
            for(const GLYPH_METRICS *gm : buffer.getMetrics()) {
                currentVertex =
                    buildGlyphGeometry(buffer.getVerts(), buffer.getTexcoords(), *gm, advanceX, currentVertex);
                advanceX += gm->advance_x;
            }

            m_textureAtlas->reloadAtlasImage();
            m_bAtlasNeedsReload = false;
        }
    } else {
        buffer.string = text.u16_str();

        const size_t totalVerts = text.length() * VERTS_PER_VAO;
        const int maxGlyphs = std::min(text.length(), (int)((double)totalVerts / (double)VERTS_PER_VAO));

        buffer.resize(totalVerts, maxGlyphs);

        buildStringGeometry(text, buffer.getVerts(), buffer.getTexcoords(), maxGlyphs, buffer.getMetrics());
    }

    // if(stringToCacheIndex(buffer.string) % 32 == 0) {
    //     size_t totalBytes = 0;
    //     for(const auto &elem : m_vStringCache) {
    //         totalBytes += (elem.string.size() * sizeof(char16_t)) + (elem.getMetrics().size() * sizeof(void *)) +
    //                       (elem.getVerts().size() * sizeof(vec3)) + (elem.getTexcoords().size() * sizeof(vec2));
    //     }
    //     debugLog("cache memory usage: {}MB", totalBytes / (1024ULL * 1024));
    // }

    m_vao->reserve(buffer.getVerts().size());

    m_vao->setVertices(buffer.getVerts());
    m_vao->setTexcoords(buffer.getTexcoords());

    m_textureAtlas->getAtlasImage()->bind();

    if(const auto &shadOpt = shadow; shadOpt.has_value() && shadOpt->col_shadow.A() > 0) {
        const auto &shadowConf = *shadOpt;
        const float px = shadowConf.offs_px;

        g->translate(px, px);
        g->setColor(shadowConf.col_shadow);

        g->drawVAO(m_vao.get());

        g->translate(-px, -px);
        g->setColor(shadowConf.col_text);
    }

    g->drawVAO(m_vao.get());

    if(cv::r_debug_drawstring_unbind.getBool()) m_textureAtlas->getAtlasImage()->unbind();

    if(!useCache) {
        buffer.clear();
    }
}

float McFontImpl::getGlyphWidth(char16_t character) const {
    if(!m_parent->isReady()) return 1.0f;

    return static_cast<float>(getGlyphMetrics(character).advance_x);
}

float McFontImpl::getGlyphHeight(char16_t character) const {
    if(!m_parent->isReady()) return 1.0f;

    return static_cast<float>(getGlyphMetrics(character).top);
}

float McFontImpl::getStringWidth(const UString &text) const {
    if(!m_parent->isReady()) return 1.0f;

    float width = 0.0f;
    for(int i = 0; i < text.length(); i++) {
        width += getGlyphMetrics(text[i]).advance_x;
    }
    return width;
}

float McFontImpl::getStringHeight(const UString &text) const {
    if(!m_parent->isReady()) return 1.0f;

    float height = 0.0f;
    for(int i = 0; i < text.length(); i++) {
        height = std::max(height, static_cast<float>(getGlyphMetrics(text[i]).top));
    }
    return height;
}

std::vector<UString> McFontImpl::wrap(const UString &text, f64 max_width) const {
    std::vector<UString> lines;
    lines.emplace_back();

    UString word{};
    u32 line = 0;
    f64 line_width = 0.0;
    f64 word_width = 0.0;
    for(int i = 0; i < text.length(); i++) {
        if(text[i] == u'\n') {
            lines[line].append(word);
            lines.emplace_back();
            line++;
            line_width = 0.0;
            word.clear();
            word_width = 0.0;
            continue;
        }

        f32 char_width = getGlyphWidth(text[i]);

        if(text[i] == u' ') {
            lines[line].append(word);
            line_width += word_width;
            word.clear();
            word_width = 0.0;

            if(line_width + char_width > max_width) {
                // Ignore spaces at the end of a line
                lines.emplace_back();
                line++;
                line_width = 0.0;
            } else if(line_width > 0.0) {
                lines[line].append(u' ');
                line_width += char_width;
            }
        } else {
            if(word_width + char_width > max_width) {
                // Split word onto new line
                lines[line].append(word);
                lines.emplace_back();
                line++;
                line_width = 0.0;
                word.clear();
                word_width = 0.0;
            } else if(line_width + word_width + char_width > max_width) {
                // Wrap word on new line
                lines.emplace_back();
                line++;
                line_width = 0.0;
            }
            // Add character to word
            word.append(text[i]);
            word_width += char_width;
        }
    }

    // Don't forget! ;)
    lines[line].append(word);

    return lines;
}

// Internal helper methods

const McFontImpl::GLYPH_METRICS &McFontImpl::getGlyphMetrics(char16_t ch) const {
    FT_Face existingFace = nullptr;
    if(const auto &it = m_mGlyphMetrics.find(ch); it != m_mGlyphMetrics.end()) {
        if(it->second->inAtlas) return *it->second;
        existingFace = it->second->face;
    }

    // either no metrics, or metrics exist but glyph was evicted from atlas
    if(m_bTryFindFallbacks) {
        auto &metrics = const_cast<McFontImpl *>(this)->m_mGlyphMetrics;
        if(const_cast<McFontImpl *>(this)->loadGlyphDynamic(ch, existingFace)) {
            return *metrics[ch];
        } else {
            // fallback to unknown character glyph
            if(const auto &it = m_mGlyphMetrics.find(UNKNOWN_CHAR); it != m_mGlyphMetrics.end()) {
                // update metrics to just always point to UNKNOWN_CHAR

                return *(metrics[ch] = std::make_unique<GLYPH_METRICS>(*it->second));
            }
        }
    } else {
        if(const auto &it = m_mGlyphMetrics.find(UNKNOWN_CHAR); it != m_mGlyphMetrics.end()) {
            return *it->second;
        }
    }

    debugLog("Font Error: Missing default backup glyph (UNKNOWN_CHAR)?");
    return m_errorGlyph;
}

bool McFontImpl::loadGlyphDynamic(char16_t ch, FT_Face existingFace) {
    assert(m_bFreeTypeInitialized);

    std::string debugstr;
    if(cv::r_debug_font_unicode.getBool()) {
        const char16_t charray[]{ch, u'\0'};
        debugstr = fmt::format("{:s} (U+{:04X})", UString{&charray[0]}, (u16)ch);
    }

    FT_Face face = existingFace;
    const bool needMetrics = !face;

    if(needMetrics) {
        face = getFontFaceForGlyph(ch);
        if(!face) {
            if(cv::r_debug_font_unicode.getBool()) {
                const char *charRange = FontTypeMap::getCharacterRangeName(ch);
                if(charRange)
                    debugLog("Font Warning: Character {} ({:s}) not supported by any font", debugstr, charRange);
                else
                    debugLog("Font Warning: Character {} not supported by any font", debugstr);
            }
            return false;
        }

        logIf(cv::r_debug_font_unicode.getBool() && face != m_ftFace,
              "Font Info (for font resource {}): Using fallback font for character {}", m_parent->getName(), debugstr);
    }

    // ensure face size is set
    setFaceSize(face);

    // load glyph once - store metrics only if this is a new glyph
    FT_BitmapGlyph bitmapGlyph = loadBitmapGlyph(ch, face, needMetrics);
    if(!bitmapGlyph) return false;

    const auto &bitmap = bitmapGlyph->bitmap;

    if(bitmap.width > 0 && bitmap.rows > 0) {
        int slotIndex = allocateDynamicSlot(ch);
        const DynamicSlot &slot = m_dynamicSlots[slotIndex];

        const int maxSlotContent = getDynSlotSize() - 2 * TextureAtlas::ATLAS_PADDING;
        if(bitmap.width > maxSlotContent || bitmap.rows > maxSlotContent) {
            if(cv::r_debug_font_unicode.getBool()) {
                debugLog("Font Info: Clipping oversized glyph {} ({}x{}) to fit dynamic slot ({}x{})", debugstr,
                         bitmap.width, bitmap.rows, maxSlotContent, maxSlotContent);
            }
        }

        renderBitmapToAtlas(ch, slot.x + TextureAtlas::ATLAS_PADDING, slot.y + TextureAtlas::ATLAS_PADDING, bitmap,
                            true /*dynamic*/);

        if(cv::r_debug_font_unicode.getBool()) {
            debugLog("Font Info: Placed glyph {} in dynamic slot {} at ({}, {})", debugstr, slotIndex, slot.x, slot.y);
        }
    } else {
        // empty glyph (e.g. space) - mark as valid without atlas rendering
        m_mGlyphMetrics[ch]->inAtlas = true;
    }

    FT_Done_Glyph(reinterpret_cast<FT_Glyph>(bitmapGlyph));
    return true;
}

// atlas management methods
int McFontImpl::allocateDynamicSlot(char16_t ch) {
    m_currentAtlasTime++;

    // look for free slot
    for(size_t i = 0; i < m_dynamicSlots.size(); i++) {
        auto &dynamicSlot = m_dynamicSlots[i];
        if(!dynamicSlot.occupied) {
            dynamicSlot.character = ch;
            dynamicSlot.lastUsed = m_currentAtlasTime;
            dynamicSlot.occupied = true;
            m_dynamicSlotMap[ch] = static_cast<int>(i);
            return static_cast<int>(i);
        }
    }

    // no free slots, find LRU slot
    int lruIndex = 0;
    uint64_t oldestTime = m_dynamicSlots[0].lastUsed;
    for(size_t i = 1; i < m_dynamicSlots.size(); i++) {
        auto &dynamicSlot = m_dynamicSlots[i];
        if(dynamicSlot.lastUsed < oldestTime) {
            oldestTime = dynamicSlot.lastUsed;
            lruIndex = static_cast<int>(i);
        }
    }

    // evict the LRU slot
    auto &dynamicLRUSlot = m_dynamicSlots[lruIndex];
    if(dynamicLRUSlot.character != 0) {
        m_dynamicSlotMap.erase(dynamicLRUSlot.character);

        // mark evicted glyph as no longer in atlas, but preserve metrics for fast re-rendering
        const auto &it = m_mGlyphMetrics.find(dynamicLRUSlot.character);
        if(it != m_mGlyphMetrics.end()) {
            it->second->inAtlas = false;
        }
    }

    dynamicLRUSlot.character = ch;
    dynamicLRUSlot.lastUsed = m_currentAtlasTime;
    dynamicLRUSlot.occupied = true;
    m_dynamicSlotMap[ch] = lruIndex;

    return lruIndex;
}

void McFontImpl::markSlotUsed(char16_t ch) {
    const auto &it = m_dynamicSlotMap.find(ch);
    if(it != m_dynamicSlotMap.end()) {
        m_currentAtlasTime++;
        m_dynamicSlots[it->second].lastUsed = m_currentAtlasTime;
    }
}

void McFontImpl::initializeDynamicRegion(int atlasSize) {
    // calculate dynamic region layout
    m_dynamicRegionY = m_staticRegionHeight + TextureAtlas::ATLAS_PADDING;
    m_slotsPerRow = atlasSize / getDynSlotSize();

    // initialize dynamic slots
    const int dynamicHeight = atlasSize - m_dynamicRegionY;
    const int slotsPerColumn = dynamicHeight / getDynSlotSize();
    const int totalSlots = m_slotsPerRow * slotsPerColumn;

    m_dynamicSlots.clear();
    m_dynamicSlots.reserve(totalSlots);
    m_dynamicSlotMap.clear();

    for(int row = 0; row < slotsPerColumn; row++) {
        for(int col = 0; col < m_slotsPerRow; col++) {
            DynamicSlot slot{.x = col * getDynSlotSize(),
                             .y = m_dynamicRegionY + row * getDynSlotSize(),
                             .character = 0,
                             .lastUsed = 0,
                             .occupied = false};
            m_dynamicSlots.push_back(slot);
        }
    }

    if(cv::r_debug_font_unicode.getBool()) {
        debugLog("Font Info: Initialized dynamic region with {} slots ({}x{} each) starting at y={}", totalSlots,
                 getDynSlotSize(), getDynSlotSize(), m_dynamicRegionY);
    }
}

// consolidated glyph processing methods
bool McFontImpl::initializeFreeType() {
    assert(s_sharedFtLibraryInitialized);

    // load this font's primary face
    {
        Sync::unique_lock<Sync::shared_mutex> lock(s_sharedResourcesMutex);
        if(FT_New_Face(s_sharedFtLibrary, m_parent->sFilePath.c_str(), 0, &m_ftFace)) {
            engine->showMessageError("Font Error", "Couldn't load font file!");
            return false;
        }
    }

    if(FT_Select_Charmap(m_ftFace, ft_encoding_unicode)) {
        engine->showMessageError("Font Error", "FT_Select_Charmap() failed!");
        FT_Done_Face(m_ftFace);
        return false;
    }

    m_bFreeTypeInitialized = true;
    return true;
}

bool McFontImpl::loadGlyphMetrics(char16_t ch) {
    if(!m_bFreeTypeInitialized) return false;

    FT_Face face = getFontFaceForGlyph(ch);
    if(!face) return false;

    // for initial load, we need metrics but not the bitmap yet (will render later after packing)
    FT_BitmapGlyph glyph = loadBitmapGlyph(ch, face, true /*storeMetrics*/);
    if(!glyph) return false;

    FT_Done_Glyph(reinterpret_cast<FT_Glyph>(glyph));
    return true;
}

std::unique_ptr<u8[]> McFontImpl::createExpandedBitmapData(const FT_Bitmap &bitmap) {
    auto expandedData = std::make_unique_for_overwrite<u8[]>(static_cast<size_t>(bitmap.width) * bitmap.rows * 4);

    std::unique_ptr<Channel[]> monoBitmapUnpacked{nullptr};
    if(!m_bAntialiasing) monoBitmapUnpacked = unpackMonoBitmap(bitmap);

    for(u32 j = 0; j < bitmap.rows; j++) {
        for(u32 k = 0; k < bitmap.width; k++) {
            const size_t srcIdx = k + static_cast<size_t>(bitmap.width) * j;
            const size_t dstIdx = srcIdx * 4;

            Channel alpha = m_bAntialiasing ? bitmap.buffer[srcIdx] : monoBitmapUnpacked[srcIdx] > 0 ? 255 : 0;
            expandedData[dstIdx + 0] = 255;    // R
            expandedData[dstIdx + 1] = 255;    // G
            expandedData[dstIdx + 2] = 255;    // B
            expandedData[dstIdx + 3] = alpha;  // A
        }
    }

    return expandedData;
}

void McFontImpl::renderBitmapToAtlas(char16_t ch, int x, int y, const FT_Bitmap &bitmap, bool isDynamicSlot) {
    if(bitmap.width == 0 || bitmap.rows == 0) return;

    const int atlasWidth = m_textureAtlas->getWidth();
    const int atlasHeight = m_textureAtlas->getHeight();

    int renderWidth = std::min(static_cast<int>(bitmap.width), atlasWidth - x);
    int renderHeight = std::min(static_cast<int>(bitmap.rows), atlasHeight - y);

    if(isDynamicSlot) {
        const int maxSlotContent = getDynSlotSize() - 2 * TextureAtlas::ATLAS_PADDING;
        renderWidth = std::min(renderWidth, maxSlotContent);
        renderHeight = std::min(renderHeight, maxSlotContent);
    }

    auto expandedData = createExpandedBitmapData(bitmap);

    // if clipping is needed, create clipped data
    if(std::cmp_less(renderWidth, bitmap.width) || std::cmp_less(renderHeight, bitmap.rows)) {
        auto clippedData = std::make_unique_for_overwrite<u8[]>(static_cast<size_t>(renderWidth) * renderHeight * 4);
        const sSz srcStride = static_cast<sSz>(bitmap.width) * 4;
        const sSz dstStride = static_cast<sSz>(renderWidth) * 4;
        for(i32 row = 0; row < renderHeight; row++) {
            std::memcpy(&clippedData[row * dstStride], &expandedData[row * srcStride], dstStride);
        }
        m_textureAtlas->putAt(x, y, renderWidth, renderHeight, clippedData.get());
    } else {
        m_textureAtlas->putAt(x, y, bitmap.width, bitmap.rows, expandedData.get());
    }

    // clear 1-pixel border on right and bottom edges for texture filtering.
    // without this, linear filtering bleeds into adjacent/old glyph data.
    if(isDynamicSlot) {
        const int rightEdgeX = x + renderWidth;
        const int bottomEdgeY = y + renderHeight;

        if(rightEdgeX < atlasWidth) {
            m_textureAtlas->clearRegion(rightEdgeX, y, 1, renderHeight);
        }
        if(bottomEdgeY < atlasHeight) {
            m_textureAtlas->clearRegion(x, bottomEdgeY, renderWidth + 1, 1);
        }
    }

    // update metrics with atlas coordinates
    GLYPH_METRICS &metrics = *m_mGlyphMetrics[ch];
    metrics.uvPixelsX = static_cast<u32>(x);
    metrics.uvPixelsY = static_cast<u32>(y);
    metrics.inAtlas = true;

    if(isDynamicSlot) {
        m_bAtlasNeedsReload = true;
    }
}

bool McFontImpl::initializeAtlas() {
    if(m_vInitialGlyphs.empty()) return true;

    // prepare packing rectangles
    std::vector<TextureAtlas::PackRect> packRects;
    std::vector<char16_t> rectsToChars;
    packRects.reserve(m_vInitialGlyphs.size());
    rectsToChars.reserve(m_vInitialGlyphs.size());

    size_t rectIndex = 0;
    for(char16_t ch : m_vInitialGlyphs) {
        auto &metricsP = m_mGlyphMetrics[ch];
        // mark all initial glyphs as in atlas (including zero-size glyphs like space)
        if(!metricsP) {
            // this is convoluted/spaghetti but we might have skipped creating some glyphs
            // for characters like DEL, just allocate the memory for it and mark it as in atlas
            metricsP = std::make_unique<GLYPH_METRICS>();
            metricsP->inAtlas = true;
            continue;
        }

        auto &metrics = *metricsP;
        metrics.inAtlas = true;

        if(metrics.sizePixelsX > 0 && metrics.sizePixelsY > 0) {
            // add packrect
            TextureAtlas::PackRect pr{.x = 0,
                                      .y = 0,
                                      .width = static_cast<int>(metrics.sizePixelsX),
                                      .height = static_cast<int>(metrics.sizePixelsY),
                                      .id = static_cast<int>(rectIndex)};
            packRects.push_back(pr);
            rectsToChars.push_back(ch);
            rectIndex++;
        }
    }

    if(packRects.empty()) return true;

    // calculate optimal size for static glyphs and create larger atlas for dynamic region
    const size_t staticAtlasSize =
        TextureAtlas::calculateOptimalSize(packRects, ATLAS_OCCUPANCY_TARGET, MIN_ATLAS_SIZE, MAX_ATLAS_SIZE);

    const size_t totalAtlasSize = static_cast<size_t>(staticAtlasSize * ATLAS_SIZE_MULTIPLIER);
    const size_t finalAtlasSize = std::min(totalAtlasSize, static_cast<size_t>(MAX_ATLAS_SIZE));

    resourceManager->requestNextLoadUnmanaged();
    m_textureAtlas.reset(resourceManager->createTextureAtlas(static_cast<int>(finalAtlasSize),
                                                             static_cast<int>(finalAtlasSize), m_bAntialiasing));

    // pack glyphs into static region only
    if(!m_textureAtlas->packRects(packRects)) {
        engine->showMessageError("Font Error", "Failed to pack glyphs into atlas!");
        return false;
    }

    // find the height used by static glyphs
    m_staticRegionHeight = 0;
    for(const auto &rect : packRects) {
        m_staticRegionHeight = std::max(m_staticRegionHeight, rect.y + rect.height + TextureAtlas::ATLAS_PADDING);
    }

    // render all packed glyphs to static region
    for(const auto &rect : packRects) {
        const char16_t ch = rectsToChars[rect.id];
        const auto &metrics = *m_mGlyphMetrics[ch];
        if(metrics.face) {
            // ensure face size is set
            setFaceSize(metrics.face);

            // load bitmap (metrics already stored, so don't overwrite)
            FT_BitmapGlyph bitmapGlyph = loadBitmapGlyph(ch, metrics.face, false /*storeMetrics*/);
            if(bitmapGlyph) {
                renderBitmapToAtlas(ch, rect.x, rect.y, bitmapGlyph->bitmap, false /*not dynamic*/);
                FT_Done_Glyph(reinterpret_cast<FT_Glyph>(bitmapGlyph));
            }
        }
    }

    // initialize dynamic region after static glyphs are placed
    initializeDynamicRegion(static_cast<int>(finalAtlasSize));

    return true;
}

// fallback font management
FT_Face McFontImpl::getFontFaceForGlyph(char16_t ch) {
    // quick blacklist check
    if(m_bTryFindFallbacks && m_parent->isAsyncReady()) {  // skip blacklisting during initial load
        Sync::shared_lock<Sync::shared_mutex> lock(s_sharedResourcesMutex);
        if(s_sharedFallbackFaceBlacklist.contains(ch)) {
            return nullptr;
        }
    }

    // check primary font first
    FT_UInt glyphIndex = FT_Get_Char_Index(m_ftFace, ch);
    if(glyphIndex != 0) return m_ftFace;
    if(!m_bTryFindFallbacks) return nullptr;

    // search through shared fallback fonts
    FT_Face foundFace = nullptr;
    {
        Sync::shared_lock<Sync::shared_mutex> lock(s_sharedResourcesMutex);
        for(auto &fallback : s_sharedFallbackFonts) {
            glyphIndex = FT_Get_Char_Index(fallback.face, ch);
            if(glyphIndex != 0) {
                foundFace = fallback.face;
                break;
            }
        }
    }

    if(foundFace) return foundFace;

    // character not found in any font, add to blacklist
    // NOTE: skip blacklisting during initial load to allow lazier synchronization
    // with other fonts that may be loading simultaneously
    if(m_parent->isAsyncReady()) {
        Sync::unique_lock<Sync::shared_mutex> lock(s_sharedResourcesMutex);
        s_sharedFallbackFaceBlacklist.insert(ch);
    }

    return nullptr;
}

FT_BitmapGlyph McFontImpl::loadBitmapGlyph(char16_t ch, FT_Face face, bool storeMetrics) {
    if(FT_Load_Glyph(face, FT_Get_Char_Index(face, ch),
                     m_bAntialiasing ? FT_LOAD_TARGET_NORMAL : FT_LOAD_TARGET_MONO)) {
        debugLog("Font Error: Failed to load glyph for character U+{:04X}", (unsigned int)ch);
        return nullptr;
    }

    FT_Glyph glyph{};
    if(FT_Get_Glyph(face->glyph, &glyph)) {
        debugLog("Font Error: Failed to get glyph for character U+{:04X}", (unsigned int)ch);
        return nullptr;
    }

    FT_Glyph_To_Bitmap(&glyph, m_bAntialiasing ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO, nullptr, 1);

    auto bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>(glyph);

    if(storeMetrics) {
        auto &metricsPtr = m_mGlyphMetrics[ch];
        assert(!metricsPtr);
        metricsPtr = std::make_unique<GLYPH_METRICS>();
        auto &metrics = *metricsPtr;

        metrics.left = bitmapGlyph->left;
        metrics.top = bitmapGlyph->top;
        metrics.width = bitmapGlyph->bitmap.width;
        metrics.rows = bitmapGlyph->bitmap.rows;
        metrics.advance_x = static_cast<float>(face->glyph->advance.x >> 6);

        // to be updated when rendered to texture atlas
        metrics.inAtlas = false;
        metrics.face = face;
        metrics.uvPixelsX = 0;
        metrics.uvPixelsY = 0;
        metrics.sizePixelsX = bitmapGlyph->bitmap.width;
        metrics.sizePixelsY = bitmapGlyph->bitmap.rows;
    }

    return bitmapGlyph;
}

size_t McFontImpl::buildGlyphGeometry(std::vector<vec3> &vertsOut, std::vector<vec2> &texcoordsOut,
                                      const GLYPH_METRICS &gm, float advanceX, size_t startVertex) {
    const float atlasWidth{static_cast<float>(m_textureAtlas->getAtlasImage()->getWidth())};
    const float atlasHeight{static_cast<float>(m_textureAtlas->getAtlasImage()->getHeight())};

    const float x{+static_cast<float>(gm.left) + advanceX};
    const float y{-static_cast<float>(gm.top - gm.rows)};
    const float sx{static_cast<float>(gm.width)};
    const float sy{static_cast<float>(-gm.rows)};

    const float texX{static_cast<float>(gm.uvPixelsX) / atlasWidth};
    const float texY{static_cast<float>(gm.uvPixelsY) / atlasHeight};
    const float texSizeX{static_cast<float>(gm.sizePixelsX) / atlasWidth};
    const float texSizeY{static_cast<float>(gm.sizePixelsY) / atlasHeight};

    // corners of the "quad"
    vec3 bottomLeft{x, y + sy, 0};
    vec3 topLeft{x, y, 0};
    vec3 topRight{x + sx, y, 0};
    vec3 bottomRight{x + sx, y + sy, 0};

    // texcoords
    vec2 texBottomLeft{texX, texY};
    vec2 texTopLeft{texX, texY + texSizeY};
    vec2 texTopRight{texX + texSizeX, texY + texSizeY};
    vec2 texBottomRight{texX + texSizeX, texY};

    if constexpr(VERTS_PER_VAO > 4) {
        // triangles (quads are slower for GL ES because they need to be converted to triangles at submit time)
        // first triangle (bottom-left, top-left, top-right)
        vertsOut[startVertex] = bottomLeft;
        vertsOut[startVertex + 1] = topLeft;
        vertsOut[startVertex + 2] = topRight;

        texcoordsOut[startVertex] = texBottomLeft;
        texcoordsOut[startVertex + 1] = texTopLeft;
        texcoordsOut[startVertex + 2] = texTopRight;

        // second triangle (bottom-left, top-right, bottom-right)
        vertsOut[startVertex + 3] = bottomLeft;
        vertsOut[startVertex + 4] = topRight;
        vertsOut[startVertex + 5] = bottomRight;

        texcoordsOut[startVertex + 3] = texBottomLeft;
        texcoordsOut[startVertex + 4] = texTopRight;
        texcoordsOut[startVertex + 5] = texBottomRight;
    } else {
        // quads
        vertsOut[startVertex] = bottomLeft;       // bottom-left
        vertsOut[startVertex + 1] = topLeft;      // top-left
        vertsOut[startVertex + 2] = topRight;     // top-right
        vertsOut[startVertex + 3] = bottomRight;  // bottom-right

        texcoordsOut[startVertex] = texBottomLeft;
        texcoordsOut[startVertex + 1] = texTopLeft;
        texcoordsOut[startVertex + 2] = texTopRight;
        texcoordsOut[startVertex + 3] = texBottomRight;
    }
    return startVertex + VERTS_PER_VAO;
}

void McFontImpl::buildStringGeometry(const UString &text, std::vector<vec3> &vertsOut, std::vector<vec2> &texcoordsOut,
                                     size_t maxGlyphs, std::vector<const GLYPH_METRICS *> &gmOut) {
    float advanceX = 0.0f;
    size_t startVertex = 0;

    for(int i = 0; i < maxGlyphs; i++) {
        const GLYPH_METRICS &gm = getGlyphMetrics(text[i]);

        startVertex = buildGlyphGeometry(vertsOut, texcoordsOut, gm, advanceX, startVertex);
        advanceX += gm.advance_x;

        // mark dynamic slot as recently used (if this character is in a dynamic slot)
        markSlotUsed(text[i]);

        // add glyph metrics to out parameter
        gmOut[i] = &gm;
    }

    // reload atlas if new glyphs were added to dynamic slots
    if(m_bAtlasNeedsReload) {
        m_textureAtlas->reloadAtlasImage();
        m_bAtlasNeedsReload = false;
    }

    return;
}

std::unique_ptr<Channel[]> McFontImpl::unpackMonoBitmap(const FT_Bitmap &bitmap) {
    auto result = std::make_unique_for_overwrite<Channel[]>(static_cast<size_t>(bitmap.rows) * bitmap.width);

    for(u32 y = 0; y < bitmap.rows; y++) {
        for(i32 byteIdx = 0; byteIdx < bitmap.pitch; byteIdx++) {
            const u8 byteValue = bitmap.buffer[y * bitmap.pitch + byteIdx];
            const u32 numBitsDone = byteIdx * 8;
            const u32 rowstart = y * bitmap.width + byteIdx * 8;

            // why do these have to be 32bit ints exactly... ill look into it later
            const int bits = std::min(8, static_cast<int>(bitmap.width - numBitsDone));
            for(int bitIdx = 0; bitIdx < bits; bitIdx++) {
                result[rowstart + bitIdx] = (byteValue & (1 << (7 - bitIdx))) ? 1 : 0;
            }
        }
    }

    return result;
}

// helper to set font size on any face
void McFontImpl::setFaceSize(FT_Face face) {
    if(s_lastSizedFace.face != face || s_lastSizedFace.size != m_iFontSize || s_lastSizedFace.dpi != m_iFontDPI) {
        s_lastSizedFace = {.face = face, .size = m_iFontSize, .dpi = m_iFontDPI};

        FT_Set_Char_Size(face, (FT_F26Dot6)(m_iFontSize * 64L), (FT_F26Dot6)(m_iFontSize * 64L), m_iFontDPI,
                         m_iFontDPI);
    }
}

////////////////////////////////////////////////////////////////////////////////////
// shared/static FreeType management stuff below
////////////////////////////////////////////////////////////////////////////////////

namespace {  // static namespace

bool loadFallbackFont(const UString &fontPath, bool isSystemFont) {
    FT_Face face{};
    if(FT_New_Face(s_sharedFtLibrary, fontPath.toUtf8(), 0, &face)) {
        logIfCV(r_debug_font_unicode, "Font Warning: Failed to load fallback font: {:s}", fontPath);
        return false;
    }

    if(FT_Select_Charmap(face, ft_encoding_unicode)) {
        logIfCV(r_debug_font_unicode, "Font Warning: Failed to select unicode charmap for fallback font: {:s}",
                fontPath);
        FT_Done_Face(face);
        return false;
    }

    // don't set font size here, will be set when the face is used by individual font instances
    s_sharedFallbackFonts.push_back(FallbackFont{fontPath, face, isSystemFont});
    return true;
}

void discoverSystemFallbacks() {
#ifdef MCENGINE_PLATFORM_WINDOWS
    std::string windir;
    windir.resize(MAX_PATH + 1);
    int ret = GetWindowsDirectoryA(windir.data(), MAX_PATH);
    if(ret <= 0) return;
    windir.resize(ret);

    std::vector<std::string> systemFonts = {
        windir + "\\Fonts\\arial.ttf",
        windir + "\\Fonts\\msyh.ttc",      // Microsoft YaHei (Chinese)
        windir + "\\Fonts\\malgun.ttf",    // Malgun Gothic (Korean)
        windir + "\\Fonts\\meiryo.ttc",    // Meiryo (Japanese)
        windir + "\\Fonts\\seguiemj.ttf",  // Segoe UI Emoji
        windir + "\\Fonts\\seguisym.ttf"   // Segoe UI Symbol
    };
#elif defined(MCENGINE_PLATFORM_LINUX)
    // linux system fonts (common locations)
    std::vector<std::string> systemFonts = {"/usr/share/fonts/TTF/dejavu/DejaVuSans.ttf",
                                            "/usr/share/fonts/TTF/DejaVuSans.ttf",
                                            "/usr/share/fonts/TTF/liberation/LiberationSans-Regular.ttf",
                                            "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
                                            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
                                            "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                                            "/usr/share/fonts/TTF/noto/NotoColorEmoji.ttf"};
#else  // TODO: loading WOFF fonts in wasm? idk
    std::vector<std::string> systemFonts;
    return;
#endif

    for(auto &fontPath : systemFonts) {
        if(File::existsCaseInsensitive(fontPath) == File::FILETYPE::FILE) {
            loadFallbackFont(fontPath.c_str(), true);
        }
    }
}

}  // namespace

bool McFont::initSharedResources() {
    if(!s_sharedFtLibraryInitialized) {
        if(FT_Init_FreeType(&s_sharedFtLibrary)) {
            engine->showMessageError("Font Error", "FT_Init_FreeType() failed!");
            return false;
        }
    }

    s_sharedFtLibraryInitialized = true;

    // check all bundled fonts first
    std::vector<std::string> bundledFallbacks = env->getFilesInFolder(MCENGINE_FONTS_PATH "/");
    for(const auto &fontName : bundledFallbacks) {
        if(loadFallbackFont(UString{fontName}, false)) {
            logIfCV(r_debug_font_unicode, "Font Info: Loaded bundled fallback font: {:s}", fontName);
        }
    }

    // then find likely system fonts
    if(!Env::cfg(OS::WASM) && cv::font_load_system.getBool()) discoverSystemFallbacks();

    s_sharedFallbacksInitialized = true;

    return s_sharedFtLibraryInitialized && s_sharedFallbacksInitialized;
}

void McFont::cleanupSharedResources() {
    // clean up shared fallback fonts
    for(auto &fallbackFont : s_sharedFallbackFonts) {
        if(fallbackFont.face) FT_Done_Face(fallbackFont.face);
    }
    s_sharedFallbackFonts.clear();
    s_sharedFallbacksInitialized = false;

    // clean up shared freetype library
    if(s_sharedFtLibraryInitialized) {
        if(s_sharedFtLibrary) {
            FT_Done_FreeType(s_sharedFtLibrary);
            s_sharedFtLibrary = nullptr;
        }
        s_sharedFtLibraryInitialized = false;
    }
}

void McFont::drawTextureAtlas() const {
    // debug
    g->setColor(0xFFFFFFFF);
    g->pushTransform();
    {
        const auto &ta = pImpl->m_textureAtlas;
        const i32 offscreenPixels = ta->getHeight() - (engine->getScreenHeight() * 0.75);
        const f32 textYRatio = (f32)(engine->getScreenHeight() * 0.75) / (f32)ta->getHeight();

        const i32 yOffset =
            textYRatio >= 1
                ? 0
                : -(i32)((f32)(offscreenPixels) * (mouse->getPos().y / (f32)(engine->getScreenHeight() * 0.75)));
        const f32 fitWidth = (engine->getScreenWidth() * 0.75) / ta->getWidth();

        g->scale(fitWidth, fitWidth);
        g->translate(ta->getWidth() / 2.f, (ta->getHeight() / 2.f) + yOffset);
        g->drawImage(ta->getAtlasImage());
    }
    g->popTransform();
}

void McFont::drawDebug() const {
    if(m_bDebugDrawAtlas) {
        this->drawTextureAtlas();
    }
}

namespace cv {
static ConVar r_debug_draw_font_atlas("r_debug_draw_font_atlas", "",
                                      CLIENT | NOSAVE | (!Env::cfg(BUILD::DEBUG) ? HIDDEN : 0),
                                      [](std::string_view atlases) -> void {
                                          if(!resourceManager) return;
                                          const auto &fonts = resourceManager->getFonts();

                                          if(atlases.empty()) {
                                              for(auto *font : fonts) {
                                                  font->m_bDebugDrawAtlas = false;
                                              }
                                              return;
                                          }
                                          std::string fontNames;
                                          for(auto *font : fonts) {
                                              fontNames += font->getName() + ',';
                                          }
                                          logRaw("got font names {}", fontNames);

                                          const auto &csvs = SString::split(atlases, ',');
                                          for(auto atlas : csvs) {
                                              if(const auto &it = std::ranges::find_if(
                                                     fonts,
                                                     [atlas](const auto &font) {
                                                         return SString::to_lower(font->getName()) ==
                                                                SString::to_lower(atlas);
                                                     });

                                                 it != fonts.end()) {
                                                  (*it)->m_bDebugDrawAtlas = true;
                                              }
                                          }
                                      });

}  // namespace cv