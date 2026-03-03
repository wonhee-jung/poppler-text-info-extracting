//========================================================================
//
// pdftofilteredtext.cc
//
// PDF 텍스트 추출 도구 — 유효하지 않은 유니코드 자동 필터링 버전.
//
// pdftotext와 동일한 방식으로 동작하되, FilteredTextOutputDev를 사용하여
// 제어 문자·PUA·서로게이트·U+FFFD 등 유효하지 않은 유니코드를 출력에서
// 자동으로 제거한다.
//
// 사용법:
//   pdftofilteredtext [options] <PDF파일> [출력텍스트파일]
//
// 출력텍스트파일을 생략하면 <PDF파일>.txt 로 저장된다.
// 출력텍스트파일로 '-' 를 지정하면 표준 출력(stdout)으로 내보낸다.
//
//========================================================================

// Added by [Wonhee-jung], 2026-02-27
// PDF text extraction with automatic invalid-unicode filtering.
// This file is part of a modified version of Poppler (GPL).

#include "config.h"
#include <poppler-config.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "parseargs.h"
#include "goo/GooString.h"
#include "GlobalParams.h"
#include "PDFDoc.h"
#include "PDFDocFactory.h"
#include "FilteredTextOutputDev.h"
#include "Error.h"
#include "Win32Console.h"

//------------------------------------------------------------------------
// 커맨드라인 옵션
//------------------------------------------------------------------------

static int  firstPage   = 1;
static int  lastPage    = 0;
static bool physLayout  = false;
static double fixedPitch = 0;
static bool rawOrder    = false;
static bool quiet       = false;
static bool printVersion = false;
static bool printHelp   = false;
static char ownerPassword[33] = "\001";
static char userPassword[33]  = "\001";

static const ArgDesc argDesc[] = {
    { "-f",       argInt,    &firstPage,      0,
      "first page to convert" },
    { "-l",       argInt,    &lastPage,       0,
      "last page to convert" },
    { "-layout",  argFlag,   &physLayout,     0,
      "maintain original physical layout" },
    { "-fixed",   argFP,     &fixedPitch,     0,
      "assume fixed-pitch (tabular) text" },
    { "-raw",     argFlag,   &rawOrder,       0,
      "keep strings in content stream order" },
    { "-opw",     argString, ownerPassword, sizeof(ownerPassword),
      "owner password (for encrypted files)" },
    { "-upw",     argString, userPassword,  sizeof(userPassword),
      "user password (for encrypted files)" },
    { "-q",       argFlag,   &quiet,          0,
      "don't print any messages or errors" },
    { "-v",       argFlag,   &printVersion,   0,
      "print copyright and version info" },
    { "-h",       argFlag,   &printHelp,      0,
      "print usage information" },
    { "-help",    argFlag,   &printHelp,      0,
      "print usage information" },
    { "--help",   argFlag,   &printHelp,      0,
      "print usage information" },
    { "-?",       argFlag,   &printHelp,      0,
      "print usage information" },
    {}
};

//------------------------------------------------------------------------
// main
//------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    Win32Console win32Console(&argc, &argv);

    // 커맨드라인 파싱
    const bool ok = parseArgs(argDesc, &argc, argv);

    if (printVersion || !quiet) {
        fprintf(stderr, "pdftofilteredtext version %s\n", PACKAGE_VERSION);
    }
    if (!ok || printHelp || argc < 2 || argc > 3) {
        printUsage("pdftofilteredtext", "<PDF-file> [<text-file>]", argDesc);
        return (printHelp && ok) ? 0 : 99;
    }

    const char *pdfFileName  = argv[1];
    const char *textFileName = (argc >= 3) ? argv[2] : nullptr;

    // globalParams 초기화
    globalParams = std::make_unique<GlobalParams>();
    if (quiet) {
        globalParams->setErrQuiet(true);
    }

    // 비밀번호 처리
    std::optional<GooString> ownerPW, userPW;
    if (ownerPassword[0] != '\001') {
        ownerPW = GooString(ownerPassword);
    }
    if (userPassword[0] != '\001') {
        userPW = GooString(userPassword);
    }

    // PDF 파일 열기
    GooString fileNameStr(pdfFileName);
    auto doc = PDFDocFactory().createPDFDoc(fileNameStr, ownerPW, userPW);

    if (!doc || !doc->isOk()) {
        fprintf(stderr, "Error: could not open '%s'\n", pdfFileName);
        return 1;
    }

#ifdef ENFORCE_PERMISSIONS
    if (!doc->okToCopy()) {
        fprintf(stderr, "Error: copying text from this document is not allowed.\n");
        return 3;
    }
#endif

    // 출력 파일명 결정
    std::string outFileName;
    bool toStdout = false;

    if (textFileName != nullptr) {
        if (strcmp(textFileName, "-") == 0) {
            toStdout = true;
        } else {
            outFileName = textFileName;
        }
    } else {
        // <PDF파일>.txt 로 자동 결정
        outFileName = pdfFileName;
        // 확장자 .pdf 또는 .PDF 제거
        if (outFileName.size() >= 4) {
            const std::string ext = outFileName.substr(outFileName.size() - 4);
            if (ext == ".pdf" || ext == ".PDF") {
                outFileName.resize(outFileName.size() - 4);
            }
        }
        outFileName += ".txt";
    }

    // 페이지 범위 보정
    const int numPages = doc->getNumPages();
    if (lastPage  < 1 || lastPage  > numPages) { lastPage  = numPages; }
    if (firstPage < 1)                          { firstPage = 1;        }
    if (firstPage > lastPage) {
        fprintf(stderr, "Error: first page (%d) is after last page (%d)\n",
                firstPage, lastPage);
        return 99;
    }

    // FilteredTextOutputDev 생성
    std::unique_ptr<FilteredTextOutputDev> textOut;
    if (toStdout) {
        // stdout 출력: 콜백 방식
        auto writeToStdout = [](void * /*stream*/, const char *text, int len) {
            fwrite(text, 1, len, stdout);
        };
        textOut = std::make_unique<FilteredTextOutputDev>(
            writeToStdout, nullptr, physLayout, fixedPitch, rawOrder);
    } else {
        textOut = std::make_unique<FilteredTextOutputDev>(
            outFileName.c_str(), physLayout, fixedPitch, rawOrder, /*append=*/false);
    }

    if (!textOut->isOk()) {
        fprintf(stderr, "Error: could not open output file '%s'\n", outFileName.c_str());
        return 2;
    }

    // 페이지 렌더링
    for (int page = firstPage; page <= lastPage; ++page) {
        doc->displayPage(textOut.get(),
                         page,
                         72.0,   // hDPI
                         72.0,   // vDPI
                         0,      // rotate
                         true,   // useMediaBox
                         false,  // crop
                         false); // printing
    }

    // 통계 출력 (quiet 모드가 아닐 때)
    if (!quiet) {
        fprintf(stderr,
                "Pages: %d-%d  |  Passed: %d chars  |  Skipped (invalid unicode): %d chars\n",
                firstPage, lastPage,
                textOut->getFilteredCharCount(),
                textOut->getSkippedCharCount());

        if (!toStdout) {
            fprintf(stderr, "Output written to: %s\n", outFileName.c_str());
        }
    }

    return 0;
}
