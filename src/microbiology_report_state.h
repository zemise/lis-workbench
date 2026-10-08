#pragma once

#ifdef _WIN32

#include "module_registry.h"
#include "search_app.h"
#include "search_core.h"
#include "window_task.h"

#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>

#include <memory>
#include <string>
#include <vector>

// ============================================================================
// IDs and dimensions (shared across all microbiology_report_* files)
// ============================================================================

constexpr const wchar_t* MICROBIOLOGY_REPORT_WND_CLASS = L"MicrobiologyReportChild";
constexpr const wchar_t* MICROBIOLOGY_REPORT_PICTURE_POPUP_CLASS = L"MicrobiologyReportPicturePopup";
constexpr const wchar_t* MICROBIOLOGY_REPORT_WINDOW_TITLE = L"微生物报告";
constexpr const wchar_t* MICROBIOLOGY_REPORT_PROP_STATE = L"MicrobiologyReportSt";
constexpr const wchar_t* MICROBIOLOGY_REPORT_PROP_DATE_FORMAT = L"MicrobiologyReportDateFormat";
constexpr const wchar_t* MICROBIOLOGY_REPORT_BLANK_DATE_FORMAT = L" ";

constexpr UINT_PTR IDT_MICROBIOLOGY_REPORT_INITIAL_QUICK_MACHINE = 6311;

constexpr int MICROBIOLOGY_IDC_RESULT_LIST = 5201;
constexpr int MICROBIOLOGY_IDC_REPORT_LIST = 5202;
constexpr int MICROBIOLOGY_IDC_SPLITTER = 5204;
constexpr int MICROBIOLOGY_IDC_LEFT_SCROLL = 5205;
constexpr int MICROBIOLOGY_IDC_RIGHT_TAB = 5206;
constexpr int MICROBIOLOGY_IDC_MIDDLE_TAB = 5207;
constexpr int MICROBIOLOGY_IDC_MACHINE_PICKER_BUTTON = 5208;
constexpr int MICROBIOLOGY_IDC_MACHINE_PICKER_ROOM = 5209;
constexpr int MICROBIOLOGY_IDC_MACHINE_PICKER_MACH = 5210;
constexpr int MICROBIOLOGY_IDC_MACHINE_PICKER_SEARCH = 5219;
constexpr int MICROBIOLOGY_IDC_INSPECT_DATE = 5211;
constexpr int MICROBIOLOGY_IDC_REPORT_FIRST_BUTTON = 5212;
constexpr int MICROBIOLOGY_IDC_REPORT_LAST_BUTTON = 5213;
constexpr int MICROBIOLOGY_IDC_BOTTOM_MACHINE_1 = 5401;
constexpr int MICROBIOLOGY_IDC_BOTTOM_REFRESH = 5402;
constexpr int MICROBIOLOGY_IDC_BOTTOM_PRINT_BARCODE = 5406;
constexpr int MICROBIOLOGY_IDC_BOTTOM_MACHINE_2 = 5411;
constexpr int MICROBIOLOGY_IDC_BOTTOM_BATCH_PRINT_BARCODE = 5416;
constexpr int MICROBIOLOGY_IDC_BOTTOM_MACHINE_3 = 5420;
constexpr int MICROBIOLOGY_IDC_BOTTOM_GRAPH = 5424;
constexpr int MICROBIOLOGY_IDC_BOTTOM_PREV_REPORT = 5408;
constexpr int MICROBIOLOGY_IDC_BOTTOM_NEXT_REPORT = 5409;
constexpr int MICROBIOLOGY_IDM_REPORT_PRINT_BARCODE = 5220;
constexpr int MICROBIOLOGY_IDM_REPORT_PRINT_CHECKED_BARCODES = 5221;
constexpr int MICROBIOLOGY_IDM_REPORT_TREND = 5222;
constexpr int MICROBIOLOGY_IDC_SUBMIT_DATE_START = 5223;
constexpr int MICROBIOLOGY_IDC_SUBMIT_DATE_END = 5224;
constexpr int MICROBIOLOGY_IDC_REVIEW_PENDING = 5225;
constexpr int MICROBIOLOGY_IDC_REVIEWED = 5226;
constexpr int MICROBIOLOGY_IDC_BOTTOM_TREND = 5425;

constexpr int MICROBIOLOGY_REPORT_COLUMN_COUNT = 30;
constexpr int MICROBIOLOGY_RESULT_VALUE_COL = 5;
constexpr int MICROBIOLOGY_RIGHT_REPORT_PRINT_COL = 8;

constexpr UINT_PTR MICROBIOLOGY_LEFT_PANEL_SUBCLASS = 6205;
constexpr UINT_PTR MICROBIOLOGY_LEFT_CONTENT_SUBCLASS = 6206;
constexpr UINT_PTR MICROBIOLOGY_RIGHT_PANEL_SUBCLASS = 6207;
constexpr UINT_PTR MICROBIOLOGY_MIDDLE_PANEL_SUBCLASS = 6208;
constexpr UINT_PTR MICROBIOLOGY_BOTTOM_PANEL_SUBCLASS = 6209;
constexpr UINT_PTR MICROBIOLOGY_SAMPLE_INPUT_SUBCLASS = 6210;
constexpr UINT_PTR MICROBIOLOGY_PICTURE_VIEW_SUBCLASS = 6211;
constexpr UINT_PTR MICROBIOLOGY_PICTURE_VIEWPORT_SUBCLASS = 6212;
constexpr UINT_PTR MICROBIOLOGY_RESULT_EDIT_SUBCLASS = 6213;
constexpr UINT_PTR MICROBIOLOGY_RESULT_LIST_SUBCLASS = 6214;
constexpr UINT_PTR MICROBIOLOGY_LEFT_TAB_SUBCLASS = 6215;
constexpr UINT_PTR MICROBIOLOGY_MACHINE_PICKER_SEARCH_SUBCLASS = 6216;

constexpr int MICROBIOLOGY_LEFT_CONTENT_HEIGHT = 875;
constexpr int MICROBIOLOGY_LEFT_SCROLL_STEP = 36;
constexpr int MICROBIOLOGY_LEFT_PANEL_MIN_W = 400;
constexpr int MICROBIOLOGY_LEFT_PANEL_MAX_W = 400;
constexpr int MICROBIOLOGY_PAD = 8;
constexpr int MICROBIOLOGY_GAP = 6;
constexpr int MICROBIOLOGY_SPLITTER_W = 8;
constexpr int MICROBIOLOGY_TAB_H = 30;
constexpr int MICROBIOLOGY_COMPACT_BUTTON_H = 28;
constexpr int MICROBIOLOGY_BOTTOM_PANEL_H = 102;
constexpr int MICROBIOLOGY_MIDDLE_TOOLBAR_Y = 32;
constexpr int MICROBIOLOGY_MIDDLE_LIST_Y = 66;
constexpr int MICROBIOLOGY_MIDDLE_LIST_BOTTOM_MARGIN = 88;
constexpr int MICROBIOLOGY_MIDDLE_STATUS_BOTTOM = 22;
constexpr int MICROBIOLOGY_MIDDLE_STATUS_H = 18;
constexpr int MICROBIOLOGY_PICTURE_FIXED_W = 1560;
constexpr int MICROBIOLOGY_PICTURE_FIXED_H = 1050;
constexpr int MICROBIOLOGY_PICTURE_POPUP_DEFAULT_W = 980;
constexpr int MICROBIOLOGY_PICTURE_POPUP_DEFAULT_H = 700;
constexpr int MICROBIOLOGY_PICTURE_POPUP_MIN_W = 360;
constexpr int MICROBIOLOGY_PICTURE_POPUP_MIN_H = 260;

constexpr int MICROBIOLOGY_QUICK_MACHINE_COUNT = 3;
constexpr int MICROBIOLOGY_RIGHT_FILTER_CONTROL_Y = 56;
constexpr int MICROBIOLOGY_RIGHT_TAB_Y = 90;
constexpr int MICROBIOLOGY_RIGHT_SEARCH_LABEL_Y = 128;
constexpr int MICROBIOLOGY_RIGHT_SEARCH_CONTROL_Y = 124;
constexpr int MICROBIOLOGY_RIGHT_LIST_Y = 162;

constexpr const wchar_t* MICROBIOLOGY_MACHINE_PICKER_CLASS = L"MicrobiologyReportMachinePicker";
constexpr int MICROBIOLOGY_MACHINE_PICKER_CLIENT_W = 644;
constexpr int MICROBIOLOGY_MACHINE_PICKER_INITIAL_H = 224;
constexpr int MICROBIOLOGY_MACHINE_PICKER_INPUT_X = 10;
constexpr int MICROBIOLOGY_MACHINE_PICKER_SEARCH_LABEL_Y = 14;
constexpr int MICROBIOLOGY_MACHINE_PICKER_SEARCH_Y = 10;
constexpr int MICROBIOLOGY_MACHINE_PICKER_SEARCH_LABEL_W = 66;
constexpr int MICROBIOLOGY_MACHINE_PICKER_ROOM_Y = 44;
constexpr int MICROBIOLOGY_MACHINE_PICKER_LIST_Y = 76;
constexpr int MICROBIOLOGY_MACHINE_PICKER_LIST_W = 618;
constexpr int MICROBIOLOGY_MACHINE_PICKER_CODE_COL_W = 64;
constexpr int MICROBIOLOGY_MACHINE_PICKER_PY_COL_W = 80;
constexpr int MICROBIOLOGY_MACHINE_PICKER_GROUP_CODE_COL_W = 74;
constexpr int MICROBIOLOGY_MACHINE_PICKER_GROUP_NAME_COL_W = 150;
constexpr int MICROBIOLOGY_MACHINE_PICKER_SAMPLE_COL_W = 66;
constexpr int MICROBIOLOGY_MACHINE_PICKER_COL_GAP = 8;
constexpr int MICROBIOLOGY_MACHINE_PICKER_NAME_COL_W =
    MICROBIOLOGY_MACHINE_PICKER_LIST_W - MICROBIOLOGY_MACHINE_PICKER_CODE_COL_W -
    MICROBIOLOGY_MACHINE_PICKER_GROUP_CODE_COL_W - MICROBIOLOGY_MACHINE_PICKER_GROUP_NAME_COL_W -
    MICROBIOLOGY_MACHINE_PICKER_SAMPLE_COL_W - MICROBIOLOGY_MACHINE_PICKER_PY_COL_W -
    MICROBIOLOGY_MACHINE_PICKER_COL_GAP;
constexpr int MICROBIOLOGY_MACHINE_PICKER_COMBO_DROP_H = 180;
constexpr int MICROBIOLOGY_MACHINE_PICKER_INITIAL_LIST_H = 92;
constexpr int MICROBIOLOGY_MACHINE_PICKER_MIN_ROWS = 3;
constexpr int MICROBIOLOGY_MACHINE_PICKER_MAX_ROWS = 8;
constexpr int MICROBIOLOGY_MACHINE_PICKER_HEADER_H = 28;
constexpr int MICROBIOLOGY_MACHINE_PICKER_BOTTOM_PAD = 10;
constexpr int MICROBIOLOGY_MACHINE_PICKER_LIST_EXTRA_H = 6;

constexpr COLORREF MICROBIOLOGY_COLOR_WHITE = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF MICROBIOLOGY_COLOR_RESULT_SIDE_BG = RGB(0xF0, 0xF0, 0xF0);
constexpr COLORREF MICROBIOLOGY_COLOR_REPORT_REVIEWED = RGB(0x6F, 0x94, 0xE6);
constexpr COLORREF MICROBIOLOGY_COLOR_REPORT_SENT = RGB(0x98, 0xBB, 0x8F);
constexpr COLORREF MICROBIOLOGY_COLOR_CRITICAL_PENDING = RGB(0xFA, 0xC0, 0xCB);
constexpr COLORREF MICROBIOLOGY_COLOR_CRITICAL_FINAL = RGB(0xFF, 0xFF, 0x39);

// ============================================================================
// Shared structs
// ============================================================================

struct MicrobiologyColumnDef {
    int index;
    const wchar_t* title;
    int width;
};

struct MicrobiologyButtonDef {
    int id;
    const wchar_t* text;
};

// Drawn on leftContent instead of real GROUPBOX windows so sibling clipping can
// be enabled on the child controls without the group frame hiding them.
struct MicrobiologyGroupFrame {
    RECT rect{};
    const wchar_t* title = L"";
};

struct MicrobiologyRightHeaderLayout {
    RECT line1{};
    RECT line2{};
    int bottom = 0;
};

struct MicrobiologyAgeDisplayParts {
    std::string value;
    std::string unit = "岁";
};

// ============================================================================
// Main state structures
// ============================================================================

struct MicrobiologyReportState;

struct MicrobiologyMachinePickerState {
    MicrobiologyReportState* report = nullptr;
    HWND searchEdit = nullptr;
    HWND roomCombo = nullptr;
    HWND machineList = nullptr;
    std::vector<search::RoomOption> rooms;
    std::vector<search::MachineOption> allMachines;
    std::vector<search::MachineOption> machines;
    bool closePosted = false;
    bool syncingRoom = false;
    bool roomChosenByUser = false;
    bool refreshingMachines = false;
};

struct MicrobiologyReportLoadResult {
    int generation = 0;
    bool ok = false;
    bool preserveState = false;
    std::vector<search::ReportRow> rows;
    std::string connectionString;
    std::string queryDate;
    std::string error;
};

struct MicrobiologyResultLoadResult {
    int generation = 0;
    bool ok = false;
    std::vector<search::ResultRow> rows;
    std::string error;
};

struct MicrobiologyPictureLoadResult {
    int generation = 0;
    bool ok = false;
    std::vector<unsigned char> picture;
    std::string error;
};

struct MicrobiologyPicturePopupLoadResult {
    int generation = 0;
    bool ok = false;
    std::vector<unsigned char> picture;
    std::string error;
};

struct MicrobiologyPicturePopupState {
    MicrobiologyReportState* owner = nullptr;
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    IStream* stream = nullptr;
    Gdiplus::Image* image = nullptr;
    ULONG_PTR gdiplusToken = 0;
    bool gdiplusReady = false;
    bool loading = false;
    app::WindowTask pictureTask;
    int generation = 0;
    std::string repNo;
    std::wstring status;
};

struct MicrobiologyReportState {
    ModuleContext ctx;
    HWND hwnd = nullptr;
    HWND leftPanel = nullptr;
    HWND leftContent = nullptr;
    HWND leftScrollBar = nullptr;
    HWND middlePanel = nullptr;
    HWND rightPanel = nullptr;
    HWND bottomPanel = nullptr;
    HWND splitter = nullptr;
    HWND middleTab = nullptr;
    HWND rightTab = nullptr;
    HWND rightSubmitTimeLabel = nullptr;
    HWND rightSubmitStartPicker = nullptr;
    HWND rightSubmitRangeSeparator = nullptr;
    HWND rightSubmitEndPicker = nullptr;
    HWND rightPendingRadio = nullptr;
    HWND rightReviewedRadio = nullptr;
    HWND rightSearchLabel = nullptr;
    HWND rightSearchEdit = nullptr;
    HWND rightSearchIndexButton = nullptr;
    HWND rightSearchUpButton = nullptr;
    HWND rightSearchDownButton = nullptr;
    HWND rightSearchMenuButton = nullptr;
    HWND machineEdit = nullptr;
    HWND machinePickerButton = nullptr;
    HWND machinePickerPopup = nullptr;
    HWND groupEdit = nullptr;
    HWND sampleEdit = nullptr;
    HWND reportNoEdit = nullptr;
    HWND operNoEdit = nullptr;
    HWND patientTypeCombo = nullptr;
    HWND urgentCheck = nullptr;
    HWND urgentLabel = nullptr;
    HWND urgentEdit = nullptr;
    HWND barcodeEdit = nullptr;
    HWND regNoEdit = nullptr;
    HWND patientNameEdit = nullptr;
    HWND sexEdit = nullptr;
    HWND ageEdit = nullptr;
    HWND ageUnitCombo = nullptr;
    HWND bedEdit = nullptr;
    HWND phoneEdit = nullptr;
    HWND deptEdit = nullptr;
    HWND diagEdit = nullptr;
    HWND reqDoctorEdit = nullptr;
    HWND feeEdit = nullptr;
    HWND testerEdit = nullptr;
    HWND auditEdit = nullptr;
    HWND noteCodeEdit = nullptr;
    HWND noteEdit = nullptr;
    HWND applyDatePicker = nullptr;
    HWND receiveDatePicker = nullptr;
    HWND machineDatePicker = nullptr;
    HWND reportDatePicker = nullptr;
    HWND inspectDatePicker = nullptr;
    HWND collectDateEdit = nullptr;
    HWND resultList = nullptr;
    HWND resultEdit = nullptr;
    HWND pictureViewport = nullptr;
    HWND pictureView = nullptr;
    HWND pictureHScroll = nullptr;
    HWND pictureVScroll = nullptr;
    HWND picturePopup = nullptr;
    HWND reportList = nullptr;
    HWND status = nullptr;
    int splitterX = 0;
    int pendingSplitterX = 0;
    int leftScrollY = 0;
    int pictureScrollX = 0;
    int pictureScrollY = 0;
    int resultEditRow = -1;
    int leftContentHeight = MICROBIOLOGY_LEFT_CONTENT_HEIGHT;
    bool splitterUserSet = false;
    int reportQueryGeneration = 0;
    int resultQueryGeneration = 0;
    int pictureQueryGeneration = 0;
    int selectedReportIndex = -1;
    bool reportQueryLoading = false;
    bool resultQueryLoading = false;
    bool pictureQueryLoading = false;
    app::WindowTask reportQueryTask;
    app::WindowTask resultQueryTask;
    app::WindowTask pictureQueryTask;
    app::WindowTask barcodePrintTask;
    bool initialQuickMachineTimerActive = false;
    bool skipInitialQuickMachineLoad = false;
    bool suppressInspectDateQuery = false;
    bool suppressRightFilterQuery = false;
    bool suppressReportSelectionQuery = false;
    int reportSortColumn = -1;
    bool reportSortAscending = true;
    std::vector<HWND> leftControls;
    std::vector<HWND> leftTabControls;
    std::vector<MicrobiologyGroupFrame> leftGroupFrames;
    std::vector<HWND> middleResultControls;
    std::vector<HWND> middlePictureControls;
    std::vector<HWND> rightInfoControls;
    HBRUSH bgBrush = nullptr;
    HBRUSH panelBrush = nullptr;
    HBRUSH blackBrush = nullptr;
    HFONT groupTitleFont = nullptr;
    std::string selectedMachineCode;
    std::string selectedRoomCode;
    bool machinePickerCacheLoaded = false;
    std::string machinePickerCacheConnectionString;
    std::vector<search::RoomOption> cachedMachinePickerRooms;
    std::vector<search::MachineOption> cachedMachinePickerMachines;
    std::string reportConnectionString;
    std::string reportQueryDate;
    bool pendingOpenReport = false;
    std::string pendingOpenRepNo;
    std::string pendingOpenOperNo;
    std::vector<search::ReportRow> reportRows;
    std::vector<search::ResultRow> resultRows;
    std::vector<std::string> highlightItemCodes;
    std::string highlightReportRepNo;
    std::wstring pictureStatus;
    std::string pictureRepNo;
    IStream* pictureStream = nullptr;
    Gdiplus::Image* pictureImage = nullptr;
    ULONG_PTR gdiplusToken = 0;
    bool gdiplusReady = false;
    int contextReportIndex = -1;
};

// ============================================================================
// Forward declarations for cross-file functions
// ============================================================================

// utils
int microbiologyS(HWND hwnd, int value);
HWND microbiologyMakeStatic(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD style = SS_LEFT);
HWND microbiologyMakeEdit(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD extra = ES_AUTOHSCROLL);
HWND microbiologyMakeButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h);
HWND microbiologyMakeCombo(HWND parent, const wchar_t* text, int x, int y, int w, int h);
HWND microbiologyMakeDatePicker(HWND parent, int x, int y, int w, int h, const wchar_t* format, const SYSTEMTIME* value, int id = 0);
HWND microbiologyAddClipSiblings(HWND hwnd);

SYSTEMTIME microbiologyTodayDate();
SYSTEMTIME microbiologyNormalizeDate(SYSTEMTIME st);
SYSTEMTIME microbiologyAddDays(SYSTEMTIME st, int days);

std::string microbiologyDatePickerValue(HWND hwnd);
SYSTEMTIME microbiologyDatePickerSystemTime(HWND hwnd);
std::string microbiologySlashDate(const std::string& value);
std::string microbiologySlashDateTimeMinute(const std::string& value);
bool microbiologyParseDateTimeText(const std::string& value, SYSTEMTIME& out);

void microbiologySetControlText(HWND hwnd, const std::string& text);
std::wstring microbiologyWindowText(HWND hwnd);
void microbiologySetComboSingleText(HWND hwnd, const std::string& text);
MicrobiologyAgeDisplayParts microbiologySplitAgeDisplayText(const std::string& text);
void microbiologyFillAgeUnitCombo(HWND combo, const std::string& selectedUnit = "岁");
void microbiologySetDatePickerValue(HWND hwnd, const std::string& text);
void microbiologyClearDatePickerValue(HWND hwnd);
void microbiologySetControlsEnabled(bool enabled, std::initializer_list<HWND> controls);

void microbiologyApplyFont(HWND hwnd, HFONT font);
HFONT microbiologyCreateBoldFont(HFONT base);
void microbiologyRefreshLeftGroupTitleFont(MicrobiologyReportState* st);
int microbiologyTextLogicalWidth(HWND hwnd, HFONT font, const wchar_t* text);
int microbiologyFontLogicalHeight(HWND hwnd, HFONT font);
int microbiologyWrappedTextHeightPx(HWND hwnd, HFONT font, const wchar_t* text, int widthPx, int minHeightPx);
MicrobiologyRightHeaderLayout microbiologyRightHeaderLayout(HWND hwnd, HFONT font, int panelWidthPx,
                                            const std::wstring& summaryLine1,
                                            const std::wstring& summaryLine2);
int microbiologyLeftLabelWidth(HWND hwnd, HFONT font);
int microbiologyRightLabelWidth(HWND hwnd, HFONT font);

// combos
void microbiologyComboReset(HWND combo);
void microbiologyComboAdd(HWND combo, const std::wstring& text);
void microbiologyComboSelectFirst(HWND combo);

std::wstring microbiologyRightSummaryLine1(const MicrobiologyReportState* st);
std::wstring microbiologyRightSummaryLine2(const MicrobiologyReportState* st);

bool microbiologyQuickMachineMatchesCurrent(const MicrobiologyReportState* st, int slot);
void microbiologyUpdateQuickMachineButtonLabels(MicrobiologyReportState* st);
bool microbiologyIsAllowedMachineCode(const std::string& machineCode);

// panels
void microbiologyCreateControls(HWND hwnd, MicrobiologyReportState* st);
void microbiologyLayout(HWND hwnd, MicrobiologyReportState* st);
void microbiologyClearLeftPanel(MicrobiologyReportState* st);
void microbiologySeedLists(MicrobiologyReportState* st);

// picture
void microbiologyClearPictureView(MicrobiologyReportState* st, const std::wstring& status);
void microbiologyQuerySelectedPicture(MicrobiologyReportState* st, int selected);
void microbiologyUpdatePictureViewport(MicrobiologyReportState* st);
void microbiologyOpenPicturePopupForSelection(MicrobiologyReportState* st);
void microbiologyScrollPictureViewport(MicrobiologyReportState* st, int targetX, int targetY);

// queries
void microbiologyRunReportQuery(MicrobiologyReportState* st, bool preserveState = false);
void microbiologyRunAutoRefreshQuery(MicrobiologyReportState* st);
void microbiologyQuerySelectedResults(MicrobiologyReportState* st, int selected);

// data
void microbiologyPopulateLeftPanelFromReport(MicrobiologyReportState* st, int selected);
void microbiologySortReportRowsByColumn(MicrobiologyReportState* st, int column);
void microbiologyBeginResultEdit(MicrobiologyReportState* st, int row);
void microbiologyFinishResultEdit(MicrobiologyReportState* st, bool commit, bool moveNext = false,
                              bool restoreListFocus = true);
void microbiologySelectReportRow(MicrobiologyReportState* st, int index);
void microbiologySelectAdjacentReportRow(MicrobiologyReportState* st, int delta);
bool microbiologyHasSelectedReportRow(const MicrobiologyReportState* st);
int microbiologyCurrentReportIndex(const MicrobiologyReportState* st);
void microbiologyShowReportContextMenu(MicrobiologyReportState* st, const NMITEMACTIVATE* item);
std::vector<int> microbiologyCheckedReportIndexes(const MicrobiologyReportState* st);
void microbiologyClearReportChecks(MicrobiologyReportState* st);
const search::ReportRow* microbiologyContextReportRow(const MicrobiologyReportState* st);

// barcode
std::wstring microbiologyPrintBarcodeForContext(MicrobiologyReportState* st);
std::wstring microbiologyPrintCheckedBarcodes(MicrobiologyReportState* st);
void microbiologyShowBatchBarcodeDialog(MicrobiologyReportState* st);
void microbiologyShowTrendForContext(MicrobiologyReportState* st);

// queries
search::QueryInput microbiologyBuildReportQueryInput(MicrobiologyReportState* st);
bool microbiologyInspectDateMatchesCurrentQuery(const MicrobiologyReportState* st);
void microbiologySetInspectDateAndQuery(MicrobiologyReportState* st, SYSTEMTIME date,
                                   bool preserveWhenPossible = false);
void microbiologyApplyQuickMachine(MicrobiologyReportState* st, int slot);
void microbiologyFinishReportQuery(MicrobiologyReportState* st, HWND hwnd,
                               std::unique_ptr<MicrobiologyReportLoadResult> result);
void microbiologyFinishResultQuery(MicrobiologyReportState* st, HWND hwnd,
                               std::unique_ptr<MicrobiologyResultLoadResult> result);
void microbiologyFinishPictureQuery(MicrobiologyReportState* st, HWND hwnd,
                                std::unique_ptr<MicrobiologyPictureLoadResult> result);
void microbiologyPresentResultRows(MicrobiologyReportState* st);
void microbiologyPresentReportRows(MicrobiologyReportState* st,
                               const std::vector<search::ReportRow>* previousRows = nullptr);

// color helpers (used by panels' NM_CUSTOMDRAW)
bool microbiologyReportIsSent(const search::ReportRow& report);
bool microbiologyReportIsReviewed(const search::ReportRow& report);
bool microbiologyReportIsCriticalReport(const search::ReportRow& report);
bool microbiologyReportUsesEmergencyTextColor(const search::ReportRow& report);
bool microbiologyReportHasBarcodeEmergencyLabel(const search::ReportRow& report);
COLORREF microbiologyReportRowColor(const MicrobiologyReportState* st, int row);
COLORREF microbiologyReportPrintCellColor(const MicrobiologyReportState* st, int row);
COLORREF microbiologyResultTextColor(const search::ResultRow& row);
bool microbiologyResultRowHasCriticalValue(const search::ResultRow& row);
bool microbiologyListViewRowSelected(HWND list, int row);
bool microbiologyCustomDrawListSelection(NMLVCUSTOMDRAW* cd, HWND list, int row);
void microbiologyRedrawSelectedListRow(HWND list);

// picture sub-procs (defined in picture.cpp, used by panels)
LRESULT CALLBACK microbiologyPictureViewProc(HWND hwnd, UINT msg, WPARAM wp,
                                             LPARAM lp, UINT_PTR subclassId,
                                             DWORD_PTR data);
LRESULT CALLBACK microbiologyPictureViewportProc(HWND hwnd, UINT msg, WPARAM wp,
                                                 LPARAM lp, UINT_PTR subclassId,
                                                 DWORD_PTR data);
int microbiologyScrollTargetFromCode(HWND hwnd, int code, const SCROLLINFO& si, int current);

// sample input
void microbiologySelectReportRowBySampleInput(MicrobiologyReportState* st);

#endif
