//========================================================================
//
// TextAnalysisOutputDev.cc
//
// PDF text extraction analysis tool implementation
//
//========================================================================

// Modified by [Wonhee-jung], 2026-02-11
// Added text layout analysis functionality.
// This file is part of a modified version of Poppler (GPL).

#include "config.h"
#include "TextAnalysisOutputDev.h"
#include "GfxFont.h"
#include "GfxState.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

//------------------------------------------------------------------------
// Helper functions
//------------------------------------------------------------------------

static std::string fontTypeToString(GfxFontType type)
{
    switch (type) {
    case fontType1:
        return "Type1";
    case fontType1C:
        return "Type1C";
    case fontType1COT:
        return "Type1COT";
    case fontType3:
        return "Type3";
    case fontTrueType:
        return "TrueType";
    case fontTrueTypeOT:
        return "TrueTypeOT";
    case fontCIDType0:
        return "CIDType0";
    case fontCIDType0C:
        return "CIDType0C";
    case fontCIDType0COT:
        return "CIDType0COT";
    case fontCIDType2:
        return "CIDType2";
    case fontCIDType2OT:
        return "CIDType2OT";
    default:
        return "Unknown";
    }
}

static std::string escapeJsonString(const std::string &s)
{
    std::ostringstream oss;
    for (unsigned char c : s) {
        switch (c) {
        case '"':
            oss << "\\\"";
            break;
        case '\\':
            oss << "\\\\";
            break;
        case '\n':
            oss << "\\n";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\t':
            oss << "\\t";
            break;
        default:
            if (c < 0x20) {
                oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
            } else {
                oss << c;
            }
        }
    }
    return oss.str();
}

//------------------------------------------------------------------------
// TextAnalysisOutputDev
//------------------------------------------------------------------------

// static
// 유효한 유니코드인지 판별하는 메서드
bool TextAnalysisOutputDev::isInvalidUnicode(Unicode cp)
{
    if (cp == 0xFFFD) // 대체 문자
        return true;
    if (cp == 0x0000) // Null
        return true;
    if (cp >= 0x0001 && cp <= 0x001F) // 아스키 제어 문자
        return true;
    if (cp >= 0xE000 && cp <= 0xF8FF) // PUA
        return true;
    // Isolated surrogates (U+D800–U+DFFF) 
    if (cp >= 0xD800 && cp <= 0xDFFF) // 분리된 서로게이트 영역
        return true;
    return false;
}

TextAnalysisOutputDev::TextAnalysisOutputDev()
    : hasInvisibleText(false)
    , invisibleGlyphCount(0)
    , invisibleGlyphWithToUnicodeCount(0)
    , allGlyphsInsideImage(true)
{
    currentPageStats.pageNumber = 0;
    currentPageStats.drawCharCount = 0;
    currentPageStats.unicodeMappedCount = 0;
    currentPageStats.pageType = "";
}

TextAnalysisOutputDev::~TextAnalysisOutputDev() = default;

void TextAnalysisOutputDev::startPage(int pageNum, GfxState * /*state*/, XRef * /*xref*/)
{
    currentPageStats.pageNumber = pageNum;
    currentPageStats.drawCharCount = 0;
    currentPageStats.unicodeMappedCount = 0;
    currentPageStats.pageType = "";
    currentPageStats.undecodedChars.clear();
    seenUndecodedKeys.clear();

    hasInvisibleText = false;
    allGlyphsInsideImage = true;

    invisibleGlyphCount = 0;
    invisibleGlyphWithToUnicodeCount = 0;

    imageBBoxes.clear();
    glyphBBoxes.clear();
}

// 이미지 박스 정의 
void TextAnalysisOutputDev::registerImageBBox(GfxState *state, int width, int height)
{
    if (!state)
        return;

    double w = static_cast<double>(width);
    double h = static_cast<double>(height);

    // Transform the 4 corners of the image from image space to device space
    double corners[4][2] = {
        { 0, 0 },
        { w, 0 },
        { 0, h },
        { w, h }
    };

    double txMin = 0, tyMin = 0, txMax = 0, tyMax = 0;
    bool first = true;

    for (auto &corner : corners) {
        double tx, ty;
        state->transform(corner[0], corner[1], &tx, &ty);
        if (first) {
            txMin = txMax = tx;
            tyMin = tyMax = ty;
            first = false;
        } else {
            txMin = std::min(txMin, tx);
            txMax = std::max(txMax, tx);
            tyMin = std::min(tyMin, ty);
            tyMax = std::max(tyMax, ty);
        }
    }

    imageBBoxes.push_back({ txMin, tyMin, txMax, tyMax });
}

void TextAnalysisOutputDev::drawImageMask(GfxState *state, Object * /*ref*/, Stream * /*str*/, int width, int height, bool /*invert*/, bool /*interpolate*/, bool /*inlineImg*/)
{
    registerImageBBox(state, width, height);
}

void TextAnalysisOutputDev::drawImage(GfxState *state, Object * /*ref*/, Stream * /*str*/, int width, int height, GfxImageColorMap * /*colorMap*/, bool /*interpolate*/, const int * /*maskColors*/, bool /*inlineImg*/)
{
    registerImageBBox(state, width, height);
}

void TextAnalysisOutputDev::drawSoftMaskedImage(GfxState *state, Object * /*ref*/, Stream * /*str*/, int width, int height, GfxImageColorMap * /*colorMap*/, bool /*interpolate*/, Stream * /*maskStr*/, int /*maskWidth*/, int /*maskHeight*/, GfxImageColorMap * /*maskColorMap*/, bool /*maskInterpolate*/)
{
    registerImageBBox(state, width, height);
}

void TextAnalysisOutputDev::endPage()
{
    // --- Step 1: Compute ALL_GLYPHS_INSIDE_IMAGE ---
    constexpr double epsilon = 0.01;

    for (const auto &glyph : glyphBBoxes) {
        bool insideAnyImage = false;
        for (const auto &image : imageBBoxes) {
            if (glyph.xMin >= image.xMin - epsilon &&
                glyph.yMin >= image.yMin - epsilon &&
                glyph.xMax <= image.xMax + epsilon &&
                glyph.yMax <= image.yMax + epsilon)
            {
                insideAnyImage = true;
                break;
            }
        }
        if (!insideAnyImage) {
            allGlyphsInsideImage = false;
            break;
        }
    }

    // --- Step 2: OCR strict Boolean classification (highest priority) ---
    bool allInvisibleHaveToUnicode =
        (invisibleGlyphCount > 0) &&
        (invisibleGlyphCount == invisibleGlyphWithToUnicodeCount);

    if (hasInvisibleText &&
        allInvisibleHaveToUnicode &&
        allGlyphsInsideImage)
    {
        currentPageStats.pageType = "OCR_INVISIBLE_LAYER";
        pageResults.push_back(currentPageStats);
        return;
    }

    // --- Step 3: UNICODE_* classification ---
    if (currentPageStats.drawCharCount == 0) {
        currentPageStats.pageType = "IMAGE";
    } else if (currentPageStats.unicodeMappedCount == currentPageStats.drawCharCount) {
        if (currentPageStats.drawCharCount < MIN_CHAR_THRESHOLD) {
            currentPageStats.pageType = "UNICODE_ALL_MATCH_BUT_LESS_CHARS";
        } else {
            currentPageStats.pageType = "UNICODE_ALL_MATCH";
        }
    } else if (currentPageStats.unicodeMappedCount == 0) {
        currentPageStats.pageType = "UNICODE_NONE_MATCH";
    } else {
        currentPageStats.pageType = "UNICODE_PARTIAL_MATCH";
    }

    pageResults.push_back(currentPageStats);
}

void TextAnalysisOutputDev::drawChar(GfxState *state, double x, double y, double dx, double dy, double /*originX*/, double /*originY*/, CharCode c, int /*nBytes*/, const Unicode *u, int uLen)
{
    currentPageStats.drawCharCount++;

    // --- Invisible glyph detection (render mode 3 = invisible) ---
    int renderMode = state ? state->getRender() : -1;

    if (renderMode == 3) {
        hasInvisibleText = true;
        invisibleGlyphCount++;

        // Strict ToUnicode check for invisible glyphs
        if (state) {
            const auto &font = state->getFont();
            bool hasToUnicode = font && font->hasToUnicodeCMap();
            if (hasToUnicode) {
                invisibleGlyphWithToUnicodeCount++;
            }
        }
    }

    // --- Glyph bbox in device space ---
    if (state) {
        double tx1, ty1, tx2, ty2;
        state->transform(x,      y,      &tx1, &ty1);
        state->transform(x + dx, y + dy, &tx2, &ty2);

        BBox glyph;
        glyph.xMin = std::min(tx1, tx2);
        glyph.xMax = std::max(tx1, tx2);
        glyph.yMin = std::min(ty1, ty2);
        glyph.yMax = std::max(ty1, ty2);
        glyphBBoxes.push_back(glyph);
    }

    // --- Unicode validity check ---
    bool validUnicode = true;

    if (!u || uLen == 0) {
        validUnicode = false;
    } else {
        for (int i = 0; i < uLen && validUnicode; ++i) {
            if (isInvalidUnicode(u[i]))
                validUnicode = false;
        }
    }

    if (validUnicode) {
        currentPageStats.unicodeMappedCount++;
    } else {
        // Collect undecoded character info (deduplicated by charCode + fontName)
        std::string fontName = "(no font)";
        std::string fontType = "Unknown";
        std::string encodingName;
        bool hasToUnicodeCMap = false;

        if (state) {
            const std::shared_ptr<GfxFont> &font = state->getFont();
            if (font) {
                fontName = font->getName() ? *font->getName() : "(unnamed)";
                fontType = fontTypeToString(font->getType());
                encodingName = font->getEncodingName();
                hasToUnicodeCMap = font->hasToUnicodeCMap();
            }
        }

        auto key = std::make_pair(static_cast<int>(c), fontName);
        if (seenUndecodedKeys.find(key) == seenUndecodedKeys.end()) {
            seenUndecodedKeys.insert(key);

            UndecodedCharInfo info;
            info.charCode = static_cast<int>(c);
            info.fontName = std::move(fontName);
            info.fontType = std::move(fontType);
            info.encodingName = std::move(encodingName);
            info.hasToUnicodeCMap = hasToUnicodeCMap;
            currentPageStats.undecodedChars.push_back(std::move(info));
        }
    }
}

std::string TextAnalysisOutputDev::toJSON() const
{
    std::ostringstream oss;

    oss << "{\n";
    oss << "  \"status\": \"OK\",\n";
    oss << "  \"pages\": [\n";

    for (size_t i = 0; i < pageResults.size(); ++i) {
        const auto &stats = pageResults[i];

        oss << "    {\n";
        oss << "      \"page\": " << stats.pageNumber << ",\n";
        oss << "      \"type\": \"" << stats.pageType << "\"";

        if (!stats.undecodedChars.empty()) {
            oss << ",\n";
            oss << "      \"undecoded_chars\": [\n";

            for (size_t j = 0; j < stats.undecodedChars.size(); ++j) {
                const auto &ch = stats.undecodedChars[j];

                oss << "        {\n";
                oss << "          \"char_code\": " << ch.charCode << ",\n";
                oss << "          \"char_code_hex\": \"0x" << std::hex << std::uppercase
                    << std::setw(4) << std::setfill('0') << ch.charCode
                    << std::dec << std::nouppercase << "\",\n";
                oss << "          \"font_name\": \"" << escapeJsonString(ch.fontName) << "\",\n";
                oss << "          \"font_type\": \"" << ch.fontType << "\",\n";
                oss << "          \"encoding_name\": \"" << escapeJsonString(ch.encodingName) << "\",\n";
                oss << "          \"has_to_unicode_cmap\": " << (ch.hasToUnicodeCMap ? "true" : "false") << "\n";
                oss << "        }";

                if (j < stats.undecodedChars.size() - 1) {
                    oss << ",";
                }
                oss << "\n";
            }

            oss << "      ]";
        }

        oss << "\n    }";

        if (i < pageResults.size() - 1) {
            oss << ",";
        }
        oss << "\n";
    }

    oss << "  ]\n";
    oss << "}\n";

    return oss.str();
}

std::string TextAnalysisOutputDev::toErrorJSON(const std::string &errorReason)
{
    std::ostringstream oss;

    oss << "{\n";
    oss << "  \"status\": \"ERROR\",\n";
    oss << "  \"error\": \"" << errorReason << "\"\n";
    oss << "}\n";

    return oss.str();
}
