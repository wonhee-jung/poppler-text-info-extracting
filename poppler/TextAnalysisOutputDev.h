//========================================================================
//
// TextAnalysisOutputDev.h
//
// PDF text extraction analysis tool
// Analyzes whether text can be reliably extracted from PDF pages
//
//========================================================================

// Modified by [Wonhee-jung], 2026-02-11
// Added text layout analysis functionality.
// This file is part of a modified version of Poppler (GPL).

#ifndef TEXTANALYSISOUTPUTDEV_H
#define TEXTANALYSISOUTPUTDEV_H

#include "poppler_private_export.h"
#include "OutputDev.h"
#include "CharTypes.h"
#include <vector>
#include <string>
#include <set>
#include <utility>

class GfxState;
class XRef;

//------------------------------------------------------------------------
// TextAnalysisOutputDev
//------------------------------------------------------------------------

class POPPLER_PRIVATE_EXPORT TextAnalysisOutputDev : public OutputDev
{
public:
    // Constructor
    TextAnalysisOutputDev();

    // Destructor
    ~TextAnalysisOutputDev() override;

    //---- get info about output device

    // Does this device use upside-down coordinates?
    bool upsideDown() override { return true; }

    // Does this device use drawChar() or drawString()?
    bool useDrawChar() override { return true; }

    // Does this device use beginType3Char/endType3Char?
    bool interpretType3Chars() override { return false; }

    // Does this device need non-text content?
    bool needNonText() override { return false; }

    //----- initialization and control

    // Start a page
    void startPage(int pageNum, GfxState *state, XRef *xref) override;

    // End a page
    void endPage() override;

    //----- text drawing

    // Draw a character
    void drawChar(GfxState *state, double x, double y, double dx, double dy, double originX, double originY, CharCode c, int nBytes, const Unicode *u, int uLen) override;

    //----- output

    // Generate JSON output with document-level status
    std::string toJSON() const;

    // Generate error JSON output
    static std::string toErrorJSON(const std::string &errorReason);

    // Info about a single character that could not be mapped to Unicode
    struct UndecodedCharInfo
    {
        int charCode;             // Original character code (decimal)
        std::string fontName;     // Font name
        std::string fontType;     // Font type string (e.g. "Type1", "TrueType", "CIDType0")
        std::string encodingName; // Encoding name (e.g. "Identity-H", "WinAnsiEncoding")
        bool hasToUnicodeCMap;    // Whether the font has an explicit ToUnicode CMap
    };

    // Per-page analysis results
    struct PageStats
    {
        int pageNumber;
        int drawCharCount;
        int unicodeMappedCount;
        std::string pageType; // "IMAGE", "UNICODE_ALL_MATCH", "UNICODE_ALL_MATCH_BUT_LESS_CHARS", "UNICODE_NONE_MATCH", "UNICODE_PARTIAL_MATCH"
        std::vector<UndecodedCharInfo> undecodedChars; // Unique (charCode, fontName) pairs that failed Unicode mapping
    };

    const std::vector<PageStats> &getPageResults() const { return pageResults; }

private:
    static constexpr int MIN_CHAR_THRESHOLD = 10; // Minimum character count to consider page as having meaningful text

    std::vector<PageStats> pageResults;
    PageStats currentPageStats;
    std::set<std::pair<int, std::string>> seenUndecodedKeys; // Tracks (charCode, fontName) seen on current page for deduplication
};

#endif
