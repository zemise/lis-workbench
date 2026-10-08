#include "microbiology_label_template.h"
#include "labelprint/labelprint.h"

#include <array>
#include <charconv>
#include <stdexcept>

namespace search {
namespace {

#ifdef _WIN32
struct OrderBitmap {
    int width_bytes;
    std::string pixels;
};

OrderBitmap renderOrderBitmap(const labelprint::TextElement& text,
                              const labelprint::LabelSettings& settings,
                              const labelprint::PrinterProfile& profile) {
    labelprint::LabelDocument single(settings);
    single.addText(text);
    labelprint::TsplBitmapBackend rasterizer;
    const std::string raster = rasterizer.render(single, profile).asText();
    const auto start = raster.find("BITMAP ");
    if (start == std::string::npos) throw std::runtime_error("医嘱文字位图生成失败");

    // Read the five numeric header fields before the binary payload. In
    // particular, do not search for delimiters inside binary pixel data.
    std::array<int, 5> fields{};
    std::size_t position = start + 7;
    for (auto& field : fields) {
        const auto comma = raster.find(',', position);
        if (comma == std::string::npos) throw std::runtime_error("医嘱文字位图格式异常");
        const auto parsed = std::from_chars(raster.data() + position,
                                            raster.data() + comma, field);
        if (parsed.ec != std::errc{} || parsed.ptr != raster.data() + comma)
            throw std::runtime_error("医嘱文字位图格式异常");
        position = comma + 1;
    }
    const int widthBytes = fields[2];
    const int rasterHeight = fields[3];
    if (widthBytes <= 0 || rasterHeight < text.height || text.height <= 0 || fields[4] != 0)
        throw std::runtime_error("医嘱文字位图尺寸异常");
    const auto rasterBytes = static_cast<std::size_t>(widthBytes) * rasterHeight;
    if (rasterBytes > raster.size() - position)
        throw std::runtime_error("医嘱文字位图数据不完整");

    // LabelPrint allocates at least eight rows. Keep the requested height;
    // otherwise very small fonts would overlap following order lines.
    const auto keptBytes = static_cast<std::size_t>(widthBytes) * text.height;
    return {widthBytes, raster.substr(position, keptBytes)};
}
#endif

}  // namespace

labelprint::PrintJob render_microbiology_label(
    const labelprint::LabelDocument& doc, const labelprint::PrinterProfile& profile) {
    if (profile.language == labelprint::PrinterLanguage::ZPL) {
        labelprint::ZplBackend backend;
        return backend.render(doc, profile);
    }

    labelprint::LabelDocument native(doc.settings());
    for (const auto& t : doc.texts())
        if (t.renderMode != labelprint::TextRenderMode::Bitmap) native.addText(t);
    labelprint::PrintJob job;
    const bool godex = profile.language == labelprint::PrinterLanguage::EZPL;
    if (godex) {
        labelprint::EzplGb2312Backend backend;
        job = backend.render(native, profile);
    } else {
        labelprint::TsplGb18030Backend backend;
        job = backend.render(native, profile);
    }

    std::string graphics;
    for (const auto& t : doc.texts()) {
        if (t.renderMode != labelprint::TextRenderMode::Bitmap) continue;
#ifdef _WIN32
        const auto bitmap = renderOrderBitmap(t, doc.settings(), profile);
        if (godex) {
            // EZPL Q pattern command: width is bytes; data length = width * height.
            // GoDEX EZPL programming manual, Qx,y,width,height (page 53).
            graphics += "Q" + std::to_string(t.x + doc.settings().homeX) + "," +
                        std::to_string(t.y + doc.settings().homeY) + "," +
                        std::to_string(bitmap.width_bytes) + "," + std::to_string(t.height) + "\r\n";
            graphics += bitmap.pixels;
            graphics += "\r\n";
        } else {
            // The rasterizer allocates at least eight rows. Keep only the
            // requested rows and describe that height explicitly in the command.
            graphics += "BITMAP " + std::to_string(t.x) + "," + std::to_string(t.y) + "," +
                        std::to_string(bitmap.width_bytes) + "," + std::to_string(t.height) + ",0,";
            graphics += bitmap.pixels;
            graphics += "\r\n";
        }
#else
        throw std::runtime_error("医嘱文字位图需要 Windows 打印环境");
#endif
    }
    if (!graphics.empty()) {
        std::string commands = job.asText();
        const auto printAt = commands.rfind(godex ? "E\r\n" : "PRINT ");
        if (printAt == std::string::npos) throw std::runtime_error("标签打印指令不完整");
        commands.insert(printAt, graphics);
        job.data.assign(commands.begin(), commands.end());
        job.format = godex ? "ezpl-bitmap" : "tspl-bitmap";
        job.debugText += "\n// Reduced order text rendered as bitmap\n";
    }
    return job;
}

}  // namespace search
