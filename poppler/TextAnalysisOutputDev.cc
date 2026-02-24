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

TextAnalysisOutputDev::TextAnalysisOutputDev()
{
    currentPageStats.pageNumber = 0;
    currentPageStats.drawCharCount = 0;
    currentPageStats.unicodeMappedCount = 0;
    currentPageStats.pageType = "";
}

TextAnalysisOutputDev::~TextAnalysisOutputDev() = default;

void TextAnalysisOutputDev::startPage(int pageNum, GfxState * /*state*/, XRef * /*xref*/)
{
    // Initialize statistics for new page
    currentPageStats.pageNumber = pageNum;
    currentPageStats.drawCharCount = 0;
    currentPageStats.unicodeMappedCount = 0;
    currentPageStats.pageType = "";
    currentPageStats.undecodedChars.clear();
    seenUndecodedKeys.clear();
}

void TextAnalysisOutputDev::endPage()
{
    // Classify page based on CID → Unicode mapping completeness
    if (currentPageStats.drawCharCount == 0) {
        // No text objects found - image-based PDF
        currentPageStats.pageType = "IMAGE";
    } else if (currentPageStats.unicodeMappedCount == currentPageStats.drawCharCount) {
        // All characters have valid Unicode mappings
        if (currentPageStats.drawCharCount < MIN_CHAR_THRESHOLD) {
            // Too few characters (e.g., page numbers, headers only)
            currentPageStats.pageType = "UNICODE_ALL_MATCH_BUT_LESS_CHARS";
        } else {
            // Sufficient characters with full Unicode mapping
            currentPageStats.pageType = "UNICODE_ALL_MATCH";
        }
    } else if (currentPageStats.unicodeMappedCount == 0) {
        // No characters have valid Unicode mappings
        currentPageStats.pageType = "UNICODE_NONE_MATCH";
    } else {
        // Some but not all characters have valid Unicode mappings
        currentPageStats.pageType = "UNICODE_PARTIAL_MATCH";
    }

    // Store results for this page
    pageResults.push_back(currentPageStats);
}

void TextAnalysisOutputDev::drawChar(GfxState *state, double /*x*/, double /*y*/, double /*dx*/, double /*dy*/, double /*originX*/, double /*originY*/, CharCode c, int /*nBytes*/, const Unicode *u, int uLen)
{
    // Increment total character count
    currentPageStats.drawCharCount++;

    // Check if Unicode mapping exists and is valid
    // U+FFFD is the replacement character indicating mapping failure
    if (uLen > 0 && u != nullptr && u[0] != 0xFFFD && u[0] != 0) {
        // Valid Unicode mapping found
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
