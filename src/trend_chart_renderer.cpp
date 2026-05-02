#include "trend_chart_renderer.h"

#ifdef _WIN32

#include "search_text.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <iomanip>
#include <sstream>
#include <string>

namespace search {
namespace {

struct PlotPoint {
    const TrendPoint* source = nullptr;
    double value = 0.0;
    std::time_t time = 0;
};

struct Bounds {
    bool has_lower = false;
    bool has_upper = false;
    double lower = 0.0;
    double upper = 0.0;
};

std::string trim_copy(const std::string& text) {
    return trim(text);
}

bool parse_double(const std::string& text, double& value) {
    const auto cleaned = trim_copy(text);
    if (cleaned.empty()) {
        return false;
    }
    char* end = nullptr;
    value = std::strtod(cleaned.c_str(), &end);
    return end && *end == '\0';
}

bool parse_report_time(const std::string& text, std::time_t& out) {
    std::tm tm{};
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return false;
    }
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = -1;
    out = std::mktime(&tm);
    return out != static_cast<std::time_t>(-1);
}

int decimal_places(const std::string& text) {
    const auto cleaned = trim_copy(text);
    const auto dot = cleaned.find('.');
    if (dot == std::string::npos) {
        return 0;
    }
    int count = 0;
    for (size_t i = dot + 1; i < cleaned.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(cleaned[i]))) {
            break;
        }
        ++count;
    }
    return count;
}

int value_precision(const std::vector<PlotPoint>& points, const Bounds& bounds) {
    int precision = 0;
    for (const auto& point : points) {
        precision = std::max(precision, decimal_places(point.source->result_text));
    }
    if (bounds.has_lower) {
        precision = std::max(precision, decimal_places(points.front().source->lower_bound));
    }
    if (bounds.has_upper) {
        precision = std::max(precision, decimal_places(points.front().source->upper_bound));
    }
    return std::clamp(precision, 0, 4);
}

std::wstring format_value(double value, int precision) {
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(precision) << value;
    return ss.str();
}

std::wstring format_time_label(std::time_t time) {
    std::tm tm{};
#ifdef _MSC_VER
    localtime_s(&tm, &time);
#else
    tm = *std::localtime(&time);
#endif
    wchar_t buffer[32] = {};
    std::wcsftime(buffer, 32, L"%m-%d %H:%M", &tm);
    return buffer;
}

std::vector<PlotPoint> build_plot_points(const std::vector<const TrendPoint*>& points) {
    std::vector<PlotPoint> out;
    for (const auto* point : points) {
        std::time_t parsed_time = 0;
        if (!point || !point->has_numeric_value || !parse_report_time(point->report_time, parsed_time)) {
            continue;
        }
        out.push_back({point, point->result_value, parsed_time});
    }
    std::sort(out.begin(), out.end(), [](const PlotPoint& lhs, const PlotPoint& rhs) {
        if (lhs.time != rhs.time) {
            return lhs.time < rhs.time;
        }
        return lhs.source->rep_no < rhs.source->rep_no;
    });
    return out;
}

Bounds detect_bounds(const std::vector<PlotPoint>& points) {
    Bounds bounds;
    if (points.empty()) {
        return bounds;
    }
    double lower = 0.0;
    double upper = 0.0;
    bounds.has_lower = parse_double(points.front().source->lower_bound, lower);
    bounds.has_upper = parse_double(points.front().source->upper_bound, upper);
    bounds.lower = lower;
    bounds.upper = upper;
    if (bounds.has_lower && bounds.has_upper && bounds.lower > bounds.upper) {
        std::swap(bounds.lower, bounds.upper);
    }
    return bounds;
}

COLORREF point_color(const TrendPoint& point) {
    const auto code = trim_copy(point.normal);
    if (code == "1") {
        return RGB(210, 40, 40);
    }
    if (code == "5") {
        return RGB(40, 80, 210);
    }
    return RGB(35, 35, 35);
}

std::wstring chart_title(const PlotPoint& point) {
    std::string title = point.source->item_name;
    if (!trim_copy(point.source->item_eng).empty()) {
        title += " (" + point.source->item_eng + ")";
    }
    if (!trim_copy(point.source->unit).empty()) {
        title += " - " + point.source->unit;
    }
    title += " 趋势图";
    return utf8_to_wide(title);
}

void draw_text(HDC dc, int x, int y, const std::wstring& text) {
    TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
}

void draw_centered_text(HDC dc, const RECT& rect, const wchar_t* text) {
    DrawTextW(dc, text, -1, const_cast<RECT*>(&rect), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void fill_solid_rect(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void draw_line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1, int style = PS_SOLID) {
    HPEN pen = CreatePen(style, width, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

int x_for_index(const RECT& plot, size_t index, size_t total) {
    if (total <= 1) {
        return (plot.left + plot.right) / 2;
    }
    const double ratio = static_cast<double>(index) / static_cast<double>(total - 1);
    return plot.left + static_cast<int>(ratio * (plot.right - plot.left));
}

int y_for_value(const RECT& plot, double value, double min_value, double max_value) {
    const double ratio = (value - min_value) / (max_value - min_value);
    return plot.bottom - static_cast<int>(ratio * (plot.bottom - plot.top));
}

void draw_reference_band(HDC dc, const RECT& plot, const Bounds& bounds, double min_value, double max_value) {
    if (!bounds.has_lower || !bounds.has_upper || max_value <= min_value) {
        return;
    }
    const int top = y_for_value(plot, bounds.upper, min_value, max_value);
    const int bottom = y_for_value(plot, bounds.lower, min_value, max_value);
    RECT band{plot.left, std::min(top, bottom), plot.right, std::max(top, bottom)};
    fill_solid_rect(dc, band, RGB(242, 242, 242));
    draw_line(dc, plot.left, top, plot.right, top, RGB(150, 150, 150), 1, PS_DOT);
    draw_line(dc, plot.left, bottom, plot.right, bottom, RGB(150, 150, 150), 1, PS_DOT);
}

void draw_grid_and_axes(HDC dc, const RECT& plot, double min_value, double max_value, int precision) {
    constexpr int tick_count = 5;
    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i <= tick_count; ++i) {
        const double ratio = static_cast<double>(i) / tick_count;
        const int y = plot.bottom - static_cast<int>(ratio * (plot.bottom - plot.top));
        draw_line(dc, plot.left, y, plot.right, y, RGB(225, 225, 225));
        const double value = min_value + (max_value - min_value) * ratio;
        const auto label = format_value(value, precision);
        draw_text(dc, 8, y - 8, label);
    }
    draw_line(dc, plot.left, plot.top, plot.left, plot.bottom, RGB(40, 40, 40));
    draw_line(dc, plot.left, plot.bottom, plot.right, plot.bottom, RGB(40, 40, 40));
}

void draw_time_axis(HDC dc, const RECT& plot, const std::vector<PlotPoint>& points) {
    if (points.empty()) {
        return;
    }
    const size_t total = points.size();
    const size_t desired_ticks = std::min<size_t>(5, total);
    size_t last_index = static_cast<size_t>(-1);
    for (size_t tick = 0; tick < desired_ticks; ++tick) {
        const size_t index = desired_ticks <= 1
                                 ? 0
                                 : static_cast<size_t>(std::llround(static_cast<double>(tick) * (total - 1) /
                                                                    static_cast<double>(desired_ticks - 1)));
        if (index == last_index) {
            continue;
        }
        last_index = index;
        const int x = x_for_index(plot, index, total);
        draw_line(dc, x, plot.bottom, x, plot.bottom + 4, RGB(40, 40, 40));
        const auto label = format_time_label(points[index].time);
        SIZE size{};
        GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &size);
        draw_text(dc, x - size.cx / 2, plot.bottom + 8, label);
    }
}

void draw_legend(HDC dc, const RECT& rect) {
    const int x = rect.right - 260;
    const int y = rect.top + 32;
    draw_line(dc, x, y + 7, x + 28, y + 7, RGB(30, 95, 180), 2);
    draw_text(dc, x + 36, y, L"结果线");
    fill_solid_rect(dc, RECT{x, y + 22, x + 28, y + 34}, RGB(242, 242, 242));
    draw_text(dc, x + 36, y + 18, L"参考范围");
    HBRUSH high = CreateSolidBrush(RGB(210, 40, 40));
    HBRUSH low = CreateSolidBrush(RGB(40, 80, 210));
    HBRUSH old = reinterpret_cast<HBRUSH>(SelectObject(dc, high));
    Ellipse(dc, x + 2, y + 42, x + 12, y + 52);
    SelectObject(dc, low);
    Ellipse(dc, x + 118, y + 42, x + 128, y + 52);
    SelectObject(dc, old);
    DeleteObject(high);
    DeleteObject(low);
    draw_text(dc, x + 18, y + 38, L"高值");
    draw_text(dc, x + 134, y + 38, L"低值");
}

}  // namespace

void draw_trend_chart(HWND hwnd, HDC dc, const std::vector<const TrendPoint*>& points) {
    RECT rect{};
    GetClientRect(hwnd, &rect);
    FillRect(dc, &rect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    SetBkMode(dc, TRANSPARENT);

    const auto plot_points = build_plot_points(points);
    if (plot_points.size() < 2) {
        draw_centered_text(dc, rect, L"当前项目不足两个有效时间数值点，暂无法绘制趋势线");
        return;
    }

    RECT plot = rect;
    plot.left += 82;
    plot.right -= 28;
    plot.top += 72;
    plot.bottom -= 58;
    if (plot.right <= plot.left || plot.bottom <= plot.top) {
        return;
    }

    const auto bounds = detect_bounds(plot_points);
    double min_value = plot_points.front().value;
    double max_value = plot_points.front().value;
    for (const auto& point : plot_points) {
        min_value = std::min(min_value, point.value);
        max_value = std::max(max_value, point.value);
    }
    if (bounds.has_lower) {
        min_value = std::min(min_value, bounds.lower);
    }
    if (bounds.has_upper) {
        max_value = std::max(max_value, bounds.upper);
    }
    if (std::fabs(max_value - min_value) < 1e-9) {
        min_value -= 1.0;
        max_value += 1.0;
    }
    const double padding = (max_value - min_value) * 0.12;
    min_value -= padding;
    max_value += padding;

    const int precision = value_precision(plot_points, bounds);
    auto title = chart_title(plot_points.front());
    DrawTextW(dc, title.c_str(), -1, &rect, DT_TOP | DT_CENTER | DT_SINGLELINE);
    draw_legend(dc, rect);
    draw_reference_band(dc, plot, bounds, min_value, max_value);
    draw_grid_and_axes(dc, plot, min_value, max_value, precision);
    draw_time_axis(dc, plot, plot_points);

    draw_text(dc, (plot.left + plot.right) / 2 - 86, rect.bottom - 22, L"检测日期（按结果顺序等距）");
    const std::string unit = trim_copy(plot_points.front().source->unit);
    const auto y_title = utf8_to_wide(unit.empty() ? "结果值" : "结果值 (" + unit + ")");
    draw_text(dc, 8, plot.top - 30, y_title);

    HPEN line_pen = CreatePen(PS_SOLID, 2, RGB(30, 95, 180));
    HGDIOBJ old_pen = SelectObject(dc, line_pen);
    for (size_t i = 0; i < plot_points.size(); ++i) {
        const int x = x_for_index(plot, i, plot_points.size());
        const int y = y_for_value(plot, plot_points[i].value, min_value, max_value);
        if (i == 0) {
            MoveToEx(dc, x, y, nullptr);
        } else {
            LineTo(dc, x, y);
        }
    }
    SelectObject(dc, old_pen);
    DeleteObject(line_pen);

    HPEN point_pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    old_pen = SelectObject(dc, point_pen);
    for (size_t i = 0; i < plot_points.size(); ++i) {
        const auto& point = plot_points[i];
        const int x = x_for_index(plot, i, plot_points.size());
        const int y = y_for_value(plot, point.value, min_value, max_value);
        HBRUSH brush = CreateSolidBrush(point_color(*point.source));
        HGDIOBJ old_brush = SelectObject(dc, brush);
        Ellipse(dc, x - 4, y - 4, x + 5, y + 5);
        SelectObject(dc, old_brush);
        DeleteObject(brush);
    }
    SelectObject(dc, old_pen);
    DeleteObject(point_pen);
}

}  // namespace search

#endif
