//========================================================================
//
// FilteredTextOutputDev.h
//
// 유효하지 않은 유니코드 문자를 자동 제거하는 텍스트 추출 출력 디바이스.
// TextOutputDev를 상속받아 drawChar() 단계에서 제어 문자, 깨진 매핑,
// 검증되지 않은 PUA 등을 걸러낸 뒤 유효한 문자만 부모 클래스에 전달한다.
//
//========================================================================

// Added by [Wonhee-jung], 2026-02-27
// Implements a filtered text extraction device based on TextOutputDev.
// This file is part of a modified version of Poppler (GPL).

#ifndef FILTEREDTEXTOUTPUTDEV_H
#define FILTEREDTEXTOUTPUTDEV_H

#include "poppler_private_export.h"
#include "TextOutputDev.h"
#include "CharTypes.h"

class GfxState;

//------------------------------------------------------------------------
// FilteredTextOutputDev
//
// TextOutputDev를 상속하여, drawChar() 호출 시 유효하지 않은 유니코드
// 코드포인트를 제거한 뒤 부모 클래스에 전달한다.
//
// 코드포인트 자체만 보고 유효하지 않다고 판단하는 범위:
//   U+0000        Null 문자
//   U+0001~001F   ASCII 제어 문자 (HT/LF/CR 제외)
//   U+007F        ASCII DEL 제어 문자
//   잘못된 유니코드 scalar 값
//   U+FFFD        대체 문자 (Unicode Replacement Character)
//
// PUA는 코드포인트만으로는 제거하지 않고, drawChar()에서 ToUnicode 매핑
// 검증 결과에 따라 제거 여부를 결정한다.
//------------------------------------------------------------------------

class POPPLER_PRIVATE_EXPORT FilteredTextOutputDev : public TextOutputDev
{
public:
    // 파일로 직접 출력하는 생성자.
    // fileName이 nullptr이면 파일을 생성하지 않는다 (검색 전용 모드).
    FilteredTextOutputDev(const char *fileName,
                          bool physLayout,
                          double fixedPitch,
                          bool rawOrder,
                          bool append);

    // 콜백 함수(스트림)로 출력하는 생성자.
    FilteredTextOutputDev(TextOutputFunc func,
                          void *stream,
                          bool physLayout,
                          double fixedPitch,
                          bool rawOrder);

    ~FilteredTextOutputDev() override;

    // 복사 금지
    FilteredTextOutputDev(const FilteredTextOutputDev &) = delete;
    FilteredTextOutputDev &operator=(const FilteredTextOutputDev &) = delete;

    // 글리프 하나가 렌더링될 때 호출된다.
    // 유효하지 않은 유니코드 코드포인트를 제거한 뒤 부모 클래스로 전달한다.
    void drawChar(GfxState *state,
                  double x, double y,
                  double dx, double dy,
                  double originX, double originY,
                  CharCode c, int nBytes,
                  const Unicode *u, int uLen) override;

    // ActualText 스팬은 drawChar() 필터를 우회할 수 있으므로 무시한다.
    void beginActualText(GfxState *state, const GooString *text) override;
    void endActualText(GfxState *state) override;

    // 필터링 후 실제로 출력된 문자 수를 반환한다.
    int getFilteredCharCount() const { return filteredCharCount; }

    // 필터링으로 제거된 문자 수를 반환한다.
    int getSkippedCharCount() const { return skippedCharCount; }

private:
    // 주어진 유니코드 코드포인트가 출력 목적상 유효하지 않으면 true를 반환한다.
    // PUA 여부는 이 함수가 아닌 drawChar()의 매핑 검증에서 판정한다.
    static bool isInvalidUnicode(Unicode cp);

    int filteredCharCount; // 유효 유니코드로 통과된 문자 수
    int skippedCharCount;  // 유효하지 않아 제거된 문자 수
};

#endif // FILTEREDTEXTOUTPUTDEV_H
