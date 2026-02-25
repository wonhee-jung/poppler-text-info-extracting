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
    case fontType1:      return "Type1";
    case fontType1C:     return "Type1C";
    case fontType1COT:   return "Type1COT";
    case fontType3:      return "Type3";
    case fontTrueType:   return "TrueType";
    case fontTrueTypeOT: return "TrueTypeOT";
    case fontCIDType0:   return "CIDType0";
    case fontCIDType0C:  return "CIDType0C";
    case fontCIDType0COT:return "CIDType0COT";
    case fontCIDType2:   return "CIDType2";
    case fontCIDType2OT: return "CIDType2OT";
    default:             return "Unknown";
    }
}

// JSON 문자열 내부에 포함될 수 없는 특수문자를 이스케이프 처리
// 예: " → \", \ → \\, 제어문자 → \uXXXX
static std::string escapeJsonString(const std::string &s)
{
    std::ostringstream oss;
    for (unsigned char c : s) {
        switch (c) {
        case '"':  oss << "\\\""; break;
        case '\\': oss << "\\\\"; break;
        case '\n': oss << "\\n";  break;
        case '\r': oss << "\\r";  break;
        case '\t': oss << "\\t";  break;
        default:
            if (c < 0x20) {
                // U+0000~U+001F 제어문자는 \uXXXX 형태로 출력
                oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(c) << std::dec;
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

// 텍스트 추출 목적상 "유효하지 않은" Unicode 코드포인트인지 판별
bool TextAnalysisOutputDev::isInvalidUnicode(Unicode cp)
{
    if (cp == 0xFFFD)                      return true; // U+FFFD: 변환 실패 대체 문자
    if (cp == 0x0000)                      return true; // U+0000: Null
    if (cp >= 0x0001 && cp <= 0x001F)      return true; // U+0001~001F: ASCII 제어 문자
    if (cp >= 0xE000 && cp <= 0xF8FF)      return true; // PUA(사용자 정의 영역): 공통 유니코드가 아니므로 의미없음
    if (cp >= 0xD800 && cp <= 0xDFFF)      return true; // 분리 서로게이트: 단독으로 유효하지 않음
    return false;
}

// 생성자: 페이지 단위 카운터를 0으로 초기화
// currentPageStats의 나머지 필드는 startPage()에서 매 페이지마다 초기화
TextAnalysisOutputDev::TextAnalysisOutputDev()
    : totalGlyphCount(0) // 전체 글리프 수 (visible + invisible)
    , invisibleGlyphCount(0) // Tr=3 또는 Tr=7 글리프 수
    , invisibleValidUnicodeCount(0) // invisible 중 유효 Unicode 보유 글리프 수
{
    currentPageStats.pageNumber       = 0;
    currentPageStats.drawCharCount    = 0;
    currentPageStats.unicodeMappedCount = 0;
    currentPageStats.pageType         = "";
}

TextAnalysisOutputDev::~TextAnalysisOutputDev() = default;

// 새 페이지 렌더링이 시작될 때 Poppler가 호출
// 이전 페이지에서 쌓인 모든 상태를 초기화
void TextAnalysisOutputDev::startPage(int pageNum, GfxState * /*state*/, XRef * /*xref*/)
{
    currentPageStats.pageNumber       = pageNum;
    currentPageStats.drawCharCount    = 0;  // visible 글리프 수 (invisible 제외)
    currentPageStats.unicodeMappedCount = 0; // visible 중 Unicode 변환 성공 수
    currentPageStats.pageType         = "";
    currentPageStats.undecodedChars.clear();
    seenUndecodedKeys.clear();

    // OCR 판정용 카운터 초기화
    totalGlyphCount          = 0; // 전체 글리프 (visible + invisible)
    invisibleGlyphCount      = 0; // Tr=3 또는 Tr=7 글리프
    invisibleValidUnicodeCount = 0; // invisible 중 유효 Unicode 보유 글리프

    // 이미지/글리프 bbox 목록 초기화
    imageBBoxes.clear();
    glyphBBoxes.clear();
}

// 이미지 XObject가 렌더링될 때 그 bbox를 device space로 변환
// 이미지 공간(0,0)~(width,height)의 네 꼭짓점을 CTM으로 변환한 뒤
// 축 정렬 bbox(AABB)를 구해 저장
void TextAnalysisOutputDev::registerImageBBox(GfxState *state, int width, int height)
{
    if (!state)
        return;

    double w = static_cast<double>(width);
    double h = static_cast<double>(height);

    // 이미지 공간의 네 꼭짓점
    double corners[4][2] = {
        { 0, 0 }, { w, 0 }, { 0, h }, { w, h }
    };

    double txMin = 0, tyMin = 0, txMax = 0, tyMax = 0;
    bool first = true;

    for (auto &corner : corners) {
        double tx, ty;
        state->transform(corner[0], corner[1], &tx, &ty); // CTM 적용
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


// 이미지 마스크 렌더링
void TextAnalysisOutputDev::drawImageMask(GfxState *state, Object * /*ref*/, Stream * /*str*/,
                                          int width, int height, bool /*invert*/,
                                          bool /*interpolate*/, bool /*inlineImg*/)
{
    registerImageBBox(state, width, height);
}

// 일반 컬러 래스터 이미지 렌더링
void TextAnalysisOutputDev::drawImage(GfxState *state, Object * /*ref*/, Stream * /*str*/,
                                      int width, int height, GfxImageColorMap * /*colorMap*/,
                                      bool /*interpolate*/, const int * /*maskColors*/,
                                      bool /*inlineImg*/)
{
    registerImageBBox(state, width, height);
}

// 소프트 마스크 이미지 렌더링
void TextAnalysisOutputDev::drawSoftMaskedImage(GfxState *state, Object * /*ref*/, Stream * /*str*/,
                                                int width, int height,
                                                GfxImageColorMap * /*colorMap*/,
                                                bool /*interpolate*/, Stream * /*maskStr*/,
                                                int /*maskWidth*/, int /*maskHeight*/,
                                                GfxImageColorMap * /*maskColorMap*/,
                                                bool /*maskInterpolate*/)
{
    registerImageBBox(state, width, height);
}

// 페이지 렌더링이 끝날 때 Poppler가 호출
// drawChar()에서 수집한 통계를 바탕으로 페이지 유형을 결정하고 결과를 저장
void TextAnalysisOutputDev::endPage()
{
    // ---------------------------------------------------------------
    // 단계 1: 모든 글리프가 이미지 bbox 안에 있는지 확인
    // ---------------------------------------------------------------
    // OCR PDF는 구조적으로 "이미지 위에 invisible 텍스트가 겹쳐 있는" 형태이므로
    // 글리프의 위치가 반드시 이미지 영역 내부에 있어야 한다.
    // 이미지 없음 / 글리프 없음 / bbox 미기록 중 하나라도 해당하면 false로 시작.
    bool allGlyphsContainedInImage = false;

    if (!imageBBoxes.empty() && totalGlyphCount > 0 && !glyphBBoxes.empty()) {
        allGlyphsContainedInImage = true;
        for (const auto &glyph : glyphBBoxes) {
            bool inside = false;
            for (const auto &image : imageBBoxes) {
                // 글리프 bbox가 이미지 bbox에 완전히 포함되는지 엄격하게 비교
                // (tolerance 없음 — PDF 명세에 허용 범위 정의 없음)
                if (glyph.xMin >= image.xMin &&
                    glyph.yMin >= image.yMin &&
                    glyph.xMax <= image.xMax &&
                    glyph.yMax <= image.yMax)
                {
                    inside = true;
                    break;
                }
            }
            if (!inside) {
                allGlyphsContainedInImage = false;
                break;
            }
        }
    }

    // ---------------------------------------------------------------
    // 단계 2: OCR_INVISIBLE_LAYER 판정 (최우선 분류)
    // ---------------------------------------------------------------
    // ISO 32000-1 기준 OCR PDF의 구조적 필요조건 5가지:
    //   (a) 이미지가 1개 이상 존재        (§4.8 Images)
    //   (b) 글리프가 1개 이상 존재
    //   (c) 모든 글리프가 invisible       (§5.2 Tr=3 또는 Tr=7, visible 글리프 0개)
    //   (d) 모든 invisible 글리프가 유효한 Unicode를 가짐  (§5.9 ToUnicode)
    //   (e) 모든 글리프 위치가 이미지 bbox 안에 포함됨
    // 5가지 모두 충족할 때만 OCR로 판정한다.
    bool hasImage           = !imageBBoxes.empty();
    bool hasGlyphs          = (totalGlyphCount > 0);
    // drawCharCount는 visible 글리프만 세므로, 0이면 전부 invisible
    bool allGlyphsInvisible = (hasGlyphs && currentPageStats.drawCharCount == 0);
    // 모든 invisible 글리프의 u[] 배열에 유효한 Unicode가 있어야 함
    bool allInvisibleValidUnicode =
        (invisibleGlyphCount > 0 &&
         invisibleGlyphCount == invisibleValidUnicodeCount);

    if (hasImage &&
        hasGlyphs &&
        allGlyphsInvisible &&
        allInvisibleValidUnicode &&
        allGlyphsContainedInImage)
    {
        currentPageStats.pageType = "OCR_INVISIBLE_LAYER";
        pageResults.push_back(currentPageStats);
        return; // OCR 확정 → 이후 분류 불필요
    }

    // ---------------------------------------------------------------
    // 단계 3: UNICODE_* 분류 (visible 글리프 기준)
    // ---------------------------------------------------------------
    // invisible 글리프는 OCR 텍스트이므로 일반 Unicode 분류에서 제외한다.
    // drawCharCount / unicodeMappedCount 는 모두 visible 글리프만 집계한다.
    int visibleCount = currentPageStats.drawCharCount;
    int mappedCount  = currentPageStats.unicodeMappedCount;

    if (totalGlyphCount == 0) {
        // 글리프가 전혀 없는 페이지 → 이미지만 있는 페이지
        currentPageStats.pageType = "IMAGE";
    } else if (visibleCount == 0) {
        // 글리프는 있지만 전부 invisible (Tr=3 또는 Tr=7)이며, OCR 구조 조건 미달
        // OCR_INVISIBLE_LAYER와 달리 이미지 없음 / Unicode 미보유 / bbox 불일치 등으로 OCR 판정 실패
        currentPageStats.pageType = "NOT_OCR_INVISIBLE_GLYPH";
    } else if (visibleCount == mappedCount) {
        // visible 글리프가 존재하고, 그 전부가 Unicode 변환 성공
        currentPageStats.pageType = "UNICODE_ALL_MATCH";
    } else if (mappedCount == 0) {
        // visible 글리프가 존재하지만, 단 하나도 Unicode 변환 불가
        currentPageStats.pageType = "UNICODE_NONE_MATCH";
    } else {
        // visible 글리프 중 일부만 Unicode 변환 성공 (0 < mappedCount < visibleCount)
        currentPageStats.pageType = "UNICODE_PARTIAL_MATCH";
    }

    pageResults.push_back(currentPageStats);
}

// 글리프 하나가 렌더링될 때마다 Poppler가 호출한다.
// invisible / visible 를 구분해 각각의 카운터와 통계를 독립적으로 관리한다.
void TextAnalysisOutputDev::drawChar(GfxState *state, double x, double y,
                                     double dx, double dy,
                                     double /*originX*/, double /*originY*/,
                                     CharCode c, int /*nBytes*/,
                                     const Unicode *u, int uLen)
{
    totalGlyphCount++; // 이 페이지의 전체 글리프 수 (visible + invisible)

    // ---- invisible 여부 판별 ----
    // ISO 32000-1 §5.2 Text Rendering Mode:
    //   Tr=3: fill도 stroke도 없음 → 화면에 표시되지 않음
    //   Tr=7: Tr=3과 동일하지만 추가로 clipping path에 반영됨
    // 두 모드 모두 시각적으로 보이지 않는 텍스트이다.
    int renderMode  = state ? state->getRender() : -1;
    bool isInvisible = (renderMode == 3 || renderMode == 7);

    // ---- 글리프 bbox 기록 (device space, advance vector 기반) ----
    // (x, y) → 글리프 원점,  (x+dx, y+dy) → advance 끝점
    // 실제 ink bbox가 아닌 advance 기반 근사값임에 유의.
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

    // ---- Unicode 유효성 판정 ----
    // ToUnicode CMap 존재 여부가 아닌, Poppler가 실제로 변환한 u[] 배열로 판단
    // u 배열이 없거나 비어 있으면 변환 실패, 있더라도 각 코드포인트를 검증
    bool validUnicode = (u != nullptr && uLen > 0);
    if (validUnicode) {
        for (int i = 0; i < uLen; ++i) {
            if (isInvalidUnicode(u[i])) {
                validUnicode = false;
                break;
            }
        }
    }

    if (isInvisible) {
        // ---- invisible 글리프 처리 ----
        // OCR 판정 전용 카운터에만 집계하고, visible 통계(drawCharCount 등)에는 포함하지 않음.
        invisibleGlyphCount++;
        if (validUnicode)
            invisibleValidUnicodeCount++;
    } else {
        // ---- visible 글리프 처리 ----
        // 일반 페이지 Unicode 분류 통계에 반영
        currentPageStats.drawCharCount++;

        if (validUnicode) {
            currentPageStats.unicodeMappedCount++;
        } else {
            // Unicode 변환 실패 글자 수집 (같은 페이지에서 동일 charCode+fontName은 1번만 기록)
            std::string fontName = "(no font)";
            std::string fontType = "Unknown";
            std::string encodingName;
            bool hasToUnicodeCMap = false;

            if (state) {
                const std::shared_ptr<GfxFont> &font = state->getFont();
                if (font) {
                    fontName      = font->getName() ? *font->getName() : "(unnamed)";
                    fontType      = fontTypeToString(font->getType());
                    encodingName  = font->getEncodingName();
                    hasToUnicodeCMap = font->hasToUnicodeCMap(); // 참고 정보로만 기록
                }
            }

            // (charCode, fontName) 쌍으로 중복 제거
            auto key = std::make_pair(static_cast<int>(c), fontName);
            if (seenUndecodedKeys.find(key) == seenUndecodedKeys.end()) {
                seenUndecodedKeys.insert(key);

                UndecodedCharInfo info;
                info.charCode        = static_cast<int>(c);
                info.fontName        = std::move(fontName);
                info.fontType        = std::move(fontType);
                info.encodingName    = std::move(encodingName);
                info.hasToUnicodeCMap = hasToUnicodeCMap;
                currentPageStats.undecodedChars.push_back(std::move(info));
            }
        }
    }
}

// 분석 결과 전체를 JSON 문자열로 직렬화해 반환한다.
// 출력 형식:
// {
//   "status": "OK",
//   "pages": [
//     { "page": 1, "type": "UNICODE_ALL_MATCH" },
//     { "page": 2, "type": "OCR_INVISIBLE_LAYER" },
//     { "page": 3, "type": "UNICODE_PARTIAL_MATCH",
//       "undecoded_chars": [ { "char_code": ..., ... } ] }
//   ]
// }
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

        // Unicode 변환 실패 글자가 있을 때만 undecoded_chars 배열을 출력한다.
        if (!stats.undecodedChars.empty()) {
            oss << ",\n";
            oss << "      \"undecoded_chars\": [\n";

            for (size_t j = 0; j < stats.undecodedChars.size(); ++j) {
                const auto &ch = stats.undecodedChars[j];

                oss << "        {\n";
                oss << "          \"char_code\": " << ch.charCode << ",\n";
                oss << "          \"char_code_hex\": \"0x"
                    << std::hex << std::uppercase
                    << std::setw(4) << std::setfill('0') << ch.charCode
                    << std::dec << std::nouppercase << "\",\n";
                oss << "          \"font_name\": \""     << escapeJsonString(ch.fontName)     << "\",\n";
                oss << "          \"font_type\": \""     << ch.fontType                       << "\",\n";
                oss << "          \"encoding_name\": \"" << escapeJsonString(ch.encodingName) << "\",\n";
                oss << "          \"has_to_unicode_cmap\": " << (ch.hasToUnicodeCMap ? "true" : "false") << "\n";
                oss << "        }";

                if (j < stats.undecodedChars.size() - 1)
                    oss << ",";
                oss << "\n";
            }

            oss << "      ]";
        }

        oss << "\n    }";
        if (i < pageResults.size() - 1)
            oss << ",";
        oss << "\n";
    }

    oss << "  ]\n";
    oss << "}\n";

    return oss.str();
}

// 처리 중 오류가 발생했을 때 반환할 에러 JSON을 생성한다.
// { "status": "ERROR", "error": "<errorReason>" }
std::string TextAnalysisOutputDev::toErrorJSON(const std::string &errorReason)
{
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"status\": \"ERROR\",\n";
    oss << "  \"error\": \"" << errorReason << "\"\n";
    oss << "}\n";
    return oss.str();
}
