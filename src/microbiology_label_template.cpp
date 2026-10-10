#include "microbiology_label_template.h"

#include <algorithm>
#include <vector>

namespace search {
namespace {

constexpr int GROUP_HEADING_OFFSET_X = 16;
constexpr int ORDER_X = 5;
constexpr int ORDER_Y = 65;
constexpr int ORDER_WIDTH = 380;
constexpr int ORDER_HEIGHT = 68;
constexpr int NATIVE_CJK_CELL_SIZE = 24;

// Normalize control whitespace; only the order field may produce multiple lines.
std::vector<std::string> characters(const std::string& text) {
    std::vector<std::string> result;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        length = std::min(length, text.size() - i);
        result.push_back(c < 0x20 || c == 0x7F ? " " : text.substr(i, length));
        i += length;
    }
    return result;
}

void addSingleLine(labelprint::LabelDocument& doc, const std::string& text,
                   const labelprint::MedicalLabelTextLayout& layout) {
    if (text.empty()) return;
    const auto chars = characters(text);
    std::string value;
    for (const auto& ch : chars) value += ch;
    int x = layout.pos.x;
    if (layout.align == labelprint::MedicalLabelTextAlign::Center && layout.maxWidth > 0)
        x += std::max(0, (layout.maxWidth - static_cast<int>(chars.size()) * layout.width) / 2);
    doc.addText(x, layout.pos.y, value, layout.height, layout.width, layout.font);
}

// Match the existing shared printing path, including its Zebra custom layout
// and LabelPrint's default XP-360B / Godex field sizes. No content-based resizing.
labelprint::MedicalLabelLayout originalLayout(labelprint::MedicalLabelPrinterModel model) {
    labelprint::MedicalLabelLayout layout;
    const labelprint::PrinterProfile* profile = &labelprint::PrinterProfiles::xprinter_xp360b();
    if (model == labelprint::MedicalLabelPrinterModel::ZebraZd888) {
        profile = &labelprint::PrinterProfiles::zebra_zd888();
        layout.testItem.pos.x = layout.barcode.pos.x;
        layout.testItem.maxWidth = 290;
        layout.testItem.align = labelprint::MedicalLabelTextAlign::Center;
    } else if (model == labelprint::MedicalLabelPrinterModel::GodexG500u) {
        profile = &labelprint::PrinterProfiles::godex_g500u();
        layout.sampleNo.pos.x = 13;
        layout.patientName.pos.x = 13;
        layout.patientName.height = 24;
        layout.specimenType.height = 24;
        layout.department.height = 24;
        layout.patientId.pos.x = 13;
    } else {
        layout.testItem.height = 18;
        layout.testItem.width = 13;
        layout.patientName.height = 14;
        layout.patientName.width = 11;
        layout.specimenType.height = 13;
        layout.specimenType.width = 10;
        layout.department.height = 13;
        layout.department.width = 10;
        layout.patientId.height = 16;
        layout.patientId.width = 11;
        layout.timestamp.height = 15;
        layout.timestamp.width = 9;
    }
    // Move only the group heading right by about 2 mm at 203 DPI.
    layout.testItem.pos.x += GROUP_HEADING_OFFSET_X;
    layout.settings.darkness = profile->darkness;
    layout.settings.printSpeed = profile->speed;
    return layout;
}

void addOrder(labelprint::LabelDocument& doc, const std::string& text,
              labelprint::MedicalLabelTextLayout layout,
              labelprint::MedicalLabelPrinterModel model) {
    if (text.empty()) return;
    const auto chars = characters(text);
    // TSPL / EZPL native CJK fonts use 24-dot cells at these original sizes.
    // Reserve their actual cell size when wrapping, without changing the font.
    const bool zebra = model == labelprint::MedicalLabelPrinterModel::ZebraZd888;
    int cellWidth = zebra ? layout.width : NATIVE_CJK_CELL_SIZE;
    int lineHeight = zebra ? layout.height : NATIVE_CJK_CELL_SIZE;
    auto capacity = [&] {
        return static_cast<std::size_t>(ORDER_WIDTH / cellWidth) *
               ((ORDER_HEIGHT + layout.lineGap) / (lineHeight + layout.lineGap));
    };
    const bool shrink = chars.size() > capacity();
    if (shrink) {
        const int originalHeight = layout.height;
        const int originalWidth = layout.width;
        // Native fixed CJK cells cannot shrink. Render only reduced orders as
        // bitmaps on TSPL / EZPL, retaining the original native fields elsewhere.
        for (int height = originalHeight - 1; height >= 1; --height) {
            layout.height = height;
            layout.width = zebra ? std::max(1, originalWidth * height / originalHeight) : height;
            layout.lineGap = height >= 12 ? 2 : height >= 6 ? 1 : 0;
            cellWidth = layout.width;
            lineHeight = layout.height;
            if (chars.size() <= capacity()) break;
        }
    }
    const int perLine = ORDER_WIDTH / cellWidth;
    layout.pos.x = ORDER_X;
    layout.align = labelprint::MedicalLabelTextAlign::Left;
    for (std::size_t offset = 0, line = 0; offset < chars.size(); ++line) {
        std::string value;
        const auto end = std::min(chars.size(), offset + perLine);
        while (offset < end) value += chars[offset++];
        layout.pos.y = ORDER_Y + static_cast<int>(line) * (lineHeight + layout.lineGap);
        if (shrink && !zebra) {
            auto& element = doc.addText(layout.pos.x, layout.pos.y, value,
                                        layout.height, layout.width, layout.font);
            element.renderMode = labelprint::TextRenderMode::Bitmap;
        } else {
            addSingleLine(doc, value, layout);
        }
    }
}

}  // namespace

labelprint::LabelDocument build_microbiology_label(
    const BarcodeLabelPayload& p, labelprint::MedicalLabelPrinterModel model) {
    const auto layout = originalLayout(model);
    labelprint::LabelDocument doc(layout.settings);
    addSingleLine(doc, p.sample_no, layout.sampleNo);
    addSingleLine(doc, p.test_item, layout.testItem);
    addOrder(doc, p.order_text, layout.testItem, model);
    auto specimen = layout.specimenType;
    specimen.pos = {5, 137};
    addSingleLine(doc, p.specimen_type, specimen);
    addSingleLine(doc, p.patient_name, layout.patientName);
    std::string demographics = p.sex;
    if (!p.age.empty()) {
        if (!demographics.empty()) demographics += " ";
        demographics += p.age;
    }
    addSingleLine(doc, demographics, layout.specimenType);
    addSingleLine(doc, p.department, layout.department);
    addSingleLine(doc, p.patient_id, layout.patientId);
    addSingleLine(doc, p.timestamp, layout.timestamp);
    return doc;
}

}  // namespace search
