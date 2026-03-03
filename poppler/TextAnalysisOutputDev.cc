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

#include "config.h"                 // Poppler 라이브러리 빌드 설정
#include "TextAnalysisOutputDev.h"  // TextAnalysisOutputDev 클래스 헤더 파일
#include "GfxFont.h"                // 폰트 관련 정보를 정의하는 클래스
#include "GfxState.h"               // 폰트 상태 관련 정보를 관리하는 클래스
#include <sstream>                  // JSON 출력 상태를 관리하는 라이브러리
#include <iomanip>

//------------------------------------------------------------------------
// Helper functions
//------------------------------------------------------------------------

// 폰트 종류를 관리하는 메서드
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

// JSON 출력 파싱 메서드
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

// 유효하지 않은 유니코드를 정의한 메서드
bool TextAnalysisOutputDev::isInvalidUnicode(Unicode cp)
{
    if (cp == 0xFFFD)                      return true; // U+FFFD: 대체 문자
    if (cp == 0x0000)                      return true; // U+0000: Null 문자 
    if (cp >= 0x0001 && cp <= 0x001F)      return true; // U+0001~001F: ASCII 제어 문자
    if (cp >= 0xE000 && cp <= 0xF8FF)      return true; // PUA(사용자 정의 영역): 사용자가 직접 정의해서 사용하는 커스텀 영역, 표준 유니코드를 갖지 않음
    if (cp >= 0xD800 && cp <= 0xDFFF)      return true; // 분리 서로게이트, PUA와 비슷한 커스텀 영역, 다만 정의한 유니코드를 조합해서 사용
    return false;
}

// 생성자 : 페이지별로 변수 관리
TextAnalysisOutputDev::TextAnalysisOutputDev()
{
    currentPageStats.pageNumber       = 0;             // 페이지 번호
    currentPageStats.drawCharCount    = 0;             // 페이지 내에서 카운트된 글자 갯수 -> 텍스트 추출 로직에서 사용
    currentPageStats.unicodeMappedCount = 0;           // 카운트된 글자를 유니코드로 매핑했을때 유효한 글자 갯수 -> 유니코드 디코딩 성공 판별 로직에서 사용
    currentPageStats.decodeSuccessRate = 0.0;          // 페이지 디코딩 성공률
    currentPageStats.pageType         = "";            // PDF 유형 결정시 사용
}

TextAnalysisOutputDev::~TextAnalysisOutputDev() = default;

// 새 페이지 렌더링이 시작될 때 호출
// 이전 페이지에서 쌓인 모든 상태(변수, 이미 찾은 유니코드)를 초기화하는 메서드
void TextAnalysisOutputDev::startPage(int pageNum, GfxState * /*state*/, XRef * /*xref*/)
{
    currentPageStats.pageNumber       = pageNum;
    currentPageStats.drawCharCount    = 0;
    currentPageStats.unicodeMappedCount = 0;
    currentPageStats.decodeSuccessRate = 0.0;
    currentPageStats.pageType         = "";
    currentPageStats.undecodedChars.clear();
    seenUndecodedKeys.clear();
}

// 페이지 렌더링이 끝날 때 호출
// drawCharCount와 unicodeMappedCount 값으로 페이지 유형 분류
void TextAnalysisOutputDev::endPage()
{
    int count  = currentPageStats.drawCharCount; // 페이지 내에서 카운트된 글자 갯수
    int mapped = currentPageStats.unicodeMappedCount; // 카운트된 글자를 유니코드로 매핑했을때 유효한 글자 갯수
    constexpr double decodeFailureTolerance = 0.05; // 디코딩 실패 허용치: 5%
    currentPageStats.decodeSuccessRate = (count > 0)
        ? (static_cast<double>(mapped) / static_cast<double>(count))
        : 0.0;

    // 카운트된 글자가 없는 경우
    if (count == 0) {
        currentPageStats.pageType = "IMAGE";                  // 이미지 기반 페이지로 분류
    // 카운트된 글자가 있고, 모든 글자들의 유니코드 매핑이 모두 유효한 경우
    } else if (count == mapped) {
        currentPageStats.pageType = "UNICODE_ALL_MATCH";      // 모든 글자 디코딩 성공, 모든 유니코드가 매핑된 페이지로 분류
    // 카운트된 글자는 있지만, 유효한 유니코드 매핑이 없는 경우
    } else if (mapped == 0) {
        currentPageStats.pageType = "UNICODE_NONE_MATCH";     // 모든 글자 디코딩 실패, 모든 글자가 유니코드로 매핑 실패한 페이지, VLM으로 파싱이 필요한 페이지
    // 카운트된 글자가 있지만, 일부 글자의 유니코드 매핑이 깨진 경우
    } else {
        const int decodeFailedCount = count - mapped; // 전체 글자 갯수에서 매핑된 글자 갯수 제외
        const double decodeFailureRate = static_cast<double>(decodeFailedCount) / static_cast<double>(count);
        // 디코딩 실패율이 5% 이상이면, VLM로 파싱 필요
        if (decodeFailureRate >= decodeFailureTolerance) {
            currentPageStats.pageType = "UNICODE_PARTIAL_MATCH_NEED_VLM";
        // 디코딩 실패율이 5% 미만이면, PdfToText로 파싱
        } else {
            currentPageStats.pageType = "UNICODE_PARTIAL_MATCH_NEED_PDFTOTEXT";
        }
    }

    pageResults.push_back(currentPageStats);
}

//**
// - 글자 하나가 렌더링될 때마다 호출
// - 유효 유니코드 판단 로직 존재
// Args:
//     state: 폰트 정보
//     x, y: 글자 좌표 - 글자의 시작 위치, 절대적인 좌표값
//     dx, dy: 글자 폭
//     originX, originY: 화면으로 글리프로 렌더링 시 글자의 시작 위치, 화면 축소/확대시 영향받는 변수이므로 상대적인 변수
//     c: 글자 코드
//     nBytes: 글자 바이트 수
//     u: 유니코드 - 유니코드가 1개 이상일수도 있어 배열로 할당
//     uLen: 유니코드 배열 길이
//  */
void TextAnalysisOutputDev::drawChar(GfxState *state, double /*x*/, double /*y*/,
                                     double /*dx*/, double /*dy*/,
                                     double /*originX*/, double /*originY*/,
                                     CharCode c, int /*nBytes*/,
                                     const Unicode *u, int uLen)
{
    // ---- Unicode 유효성 판정 ----
    // ToUnicode CMap 존재 여부가 아닌, Poppler가 실제로 변환한 u[] 배열(cmap 배열)로 판단
    // u 배열이 없거나 비어 있으면 변환 실패, false 할당
    bool validUnicode = (u != nullptr && uLen > 0);
    // validUnicode가 true인 경우 유니코드 유효성 검증
    if (validUnicode) {
        for (int i = 0; i < uLen; ++i) {
            if (isInvalidUnicode(u[i])) { // 앞서 정의한 유효하지 않은 유니코드 배열과 매칭
                validUnicode = false;
                break;
            }
        }
    }
    // 글자 카운트 증가
    currentPageStats.drawCharCount++;
    // 유효한 유니코드일때 매핑된 유니코드 갯수 카운트 증가
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
                // 실패한 글자의 폰트 이름, 유형, 인코딩 정보, CMap 매핑 여부를 할당
                fontName      = font->getName() ? *font->getName() : "(unnamed)";
                fontType      = fontTypeToString(font->getType());
                encodingName  = font->getEncodingName();
                hasToUnicodeCMap = font->hasToUnicodeCMap(); // 참고 정보로만 기록
            }
        }

        // (charCode, fontName) 쌍으로 중복 제거 - 같은 폰트의 중복 글자는 1번만 기록
        auto key = std::make_pair(static_cast<int>(c), fontName);
        if (seenUndecodedKeys.find(key) == seenUndecodedKeys.end()) {
            seenUndecodedKeys.insert(key);
            // JSON으로 직렬화시 사용될 변수 할당
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

// 분석 결과 전체를 JSON 문자열로 직렬화
// 출력 형식:
// {
//   "status": "OK",
//   "pages": [
//     { "page": 1, "type": "UNICODE_ALL_MATCH" },
//     { "page": 2, "type": "UNICODE_PARTIAL_MATCH_NEED_PDFTOTEXT",
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
        oss << "      \"type\": \"" << stats.pageType << "\",\n";
        oss << "      \"decode_success_rate\": "
            << std::fixed << std::setprecision(4) << stats.decodeSuccessRate
            << std::defaultfloat;

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

// 처리 중 오류가 발생시 반환되는 JSON 데이터
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
