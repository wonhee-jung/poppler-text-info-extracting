# Modification Record (변경 이력)

## Notes
**“This repository is a modified version of Poppler.”**

이 문서는 원본 Poppler 소스 코드에 가해진 주요 변경 사항을 기록합니다.

## 프로젝트 정보
- **기반 버전**: Poppler 26.02.90
- **수정 날짜**: 2026-02-11
- **수정 목적**: PDF 텍스트 레이아웃의 정밀 분석 기능 추가

## 상세 변경 내역

### 1. Poppler Core (`poppler/`)
- **`TextOutputDev.h` / `TextOutputDev.cc`**: 
  - 기능 분석을 위해 주석 추가
- **`TextAnalysisOutputDev.h` / `TextAnalysisOutputDev.cc` [신규]**:
  - 텍스트의 기하학적 정보(좌표, 크기)와 폰트 메타데이터를 정밀하게 캡처하기 위한 전용 출력 디바이스 클래스를 구현

### 2. Utilities (`utils/`)
- **`pdftextanalysis.cc` [신규]**:
  - `TextAnalysisOutputDev`를 사용하여 PDF를 분석하고 결과를 출력하는 새로운 실행 도구의 메인 로직입니다.
- **`pdftextanalysis.1` [신규]**:
  - `pdftextanalysis` 도구에 대한 매뉴얼 페이지입니다.
- **`CMakeLists.txt`**:
  - 새로운 유틸리티 `pdftextanalysis`가 빌드 대상에 포함되도록 설정을 추가하였습니다.

### 3. Build System
- **`CMakeLists.txt` (Root)**:
  - `poppler_SRCS` 목록에 `poppler/TextAnalysisOutputDev.cc`를 추가하여 라이브러리 빌드 시 포함되도록 수정하였습니다.

## GPL 준수 고지
본 수정본은 GPL 라이선스 조항에 따라, 수정된 모든 소스 코드를 원본과 동일한 조건으로 공개합니다. 변경 사항은 Git 커밋 로그를 통해 파일 단위로 상세히 확인할 수 있습니다.
