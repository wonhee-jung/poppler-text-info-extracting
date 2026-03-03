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
class XRef;

//------------------------------------------------------------------------
// TextAnalysisOutputDev
// PDF 페이지를 렌더링하면서 텍스트 구조를 분석하고,
// 페이지 유형(IMAGE / UNICODE_*)을 판별하는 OutputDev 구현체.
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

    // 텍스트 외 비텍스트 콘텐츠 처리 불필요
    bool needNonText() override { return false; }

    //----- 페이지 시작/종료 -----

    // 새 페이지 시작 시 Poppler가 호출 — 모든 페이지 단위 상태를 초기화한다.
    void startPage(int pageNum, GfxState *state, XRef *xref) override;

    // 페이지 렌더링 완료 시 Poppler가 호출 — 수집된 데이터로 페이지 유형을 판별한다.
    void endPage() override;

    //----- 텍스트 렌더링 콜백 -----

    // 글리프 하나가 렌더링될 때마다 Poppler가 호출한다.
    // (x, y): 글리프 원점,  (dx, dy): advance vector, -> 이미지 객체 인식 로직이 사라짐에 따라 사용하지 않는 변수
    // u / uLen: Poppler가 변환한 Unicode 코드포인트 배열
    void drawChar(GfxState *state, double x, double y, double dx, double dy,
                  double originX, double originY,
                  CharCode c, int nBytes,
                  const Unicode *u, int uLen) override;

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
        int drawCharCount;       // 렌더링된 전체 글리프 수 (visibility 무관)
        int unicodeMappedCount;  // 글리프 중 유효한 Unicode로 변환된 수
        double decodeSuccessRate; // 디코딩 성공률(0.0~1.0)
        // 페이지 유형 (5가지):
        //   "IMAGE"                — 텍스트 글리프 없음
        //   "UNICODE_ALL_MATCH"    — 전체 글리프 Unicode 변환 성공
        //   "UNICODE_NONE_MATCH"   — 전체 글리프 Unicode 변환 실패
        //   "UNICODE_PARTIAL_MATCH_NEED_VLM"       — 부분 실패 + 실패율 5% 이상
        //   "UNICODE_PARTIAL_MATCH_NEED_PDFTOTEXT" — 부분 실패 + 실패율 5% 미만
        std::string pageType;
        std::vector<UndecodedCharInfo> undecodedChars; // Unicode 변환 실패 글자 목록 (중복 제거)
    };

    // 모든 페이지의 분석 결과 목록을 반환한다.
    const std::vector<PageStats> &getPageResults() const { return pageResults; }

private:
    std::vector<PageStats> pageResults;   // 완료된 페이지들의 결과 누적
    PageStats currentPageStats;           // 현재 렌더링 중인 페이지의 임시 결과
    // (charCode, fontName) 쌍 — 같은 페이지에서 동일 글자를 중복 기록하지 않기 위한 집합
    std::set<std::pair<int, std::string>> seenUndecodedKeys;

    // 주어진 Unicode 코드포인트가 텍스트 추출 목적상 "유효하지 않은" 값이면 true를 반환한다.
    static bool isInvalidUnicode(Unicode cp);
};

#endif
