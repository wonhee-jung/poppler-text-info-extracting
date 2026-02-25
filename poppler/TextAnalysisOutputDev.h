//========================================================================
//
// TextAnalysisOutputDev.h
//
// PDF 텍스트 추출 분석 도구
// 각 페이지에서 텍스트를 신뢰성 있게 추출할 수 있는지 분류한다.
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
class GfxImageColorMap;
class Object;
class Stream;
class XRef;

//------------------------------------------------------------------------
// TextAnalysisOutputDev
// PDF 페이지를 렌더링하면서 텍스트/이미지 구조를 분석하고,
// 페이지 유형(IMAGE / OCR / UNICODE_*)을 판별하는 OutputDev 구현체.
//------------------------------------------------------------------------

class POPPLER_PRIVATE_EXPORT TextAnalysisOutputDev : public OutputDev
{
public:
    TextAnalysisOutputDev();
    ~TextAnalysisOutputDev() override;

    //---- get info about output device

    // 좌표계가 위아래 반전되어 있는가? (PDF 기본 좌표계 = 왼쪽 아래 원점)
    bool upsideDown() override { return true; }

    // 문자를 drawChar() 단위로 받을 것인가? (drawString() 대신)
    bool useDrawChar() override { return true; }

    // Type3 폰트의 글리프 내부까지 해석할 것인가?
    bool interpretType3Chars() override { return false; }

    // 텍스트 외 이미지 등 비텍스트 콘텐츠도 처리할 것인가?
    // true여야 drawImage* 콜백이 호출된다 (OCR 이미지 감지에 필요).
    bool needNonText() override { return true; }

    //----- 페이지 시작/종료 -----

    // 새 페이지 시작 시 Poppler가 호출 — 모든 페이지 단위 상태를 초기화한다.
    void startPage(int pageNum, GfxState *state, XRef *xref) override;

    // 페이지 렌더링 완료 시 Poppler가 호출 — 수집된 데이터로 페이지 유형을 판별한다.
    void endPage() override;

    //----- 텍스트 렌더링 콜백 -----

    // 글리프 하나가 렌더링될 때마다 Poppler가 호출한다.
    // (x, y): 글리프 원점,  (dx, dy): advance vector,
    // u / uLen: Poppler가 변환한 Unicode 코드포인트 배열
    void drawChar(GfxState *state, double x, double y, double dx, double dy,
                  double originX, double originY,
                  CharCode c, int nBytes,
                  const Unicode *u, int uLen) override;

    //----- 이미지 렌더링 콜백 (OCR 이미지 감지용) -----

    // 1비트 마스크 이미지 (예: 흑백 스캔)
    void drawImageMask(GfxState *state, Object *ref, Stream *str,
                       int width, int height,
                       bool invert, bool interpolate, bool inlineImg) override;

    // 일반 컬러 래스터 이미지
    void drawImage(GfxState *state, Object *ref, Stream *str,
                   int width, int height, GfxImageColorMap *colorMap,
                   bool interpolate, const int *maskColors, bool inlineImg) override;

    // 소프트 마스크(투명도 마스크)가 적용된 이미지
    void drawSoftMaskedImage(GfxState *state, Object *ref, Stream *str,
                             int width, int height, GfxImageColorMap *colorMap,
                             bool interpolate, Stream *maskStr,
                             int maskWidth, int maskHeight,
                             GfxImageColorMap *maskColorMap,
                             bool maskInterpolate) override;

    //----- 결과 출력 -----

    // 전체 분석 결과를 JSON 문자열로 반환한다.
    std::string toJSON() const;

    // 오류 발생 시 에러 JSON 문자열을 생성한다 (정적 메서드).
    static std::string toErrorJSON(const std::string &errorReason);

    //---- 데이터 구조 ----

    // Unicode로 변환하지 못한 글자 하나에 대한 상세 정보.
    // undecoded_chars 배열의 원소로 JSON에 출력된다.
    struct UndecodedCharInfo
    {
        int charCode;             // 원본 CharCode (10진수)
        std::string fontName;     // 폰트 이름
        std::string fontType;     // 폰트 종류 ("Type1", "TrueType", "CIDType0" 등)
        std::string encodingName; // 인코딩 이름 ("Identity-H", "WinAnsiEncoding" 등)
        bool hasToUnicodeCMap;    // 폰트에 ToUnicode CMap이 존재하는지 여부 (참고용)
    };

    // 페이지 한 장의 분석 결과.
    struct PageStats
    {
        int pageNumber;          // 페이지 번호 (1-based)
        int drawCharCount;       // 화면에 표시되는(visible) 글리프 수
        int unicodeMappedCount;  // visible 글리프 중 유효한 Unicode로 변환된 수
        // 페이지 유형 (6가지):
        //   "IMAGE"                   — 텍스트 글리프 없음 (이미지 전용 페이지)
        //   "OCR_INVISIBLE_LAYER"     — OCR PDF: 이미지 위에 invisible 텍스트 레이어 (5가지 조건 모두 충족)
        //   "NOT_OCR_INVISIBLE_GLYPH" — 글리프가 전부 invisible이나 OCR 구조 조건 미달 (이미지 없음 / Unicode 미보유 / bbox 불일치 등)
        //   "UNICODE_ALL_MATCH"       — visible 글리프 전부 Unicode 변환 성공
        //   "UNICODE_NONE_MATCH"      — visible 글리프 전부 Unicode 변환 실패
        //   "UNICODE_PARTIAL_MATCH"   — visible 글리프 중 일부만 Unicode 변환 성공
        std::string pageType;
        std::vector<UndecodedCharInfo> undecodedChars; // Unicode 변환 실패 글자 목록 (중복 제거)
    };

    // 모든 페이지의 분석 결과 목록을 반환한다.
    const std::vector<PageStats> &getPageResults() const { return pageResults; }

    // device space 기준 사각형 영역
    struct BBox
    {
        double xMin;
        double yMin;
        double xMax;
        double yMax;
    };

private:
    std::vector<PageStats> pageResults;   // 완료된 페이지들의 결과 누적
    PageStats currentPageStats;           // 현재 렌더링 중인 페이지의 임시 결과
    // (charCode, fontName) 쌍 — 같은 페이지에서 동일 글자를 중복 기록하지 않기 위한 집합
    std::set<std::pair<int, std::string>> seenUndecodedKeys;

    // ---- OCR 판정용 페이지 단위 카운터 ----
    // totalGlyphCount        : 이 페이지에서 렌더링된 전체 글리프 수 (visible + invisible)
    // invisibleGlyphCount    : Tr=3 또는 Tr=7인 글리프 수 (ISO 32000-1 §5.2)
    // invisibleValidUnicodeCount : invisible 글리프 중 u[] 배열에 유효한 Unicode가 있는 수
    int totalGlyphCount;
    int invisibleGlyphCount;
    int invisibleValidUnicodeCount;

    std::vector<BBox> imageBBoxes; // 이 페이지에서 렌더링된 이미지들의 device space bbox
    std::vector<BBox> glyphBBoxes; // 이 페이지에서 렌더링된 글리프들의 device space bbox

    // 이미지 XObject의 bbox를 device space로 변환해 imageBBoxes에 등록한다.
    void registerImageBBox(GfxState *state, int width, int height);

    // 주어진 Unicode 코드포인트가 텍스트 추출 목적상 "유효하지 않은" 값이면 true를 반환한다.
    static bool isInvalidUnicode(Unicode cp);
};

#endif
