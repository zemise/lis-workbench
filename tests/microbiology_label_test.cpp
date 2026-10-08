#include "microbiology_label_template.h"
#include "microbiology_report_barcode_range.h"
#include "labelprint/labelprint.h"

#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "check failed line " << __LINE__ << ": " #x "\n"; return 1; } } while (false)

int main() {
    CHECK(microbiology_barcode::label_group_name("微生物培养", "301") == "微生物培养（岳麓）");
    CHECK(microbiology_barcode::label_group_name("微生物培养", " 702 ") == "微生物培养（滨水）");
    CHECK(microbiology_barcode::label_group_name("微生物培养", "3001") == "微生物培养");
    CHECK(microbiology_barcode::label_group_name("微生物培养", "") == "微生物培养");
    CHECK(microbiology_barcode::label_group_name("微生物培养（岳麓）", "301") == "微生物培养（岳麓）");
    search::BarcodeLabelPayload p;
    p.label_template = search::BarcodeLabelTemplate::Microbiology;
    p.sample_no = "123456";
    p.test_item = microbiology_barcode::label_group_name("微生物培养", "301");
    p.barcode_value = "008085125";
    p.patient_name = "张三";
    p.specimen_type = "痰液";
    p.department = "呼吸内科";
    p.patient_id = "202629988";
    p.timestamp = "2026/10/9";
    p.sex = "男";
    p.age = "65岁";
    // Long multibyte content must survive wrapping in full.
    p.order_text = "一般细菌培养及鉴定/真菌培养/药物敏感试验/细菌培养复查";
    auto settings = labelprint::MedicalLabelLayout{}.settings;
    auto doc = search::build_microbiology_label(p);
    CHECK(doc.barcodes().empty());
    CHECK(doc.texts()[1].text == "微生物培养（岳麓）");
    std::string order, specimen, demographics;
    int orderLines = 0;
    for (const auto& t : doc.texts()) {
        CHECK(t.text.find(p.barcode_value) == std::string::npos);
        CHECK(t.x >= 0 && t.y >= 0);
        CHECK(t.y + t.height <= settings.height - settings.homeY);
        if (t.y >= 65 && t.y < 137) { order += t.text; ++orderLines; CHECK(t.y + t.height <= 133); }
        if (t.y >= 137 && t.y < 175) { specimen += t.text; CHECK(t.y + t.height <= 169); }
        if (t.x == 145 && t.y == 175) demographics = t.text;
    }
    CHECK(order == p.order_text);
    CHECK(orderLines > 1);
    CHECK(specimen == "痰液");
    CHECK(demographics == "男 65岁");

    labelprint::ZplBackend zpl;
    auto zebra = labelprint::PrinterProfiles::zebra_zd888();
    zebra.nativeChineseFontFallback.clear();
    CHECK(zpl.render(doc, zebra).asText().find("^BC") == std::string::npos);
    auto xprinter = labelprint::PrinterProfiles::xprinter_xp360b();
    labelprint::TsplGb18030Backend tspl;
    CHECK(tspl.render(doc, xprinter).asText().find("BARCODE") == std::string::npos);
    labelprint::EzplGb2312Backend ezpl;
    auto godexDoc = search::build_microbiology_label(p, labelprint::MedicalLabelPrinterModel::GodexG500u);
    CHECK(godexDoc.barcodes().empty());
    for (const auto& t : godexDoc.texts()) {
        if (t.y >= 65 && t.y < 137) CHECK(t.height == 22 && t.width == 16);
        if (t.y == 137 || t.y == 175) CHECK(t.height == 24);
    }

    // Original sizes are independent of content; only orders wrap.
    for (auto model : {labelprint::MedicalLabelPrinterModel::XprinterXp360b,
                       labelprint::MedicalLabelPrinterModel::ZebraZd888,
                       labelprint::MedicalLabelPrinterModel::GodexG500u}) {
        auto longFields = p;
        longFields.sample_no = "123456789";
        longFields.test_item = "很长的组合项目名称也不能自动换行";
        longFields.specimen_type = "很长的标本名称也不能自动换行";
        longFields.patient_name = "很长的患者姓名也不能自动换行";
        longFields.department = "很长的科室名称也不能自动换行";
        auto longDoc = search::build_microbiology_label(longFields, model);
        int singleLines = 0;
        for (const auto& t : longDoc.texts()) {
            CHECK(t.text.find("医嘱：") == std::string::npos);
            CHECK(t.text.find("标本：") == std::string::npos);
            if (t.y >= 65 && t.y < 137) {
                CHECK(t.height == (model == labelprint::MedicalLabelPrinterModel::XprinterXp360b ? 18 : 22));
                CHECK(t.width == (model == labelprint::MedicalLabelPrinterModel::XprinterXp360b ? 13 : 16));
            } else ++singleLines;
        }
        CHECK(singleLines == 8);
        CHECK(longDoc.texts()[0].height == 28 && longDoc.texts()[0].width == 16);
        CHECK(longDoc.texts()[1].text == longFields.test_item);
    }
    CHECK(ezpl.render(godexDoc, labelprint::PrinterProfiles::godex_g500u()).asText().find("BQ,") == std::string::npos);

    p.sex.clear(); p.age.clear(); p.order_text.clear(); p.specimen_type.clear();
    doc = search::build_microbiology_label(p);
    CHECK(doc.barcodes().empty());
    for (const auto& t : doc.texts()) CHECK(t.x != 145 || t.y != 175);
    p.order_text = std::string(1000, 'A');
    for (auto model : {labelprint::MedicalLabelPrinterModel::XprinterXp360b,
                       labelprint::MedicalLabelPrinterModel::ZebraZd888,
                       labelprint::MedicalLabelPrinterModel::GodexG500u}) {
        auto reduced = search::build_microbiology_label(p, model);
        std::string allOrderText;
        int orderRows = 0;
        for (const auto& t : reduced.texts()) {
            if (t.y >= 65 && t.y < 137) {
                allOrderText += t.text;
                ++orderRows;
                CHECK(t.height < 18);
                CHECK(t.y + t.height <= 133);
                if (model != labelprint::MedicalLabelPrinterModel::ZebraZd888)
                    CHECK(t.renderMode == labelprint::TextRenderMode::Bitmap);
            }
        }
        CHECK(allOrderText == p.order_text);
        CHECK(orderRows > 1);
        CHECK(reduced.texts()[0].height == 28 && reduced.texts()[0].width == 16);
#ifdef _WIN32
        const auto& profile = model == labelprint::MedicalLabelPrinterModel::XprinterXp360b
            ? labelprint::PrinterProfiles::xprinter_xp360b()
            : model == labelprint::MedicalLabelPrinterModel::ZebraZd888
                ? labelprint::PrinterProfiles::zebra_zd888()
                : labelprint::PrinterProfiles::godex_g500u();
        const auto rendered = search::render_microbiology_label(reduced, profile);
        CHECK(!rendered.data.empty());
        if (model == labelprint::MedicalLabelPrinterModel::XprinterXp360b) {
            CHECK(rendered.format == "tspl-bitmap");
            CHECK(rendered.asText().find("BITMAP ") != std::string::npos);
            CHECK(rendered.asText().find("PRINT ") != std::string::npos);
        } else if (model == labelprint::MedicalLabelPrinterModel::GodexG500u) {
            CHECK(rendered.format == "ezpl-bitmap");
            CHECK(rendered.asText().find("Q10,70,") != std::string::npos);
            CHECK(rendered.asText().substr(rendered.data.size() - 3) == "E\r\n");
        }
#endif
    }

    // LabelPrint's standard template continues to contain its barcode.
    labelprint::MedicalLabelData regular;
    regular.barcodeValue = "008085125";
    CHECK(labelprint::buildMedicalLabel(regular, settings).barcodes().size() == 1);

    search::ReportRow row;
    row.oper_no = "123456";
    row.txm_no = "008085125";
    row.order_text = "细菌培养";
    row.sex = "男";
    row.age = "65岁";
    row.group_code = "301";
    std::vector<search::ReportRow> rows(6, row);
    rows[1].order_text = "真菌培养";
    rows[2].sex = "女";
    rows[3].age = "66岁";
    rows[5].group_code = "702";
    const auto range = microbiology_barcode::select_range(rows, "123456", "123456");
    CHECK(range.duplicate_count == 1);
    CHECK(range.candidates[1].default_selected);
    CHECK(range.candidates[2].default_selected);
    CHECK(range.candidates[3].default_selected);
    CHECK(!range.candidates[4].default_selected);
    CHECK(range.candidates[5].default_selected);
    return 0;
}
