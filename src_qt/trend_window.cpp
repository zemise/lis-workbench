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
#include <QProcess>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextStream>
#include <QTemporaryFile>
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
    QString local = QCoreApplication::applicationDirPath() + "/gnuplot/bin/gnuplot.exe";
    if (QFile::exists(local)) return local;
    for (const auto& base : {"C:/Program Files/gnuplot/bin/gnuplot.exe",
                              "C:/Program Files (x86)/gnuplot/bin/gnuplot.exe"}) {
        if (QFile::exists(base)) return base;
    }
    return "gnuplot";
}

void TrendWindow::setupUi() {
    itemModel_ = new QStandardItemModel(0, 1, this);
    itemModel_->setHorizontalHeaderLabels({QString::fromWCharArray(L"项目")});
    itemTable_ = new QTableView;
    itemTable_->setModel(itemModel_);
    itemTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    itemTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    itemTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    itemTable_->horizontalHeader()->setStretchLastSection(true);
    itemTable_->verticalHeader()->setVisible(false);

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

    detailModel_ = new QStandardItemModel(0, 7, this);
    detailModel_->setHorizontalHeaderLabels({
        QString::fromWCharArray(L"时间"), QString::fromWCharArray(L"项目"),
        QString::fromWCharArray(L"结果"), QString::fromWCharArray(L"单位"),
        QString::fromWCharArray(L"下限"), QString::fromWCharArray(L"上限"),
        QString::fromWCharArray(L"报告号")
    });
    detailTable_ = new QTableView;
    detailTable_->setModel(detailModel_);
    detailTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    detailTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    detailTable_->horizontalHeader()->setStretchLastSection(true);
    detailTable_->verticalHeader()->setVisible(false);

    auto* leftSplitter = new QSplitter(Qt::Vertical);
    leftSplitter->addWidget(chartContainer);
    leftSplitter->addWidget(detailTable_);
    leftSplitter->setStretchFactor(0, 2);
    leftSplitter->setStretchFactor(1, 1);

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
        if (search::query_trend_points(db, input, result, error))
            pts = std::move(result);
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
    if (!items_.empty()) itemTable_->selectRow(0);
}

void TrendWindow::onItemClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    size_t idx = static_cast<size_t>(index.row());
    if (idx >= items_.size()) return;
    currentItemCode_ = items_[idx].item_code;
    renderChart(currentItemCode_);
}

void TrendWindow::renderChart(const std::string& itemCode) {
    std::vector<const search::TrendPoint*> pts;
    for (const auto& p : points_) {
        if (p.item_code == itemCode && p.has_numeric_value) pts.push_back(&p);
    }

    // Detail table
    detailModel_->removeRows(0, detailModel_->rowCount());
    for (const auto* p : pts) {
        QList<QStandardItem*> row;
        row << cellItem(s8(p->report_time)) << cellItem(s8(p->item_name))
            << cellItem(s8(p->result_text)) << cellItem(s8(p->unit))
            << cellItem(s8(p->lower_bound)) << cellItem(s8(p->upper_bound))
            << cellItem(s8(p->rep_no));
        QColor fg = Qt::black;
        if (p->normal == "1") fg = QColor(0xD2, 0x28, 0x28);
        else if (p->normal == "5") fg = QColor(0x28, 0x50, 0xD2);
        for (auto* it : row) it->setForeground(fg);
        detailModel_->appendRow(row);
    }

    if (pts.size() < 2) {
        chartLabel_->setText(QString::fromWCharArray(L"该项目不足两个有效数值点，暂无法绘制趋势线"));
        return;
    }

    QString gp = gnuplotPath();
    QProcess test;
    test.start(gp, {"--version"});
    if (!test.waitForFinished(2000) || test.exitCode() != 0) {
        chartLabel_->setText(QString::fromWCharArray(L"未找到 gnuplot"));
        return;
    }

    // Determine range
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
    if (hasRef) { yMin = std::min(yMin, refLo) - pad; yMax = std::max(yMax, refHi) + pad; }
    else        { yMin -= pad; yMax += pad; }

    std::string title = pts[0]->item_name;
    if (!pts[0]->item_eng.empty()) title += " (" + pts[0]->item_eng + ")";
    if (!pts[0]->unit.empty()) title += " [" + pts[0]->unit + "]";
    std::string yLabel = "Value";
    if (!pts[0]->unit.empty()) yLabel += " (" + pts[0]->unit + ")";

    // Build gnuplot script
    QString script;
    QTextStream gpOut(&script);

    gpOut << "set terminal pngcairo enhanced size 1000,600\n";
    gpOut << "set output '" << QDir::tempPath().toStdString().c_str()
          << "/trend_" << QCoreApplication::applicationPid() << ".png'\n";
    gpOut << "set title '" << title.c_str() << "' font ',13'\n";
    gpOut << "set xlabel 'Date (by order)' font ',10'\n";
    gpOut << "set ylabel '" << yLabel.c_str() << "' font ',10'\n";
    gpOut << "set yrange [" << yMin << ":" << yMax << "]\n";
    gpOut << "set grid ytics lc rgb '#E0E0E0'\n";
    gpOut << "set key inside right top font ',8'\n";
    gpOut << "set format y '%.4g'\n";

    if (hasRef) {
        gpOut << "set object 1 rect from graph 0, first " << refLo
              << " to graph 1, first " << refHi
              << " fc rgb '#F0F0F0' fs solid 0.5 behind\n";
    }

    // X-ticks — date+time, max 5
    int nTicks = std::min(5, static_cast<int>(pts.size()));
    gpOut << "set xtics (";
    for (int t = 0; t < nTicks; ++t) {
        int idx = (nTicks == 1) ? 0
            : static_cast<int>(std::llround(t * (pts.size() - 1.0) / (nTicks - 1.0)));
        if (t > 0) gpOut << ", ";
        QString rt = s8(pts[idx]->report_time);
        QString lbl = (rt.size() >= 16) ? rt.mid(5,5) + "\\n" + rt.mid(11,5)
                      : (rt.size() >= 10) ? rt.mid(5,5) : rt;
        gpOut << "\"" << lbl.toStdString().c_str() << "\" " << idx;
    }
    gpOut << ") font ',9' rotate by 0 offset 0,-0.8\n";

    gpOut << "plot '-' u 1:2 w lines lw 2.5 lc rgb '#1E5FB4' ti 'Result'\n";

    // Data — one data block
    for (size_t i = 0; i < pts.size(); ++i)
        gpOut << i << " " << pts[i]->result_value << "\n";
    gpOut << "e\n";
    gpOut.flush();

    // Write script to temp file
    QString scriptPath = QDir::tempPath() + "/trend_"
                         + QString::number(QCoreApplication::applicationPid()) + ".gp";
    QFile sf(scriptPath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream sfOut(&sf);
    sfOut.setCodec("UTF-8");
    sfOut << script;
    sf.close();

    QString pngPath = QDir::tempPath() + "/trend_"
                      + QString::number(QCoreApplication::applicationPid()) + ".png";
    QFile::remove(pngPath);

    QProcess proc;
    proc.start(gp, {scriptPath});
    if (!proc.waitForStarted(3000)) { sf.remove(); return; }
    proc.waitForFinished(30000);
    sf.remove();

    if (!QFile::exists(pngPath) || !chartPixmap_.load(pngPath)) {
        chartLabel_->setText(QString::fromWCharArray(L"gnuplot error: ")
                             + QString::fromUtf8(proc.readAllStandardError()));
        QFile::remove(pngPath);
        return;
    }
    QFile::remove(pngPath);
    chartLabel_->setPixmap(chartPixmap_.scaled(chartLabel_->size(),
                                                Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
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
        out << "\xEF\xBB\xBF";
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
        // Generate PNG export: write gnuplot script, run, load result
        std::vector<const search::TrendPoint*> pts;
        for (const auto& p : points_)
            if (p.item_code == code && p.has_numeric_value) pts.push_back(&p);
        if (pts.size() < 2) continue;
        double yMin = pts[0]->result_value, yMax = pts[0]->result_value;
        bool hasRef = false; double refLo = 0, refHi = 0;
        for (const auto* p : pts) {
            yMin = std::min(yMin, p->result_value);
            yMax = std::max(yMax, p->result_value);
            if (!p->lower_bound.empty() || !p->upper_bound.empty()) {
                hasRef = true;
                try { refLo = std::stod(p->lower_bound); } catch(...) {}
                try { refHi = std::stod(p->upper_bound); } catch(...) {}
            }
        }
        double pad = (yMax - yMin) * 0.2; if (pad < 0.01) pad = 1.0;
        if (hasRef) { yMin = std::min(yMin, refLo) - pad; yMax = std::max(yMax, refHi) + pad; }
        else        { yMin -= pad; yMax += pad; }
        std::string title = pts[0]->item_name;
        if (!pts[0]->item_eng.empty()) title += " (" + pts[0]->item_eng + ")";
        if (!pts[0]->unit.empty()) title += " [" + pts[0]->unit + "]";
        std::string yLabel = "Value";
        if (!pts[0]->unit.empty()) yLabel += " (" + pts[0]->unit + ")";

        QString epath = dir + "/" + fname;
        QString scriptPath = QDir::tempPath() + "/trend_exp_"
                             + QString::number(QCoreApplication::applicationPid()) + ".gp";
        QFile sf(scriptPath);
        if (!sf.open(QIODevice::WriteOnly | QIODevice::Text)) continue;
        QTextStream gpOut(&sf);
        gpOut.setCodec("UTF-8");
        gpOut << "set terminal pngcairo enhanced size 1600,900\n";
        gpOut << "set output '" << epath.toStdString().c_str() << "'\n";
        gpOut << "set title '" << title.c_str() << "' font ',14'\n";
        gpOut << "set xlabel 'Date (by order)' font ',10'\n";
        gpOut << "set ylabel '" << yLabel.c_str() << "' font ',10'\n";
        gpOut << "set yrange [" << yMin << ":" << yMax << "]\n";
        gpOut << "set grid ytics lc rgb '#E0E0E0'\n";
        gpOut << "set key inside right top font ',8'\n";
        gpOut << "set format y '%.4g'\n";
        if (hasRef)
            gpOut << "set object 1 rect from graph 0, first " << refLo
                  << " to graph 1, first " << refHi
                  << " fc rgb '#F0F0F0' fs solid 0.5 behind\n";
        int nTicks = std::min(5, static_cast<int>(pts.size()));
        gpOut << "set xtics (";
        for (int t = 0; t < nTicks; ++t) {
            int idx = (nTicks == 1) ? 0
                : static_cast<int>(std::llround(t * (pts.size() - 1.0) / (nTicks - 1.0)));
            if (t > 0) gpOut << ", ";
            QString rt = s8(pts[idx]->report_time);
            QString lbl = (rt.size() >= 16) ? rt.mid(5,5) + "\\n" + rt.mid(11,5)
                          : (rt.size() >= 10) ? rt.mid(5,5) : rt;
            gpOut << "\"" << lbl.toStdString().c_str() << "\" " << idx;
        }
        gpOut << ") font ',9'\n";
        gpOut << "plot '-' u 1:2 w lines lw 2.5 lc rgb '#1E5FB4' notitle\n";
        for (size_t j = 0; j < pts.size(); ++j)
            gpOut << j << " " << pts[j]->result_value << "\n";
        gpOut << "e\n";
        sf.close();

        QString gp = gnuplotPath();
        QProcess proc;
        proc.start(gp, {scriptPath});
        proc.waitForStarted(3000);
        proc.waitForFinished(30000);
        QFile::remove(scriptPath);
    }
    QMessageBox::information(this, QString::fromWCharArray(L"导出完成"),
                             QString::fromWCharArray(L"已导出勾选项目的 PNG 图片。"));
}
