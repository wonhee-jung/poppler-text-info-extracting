//========================================================================
//
// FilteredTextOutputDev.cc
//
// 유효하지 않은 유니코드 문자를 자동 제거하는 텍스트 추출 출력 디바이스 구현.
//
// 핵심 동작:
//   1. drawChar() 호출 시 Poppler가 변환한 u[] 배열을 검사한다.
//   2. 코드포인트 자체가 잘못되었거나, 검증되지 않은 매핑 결과만 제거한다.
//   3. 유효한 코드포인트만 모아 부모 클래스(TextOutputDev)에 전달한다.
//   4. 유효한 코드포인트가 하나도 없으면 해당 글리프를 완전히 건너뛴다.
//
//========================================================================

// Added by [Wonhee-jung], 2026-02-27
// Implements a filtered text extraction device based on TextOutputDev.
// This file is part of a modified version of Poppler (GPL).

#include "config.h"
#include "FilteredTextOutputDev.h"

#include "CharCodeToUnicode.h"
#include "UTF.h"

#include <vector>

namespace {

bool isPrivateUseCodePoint(Unicode cp)
{
    return (cp >= 0xE000 && cp <= 0xF8FF) || (cp >= 0xF0000 && cp <= 0xFFFFD) || (cp >= 0x100000 && cp <= 0x10FFFD);
}

bool hasTrustedToUnicodeMapping(const GfxState *state, CharCode c, const Unicode *u, int uLen)
{
    if (state == nullptr || u == nullptr || uLen <= 0) {
        return false;
    }

    const std::shared_ptr<GfxFont> &font = state->getFont();
    if (!font || !font->hasToUnicodeCMap()) {
        return false;
    }

    const CharCodeToUnicode *toUnicode = font->getToUnicode();
    if (toUnicode == nullptr) {
        return false;
    }

    const Unicode *mapped = nullptr;
    const int mappedLen = toUnicode->mapToUnicode(c, &mapped);
    if (mapped == nullptr || mappedLen != uLen) {
        return false;
    }

    for (int i = 0; i < uLen; ++i) {
        if (mapped[i] != u[i]) {
            return false;
        }
    }

    return true;
}

} // namespace

//------------------------------------------------------------------------
// FilteredTextOutputDev — 생성자 / 소멸자
//------------------------------------------------------------------------

FilteredTextOutputDev::FilteredTextOutputDev(const char *fileName,
                                             bool physLayoutA,
                                             double fixedPitchA,
                                             bool rawOrderA,
                                             bool append)
    : TextOutputDev(fileName, physLayoutA, fixedPitchA, rawOrderA, append),
      filteredCharCount(0),
      skippedCharCount(0)
{
}

FilteredTextOutputDev::FilteredTextOutputDev(TextOutputFunc func,
                                             void *stream,
                                             bool physLayoutA,
                                             double fixedPitchA,
                                             bool rawOrderA)
    : TextOutputDev(func, stream, physLayoutA, fixedPitchA, rawOrderA),
      filteredCharCount(0),
      skippedCharCount(0)
{
}

FilteredTextOutputDev::~FilteredTextOutputDev() = default;

//------------------------------------------------------------------------
// isInvalidUnicode
//
// 코드포인트 자체만으로 아래 항목을 '유효하지 않음'으로 처리한다:
//
//   U+0000        Null 문자
//   U+0001~001F   ASCII 제어 문자 (HT/LF/CR 제외)
//   U+007F        ASCII DEL 제어 문자
//   잘못된 유니코드 scalar 값
//   U+FFFD        대체 문자 (변환 실패를 나타내는 마커)
//
// PUA는 코드포인트만으로는 제거하지 않고 drawChar()에서 ToUnicode 매핑을
// 신뢰할 수 있는지 확인한 뒤 제거 여부를 결정한다.
//------------------------------------------------------------------------
bool FilteredTextOutputDev::isInvalidUnicode(Unicode cp)
{
    if (cp == 0x0000) {
        return true;
    }
    if (cp == 0xFFFD) {
        return true;
    }
    if (!UnicodeIsValid(cp)) {
        return true;
    }
    if (cp >= 0x0001 && cp <= 0x001F && cp != 0x0009 && cp != 0x000A && cp != 0x000D) {
        return true;
    }
    if (cp == 0x007F) {
        return true;
    }
    if (cp == 0x00FF) {
        return true;
    }
    return false;
}

//------------------------------------------------------------------------
// drawChar
//
// 글리프 하나가 렌더링될 때 Poppler가 호출하는 콜백.
//
// 처리 순서:
//   1. u[] 배열이 없거나 비어 있으면 → 건너뜀 (유니코드 미변환 글리프)
//   2. 각 코드포인트를 검사하여 유효한 것만 수집
//   3. 유효한 코드포인트가 있으면 부모 TextOutputDev::drawChar() 호출
//   4. 유효한 코드포인트가 없으면 해당 글리프를 출력에서 완전히 제외
//------------------------------------------------------------------------
void FilteredTextOutputDev::drawChar(GfxState *state,
                                     double x, double y,
                                     double dx, double dy,
                                     double originX, double originY,
                                     CharCode c, int nBytes,
                                     const Unicode *u, int uLen)
{
    // u[] 배열이 없거나 비어 있으면 유니코드 매핑 실패 → 건너뜀
    if (u == nullptr || uLen <= 0) {
        ++skippedCharCount;
        return;
    }

    const bool trustedToUnicode = hasTrustedToUnicodeMapping(state, c, u, uLen);

    // 유효한 코드포인트만 수집
    std::vector<Unicode> validU;
    validU.reserve(static_cast<size_t>(uLen));

    for (int i = 0; i < uLen; ++i) {
        const Unicode cp = u[i];
        if (isInvalidUnicode(cp)) {
            continue;
        }

        // ToUnicode가 없는 폰트에서 단독 0xFF가 새어 나오는 경우를 제거한다.
        if (!trustedToUnicode && cp == 0x00FF) {
            continue;
        }

        // PUA는 명시적 ToUnicode 매핑으로 검증된 경우에만 통과시킨다.
        if (isPrivateUseCodePoint(cp) && !trustedToUnicode) {
            continue;
        }

        validU.push_back(cp);
    }

    // 유효한 코드포인트가 하나도 없으면 해당 글리프를 제외
    if (validU.empty()) {
        ++skippedCharCount;
        return;
    }

    // 유효한 코드포인트만 부모 클래스에 전달하여 정상 출력 처리
    ++filteredCharCount;
    TextOutputDev::drawChar(state, x, y, dx, dy, originX, originY,
                            c, nBytes,
                            validU.data(), static_cast<int>(validU.size()));
}
