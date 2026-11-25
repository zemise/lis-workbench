#pragma once

#ifdef _WIN32

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

struct RegularReportState;

constexpr int REGULAR_MANUAL_REVIEW_TAB = 2;
constexpr int REGULAR_IDC_MANUAL_REVIEW_TEMPLATE = 5235;
constexpr int REGULAR_IDC_MANUAL_REVIEW_SAVE = 5236;
constexpr int REGULAR_IDC_MANUAL_REVIEW_TEXT = 5237;
constexpr int REGULAR_IDC_MANUAL_REVIEW_MNEMONIC = 5238;
constexpr int REGULAR_IDC_MANUAL_REVIEW_SAVE_TEMPLATE = 5239;

struct ManualReviewTemplate {
    std::wstring mnemonic;
    std::wstring text;
};

struct RegularManualReviewPage {
    HWND templateLabel = nullptr;
    HWND templates = nullptr;
    HWND save = nullptr;
    HWND text = nullptr;
    HWND mnemonicLabel = nullptr;
    HWND mnemonic = nullptr;
    HWND saveTemplate = nullptr;
    std::string connection;
    std::string reportNumber;
    std::string expectedText;
    std::wstring editorBaseline;
    std::vector<ManualReviewTemplate> templateRows;
};

bool regularLoadManualReviewTemplates(const std::filesystem::path& path,
                                      std::vector<ManualReviewTemplate>& rows);
bool regularAppendManualReviewTemplate(const std::filesystem::path& path,
                                       const ManualReviewTemplate& row);

void regularCreateManualReviewPage(RegularReportState* st);
void regularLayoutManualReviewPage(RegularReportState* st, int width, int height);
void regularShowManualReviewPage(RegularReportState* st, bool show);
void regularSyncManualReviewPage(RegularReportState* st, int index);
bool regularManualReviewCommand(RegularReportState* st, WPARAM command);

#endif
