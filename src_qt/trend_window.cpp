#include "trend_window.h"
#include "trend_core.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QScreen>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextStream>
#include <QTemporaryFile>
#include <QSvgRenderer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cmath>
#include <functional>

// ── helpers ──────────────────────────────────────────────────

static QString s8(const std::string& s) {
    return QString::fromUtf8(s.c_str(), static_cast<int>(s.size()));
}

static QStandardItem* cellItem(const QString& text) {
    auto* it = new QStandardItem(text);
    it->setEditable(false);
    return it;
}

static QString sanitize(const std::string& s) {
    QString q = s8(s).trimmed();
    for (auto& ch : q) {
        switch (ch.unicode()) {
            case '\\': case '/': case ':': case '*':
            case '?':  case '"': case '<': case '>': case '|':
                ch = '_'; break;
        }
    }
    return q;
}

static QString exportBaseName(const search::QueryInput& input) {
    QStringList parts;
    auto name = sanitize(input.patient_name);
    auto no   = sanitize(input.patient_no);
    auto sD   = sanitize(input.start_date);
    auto eD   = sanitize(input.end_date);
    QString date;
    if (!sD.isEmpty() && !eD.isEmpty() && sD != eD) date = sD + "_" + eD;
    else if (!sD.isEmpty()) date = sD;
    else date = eD;
    if (!name.isEmpty()) parts << name;
    if (!no.isEmpty())   parts << no;
    if (!date.isEmpty()) parts << date;
    if (parts.isEmpty()) parts << "trend_export";
    return parts.join("-");
}

// ── gnuplot chart builder ────────────────────────────────────

static void writeGnuplotScript(QTextStream& out,
                               const std::vector<const search::TrendPoint*>& pts,
                               int width, int height,
                               const QString& output = "-") {
    // Determine ranges
    double yMin = pts[0]->result_value, yMax = pts[0]->result_value;
    bool hasRef = false;
    double refLo = 0, refHi = 0;
    for (const auto* p : pts) {
        yMin = std::min(yMin, p->result_value);
        yMax = std::max(yMax, p->result_value);
        if (!p->lower_bound.empty() || !p->upper_bound.empty()) {
            hasRef = true;
            try { refLo = std::stod(p->lower_bound); } catch(...) {}
            try { refHi = std::stod(p->upper_bound); } catch(...) {}
        }
    }
    double pad = (yMax - yMin) * 0.2;
    if (pad < 0.01) pad = 1.0;
    if (hasRef) {
        yMin = std::min(yMin, refLo) - pad;
        yMax = std::max(yMax, refHi) + pad;
    } else {
        yMin -= pad;
        yMax += pad;
    }

    // Title
    std::string title = pts[0]->item_name;
    if (!pts[0]->item_eng.empty()) title += " (" + pts[0]->item_eng + ")";
    if (!pts[0]->unit.empty()) title += " - " + pts[0]->unit;
    title += " 趋势图";

    std::string yLabel = pts[0]->unit.empty()
        ? "结果值" : "结果值 (" + pts[0]->unit + ")";

    // gnuplot script — pngcairo, enhanced text, SCI quality
    out << "set terminal svg enhanced size " << width << "," << height
        << " fname 'Microsoft YaHei' fsize 10\n";
    out << "set output '" << output.toStdString().c_str() << "'\n";
    out << "set title '" << title.c_str() << "'\n";
    out << "set xlabel '检测日期（按结果顺序）'\n";
    out << "set ylabel '" << yLabel.c_str() << "'\n";
    out << "set yrange [" << yMin << ":" << yMax << "]\n";
    out << "set grid ytics lc rgb '#E0E0E0'\n";
    out << "set key inside right top width 1\n";
    out << "set style fill transparent solid 0.3\n";
    out << "set format y '%.4g'\n";

    // Reference band
    if (hasRef) {
        out << "set object 1 rect from graph 0, first " << refLo
            << " to graph 1, first " << refHi
            << " fc rgb '#F0F0F0' fs solid 0.5 behind\n";
    }

    // X-axis labels
    out << "set xtics (";
    int maxTicks = std::min(5, static_cast<int>(pts.size()));
    for (int t = 0; t < maxTicks; ++t) {
        int idx = (maxTicks <= 1) ? 0
            : static_cast<int>(std::llround(static_cast<double>(t) * (pts.size() - 1)
                                            / (maxTicks - 1)));
        if (t > 0) out << ", ";
        QString rt = s8(pts[idx]->report_time);
        QString label = (rt.length() >= 16) ? rt.mid(5, 5) + "\\n" + rt.mid(11, 5)
                       : (rt.length() >= 10) ? rt.mid(5, 5) : rt;
        out << "\"" << label.toStdString().c_str() << "\" " << idx;
    }
    out << ") rotate by 0 offset 0,-0.8\n";

    // Plot command — line + color-coded points
    // Normal points
    out << "plot '-' using 1:2 with lines lw 2.5 lc rgb '#1E5FB4' title '结果线', \\\n";
    out << "     '' using 1:2 with points pt 7 ps 1.3 lc rgb '#232323' title '正常', \\\n";
    out << "     '' using 1:2 with points pt 7 ps 1.3 lc rgb '#D22828' title '偏高', \\\n";
    out << "     '' using 1:2 with points pt 7 ps 1.3 lc rgb '#2850D2' title '低值'\n";

    // Write data: all points for line, then filtered by normal status
    auto writePoints = [&](const std::function<bool(const search::TrendPoint*)>& filter) {
        for (size_t i = 0; i < pts.size(); ++i) {
            if (filter(pts[i])) {
                out << i << " " << pts[i]->result_value << "\n";
            }
        }
        out << "e\n";
    };
    writePoints([](const search::TrendPoint*) { return true; });  // all
    writePoints([](const search::TrendPoint* p) { return p->normal != "1" && p->normal != "5"; });  // normal
    writePoints([](const search::TrendPoint* p) { return p->normal == "1"; });  // high
    writePoints([](const search::TrendPoint* p) { return p->normal == "5"; });  // low
    out.flush();
}

// ── TrendWindow ──────────────────────────────────────────────

TrendWindow::TrendWindow(const search::DbSettings& db,
                         const search::QueryInput& lastQuery,
                         QWidget* parent)
    : QDialog(parent), db_(db), lastQuery_(lastQuery) {
    setWindowTitle(QString::fromWCharArray(L"检验结果趋势图"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    auto screen = QGuiApplication::primaryScreen()->availableGeometry();
    resize(std::max(static_cast<int>(screen.width() * 0.8), 1200),
           std::max(static_cast<int>(screen.height() * 0.75), 700));
    setupUi();
    loadTrendData();
}

QString TrendWindow::gnuplotPath() const {
    // 1. Bundled with app (deployment)
    QString local = QCoreApplication::applicationDirPath() + "/gnuplot/bin/gnuplot.exe";
    if (QFile::exists(local)) {
        // Set PATH so gnuplot finds its DLLs
        QString gnuplotBin = QCoreApplication::applicationDirPath() + "/gnuplot/bin";
        qputenv("PATH", (gnuplotBin + ";" + QString::fromLocal8Bit(qgetenv("PATH"))).toLocal8Bit());
        return local;
    }

    // 2. System install (development)
    for (const auto& base : {"C:/Program Files/gnuplot/bin/gnuplot.exe",
                              "C:/Program Files (x86)/gnuplot/bin/gnuplot.exe"}) {
        if (QFile::exists(base)) return base;
    }

    // 3. System PATH
    QProcess test;
    test.start("gnuplot", {"--version"});
    if (test.waitForFinished(2000) && test.exitCode() == 0) return "gnuplot";

    return {};
}

void TrendWindow::setupUi() {
    // Item table (left panel)
    itemModel_ = new QStandardItemModel(0, 1, this);
    itemModel_->setHorizontalHeaderLabels({QString::fromWCharArray(L"项目")});
    itemTable_ = new QTableView;
    itemTable_->setModel(itemModel_);
    itemTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    itemTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    itemTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    itemTable_->horizontalHeader()->setStretchLastSection(true);
    itemTable_->verticalHeader()->setVisible(false);

    // Chart display — QLabel shows gnuplot PNG
    chartLabel_ = new QLabel(QString::fromWCharArray(L"请在左侧选择项目"));
    chartLabel_->setAlignment(Qt::AlignCenter);
    chartLabel_->setMinimumSize(600, 400);
    chartLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    chartLabel_->setStyleSheet("QLabel { background: white; }");

    loadingLabel_ = new QLabel(QString::fromWCharArray(L"正在加载趋势数据..."));
    loadingLabel_->setAlignment(Qt::AlignCenter);

    auto* chartArea = new QVBoxLayout;
    chartArea->setContentsMargins(0, 0, 0, 0);
    chartArea->addWidget(chartLabel_);
    chartArea->addWidget(loadingLabel_);
    auto* chartContainer = new QWidget;
    chartContainer->setMinimumHeight(300);
    chartContainer->setLayout(chartArea);

    // Detail table
    detailModel_ = new QStandardItemModel(0, 7, this);
    detailModel_->setHorizontalHeaderLabels({
        QString::fromWCharArray(L"时间"),
        QString::fromWCharArray(L"项目"),
        QString::fromWCharArray(L"结果"),
        QString::fromWCharArray(L"单位"),
        QString::fromWCharArray(L"下限"),
        QString::fromWCharArray(L"上限"),
        QString::fromWCharArray(L"报告号")
    });
    detailTable_ = new QTableView;
    detailTable_->setModel(detailModel_);
    detailTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    detailTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    detailTable_->horizontalHeader()->setStretchLastSection(true);
    detailTable_->verticalHeader()->setVisible(false);

    // Left panel: chart (top) + detail table (bottom)
    auto* leftSplitter = new QSplitter(Qt::Vertical);
    leftSplitter->addWidget(chartContainer);
    leftSplitter->addWidget(detailTable_);
    leftSplitter->setStretchFactor(0, 2);
    leftSplitter->setStretchFactor(1, 1);

    // Right panel: item list + buttons
    exportCsvBtn_ = new QPushButton(QString::fromWCharArray(L"导出勾选项目"));
    exportImageBtn_ = new QPushButton(QString::fromWCharArray(L"导出勾选图片"));
    exportCsvBtn_->setEnabled(false);
    exportImageBtn_->setEnabled(false);
    exportCsvBtn_->setFixedHeight(32);
    exportImageBtn_->setFixedHeight(32);

    auto* rightPanel = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);
    rightLayout->addWidget(itemTable_, 1);
    rightLayout->addWidget(exportImageBtn_);
    rightLayout->addWidget(exportCsvBtn_);

    rightPanel->setMinimumWidth(200);
    rightPanel->setMaximumWidth(350);
    mainSplitter_ = new QSplitter(Qt::Horizontal);
    mainSplitter_->addWidget(leftSplitter);
    mainSplitter_->addWidget(rightPanel);
    mainSplitter_->setStretchFactor(0, 1);
    mainSplitter_->setStretchFactor(1, 0);
    mainSplitter_->setSizes({800, 350});

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(mainSplitter_);

    connect(itemTable_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &TrendWindow::onItemClicked);
    connect(exportCsvBtn_, &QPushButton::clicked, this, &TrendWindow::onExportCsv);
    connect(exportImageBtn_, &QPushButton::clicked, this, &TrendWindow::onExportImages);
}

void TrendWindow::loadTrendData() {
    auto& pts = points_;
    auto db = db_;
    auto input = lastQuery_;

    QtConcurrent::run([this, db, input, &pts]() {
        std::string error;
        std::vector<search::TrendPoint> result;
        if (search::query_trend_points(db, input, result, error)) {
            pts = std::move(result);
        }
        QMetaObject::invokeMethod(this, "onTrendDataLoaded", Qt::QueuedConnection);
    });
}

void TrendWindow::onTrendDataLoaded() {
    loadingLabel_->hide();

    if (points_.empty()) {
        chartLabel_->setText(QString::fromWCharArray(L"未查询到趋势数据。"));
        return;
    }

    items_ = search::trend_item_options(points_);
    itemModel_->removeRows(0, itemModel_->rowCount());
    for (size_t i = 0; i < items_.size(); ++i) {
        auto* checkItem = cellItem(s8(items_[i].item_name));
        checkItem->setCheckable(true);
        checkItem->setCheckState(Qt::Unchecked);
        itemModel_->appendRow(checkItem);
    }

    exportCsvBtn_->setEnabled(true);
    exportImageBtn_->setEnabled(true);

    if (!items_.empty()) {
        itemTable_->selectRow(0);
    }
}

void TrendWindow::onItemClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    size_t idx = static_cast<size_t>(index.row());
    if (idx >= items_.size()) return;

    currentItemCode_ = items_[idx].item_code;
    renderChart(currentItemCode_);
}

void TrendWindow::renderChart(const std::string& itemCode) {
    // Collect numeric points for this item
    std::vector<const search::TrendPoint*> itemPoints;
    for (const auto& p : points_) {
        if (p.item_code == itemCode && p.has_numeric_value) {
            itemPoints.push_back(&p);
        }
    }

    // Populate detail table
    detailModel_->removeRows(0, detailModel_->rowCount());
    for (const auto* p : itemPoints) {
        QList<QStandardItem*> row;
        row << cellItem(s8(p->report_time))
            << cellItem(s8(p->item_name))
            << cellItem(s8(p->result_text))
            << cellItem(s8(p->unit))
            << cellItem(s8(p->lower_bound))
            << cellItem(s8(p->upper_bound))
            << cellItem(s8(p->rep_no));
        QColor fg = Qt::black;
        if (p->normal == "1") fg = QColor(0xD2, 0x28, 0x28);
        else if (p->normal == "5") fg = QColor(0x28, 0x50, 0xD2);
        for (auto* it : row) it->setForeground(fg);
        detailModel_->appendRow(row);
    }

    if (itemPoints.size() < 2) {
        chartLabel_->setText(QString::fromWCharArray(L"该项目不足两个有效数值点，暂无法绘制趋势线"));
        return;
    }

    QString gp = gnuplotPath();
    if (gp.isEmpty()) {
        chartLabel_->setText(QString::fromWCharArray(L"未找到 gnuplot，请安装: winget install gnuplot.gnuplot"));
        return;
    }

    // Write script + data to temp files, let gnuplot render to temp PNG
    QTemporaryFile scriptFile(QDir::tempPath() + "/trend_XXXXXX.gp");
    scriptFile.setAutoRemove(false);
    if (!scriptFile.open()) return;
    QString scriptPath = scriptFile.fileName();

    QString svgPath = QDir::tempPath() + "/trend_" + QString::number(QCoreApplication::applicationPid()) + ".svg";
    QFile::remove(svgPath);

    int w = chartLabel_->width() > 100 ? chartLabel_->width() : 800;
    int h = chartLabel_->height() > 100 ? chartLabel_->height() : 500;
    {
        QString script;
        QTextStream buf(&script);
        writeGnuplotScript(buf, itemPoints, w, h, svgPath);
        QTextStream out(&scriptFile);
        out.setCodec("UTF-8");
        out << script;
    }
    scriptFile.close();

    QProcess proc;
    proc.start(gp, {scriptPath});
    if (!proc.waitForStarted(3000)) return;
    proc.waitForFinished(30000);

    // Render SVG to pixmap via Qt (native font support)
    QSvgRenderer renderer(svgPath);
    if (!renderer.isValid()) {
        chartLabel_->setText(QString::fromWCharArray(L"gnuplot 图表生成失败"));
        scriptFile.remove();
        QFile::remove(svgPath);
        return;
    }
    chartPixmap_ = QPixmap(w * 2, h * 2);
    chartPixmap_.fill(Qt::white);
    QPainter painter(&chartPixmap_);
    renderer.render(&painter);
    painter.end();

    scriptFile.remove();
    QFile::remove(svgPath);

    chartLabel_->setPixmap(chartPixmap_.scaled(chartLabel_->size(),
                                                Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
}

void TrendWindow::renderToFile(const std::string& itemCode,
                               const QString& pngPath, int w, int h) {
    std::vector<const search::TrendPoint*> itemPoints;
    for (const auto& p : points_) {
        if (p.item_code == itemCode && p.has_numeric_value)
            itemPoints.push_back(&p);
    }
    if (itemPoints.size() < 2) return;

    QString gp = gnuplotPath();
    if (gp.isEmpty()) return;

    // Render to temp SVG first, then convert to PNG via Qt
    QString svgPath = QDir::tempPath() + "/trend_export_" + QString::number(QCoreApplication::applicationPid()) + ".svg";
    QFile::remove(svgPath);

    QTemporaryFile scriptFile(QDir::tempPath() + "/trend_XXXXXX.gp");
    scriptFile.setAutoRemove(false);
    if (!scriptFile.open()) return;
    QString scriptPath = scriptFile.fileName();
    {
        QTextStream out(&scriptFile);
        out.setCodec("UTF-8");
        writeGnuplotScript(out, itemPoints, w, h, svgPath);
    }
    scriptFile.close();

    QProcess proc;
    proc.start(gp, {scriptPath});
    if (!proc.waitForStarted(3000)) return;
    proc.waitForFinished(30000);
    scriptFile.remove();

    // Convert SVG to PNG via Qt
    QSvgRenderer renderer(svgPath);
    if (renderer.isValid()) {
        QPixmap pix(w, h);
        pix.fill(Qt::white);
        QPainter painter(&pix);
        renderer.render(&painter);
        painter.end();
        pix.save(pngPath, "PNG");
    }
    QFile::remove(svgPath);
}

void TrendWindow::onExportCsv() {
    QString dir = QFileDialog::getExistingDirectory(this,
        QString::fromWCharArray(L"选择导出文件夹"));
    if (dir.isEmpty()) return;

    QString base = exportBaseName(lastQuery_);
    for (int i = 0; i < itemModel_->rowCount(); ++i) {
        auto* checkItem = itemModel_->item(i, 0);
        if (!checkItem || checkItem->checkState() != Qt::Checked) continue;
        if (i >= static_cast<int>(items_.size())) continue;

        const auto& code = items_[i].item_code;
        const auto& name = items_[i].item_name;
        QString fname = base + "-" + sanitize(code);
        if (!name.empty()) fname += "-" + sanitize(name);
        fname += ".csv";
        QFile file(dir + "/" + fname);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) continue;

        QTextStream out(&file);
        out.setCodec("UTF-8");
        out << "\xEF\xBB\xBF";  // BOM
        out << QString::fromWCharArray(L"时间,项目,结果,单位,下限,上限,报告号\n");
        for (const auto& p : points_) {
            if (p.item_code != code) continue;
            out << s8(p.report_time) << "," << s8(p.item_name) << ","
                << s8(p.result_text) << "," << s8(p.unit) << ","
                << s8(p.lower_bound) << "," << s8(p.upper_bound) << ","
                << s8(p.rep_no) << "\n";
        }
    }
    QMessageBox::information(this, QString::fromWCharArray(L"导出完成"),
                             QString::fromWCharArray(L"已导出勾选项目的 CSV 文件。"));
}

void TrendWindow::onExportImages() {
    QString dir = QFileDialog::getExistingDirectory(this,
        QString::fromWCharArray(L"选择导出文件夹"));
    if (dir.isEmpty()) return;

    QString base = exportBaseName(lastQuery_);
    for (int i = 0; i < itemModel_->rowCount(); ++i) {
        auto* checkItem = itemModel_->item(i, 0);
        if (!checkItem || checkItem->checkState() != Qt::Checked) continue;
        if (i >= static_cast<int>(items_.size())) continue;

        const auto& code = items_[i].item_code;
        const auto& name = items_[i].item_name;
        QString fname = base + "-" + sanitize(code);
        if (!name.empty()) fname += "-" + sanitize(name);
        fname += ".png";
        renderToFile(code, dir + "/" + fname, 1600, 900);
    }
    QMessageBox::information(this, QString::fromWCharArray(L"导出完成"),
                             QString::fromWCharArray(L"已导出勾选项目的 PNG 图片。"));
}
