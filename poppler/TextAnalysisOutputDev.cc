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
#include "GfxState.h"
#include <sstream>
#include <iomanip>

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

void TextAnalysisOutputDev::drawChar(GfxState * /*state*/, double /*x*/, double /*y*/, double /*dx*/, double /*dy*/, double /*originX*/, double /*originY*/, CharCode /*c*/, int /*nBytes*/, const Unicode *u, int uLen)
{
    // Increment total character count
    currentPageStats.drawCharCount++;

    // Check if Unicode mapping exists and is valid
    if (uLen > 0 && u != nullptr && u[0] != 0xFFFD) {
        // Valid Unicode mapping found
        // U+FFFD is the replacement character indicating mapping failure
        currentPageStats.unicodeMappedCount++;
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

        oss << "    {";
        oss << "\"page\": " << stats.pageNumber << ", ";
        oss << "\"type\": \"" << stats.pageType << "\"";
        oss << "}";

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
