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
#include "Image.h"
#include "Hashing.h"
#include "Graphics.h"
#include "Shader.h"
#include "UniString.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <set>
#include <utility>
#include <cassert>
#include <map>
#include <memory>
#include <vector>

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

// temp (hopefully) debug convar
namespace cv {
static ConVar r_debug_drawstring_fallback("r_debug_drawstring_fallback", false, CLIENT);
}

namespace {  // static namespace

// constants for atlas generation and rendering
constexpr const float ATLAS_OCCUPANCY_TARGET{0.75f};  // target atlas occupancy before resize

// how much larger to make atlas for dynamic region
// we initially pack the ASCII characters + initial characters into a region,
// then dynamically loaded glyphs are placed in the remaining space in fixed-size slots (not packed)
// this maximizes the amount of fallback glyphs we can have loaded at once for a fixed amount of memory usage
constexpr const size_t ATLAS_SIZE_MULTIPLIER{3};

constexpr const size_t MIN_ATLAS_SIZE{256};
constexpr const size_t MAX_ATLAS_SIZE{4096};

constexpr const char32_t UNKNOWN_CHAR{U'?'};  // ASCII '?'

constexpr const size_t VERTS_PER_VAO{Env::cfg(REND::GLES32 | REND::DX11 | REND::SDLGPU) ? 6 : 4};

// this is still a very conservative amount of memory
constexpr const size_t CACHED_STRINGS_PER_FONT{Env::cfg(OS::WASM) ? 1024
                                                                  : 4096};  // be more conservative in WASM (32bit)
size_t stringToCacheIndex(std::string_view str) { return std::hash<std::string_view>{}(str) % CACHED_STRINGS_PER_FONT; }

// other shared-across-instances things
struct FallbackFont {
    std::string fontPath;
    FT_Face face;
    bool isSystemFont;
};

// global shared freetype resources
FT_Library s_sharedFtLibrary{nullptr};
std::vector<FallbackFont> s_sharedFallbackFonts;
Hash::flat::set<char32_t> s_sharedFallbackFaceBlacklist;
FT_Face s_sharedEmojiFace{nullptr};

bool s_sharedFtLibraryInitialized{false};
bool s_sharedFallbacksInitialized{false};

// face size tracking to avoid redundant setFaceSize calls
struct LastSizedFTFace {
    FT_Face face{nullptr};
    int size{0};
    int dpi{0};
    [[nodiscard]] inline bool operator==(const LastSizedFTFace &) const = default;
} s_lastSizedFace{};

// text effect shader (shadow/outline in a single pass)
Shader *s_textShader{nullptr};
bool s_textShaderBroken{false};

class TextVAO : public VertexArrayObject {
    MOVECONSTRUCTONLY(TextVAO)
   public:
    constexpr TextVAO(DrawPrimitive primitive = DrawPrimitive{2} /* TRIANGLES */,
                      DrawUsageType usage = DrawUsageType{0} /* STATIC */, bool keepInSystemMemory = false)
        : VertexArrayObject(primitive, usage, keepInSystemMemory) {}
    ~TextVAO() override = default;
    friend McFontImpl;
};

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
        bool isColor;  // color bitmap glyph (e.g. emoji from CBDT font)
    };

    // texture atlas dynamic slot management
    struct DynamicSlot {
        int x, y;            // position in atlas
        char32_t character;  // character in this slot (0 if empty)
        uint64_t lastUsed;   // for LRU eviction
        bool occupied;
    };

    struct VerTexMetCacheEntry final {
        std::string string;
        float cachedExpand{0.f};

        void clear() {
            string.clear();
            cachedExpand = 0.f;
            VAO.clear();
            emojiVAO.clear();
            metrics.clear();
        }

        std::vector<vec3> &getVerts() { return VAO.vertices; }
        std::vector<vec2> &getTexcoords() { return VAO.texcoords; }
        std::vector<const GLYPH_METRICS *> &getMetrics() { return metrics; }

        [[nodiscard]] const std::vector<vec3> &getVerts() const { return VAO.vertices; }
        [[nodiscard]] const std::vector<vec2> &getTexcoords() const { return VAO.texcoords; }
        [[nodiscard]] const std::vector<const GLYPH_METRICS *> &getMetrics() const { return metrics; }

        std::vector<vec3> &getEmojiVerts() { return emojiVAO.vertices; }
        std::vector<vec2> &getEmojiTexcoords() { return emojiVAO.texcoords; }
        [[nodiscard]] const std::vector<vec3> &getEmojiVerts() const { return emojiVAO.vertices; }
        [[nodiscard]] const std::vector<vec2> &getEmojiTexcoords() const { return emojiVAO.texcoords; }

        [[nodiscard]] VertexArrayObject *getVAO() { return &VAO; }
        [[nodiscard]] VertexArrayObject *getEmojiVAO() { return &emojiVAO; }

        [[nodiscard]] const VertexArrayObject *getVAO() const { return &VAO; }
        [[nodiscard]] const VertexArrayObject *getEmojiVAO() const { return &emojiVAO; }

       private:
        TextVAO VAO{
            Env::cfg(REND::GLES32 | REND::DX11 | REND::SDLGPU) ? DrawPrimitive::TRIANGLES : DrawPrimitive::QUADS,
            DrawUsageType::DYNAMIC};
        TextVAO emojiVAO{
            Env::cfg(REND::GLES32 | REND::DX11 | REND::SDLGPU) ? DrawPrimitive::TRIANGLES : DrawPrimitive::QUADS,
            DrawUsageType::DYNAMIC};
        std::vector<const GLYPH_METRICS *> metrics{};
    };

    std::string m_sActualFilePath;

    std::vector<char32_t> m_vInitialGlyphs;
    std::unordered_map<char32_t, std::unique_ptr<GLYPH_METRICS>> m_mGlyphMetrics;

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
    std::unordered_map<char32_t, int> m_dynamicSlotMap;  // character -> slot index for O(1) lookup
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
    McFontImpl(McFont *parent, const std::span<const char32_t> &characters, int fontSize, bool antialiasing,
               int fontDPI);

    ~McFontImpl() { destroy(); }

    void init();
    void initAsync();
    void destroy();

    // Main string drawing functions
    void drawString(std::string_view text, std::optional<TextFX> effects = std::nullopt);

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

    [[nodiscard]] float getGlyphWidth(char32_t character) const;
    [[nodiscard]] float getGlyphHeight(char32_t character) const;
    [[nodiscard]] float getStringWidth(std::string_view text) const;
    [[nodiscard]] float getStringHeight(std::string_view text) const;

    // Public util
    [[nodiscard]] std::vector<std::string> wrap(std::string_view text, f64 max_width) const;
    [[nodiscard]] std::string ellipsize(std::string_view text, f64 max_width) const;

   private:
    // Shared ctor helper
    void constructor(const std::span<const char32_t> &characters, int fontSize, bool antialiasing, int fontDPI);

    // Internal helper methods below here
    bool initializeFreeType();

    // drawString helpers
    void drawStringShadered(VerTexMetCacheEntry &readyBuffer, const TextFX &effects) const;
    void drawStringPlain(VerTexMetCacheEntry &readyBuffer, bool hasShadow,
                         std::optional<TextFX> effects = std::nullopt) const;

    // atlas management methods
    int allocateDynamicSlot(char32_t ch);

    void markSlotUsed(char32_t ch);

    void initializeDynamicRegion(int atlasSize);

    // consolidated glyph processing methods

    [[nodiscard]] const GLYPH_METRICS &getGlyphMetrics(char32_t ch) const;

    // if existingFace is not nullptr, we had metrics already for this glyph (evicted)
    bool loadGlyphDynamic(char32_t ch, FT_Face existingFace);
    bool loadGlyphMetrics(char32_t ch);

    // loads glyph from face and converts to bitmap. Returns nullptr on failure.
    // if storeMetrics is true, stores metrics in m_mGlyphMetrics[ch].
    // caller is responsible for calling FT_Done_Glyph on returned glyph.
    FT_BitmapGlyph loadBitmapGlyph(char32_t ch, FT_Face face, bool storeMetrics);

    // renders bitmap to atlas at specified position. updates UV coords and sets inAtlas.
    // handles grayscale->white+alpha, BGRA->RGBA conversion, and color bitmap downscaling.
    void renderBitmapToAtlas(char32_t ch, int x, int y, const FT_Bitmap &bitmap, bool isDynamicSlot);

    // for initial glyphs, packed
    bool initializeAtlas();

    // fallback font management
    FT_Face getFontFaceForGlyph(char32_t ch);

    // puts a glyph quad/tri into given vertex/texcoord buffer at startIndex.
    // expandRight/expandDown extend the quad into atlas padding for shadow/outline visibility.
    void buildGlyphGeometry(std::vector<vec3> &vertsOut, std::vector<vec2> &texcoordsOut, const GLYPH_METRICS &gm,
                            float advanceX, size_t startIndex, float expandLeft = 0.f, float expandRight = 0.f,
                            float expandUp = 0.f, float expandDown = 0.f);

    // builds full string geometry into buffer, including emoji overlay arrays.
    void buildStringGeometry(VerTexMetCacheEntry &buffer, size_t maxGlyphs, float expandLeft = 0.f,
                             float expandRight = 0.f, float expandUp = 0.f, float expandDown = 0.f);

    static std::unique_ptr<Channel[]> unpackMonoBitmap(const FT_Bitmap &bitmap);

    // helper to set font size on any face for this font instance
    void setFaceSize(FT_Face face);

    [[nodiscard]] inline int getDynSlotSize() const {
        // pixel em size (ceiling) + padding on each side
        return (((m_iFontSize + 2 /* idk, some fudge to make this work for small fonts */) * m_iFontDPI) + 71) / 72 +
               (2 * TextureAtlas::ATLAS_PADDING);
    }
};

////////////////////////////////////////////////////////////////////////////////////
// Public passthroughs start
////////////////////////////////////////////////////////////////////////////////////

McFont::McFont(std::string filepath, int fontSize, bool antialiasing, int fontDPI)
    : Resource(FONT, std::move(filepath), /*doFilesystemExistenceCheck=*/false),
      pImpl(this, fontSize, antialiasing, fontDPI) {}

McFont::McFont(std::string filepath, const std::span<const char32_t> &characters, int fontSize, bool antialiasing,
               int fontDPI)
    : Resource(FONT, std::move(filepath), /*doFilesystemExistenceCheck=*/false),
      pImpl(this, characters, fontSize, antialiasing, fontDPI) {}

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

void McFont::drawString(std::string_view text, std::optional<TextFX> effects) {
    return pImpl->drawString(text, effects);
}

float McFont::getGlyphWidth(char32_t character) const { return pImpl->getGlyphWidth(character); }
float McFont::getGlyphHeight(char32_t character) const { return pImpl->getGlyphHeight(character); }
float McFont::getStringWidth(std::string_view text) const { return pImpl->getStringWidth(text); }
float McFont::getStringHeight(std::string_view text) const { return pImpl->getStringHeight(text); }
std::vector<std::string> McFont::wrap(std::string_view text, f64 max_width) const {
    return pImpl->wrap(text, max_width);
}
std::string McFont::ellipsize(std::string_view text, f64 max_width) const { return pImpl->ellipsize(text, max_width); }
////////////////////////////////////////////////////////////////////////////////////
// Public passthroughs end, implementation begins
////////////////////////////////////////////////////////////////////////////////////

// they are initialized in constructor()
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init, hicpp-member-init)
McFontImpl::McFontImpl(McFont *parent, int fontSize, bool antialiasing, int fontDPI) : m_parent(parent) {
    // initialize with basic ASCII, load the rest as needed
    std::array<char32_t, 96> characters;  // NOLINT
    std::ranges::iota(characters, 32);
    m_bTryFindFallbacks = true;
    constructor(characters, fontSize, antialiasing, fontDPI);
}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init, hicpp-member-init)
McFontImpl::McFontImpl(McFont *parent, const std::span<const char32_t> &characters, int fontSize, bool antialiasing,
                       int fontDPI)
    : m_parent(parent) {
    // don't try to find fallbacks if we had an explicitly-passed character set on construction
    m_bTryFindFallbacks = false;
    constructor(characters, fontSize, antialiasing, fontDPI);
}

void McFontImpl::constructor(const std::span<const char32_t> &characters, int fontSize, bool antialiasing,
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
                    .inAtlas = true,
                    .isColor = false};

    // pre-allocate space for initial glyphs
    m_vInitialGlyphs.reserve(characters.size());
    for(char32_t character : characters) {
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
    // find best font
    if(m_sActualFilePath.empty()) {
        std::string candidate = m_parent->sFilePath;
        SString::to_lower(candidate);
        // if it already ends with a font extension, use it directly
        if((candidate.ends_with(".woff2") || candidate.ends_with(".woff") || candidate.ends_with(".ttf") ||
            candidate.ends_with(".otf")) &&
           Environment::fileExists(candidate)) {
            m_sActualFilePath = candidate;
        } else {
            for(const auto &ext : std::array{".woff2"s, ".woff"s, ".ttf"s, ".otf"s}) {
                candidate = m_parent->sFilePath + ext;
                if(Environment::fileExists(candidate)) {
                    m_sActualFilePath = candidate;
                    m_parent->sFilePath = m_sActualFilePath;
                    break;
                }
            }
        }
    }
    if(m_sActualFilePath.empty()) {
        debugLog("Could not find font {}!", m_parent->sFilePath);
        return;
    }
    debugLog("Loading font: {:s}", m_sActualFilePath);
    assert(s_sharedFtLibraryInitialized);
    assert(s_sharedFallbacksInitialized);

    if(!initializeFreeType()) return;

    // set font size for this instance's primary face
    setFaceSize(m_ftFace);

    m_mGlyphMetrics.reserve(m_vInitialGlyphs.size());
    // load metrics for all initial glyphs
    for(char32_t ch : m_vInitialGlyphs) {
        loadGlyphMetrics(ch);
    }

    // create+pack initial atlas and render all glyphs
    if(!initializeAtlas()) return;

    // precalculate average/max ASCII glyph height (unless it was already set manually)
    if(!m_bHeightManuallySet && m_fHeight > 0.f) {
        m_fHeight = 0.0f;
        for(int i = 32; i < 128; i++) {
            const int curHeight = getGlyphMetrics(static_cast<char32_t>(i)).top;
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

void McFontImpl::drawStringShadered(VerTexMetCacheEntry &readyBuffer, const TextFX &sc) const {
    const float atlasW = static_cast<float>(m_textureAtlas->getAtlasImage()->getWidth());
    const float atlasH = static_cast<float>(m_textureAtlas->getAtlasImage()->getHeight());

    s_textShader->enable();
    s_textShader->setUniform4f("col", sc.col_text.Rf(), sc.col_text.Gf(), sc.col_text.Bf(), sc.col_text.Af());
    s_textShader->setUniform4f("col_shadow", sc.col_shadow.Rf(), sc.col_shadow.Gf(), sc.col_shadow.Bf(),
                               sc.col_shadow.Af());
    s_textShader->setUniform4f("col_outline", sc.col_outline.Rf(), sc.col_outline.Gf(), sc.col_outline.Bf(),
                               sc.col_outline.Af());
    s_textShader->setUniform4f("params", sc.offs_px / atlasW, sc.offs_px / atlasH, sc.outline_px / atlasW,
                               sc.outline_px / atlasH);
    s_textShader->setUniform4f("params2", sc.shadow_softness_px / atlasW, sc.shadow_softness_px / atlasH, 0.f, 0.f);

    if(!readyBuffer.getVerts().empty()) {
        g->drawVAO(readyBuffer.getVAO());
    }

    if(!readyBuffer.getEmojiVerts().empty()) {
        // emoji: use texture RGB directly (color glyphs), keep shadow/outline
        s_textShader->setUniform4f("col", 1.f, 1.f, 1.f, sc.col_text.Af());
        s_textShader->setUniform4f("params2", sc.shadow_softness_px / atlasW, sc.shadow_softness_px / atlasH, 1.f, 0.f);
        g->drawVAO(readyBuffer.getEmojiVAO());
    }

    s_textShader->disable();
}

void McFontImpl::drawStringPlain(VerTexMetCacheEntry &readyBuffer, bool hasShadow,
                                 std::optional<TextFX> effects) const {
    if(!readyBuffer.getVerts().empty()) {
        if(hasShadow) {
            const auto &shadowConf = *effects;
            const float px = shadowConf.offs_px;

            g->translate(px, px);
            g->setColor(shadowConf.col_shadow);
            g->drawVAO(readyBuffer.getVAO());
            g->translate(-px, -px);
            g->setColor(shadowConf.col_text);
        }

        g->drawVAO(readyBuffer.getVAO());
    }

    // emoji
    if(!readyBuffer.getEmojiVerts().empty()) {
        const Color savedColor = g->getColor();
        if(hasShadow) {
            const auto &shadowConf = *effects;
            const float px = shadowConf.offs_px;

            g->translate(px, px);
            g->setColor(shadowConf.col_shadow);
            g->drawVAO(readyBuffer.getEmojiVAO());
            g->translate(-px, -px);
            g->setColor(argb(shadowConf.col_text.a, 255, 255, 255));
        } else {
            g->setColor(argb(savedColor.a, 255, 255, 255));
        }

        g->drawVAO(readyBuffer.getEmojiVAO());
        g->setColor(savedColor);
    }
}

void McFontImpl::drawString(std::string_view text, std::optional<TextFX> effects) {
    if(!m_parent->isReady()) return;

    const auto numCodepoints = UniString::num_codepoints(text);
    if(numCodepoints == 0 || numCodepoints > cv::r_drawstring_max_string_length.getInt()) return;

    // compute directional expansion for shadow/outline effects (needed before cache check).
    // shadow extends to the bottom-right only; outline extends in all directions.
    const bool hasEffects = effects.has_value() && (effects->col_shadow.a > 0 || effects->col_outline.a > 0);
    const float outlinePx = (hasEffects && effects->col_outline.a > 0) ? effects->outline_px : 0.f;
    const float shadowPx = (hasEffects && effects->col_shadow.a > 0) ? effects->offs_px : 0.f;
    // cap outline expansion so that expansion + outline sampling reach stays within atlas padding.
    // without this, the shader's 8-tap outline samples at the expanded quad edge overshoot the
    // padding and read neighboring glyph data, producing colored fringe artifacts.
    // the extra 1px margin accounts for bilinear filtering interpolating across the padding boundary
    const float maxOutlineExpand = std::max(0.f, static_cast<float>(TextureAtlas::ATLAS_PADDING) - outlinePx - 1.f);
    const float clampedOutlineExpand = std::min(outlinePx, maxOutlineExpand);
    const float expLeft = clampedOutlineExpand;
    const float expRight = std::max(shadowPx, clampedOutlineExpand);
    const float expUp = clampedOutlineExpand;
    const float expDown = std::max(shadowPx, clampedOutlineExpand);
    // single value for cache comparison (sum of directional values)
    const float expand = expLeft + expRight + expUp + expDown;

    // cache entire strings' vertex/texcoord representations,
    // and only do the minimal work necessary if needing to re-upload them to the texture atlas
    const bool useCache = numCodepoints >= 8 && numCodepoints <= 384;  // arbitrary limits
    VerTexMetCacheEntry &buffer = useCache ? m_vStringCache[stringToCacheIndex(text)] : m_tempStringBuffer;
    if(useCache && buffer.string == text && buffer.cachedExpand == expand) {
        const auto &metrics = buffer.getMetrics();

        for(int i = 0; char32_t ch : UniString::codepoints(text)) {
            const auto *gm = metrics[i++];
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
            size_t emojiStartIndex{0}, regularStartIndex{0};
            float advanceX = 0.0f;
            for(const GLYPH_METRICS *gm : metrics) {
                if(gm->isColor) {
                    buildGlyphGeometry(buffer.getEmojiVerts(), buffer.getEmojiTexcoords(), *gm, advanceX,
                                       emojiStartIndex, expLeft, expRight, expUp, expDown);
                    emojiStartIndex += VERTS_PER_VAO;
                } else {
                    buildGlyphGeometry(buffer.getVerts(), buffer.getTexcoords(), *gm, advanceX, regularStartIndex,
                                       expLeft, expRight, expUp, expDown);
                    regularStartIndex += VERTS_PER_VAO;
                }
                advanceX += gm->advance_x;
            }
            // these assertions should hold because we're reusing the same buffer
            assert(emojiStartIndex == buffer.getEmojiVerts().size());
            assert(regularStartIndex == buffer.getVerts().size());

            m_textureAtlas->reloadAtlasImage();
            m_bAtlasNeedsReload = false;
        }
    } else {
        buffer.string = text;
        buffer.cachedExpand = expand;

        const size_t totalVerts = numCodepoints * VERTS_PER_VAO;
        const size_t maxGlyphs = std::min(numCodepoints, (size_t)((double)totalVerts / (double)VERTS_PER_VAO));

        buffer.getMetrics().resize(maxGlyphs);
        buffer.getVAO()->clear();
        buffer.getEmojiVAO()->clear();

        buildStringGeometry(buffer, maxGlyphs, expLeft, expRight, expUp, expDown);
    }

    m_textureAtlas->getAtlasImage()->bind();

    bool forceFallbackPath = s_textShaderBroken || cv::r_debug_drawstring_fallback.getBool();
    if(!forceFallbackPath && hasEffects) {
        if(!s_textShader) {
            // lazy load shader
            s_textShader = resourceManager->createShaderAuto("text");
        }

        if(s_textShader != nullptr && s_textShader->isReady()) {
            const auto &sc = *effects;
            drawStringShadered(buffer, sc);
        } else {
            s_textShaderBroken = true;
            forceFallbackPath = true;
            engine->showMessageError("Font Error", "Text effects shader failed to load, fancy text won't work!");
        }
    }

    if(forceFallbackPath || !hasEffects) {
        // no effects (or fallback shadow)
        const bool hasShadow = hasEffects && (effects->col_shadow.a > 0);
        drawStringPlain(buffer, hasShadow, effects);
    }

    if(cv::r_debug_drawstring_unbind.getBool()) {
        m_textureAtlas->getAtlasImage()->unbind();
    }

    if(!useCache) {
        buffer.clear();
    }
}

float McFontImpl::getGlyphWidth(char32_t character) const {
    if(!m_parent->isReady()) return 1.0f;

    return static_cast<float>(getGlyphMetrics(character).advance_x);
}

float McFontImpl::getGlyphHeight(char32_t character) const {
    if(!m_parent->isReady()) return 1.0f;

    return static_cast<float>(getGlyphMetrics(character).top);
}

float McFontImpl::getStringWidth(std::string_view text) const {
    if(!m_parent->isReady()) return 1.0f;

    float width = 0.0f;
    for(char32_t cp : UniString::codepoints(text)) {
        width += getGlyphMetrics(cp).advance_x;
    }
    return width;
}

float McFontImpl::getStringHeight(std::string_view text) const {
    if(!m_parent->isReady()) return 1.0f;

    float height = 0.0f;
    for(char32_t cp : UniString::codepoints(text)) {
        height = std::max(height, static_cast<float>(getGlyphMetrics(cp).top));
    }
    return height;
}

std::vector<std::string> McFontImpl::wrap(std::string_view text, f64 max_width) const {
    std::vector<std::string> lines;
    lines.emplace_back();

    std::string word{};
    u32 line = 0;
    f64 line_width = 0.0;
    f64 word_width = 0.0;
    for(char32_t ch : UniString::codepoints(text)) {
        if(ch == U'\n') {
            lines[line].append(word);
            lines.emplace_back();
            line++;
            line_width = 0.0;
            word.clear();
            word_width = 0.0;
            continue;
        }

        f32 char_width = getGlyphWidth(ch);

        if(ch == U' ') {
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
                lines[line].push_back(' ');
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
            const char32_t charray[]{ch, U'\0'};
            word.append(UniString::to_utf8(std::u32string_view{&charray[0]}));
            word_width += char_width;
        }
    }

    // Don't forget! ;)
    lines[line].append(word);

    return lines;
}

std::string McFontImpl::ellipsize(std::string_view text, f64 max_width) const {
    if(!m_parent->isReady()) return std::string{text};

    // if the full string fits, return it as-is
    if(getStringWidth(text) <= max_width) return std::string{text};

    const f64 ellipsis_width = getGlyphMetrics(U'.').advance_x * 3.0;

    // find how many codepoints fit with room for "..."
    std::string ret;
    f64 width = 0.0;
    for(char32_t ch : UniString::codepoints(text)) {
        const f64 ch_width = getGlyphMetrics(ch).advance_x;
        if(width + ch_width + ellipsis_width > max_width) break;
        const char32_t charray[]{ch, U'\0'};
        ret.append(UniString::to_utf8(std::u32string_view{&charray[0]}));
        width += ch_width;
    }

    ret.append("...");
    return ret;
}

// Internal helper methods

const McFontImpl::GLYPH_METRICS &McFontImpl::getGlyphMetrics(char32_t ch) const {
    FT_Face existingFace = nullptr;
    if(const auto &it = m_mGlyphMetrics.find(ch); it != m_mGlyphMetrics.end()) {
        if(it->second->inAtlas) return *it->second;
        existingFace = it->second->face;
    }

    // either no metrics, or metrics exist but glyph was evicted from atlas
    if(m_bTryFindFallbacks) {
        // we want to pretend to be const but silently load glyphs we don't have
        // NOLINTNEXTLINE
        auto *dynamic_this = const_cast<McFontImpl *>(this);
        auto &metrics = dynamic_this->m_mGlyphMetrics;
        if(dynamic_this->loadGlyphDynamic(ch, existingFace)) {
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

bool McFontImpl::loadGlyphDynamic(char32_t ch, FT_Face existingFace) {
    assert(m_bFreeTypeInitialized);

    std::string debugstr;
    if(cv::r_debug_font_unicode.getBool()) {
        const char32_t charray[]{ch, U'\0'};
        debugstr = fmt::format("{:s} (U+{:04X})", UniString::to_utf8(std::u32string_view{&charray[0]}), (u32)ch);
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
              "Font Info (for font resource {}): Using fallback font for character {} (from font {})",
              m_parent->getName(), debugstr, face->family_name);
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

        const int maxSlotContent = getDynSlotSize() - TextureAtlas::ATLAS_PADDING;
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
int McFontImpl::allocateDynamicSlot(char32_t ch) {
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

void McFontImpl::markSlotUsed(char32_t ch) {
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
    if(FT_New_Face(s_sharedFtLibrary, m_sActualFilePath.c_str(), 0, &m_ftFace)) {
        engine->showMessageError("Font Error", "Couldn't load font file!");
        return false;
    }

    if(FT_Select_Charmap(m_ftFace, FT_ENCODING_UNICODE)) {
        engine->showMessageError("Font Error", "FT_Select_Charmap() failed!");
        FT_Done_Face(m_ftFace);
        return false;
    }

    m_bFreeTypeInitialized = true;
    return true;
}

bool McFontImpl::loadGlyphMetrics(char32_t ch) {
    if(!m_bFreeTypeInitialized) return false;

    FT_Face face = getFontFaceForGlyph(ch);
    if(!face) return false;

    // for initial load, we need metrics but not the bitmap yet (will render later after packing)
    FT_BitmapGlyph glyph = loadBitmapGlyph(ch, face, true /*storeMetrics*/);
    if(!glyph) return false;

    FT_Done_Glyph(reinterpret_cast<FT_Glyph>(glyph));
    return true;
}

void McFontImpl::renderBitmapToAtlas(char32_t ch, int x, int y, const FT_Bitmap &bitmap, bool isDynamicSlot) {
    if(bitmap.width == 0 || bitmap.rows == 0) return;

    const int atlasWidth = m_textureAtlas->getWidth();
    const int atlasHeight = m_textureAtlas->getHeight();
    const sSz srcW = static_cast<sSz>(bitmap.width);
    const sSz srcH = static_cast<sSz>(bitmap.rows);

    // determine target size for color bitmaps (may need downscaling)
    sSz outW = srcW, outH = srcH;
    const auto &metricsPtr = m_mGlyphMetrics[ch];
    const bool needsDownscale = metricsPtr && metricsPtr->isColor && static_cast<sSz>(metricsPtr->sizePixelsX) < srcW &&
                                static_cast<sSz>(metricsPtr->sizePixelsY) < srcH && metricsPtr->sizePixelsX > 0 &&
                                metricsPtr->sizePixelsY > 0;
    if(needsDownscale) {
        outW = static_cast<sSz>(metricsPtr->sizePixelsX);
        outH = static_cast<sSz>(metricsPtr->sizePixelsY);
    }

    // convert bitmap to RGBA, maybe downscaling if we need to (the font has only fixed sizes)
    auto expandedData = std::make_unique_for_overwrite<u8[]>(static_cast<size_t>(outW) * outH * 4);

    if(bitmap.pixel_mode == FT_PIXEL_MODE_BGRA && needsDownscale) {
        //  BGRA->RGBA + area-averaging downscale
        const float scaleX = static_cast<float>(srcW) / static_cast<float>(outW);
        const float scaleY = static_cast<float>(srcH) / static_cast<float>(outH);
        for(sSz dy = 0; dy < outH; dy++) {
            const float sy0 = static_cast<float>(dy) * scaleY;
            const float sy1 = std::min(static_cast<float>(dy + 1) * scaleY, static_cast<float>(srcH));
            for(sSz dx = 0; dx < outW; dx++) {
                const float sx0 = static_cast<float>(dx) * scaleX;
                const float sx1 = std::min(static_cast<float>(dx + 1) * scaleX, static_cast<float>(srcW));

                float rAcc = 0, gAcc = 0, bAcc = 0, aAcc = 0, area = 0;
                for(sSz sy = static_cast<sSz>(sy0); sy < static_cast<sSz>(sy1 + 0.999f) && sy < srcH; sy++) {
                    float wy = std::min(static_cast<float>(sy + 1), sy1) - std::max(static_cast<float>(sy), sy0);
                    for(sSz sx = static_cast<sSz>(sx0); sx < static_cast<sSz>(sx1 + 0.999f) && sx < srcW; sx++) {
                        float wx = std::min(static_cast<float>(sx + 1), sx1) - std::max(static_cast<float>(sx), sx0);
                        float w = wx * wy;
                        const u8 *p = bitmap.buffer + sy * bitmap.pitch + sx * 4;
                        rAcc += static_cast<float>(p[2]) * w;  // B->R
                        gAcc += static_cast<float>(p[1]) * w;
                        bAcc += static_cast<float>(p[0]) * w;  // R->B
                        aAcc += static_cast<float>(p[3]) * w;
                        area += w;
                    }
                }
                const size_t di = (static_cast<size_t>(dy) * outW + dx) * 4;
                const float inv = area > 0 ? 1.0f / area : 0;
                expandedData[di + 0] = static_cast<u8>(std::min(rAcc * inv, 255.0f));
                expandedData[di + 1] = static_cast<u8>(std::min(gAcc * inv, 255.0f));
                expandedData[di + 2] = static_cast<u8>(std::min(bAcc * inv, 255.0f));
                expandedData[di + 3] = static_cast<u8>(std::min(aAcc * inv, 255.0f));
            }
        }
    } else if(bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
        // BGRA->RGBA, no downscale
        for(sSz j = 0; j < srcH; j++) {
            const u8 *srcRow = bitmap.buffer + j * bitmap.pitch;
            u8 *dstRow = expandedData.get() + j * srcW * 4;
            for(sSz k = 0; k < srcW; k++) {
                dstRow[k * 4 + 0] = srcRow[k * 4 + 2];  // R <- B
                dstRow[k * 4 + 1] = srcRow[k * 4 + 1];  // G
                dstRow[k * 4 + 2] = srcRow[k * 4 + 0];  // B <- R
                dstRow[k * 4 + 3] = srcRow[k * 4 + 3];  // A
            }
        }
    } else {
        // grayscale or mono bitmap: white + alpha
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
    }

    int renderWidth = std::min(static_cast<int>(outW), atlasWidth - x);
    int renderHeight = std::min(static_cast<int>(outH), atlasHeight - y);

    if(isDynamicSlot) {
        const int maxSlotContent = getDynSlotSize() - TextureAtlas::ATLAS_PADDING;
        renderWidth = std::min(renderWidth, maxSlotContent);
        renderHeight = std::min(renderHeight, maxSlotContent);
    }

    // clear the full padding area around the glyph to prevent stale data
    // from appearing when glyph quads are expanded for shadow/outline effects
    if(isDynamicSlot) {
        const int pad = TextureAtlas::ATLAS_PADDING;
        const int clearX = std::max(0, x - pad);
        const int clearY = std::max(0, y - pad);
        const int clearW = std::min(renderWidth + 2 * pad, atlasWidth - clearX);
        const int clearH = std::min(renderHeight + 2 * pad, atlasHeight - clearY);
        m_textureAtlas->clearRegion(clearX, clearY, clearW, clearH);
    }

    // if clipping is needed, create clipped data
    if(renderWidth < outW || renderHeight < outH) {
        auto clippedData = std::make_unique_for_overwrite<u8[]>(static_cast<size_t>(renderWidth) * renderHeight * 4);
        const sSz outStride = outW * 4;
        const sSz clipStride = static_cast<sSz>(renderWidth) * 4;
        for(i32 row = 0; row < renderHeight; row++) {
            std::memcpy(&clippedData[row * clipStride], &expandedData[row * outStride], clipStride);
        }
        m_textureAtlas->putAt(x, y, renderWidth, renderHeight, clippedData.get());
    } else {
        m_textureAtlas->putAt(x, y, static_cast<int>(outW), static_cast<int>(outH), expandedData.get());
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
    std::vector<char32_t> rectsToChars;
    packRects.reserve(m_vInitialGlyphs.size());
    rectsToChars.reserve(m_vInitialGlyphs.size());

    size_t rectIndex = 0;
    for(char32_t ch : m_vInitialGlyphs) {
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

    const size_t totalAtlasSize = staticAtlasSize * ATLAS_SIZE_MULTIPLIER;
    const size_t finalAtlasSize = std::min(totalAtlasSize, MAX_ATLAS_SIZE);

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
        const char32_t ch = rectsToChars[rect.id];
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
FT_Face McFontImpl::getFontFaceForGlyph(char32_t ch) {
    // quick blacklist check
    if(m_bTryFindFallbacks && m_parent->isAsyncReady()) {  // skip blacklisting during initial load
        if(s_sharedFallbackFaceBlacklist.contains(ch)) {
            return nullptr;
        }
    }

    // check primary font first
    FT_UInt glyphIndex = FT_Get_Char_Index(m_ftFace, ch);
    if(glyphIndex != 0) return m_ftFace;
    if(!m_bTryFindFallbacks) return nullptr;

    // for likely emoji codepoints, prefer the color emoji face over other fallbacks
    if(s_sharedEmojiFace) {
        const bool isLikelyEmoji = ch >= 0x1F000 || (ch >= 0x2600 && ch <= 0x27BF) || (ch >= 0x2300 && ch <= 0x23FF) ||
                                   (ch >= 0xFE00 && ch <= 0xFE0F) || (ch >= 0x200D && ch <= 0x200D) ||
                                   (ch >= 0x20E3 && ch <= 0x20E3);
        if(isLikelyEmoji) {
            glyphIndex = FT_Get_Char_Index(s_sharedEmojiFace, ch);
            if(glyphIndex != 0) return s_sharedEmojiFace;
        }
    }

    // search through shared fallback fonts
    FT_Face foundFace = nullptr;
    for(auto &fallback : s_sharedFallbackFonts) {
        glyphIndex = FT_Get_Char_Index(fallback.face, ch);
        if(glyphIndex != 0) {
            foundFace = fallback.face;
            break;
        }
    }

    if(foundFace) return foundFace;

    // character not found in any font, add to blacklist
    // NOTE: skip blacklisting during initial load to allow lazier synchronization
    // with other fonts that may be loading simultaneously
    if(m_parent->isAsyncReady()) {
        s_sharedFallbackFaceBlacklist.insert(ch);
    }

    return nullptr;
}

FT_BitmapGlyph McFontImpl::loadBitmapGlyph(char32_t ch, FT_Face face, bool storeMetrics) {
    const bool isColorFace = FT_HAS_COLOR(face);
    FT_Int32 loadFlags = isColorFace ? FT_LOAD_COLOR : (m_bAntialiasing ? FT_LOAD_TARGET_NORMAL : FT_LOAD_TARGET_MONO);

    if(FT_Load_Glyph(face, FT_Get_Char_Index(face, ch), loadFlags)) {
        debugLog("Font Error: Failed to load glyph for character U+{:04X}", (unsigned int)ch);
        return nullptr;
    }

    FT_Glyph glyph{};
    if(FT_Get_Glyph(face->glyph, &glyph)) {
        debugLog("Font Error: Failed to get glyph for character U+{:04X}", (unsigned int)ch);
        return nullptr;
    }

    if(!isColorFace) {
        FT_Glyph_To_Bitmap(&glyph, m_bAntialiasing ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO, nullptr, 1);
    } else if(glyph->format != FT_GLYPH_FORMAT_BITMAP) {
        FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, nullptr, 1);
    }

    auto bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>(glyph);

    if(storeMetrics) {
        auto &metricsPtr = m_mGlyphMetrics[ch];
        assert(!metricsPtr);
        metricsPtr = std::make_unique<GLYPH_METRICS>();
        auto &metrics = *metricsPtr;

        metrics.isColor = isColorFace;

        if(isColorFace && FT_HAS_FIXED_SIZES(face) && face->available_sizes) {
            // scale metrics from native bitmap size to target pixel size
            const int nativeHeight = face->available_sizes[0].height;
            const int targetPx = (m_iFontSize * m_iFontDPI + 36) / 72;
            const float scale = static_cast<float>(targetPx) / static_cast<float>(nativeHeight);

            metrics.left = static_cast<int>(static_cast<float>(bitmapGlyph->left) * scale);
            metrics.top = static_cast<int>(static_cast<float>(bitmapGlyph->top) * scale);
            metrics.width = static_cast<int>(static_cast<float>(bitmapGlyph->bitmap.width) * scale);
            metrics.rows = static_cast<int>(static_cast<float>(bitmapGlyph->bitmap.rows) * scale);
            metrics.advance_x = static_cast<float>(face->glyph->advance.x >> 6) * scale;
            // sizePixels stores the scaled (display) size for atlas UV calculations
            metrics.sizePixelsX =
                std::max(1u, static_cast<unsigned int>(static_cast<float>(bitmapGlyph->bitmap.width) * scale));
            metrics.sizePixelsY =
                std::max(1u, static_cast<unsigned int>(static_cast<float>(bitmapGlyph->bitmap.rows) * scale));
        } else {
            metrics.left = bitmapGlyph->left;
            metrics.top = bitmapGlyph->top;
            metrics.width = static_cast<int>(bitmapGlyph->bitmap.width);
            metrics.rows = static_cast<int>(bitmapGlyph->bitmap.rows);
            metrics.advance_x = static_cast<float>(face->glyph->advance.x >> 6);
            metrics.sizePixelsX = bitmapGlyph->bitmap.width;
            metrics.sizePixelsY = bitmapGlyph->bitmap.rows;
        }

        // to be updated when rendered to texture atlas
        metrics.inAtlas = false;
        metrics.face = face;
        metrics.uvPixelsX = 0;
        metrics.uvPixelsY = 0;
    }

    return bitmapGlyph;
}

void McFontImpl::buildGlyphGeometry(std::vector<vec3> &vertsOut, std::vector<vec2> &texcoordsOut,
                                    const GLYPH_METRICS &gm, float advanceX, size_t startIndex, float expandLeft,
                                    float expandRight, float expandUp, float expandDown) {
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

    // texcoord-per-engine-pixel ratio (handles color/emoji fonts where sizePixels != width)
    const float texPerPxX = (sx > 0.f) ? (texSizeX / sx) : 0.f;
    const float texPerPxY = (gm.rows > 0) ? (texSizeY / static_cast<float>(gm.rows)) : 0.f;

    // corners of the "quad" (note: sy is negative, so bottomLeft is visual top-left)
    // expand edges directionally into atlas padding for shadow/outline visibility
    vec3 bottomLeft{x - expandLeft, y + sy - expandUp, 0};
    vec3 topLeft{x - expandLeft, y + expandDown, 0};
    vec3 topRight{x + sx + expandRight, y + expandDown, 0};
    vec3 bottomRight{x + sx + expandRight, y + sy - expandUp, 0};

    // texcoords (texY = visual top, texY+texSizeY = visual bottom)
    vec2 texBottomLeft{texX - expandLeft * texPerPxX, texY - expandUp * texPerPxY};
    vec2 texTopLeft{texX - expandLeft * texPerPxX, texY + texSizeY + expandDown * texPerPxY};
    vec2 texTopRight{texX + texSizeX + expandRight * texPerPxX, texY + texSizeY + expandDown * texPerPxY};
    vec2 texBottomRight{texX + texSizeX + expandRight * texPerPxX, texY - expandUp * texPerPxY};

    if constexpr(VERTS_PER_VAO > 4) {
        // triangles (quads are slower for GL ES because they need to be converted to triangles at submit time)
        // first triangle (bottom-left, top-left, top-right)
        vertsOut[startIndex] = bottomLeft;
        vertsOut[startIndex + 1] = topLeft;
        vertsOut[startIndex + 2] = topRight;

        texcoordsOut[startIndex] = texBottomLeft;
        texcoordsOut[startIndex + 1] = texTopLeft;
        texcoordsOut[startIndex + 2] = texTopRight;

        // second triangle (bottom-left, top-right, bottom-right)
        vertsOut[startIndex + 3] = bottomLeft;
        vertsOut[startIndex + 4] = topRight;
        vertsOut[startIndex + 5] = bottomRight;

        texcoordsOut[startIndex + 3] = texBottomLeft;
        texcoordsOut[startIndex + 4] = texTopRight;
        texcoordsOut[startIndex + 5] = texBottomRight;
    } else {
        // quads
        vertsOut[startIndex] = bottomLeft;       // bottom-left
        vertsOut[startIndex + 1] = topLeft;      // top-left
        vertsOut[startIndex + 2] = topRight;     // top-right
        vertsOut[startIndex + 3] = bottomRight;  // bottom-right

        texcoordsOut[startIndex] = texBottomLeft;
        texcoordsOut[startIndex + 1] = texTopLeft;
        texcoordsOut[startIndex + 2] = texTopRight;
        texcoordsOut[startIndex + 3] = texBottomRight;
    }

    return;
}

void McFontImpl::buildStringGeometry(VerTexMetCacheEntry &buffer, size_t maxGlyphs, float expandLeft, float expandRight,
                                     float expandUp, float expandDown) {
    auto &verts = buffer.getVerts();
    auto &TCs = buffer.getTexcoords();
    auto &emojiVerts = buffer.getEmojiVerts();
    auto &emojiTCs = buffer.getEmojiTexcoords();
    auto &metrics = buffer.getMetrics();

    size_t emojiStartIndex{0}, regularStartIndex{0};
    float advanceX = 0.0f;
    for(int i = -1; char32_t ch : UniString::codepoints(buffer.string)) {
        ++i;
        if(i >= maxGlyphs) break;

        const GLYPH_METRICS &gm = getGlyphMetrics(ch);
        if(gm.isColor) {
            emojiVerts.resize(emojiVerts.size() + VERTS_PER_VAO);
            emojiTCs.resize(emojiTCs.size() + VERTS_PER_VAO);
            buildGlyphGeometry(emojiVerts, emojiTCs, gm, advanceX, emojiStartIndex, expandLeft, expandRight, expandUp,
                               expandDown);
            emojiStartIndex += VERTS_PER_VAO;
        } else {
            verts.resize(verts.size() + VERTS_PER_VAO);
            TCs.resize(TCs.size() + VERTS_PER_VAO);
            buildGlyphGeometry(verts, TCs, gm, advanceX, regularStartIndex, expandLeft, expandRight, expandUp,
                               expandDown);
            regularStartIndex += VERTS_PER_VAO;
        }
        advanceX += gm.advance_x;

        // mark dynamic slot as recently used (if this character is in a dynamic slot)
        markSlotUsed(ch);

        // add glyph metrics to out parameter
        metrics[i] = &gm;
    }

    // reload atlas if new glyphs were added to dynamic slots
    if(m_bAtlasNeedsReload) {
        m_textureAtlas->reloadAtlasImage();
        m_bAtlasNeedsReload = false;
    }
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

        if(FT_HAS_FIXED_SIZES(face) && !FT_IS_SCALABLE(face)) {
            // bitmap-only font (e.g. CBDT emoji): select nearest available strike
            FT_Select_Size(face, 0);
        } else {
            FT_Set_Char_Size(face, (FT_F26Dot6)(m_iFontSize * 64L), (FT_F26Dot6)(m_iFontSize * 64L), m_iFontDPI,
                             m_iFontDPI);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////
// shared/static FreeType management stuff below
////////////////////////////////////////////////////////////////////////////////////

namespace {  // static namespace

bool loadFallbackFont(const std::string &fontPath, bool isSystemFont) {
    FT_Face face{};
    if(FT_New_Face(s_sharedFtLibrary, fontPath.c_str(), 0, &face)) {
        logIfCV(r_debug_font_unicode, "Font Warning: Failed to load fallback font: {:s}", fontPath);
        return false;
    }

    if(FT_Select_Charmap(face, FT_ENCODING_UNICODE)) {
        logIfCV(r_debug_font_unicode, "Font Warning: Failed to select unicode charmap for fallback font: {:s}",
                fontPath);
        FT_Done_Face(face);
        return false;
    }

    // don't set font size here, will be set when the face is used by individual font instances
    s_sharedFallbackFonts.push_back(FallbackFont{fontPath, face, isSystemFont});

    // track the first color (emoji) face for prioritized lookup
    if(!s_sharedEmojiFace && FT_HAS_COLOR(face)) {
        s_sharedEmojiFace = face;
    }

    return true;
}

void discoverSystemFallbacks() {
#ifdef MCENGINE_PLATFORM_WINDOWS
    std::string windir;
    windir.resize(MAX_PATH + 1);
    const size_t ret = GetWindowsDirectoryA(windir.data(), MAX_PATH);
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
                                            "/usr/share/fonts/noto/NotoSansCJK-Regular.ttc",
                                            "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                                            "/usr/share/fonts/noto-emoji/NotoColorEmoji.ttf",
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
    std::vector<std::string> bundledFallbacks;
    {
        auto allFonts = Environment::getFilesInFolder(MCENGINE_FONTS_PATH "/");
        // sort to load woff2 before ttf/otf
        std::ranges::sort(allFonts, [](const std::string &font1, const std::string &font2) {
            std::string ext1 = Environment::getFileExtensionFromFilePath(font1);
            std::string ext2 = Environment::getFileExtensionFromFilePath(font2);
            SString::lower_inplace(ext1);
            SString::lower_inplace(ext2);
            if(ext1 == ext2) return false;
            if(ext1.empty() != ext2.empty()) return ext1.empty() > ext2.empty();
            return ext1.starts_with("woff"sv) > ext2.starts_with("woff"sv);
        });
        // deduplicate
        Hash::unstable_ncase_set<std::string> fontsNoExt;
        for(const auto &path : allFonts) {
            const std::string ext = Environment::getFileExtensionFromFilePath(path);
            if(ext.empty() || ext == path) continue;
            std::string pathNoExt = path.substr(0, path.length() - (ext.length() + 1));
            const auto [_, inserted] = fontsNoExt.insert(pathNoExt);
            if(inserted) {
                bundledFallbacks.push_back(MCENGINE_FONTS_PATH "/"s + path);
            }
        }
    }
    for(const auto &fontName : bundledFallbacks) {
        if(loadFallbackFont(fontName, false)) {
            logIfCV(r_debug_font_unicode, "Font Info: Loaded bundled fallback font: {:s}", fontName);
        }
    }

    // then find likely system fonts
    if(!Env::cfg(OS::WASM) && cv::font_load_system.getBool()) {
        discoverSystemFallbacks();
    }

    s_sharedFallbacksInitialized = true;

    return s_sharedFtLibraryInitialized && s_sharedFallbacksInitialized;
}

void McFont::cleanupSharedResources() {
    // clean up shared fallback fonts
    for(auto &fallbackFont : s_sharedFallbackFonts) {
        if(fallbackFont.face) FT_Done_Face(fallbackFont.face);
    }
    s_sharedFallbackFonts.clear();
    s_sharedEmojiFace = nullptr;  // owned by s_sharedFallbackFonts, already freed above
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
    g->setColor((Color)-1);
    g->pushTransform();
    {
        const auto *img = pImpl->m_textureAtlas->getAtlasImage();
        const auto engineSize = engine->getScreenSize();
        const f32 fitScale = (f32)img->getWidth() > engineSize.x    ? engineSize.x / (f32)img->getWidth()
                             : (f32)img->getHeight() > engineSize.y ? engineSize.y / (f32)img->getHeight()
                                                                    : 1.f;
        if(fitScale != 1.f) {
            g->scale(fitScale, fitScale);
        }
        vec2 centerTrans{((f32)img->getWidth() / 2.f), (f32)img->getHeight() / 2.f};
        centerTrans.x += ((engineSize.x / 2.f) - centerTrans.x);
        g->translate(centerTrans);
        g->drawImage(img);
        // draw bounds
        g->setColor(rgb(255, 50, 50));
        g->drawRect(McRect{{}, vec2{img->getSize()}, true});
    }
    g->popTransform();
}

void McFont::drawDebug() const {
    if(m_bDebugDrawAtlas) {
        this->drawTextureAtlas();
    }
}

namespace cv {
// yes yes... its not great
// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init)
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