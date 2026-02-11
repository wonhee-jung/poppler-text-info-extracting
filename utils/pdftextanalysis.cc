//========================================================================
//
// pdftextanalysis.cc
//
// PDF text extraction analysis tool
// Determines whether text can be reliably extracted from each page
//
//========================================================================

// Modified by [Wonhee-jung], 2026-02-11
// Added text layout analysis functionality.
// I annotated for original feature in the code.
// This file is part of a modified version of Poppler (GPL).

#include "config.h"
#include <poppler-config.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "goo/GooString.h"
#include "GlobalParams.h"
#include "PDFDoc.h"
#include "PDFDocFactory.h"
#include "TextAnalysisOutputDev.h"
#include "Error.h"

static void printUsage()
{
    fprintf(stderr,
            "Usage: pdftextanalysis [options] <PDF-file>\n"
            "  -v       Print version info\n"
            "  -h       Print usage information\n");
}

int main(int argc, char *argv[])
{
    std::unique_ptr<PDFDoc> doc;
    const char *pdfFileName = nullptr;
    bool printVersion = false;
    bool printHelp = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) {
            printVersion = true;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "-help") || !strcmp(argv[i], "--help")) {
            printHelp = true;
        } else if (argv[i][0] != '-') {
            pdfFileName = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage();
            return 99;
        }
    }

    if (printVersion) {
        fprintf(stderr, "pdftextanalysis version %s\n", PACKAGE_VERSION);
        return 0;
    }

    if (printHelp) {
        printUsage();
        return 0;
    }

    if (!pdfFileName) {
        fprintf(stderr, "Error: No PDF file specified\n");
        printUsage();
        return 99;
    }

    // Initialize global parameters
    globalParams = std::make_unique<GlobalParams>();

    // Open PDF file
    GooString fileName(pdfFileName);
    doc = PDFDocFactory().createPDFDoc(fileName, std::optional<GooString>(), std::optional<GooString>());

    // Check if document opened successfully
    if (!doc || !doc->isOk()) {
        std::cout << TextAnalysisOutputDev::toErrorJSON("PDF_OPEN_FAILED");
        return 1;
    }

#ifdef ENFORCE_PERMISSIONS
    // Check for copy permission (text extraction requires copy permission)
    if (!doc->okToCopy()) {
        std::cout << TextAnalysisOutputDev::toErrorJSON("PDF_PERMISSION_DENIED");
        return 3;
    }
#endif

    // Create analysis output device
    TextAnalysisOutputDev analysisOut;

    // Process all pages
    // Note: Poppler is compiled without exception handling
    // Fatal errors during displayPage will terminate the program
    int numPages = doc->getNumPages();
    for (int i = 1; i <= numPages; ++i) {
        doc->displayPage(&analysisOut, i, 72.0, // hDPI
                         72.0,                  // vDPI
                         0,                     // rotate
                         true,                  // useMediaBox
                         false,                 // crop
                         false);                // printing
    }

    // Output JSON results
    std::cout << analysisOut.toJSON();

    return 0;
}
