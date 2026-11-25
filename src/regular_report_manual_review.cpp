#include "regular_report_manual_review.h"

#ifdef _WIN32

#include "app_settings_io.h"
#include "log.h"
#include "regular_report_state.h"
#include "search_text.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <shlwapi.h>
#include <xmllite.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>

namespace {
struct ReleaseCom {
    void operator()(IUnknown* object) const { object->Release(); }
};
template <typename T>
using ComPtr = std::unique_ptr<T, ReleaseCom>;

bool decodeLegacyXml(std::string& bytes) {
    if (bytes.compare(0, 5, "<?xml") != 0) return true;
    const auto end = bytes.find("?>");
    if (end == std::string::npos) return true;
    constexpr std::string_view encodingAttribute = "encoding";
    const auto encoding = bytes.find(encodingAttribute);
    if (encoding == std::string::npos || encoding >= end) return true;
    const auto first = bytes.find_first_of("\"'", encoding + encodingAttribute.size());
    if (first == std::string::npos || first >= end) return true;
    const auto last = bytes.find(bytes[first], first + 1);
    if (last == std::string::npos || last >= end) return true;
    auto name = bytes.substr(first + 1, last - first - 1);
    for (auto& ch : name)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    const bool usesCodePage936 = name == "gb2312" || name == "gbk" || name == "windows-936" || name == "cp936";
    const UINT codePage = name == "gb18030" ? 54936 : (usesCodePage936 ? 936 : 0);
    if (codePage == 0) return true;

    // XmlLite does not natively support Chinese ANSI encodings.
    bytes.replace(first + 1, last - first - 1, "utf-8");
    if (bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
    const int size = static_cast<int>(bytes.size());
    const int count = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes.data(), size, nullptr, 0);
    if (count == 0) return false;
    std::wstring utf16(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes.data(), size, utf16.data(), count) != count)
        return false;
    bytes = search::wide_to_utf8(utf16);
    return !bytes.empty();
}

bool readAttribute(IXmlReader* reader, const wchar_t* name, std::wstring& text) {
    const wchar_t* value = nullptr;
    UINT length = 0;
    if (reader->MoveToAttributeByName(name, nullptr) != S_OK ||
        reader->GetValue(&value, &length) != S_OK) return false;
    text.assign(value, length);
    return reader->MoveToElement() == S_OK;
}

std::wstring trimText(std::wstring_view text) {
    constexpr std::wstring_view whitespace =
        L" \t\r\n\v\f\u0085\u00a0\u1680\u2000\u2001\u2002\u2003\u2004"
        L"\u2005\u2006\u2007\u2008\u2009\u200a\u2028\u2029\u202f\u205f\u3000";
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::wstring_view::npos) return {};
    return std::wstring(text.substr(first, text.find_last_not_of(whitespace) - first + 1));
}

std::wstring convertNewlines(std::wstring_view text, std::wstring_view newline) {
    std::wstring result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r' || text[i] == L'\n') {
            if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') ++i;
            result += newline;
        } else {
            result += text[i];
        }
    }
    return result;
}

std::filesystem::path templatePath() {
    const std::filesystem::path install(search::trasen_install_directory());
    std::error_code error;
    if (install.is_absolute() && std::filesystem::is_directory(install, error))
        return install / L"sgfc.xml";
    return std::filesystem::path(search::default_ini_path()).parent_path() / L"sgfc.xml";
}

HWND owner(const RegularReportState* st) {
    return st->compactWindow ? st->compactWindow : st->hwnd;
}

bool refreshTemplates(RegularManualReviewPage& page) {
    SendMessageW(page.templates, CB_RESETCONTENT, 0, 0);
    if (!regularLoadManualReviewTemplates(templatePath(), page.templateRows)) return false;
    for (const auto& row : page.templateRows)
        SendMessageW(page.templates, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.mnemonic.c_str()));
    return true;
}

void saveReview(RegularReportState* st) {
    auto& page = st->manualReview;
    if (page.reportNumber.empty() || !IsWindowEnabled(page.save)) return;
    const auto draft = trimText(regularWindowText(page.text));
    const auto text = search::wide_to_utf8(draft);
    search::ManualReviewStatus status{};
    std::string error;
    const HCURSOR cursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const bool ok = search::save_manual_review(page.connection, page.reportNumber,
                                                page.expectedText, text, status, error);
    SetCursor(cursor);
    if (!ok) {
        LOG_WARN("regular report manual review save failed: " + error);
        MessageBoxW(owner(st), L"未能确认保存结果，请刷新验单后核对。", L"手工复查", MB_ICONWARNING);
        return;
    }

    const wchar_t* message = L"保存成功。";
    using Status = search::ManualReviewStatus;
    switch (status) {
        case Status::saved:
            for (auto& row : st->reportRows)
                if (row.rep_no == page.reportNumber) row.manual_review = text;
            page.expectedText = text;
            SetWindowTextW(page.text, convertNewlines(draft, L"\r\n").c_str());
            page.editorBaseline = regularWindowText(page.text);
            regularSyncManualReviewPage(st, st->selectedReportIndex);
            break;
        case Status::unavailable: message = L"当前验单已不可用，请刷新后核对。"; break;
        case Status::read_only:
            message = L"此结果已经审核，不能修改。";
            SendMessageW(page.text, EM_SETREADONLY, TRUE, 0);
            EnableWindow(page.save, FALSE);
            EnableWindow(page.templates, FALSE);
            break;
        case Status::conflict:
            message = L"说明已被其他工作站修改。本次输入仍保留，切换验单后可重新加载原文。";
            break;
        case Status::too_long: message = L"复查说明不能超过 300 字节。请缩短后再保存。"; break;
        case Status::unrepresentable: message = L"复查说明含数据库编码无法保存的字符。"; break;
    }
    MessageBoxW(owner(st), message, L"手工复查", status == Status::saved ? MB_ICONINFORMATION : MB_ICONWARNING);
}

void saveTemplate(RegularReportState* st) {
    auto& page = st->manualReview;
    const ManualReviewTemplate row{trimText(regularWindowText(page.mnemonic)),
                                   trimText(regularWindowText(page.text))};
    const wchar_t* message = nullptr;
    if (row.mnemonic.empty()) message = L"助记符不能为空。";
    else if (row.text.empty()) message = L"模板内容为空。";
    else if (!regularAppendManualReviewTemplate(templatePath(), row))
        message = L"无法保存模板，请核对本机 sgfc.xml 是否可写且格式有效。";
    if (message) {
        MessageBoxW(owner(st), message, L"手工复查模板", MB_ICONWARNING);
        return;
    }
    refreshTemplates(page);
    SetWindowTextW(page.mnemonic, L"");
}

void escapeAttribute(std::wstring& xml, std::wstring_view value) {
    for (const auto ch : value) {
        switch (ch) {
            case L'&': xml += L"&amp;"; break;
            case L'<': xml += L"&lt;"; break;
            case L'>': xml += L"&gt;"; break;
            case L'"': xml += L"&quot;"; break;
            case L'\r': xml += L"&#xD;"; break;
            case L'\n': xml += L"&#xA;"; break;
            case L'\t': xml += L"&#x9;"; break;
            default: xml += ch; break;
        }
    }
}
}  // namespace

bool regularLoadManualReviewTemplates(const std::filesystem::path& path,
                                      std::vector<ManualReviewTemplate>& rows) {
    rows.clear();
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    if (file.bad() || !decodeLegacyXml(bytes) || bytes.empty() ||
        bytes.size() > static_cast<size_t>(std::numeric_limits<UINT>::max())) return false;
    ComPtr<IStream> stream(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()),
                                           static_cast<UINT>(bytes.size())));
    if (!stream) return false;
    IXmlReader* rawReader = nullptr;
    if (FAILED(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(&rawReader), nullptr)))
        return false;
    ComPtr<IXmlReader> reader(rawReader);
    if (FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit)) ||
        FAILED(reader->SetInput(stream.get()))) return false;

    std::vector<ManualReviewTemplate> parsed;
    XmlNodeType type;
    bool rootFound = false;
    HRESULT next;
    while ((next = reader->Read(&type)) == S_OK) {
        if (type != XmlNodeType_Element) continue;
        UINT depth = 0, length = 0;
        const wchar_t* name = nullptr;
        if (reader->GetDepth(&depth) != S_OK || reader->GetLocalName(&name, &length) != S_OK) return false;
        const std::wstring_view element(name, length);
        if (depth == 0 && element == L"root" && !rootFound) {
            rootFound = true;
        } else if (depth == 1 && rootFound && element == L"node") {
            ManualReviewTemplate entry;
            if (!readAttribute(reader.get(), L"zjf", entry.mnemonic) ||
                !readAttribute(reader.get(), L"sgfc", entry.text)) return false;
            parsed.push_back(std::move(entry));
        } else {
            return false;
        }
    }
    if (next != S_FALSE || !rootFound) return false;
    rows = std::move(parsed);
    return true;
}

bool regularAppendManualReviewTemplate(const std::filesystem::path& path,
                                       const ManualReviewTemplate& row) {
    std::vector<ManualReviewTemplate> rows;
    if (!regularLoadManualReviewTemplates(path, rows)) return false;
    rows.push_back(row);
    std::wstring xml = L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n<root>\r\n";
    for (const auto& entry : rows) {
        xml += L"  <node zjf=\"";
        escapeAttribute(xml, entry.mnemonic);
        xml += L"\" sgfc=\"";
        escapeAttribute(xml, entry.text);
        xml += L"\" />\r\n";
    }
    xml += L"</root>\r\n";

    wchar_t temp[MAX_PATH]{};
    if (!GetTempFileNameW(path.parent_path().c_str(), L"sgf", 0, temp)) return false;
    const auto bytes = search::wide_to_utf8(xml);
    std::ofstream output(std::filesystem::path(temp), std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    std::vector<ManualReviewTemplate> checked;
    if (output && regularLoadManualReviewTemplates(temp, checked) &&
        MoveFileExW(temp, path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileW(temp);
    return false;
}

void regularCreateManualReviewPage(RegularReportState* st) {
    auto& page = st->manualReview;
    const auto parent = st->middlePanel;
    auto control = [&](const wchar_t* type, const wchar_t* text, int id, DWORD style, DWORD extra = 0) {
        return CreateWindowExW(extra, type, text, WS_CHILD | WS_CLIPSIBLINGS | style, 0, 0, 0, 0,
                               parent, win32_control_id(id), st->ctx.instance, nullptr);
    };
    page.templateLabel = control(L"STATIC", L"模板选择", 0, SS_CENTERIMAGE);
    page.templates = control(L"COMBOBOX", L"", REGULAR_IDC_MANUAL_REVIEW_TEMPLATE,
                             CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP);
    page.save = control(L"BUTTON", L"保存", REGULAR_IDC_MANUAL_REVIEW_SAVE, WS_TABSTOP);
    page.text = control(L"EDIT", L"", REGULAR_IDC_MANUAL_REVIEW_TEXT,
                        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_READONLY | WS_VSCROLL | WS_TABSTOP,
                        WS_EX_CLIENTEDGE);
    SendMessageW(page.text, EM_SETLIMITTEXT, 4096, 0);
    page.mnemonicLabel = control(L"STATIC", L"模板助记符", 0, SS_CENTERIMAGE);
    page.mnemonic = control(L"EDIT", L"", REGULAR_IDC_MANUAL_REVIEW_MNEMONIC,
                            ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE);
    page.saveTemplate = control(L"BUTTON", L"保存为模板", REGULAR_IDC_MANUAL_REVIEW_SAVE_TEMPLATE, WS_TABSTOP);
    regularSyncManualReviewPage(st, -1);
}

void regularLayoutManualReviewPage(RegularReportState* st, int width, int height) {
    const auto& page = st->manualReview;
    const int pad = regularS(st->hwnd, REGULAR_PAD);
    const int gap = regularS(st->hwnd, REGULAR_GAP);
    const int top = regularS(st->hwnd, REGULAR_MIDDLE_TOOLBAR_Y);
    const int rowHeight = regularS(st->hwnd, REGULAR_COMPACT_BUTTON_H);
    const int labelWidth = regularS(st->hwnd, 88);
    const int contentWidth = std::max(1, width - 2 * pad);
    const int buttonWidth = regularS(st->hwnd, 94);
    const int fieldX = pad + labelWidth + gap;
    const int fieldWidth = std::max(1, contentWidth - labelWidth - buttonWidth - 2 * gap);
    const int buttonX = fieldX + fieldWidth + gap;
    const int textY = top + rowHeight + gap;
    const int lowerY = std::max(textY + gap + 1, height - pad - rowHeight);
    MoveWindow(page.templateLabel, pad, top, labelWidth, rowHeight, TRUE);
    MoveWindow(page.templates, fieldX, top, fieldWidth, regularS(st->hwnd, 180), TRUE);
    MoveWindow(page.save, buttonX, top, buttonWidth, rowHeight, TRUE);
    MoveWindow(page.text, pad, textY, contentWidth, lowerY - textY - gap, TRUE);
    MoveWindow(page.mnemonicLabel, pad, lowerY, labelWidth, rowHeight, TRUE);
    MoveWindow(page.mnemonic, fieldX, lowerY, fieldWidth, rowHeight, TRUE);
    MoveWindow(page.saveTemplate, buttonX, lowerY, buttonWidth, rowHeight, TRUE);
}

void regularShowManualReviewPage(RegularReportState* st, bool show) {
    auto& page = st->manualReview;
    for (const HWND control : {page.templateLabel, page.templates, page.save, page.text,
                              page.mnemonicLabel, page.mnemonic, page.saveTemplate})
        ShowWindow(control, show ? SW_SHOW : SW_HIDE);
    if (show) {
        regularSyncManualReviewPage(st, st->selectedReportIndex);
        if (!refreshTemplates(page)) LOG_WARN("regular report manual review template read failed");
    }
}

void regularSyncManualReviewPage(RegularReportState* st, int index) {
    auto& page = st->manualReview;
    if (!page.text) return;
    const search::ReportRow* row = index >= 0 && static_cast<size_t>(index) < st->reportRows.size()
        ? &st->reportRows[static_cast<size_t>(index)] : nullptr;
    const std::string repNo = row ? row->rep_no : "";
    const std::string text = row ? row->manual_review : "";
    const auto& connection = st->reportConnectionString;
    const bool sameReport = page.reportNumber == repNo && page.connection == connection;
    if (!sameReport || regularWindowText(page.text) == page.editorBaseline) {
        page.expectedText = text;
        const auto display = convertNewlines(search::utf8_to_wide(text), L"\r\n");
        if (display != page.editorBaseline || !sameReport)
            SetWindowTextW(page.text, display.c_str());
        page.editorBaseline = regularWindowText(page.text);
    }
    if (!sameReport) SendMessageW(page.templates, CB_SETCURSEL, -1, 0);
    page.reportNumber = repNo;
    page.connection = connection;

    const bool selected = !repNo.empty() && !connection.empty();
    const bool editable = selected && (row->chk_flag == "F" || row->chk_flag == "C");
    SendMessageW(page.text, EM_SETREADONLY, !editable, 0);
    EnableWindow(page.save, editable);
    EnableWindow(page.templates, editable);
    EnableWindow(page.mnemonic, selected);
    EnableWindow(page.saveTemplate, selected);

    const wchar_t* caption = !row ? L"手工复查" : text.empty() ? L"手工复查[无]" : L"手工复查[有]";
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(caption);
    TabCtrl_SetItem(st->middleTab, REGULAR_MANUAL_REVIEW_TAB, &item);
}

bool regularManualReviewCommand(RegularReportState* st, WPARAM command) {
    auto& page = st->manualReview;
    const int id = LOWORD(command);
    if (id != REGULAR_IDC_MANUAL_REVIEW_SAVE && id != REGULAR_IDC_MANUAL_REVIEW_SAVE_TEMPLATE &&
        id != REGULAR_IDC_MANUAL_REVIEW_TEMPLATE) return false;
    if (app::window_task_detail::sessionSuspended()) return true;
    if (id == REGULAR_IDC_MANUAL_REVIEW_SAVE && HIWORD(command) == BN_CLICKED) {
        saveReview(st);
    } else if (id == REGULAR_IDC_MANUAL_REVIEW_SAVE_TEMPLATE && HIWORD(command) == BN_CLICKED) {
        if (IsWindowEnabled(page.saveTemplate)) saveTemplate(st);
    } else if (id == REGULAR_IDC_MANUAL_REVIEW_TEMPLATE && HIWORD(command) == CBN_SELCHANGE &&
               IsWindowEnabled(page.templates)) {
        const auto selected = SendMessageW(page.templates, CB_GETCURSEL, 0, 0);
        if (selected != CB_ERR && static_cast<size_t>(selected) < page.templateRows.size()) {
            const auto mnemonic = trimText(page.templateRows[static_cast<size_t>(selected)].mnemonic);
            const auto first = std::find_if(page.templateRows.begin(), page.templateRows.end(),
                [&](const auto& entry) { return trimText(entry.mnemonic) == mnemonic; });
            if (!mnemonic.empty() && first != page.templateRows.end())
                SetWindowTextW(page.text, convertNewlines(trimText(first->text), L"\r\n").c_str());
        }
    }
    return true;
}

#endif
