#include "trend_window.h"
#include "search_text.h"
#include "trend_core.h"

#include <QApplication>
#include <QFileDialog>
#include <QGuiApplication>
#include <QScreen>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cmath>

#include "qcustomplot.h"

// ── helpers ──────────────────────────────────────────────────

static QString fmt(const std::string& s) {
    return QString::fromUtf8(s.c_str(), static_cast<int>(s.size()));
}

static QStandardItem* item(const QString& text) {
    auto* it = new QStandardItem(text);
    it->setEditable(false);
    return it;
}

static QString sanitize(const std::string& s) {
    QString q = fmt(s).trimmed();
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
    auto startDate = sanitize(input.start_date);
    auto endDate   = sanitize(input.end_date);
    QString date;
    if (!startDate.isEmpty() && !endDate.isEmpty() && startDate != endDate) {
        date = startDate + "_" + endDate;
    } else if (!startDate.isEmpty()) {
        date = startDate;
    } else {
        date = endDate;
    }
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
    resize(std::max(static_cast<int>(screen.width() * 0.8), 1280),
           std::max(static_cast<int>(screen.height() * 0.75), 720));
    setupUi();
    loadTrendData();
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

    // Chart (top-left)
    chart_ = new QCustomPlot;
    chart_->setInteractions(0);
    chart_->setAntialiasedElements(QCP::aeAll);
    chart_->setMinimumHeight(300);
    chart_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    chart_->xAxis->setLabel(QString::fromWCharArray(L"检测日期（按结果顺序等距）"));
    chart_->yAxis->setLabel(QString::fromWCharArray(L"结果值"));
    chart_->legend->setVisible(true);
    chart_->legend->setFont(QFont("Microsoft YaHei", 8));

    loadingLabel_ = new QLabel(QString::fromWCharArray(L"正在加载趋势数据..."));
    loadingLabel_->setAlignment(Qt::AlignCenter);
    auto* chartArea = new QVBoxLayout;
    chartArea->setContentsMargins(0, 0, 0, 0);
    chartArea->addWidget(chart_);
    chartArea->addWidget(loadingLabel_);
    auto* chartContainer = new QWidget;
    chartContainer->setMinimumHeight(300);
    chartContainer->setLayout(chartArea);

    // Detail table (bottom-left)
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
    leftSplitter->setStretchFactor(0, 2);  // chart
    leftSplitter->setStretchFactor(1, 1);  // detail table

    // Right panel: item list (top) + buttons (bottom)
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

    // Main splitter: left (chart+detail) | right (items+buttons)
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
    auto& pts = points_;  // capture ref for lambda
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
        QMessageBox::information(this, QString::fromWCharArray(L"趋势图"),
                                 QString::fromWCharArray(L"未查询到趋势数据。"));
        return;
    }

    items_ = search::trend_item_options(points_);
    itemModel_->removeRows(0, itemModel_->rowCount());
    for (size_t i = 0; i < items_.size(); ++i) {
        auto* checkItem = item(fmt(items_[i].item_name));
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
    updateChart(currentItemCode_);
}

void TrendWindow::updateChart(const std::string& itemCode) {
    chart_->clearGraphs();
    chart_->clearPlottables();
    chart_->clearItems();

    // Collect points for this item
    std::vector<const search::TrendPoint*> itemPoints;
    for (const auto& p : points_) {
        if (p.item_code == itemCode && p.has_numeric_value) {
            itemPoints.push_back(&p);
        }
    }

    if (itemPoints.size() < 2) {
        chart_->replot();
        detailModel_->removeRows(0, detailModel_->rowCount());
        return;
    }

    // Build data vectors
    QVector<double> x, y;
    QVector<double> xHigh, yHigh, xLow, yLow, xNormal, yNormal;
    double minVal = itemPoints[0]->result_value;
    double maxVal = itemPoints[0]->result_value;
    bool hasRef = false;
    double refLow = 0.0, refHigh = 0.0;

    for (const auto* p : itemPoints) {
        double xi = static_cast<double>(x.size());
        x.push_back(xi);
        y.push_back(p->result_value);
        minVal = std::min(minVal, p->result_value);
        maxVal = std::max(maxVal, p->result_value);

        if (p->normal == "1")      { xHigh.push_back(xi);   yHigh.push_back(p->result_value); }
        else if (p->normal == "5") { xLow.push_back(xi);    yLow.push_back(p->result_value); }
        else                       { xNormal.push_back(xi); yNormal.push_back(p->result_value); }

        if (!p->lower_bound.empty() || !p->upper_bound.empty()) {
            hasRef = true;
            try { refLow = std::stod(p->lower_bound); } catch (...) {}
            try { refHigh = std::stod(p->upper_bound); } catch (...) {}
        }
    }

    double padding = (maxVal - minVal) * 0.2;
    if (padding < 1e-9) padding = 1.0;
    double yMin = minVal - padding;
    double yMax = maxVal + padding;
    if (hasRef) {
        yMin = std::min(yMin, refLow - padding);
        yMax = std::max(yMax, refHigh + padding);
    }

    // ── Color palette (colorblind-safe, muted tones) ──────
    const auto& unit = itemPoints[0]->unit;
    const QString YUnitLabel = unit.empty()
        ? QString::fromWCharArray(L"结果值")
        : QString::fromWCharArray(L"结果值 (") + fmt(unit) + ")";
    // Win32-matched colors
    const QColor lineColor(0x1E, 0x5F, 0xB4);    // blue trend line (RGB 30,95,180)
    const QColor normalColor(0x23, 0x23, 0x23);   // dark gray (RGB 35,35,35)
    const QColor highColor(0xD2, 0x28, 0x28);     // red (RGB 210,40,40)
    const QColor lowColor(0x28, 0x50, 0xD2);      // blue (RGB 40,80,210)
    const QColor refFill(0xF2, 0xF2, 0xF2);
    const QColor gridColor(0xEA, 0xEA, 0xEA);
    const QColor axisColor(0x4D, 0x4D, 0x4D);

    // ── Reference range band ───────────────────────────────
    if (hasRef) {
        QVector<double> bandX = {0.0, static_cast<double>(itemPoints.size() - 1)};
        auto* upper = chart_->addGraph();
        upper->setData(bandX, QVector<double>(2, refHigh));
        upper->setPen(Qt::NoPen);
        upper->removeFromLegend();
        auto* lower = chart_->addGraph();
        lower->setData(bandX, QVector<double>(2, refLow));
        lower->setPen(QPen(axisColor, 0.8, Qt::DashLine));
        lower->setChannelFillGraph(upper);
        lower->setBrush(refFill);
        lower->setName(QString::fromWCharArray(L"参考区间"));
    }

    // ── Main trend line (Win32: blue, width 2) ───────────
    auto* line = chart_->addGraph();
    line->setData(x, y);
    line->setPen(QPen(lineColor, 2.0));
    line->setLineStyle(QCPGraph::lsLine);
    line->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssNone));
    line->setName(QString::fromWCharArray(L"结果线"));

    // ── Scatter points (filled + white border, matching Win32 Ellipse) ──
    auto addScatter = [&](const QVector<double>& xs, const QVector<double>& ys,
                          const QColor& fill, const QString& name) {
        if (xs.isEmpty()) return;
        auto* g = chart_->addGraph();
        g->setData(xs, ys);
        g->setPen(Qt::NoPen);
        g->setLineStyle(QCPGraph::lsNone);
        QCPScatterStyle ss(QCPScatterStyle::ssCircle, QPen(Qt::white, 1.0), QBrush(fill), 7);
        g->setScatterStyle(ss);
        g->setName(name);
    };
    addScatter(xNormal, yNormal, normalColor, QString::fromWCharArray(L"正常"));
    addScatter(xHigh,   yHigh,   highColor,   QString::fromWCharArray(L"偏高"));
    addScatter(xLow,    yLow,    lowColor,    QString::fromWCharArray(L"偏低"));

    // ── Axes styling (ggplot2 theme_bw equivalent) ──────
    chart_->xAxis->setRange(0.0, static_cast<double>(itemPoints.size()) - 1.0);
    chart_->yAxis->setRange(yMin, yMax);

    // Common axis pen: clean thin line, ticks outward
    QPen axisPen(axisColor, 1.2);
    chart_->xAxis->setBasePen(axisPen);
    chart_->xAxis->setTickPen(axisPen);
    chart_->xAxis->setSubTickPen(Qt::NoPen);
    chart_->xAxis->setTickLengthOut(6);
    chart_->xAxis->setSubTicks(false);
    chart_->xAxis->setTickLabelColor(axisColor);
    chart_->xAxis->grid()->setVisible(false);
    chart_->xAxis->setLabel(QString::fromWCharArray(L"检测日期（按结果顺序）"));
    chart_->xAxis->setLabelColor(axisColor);
    chart_->xAxis->setLabelFont(QFont("Microsoft YaHei", 10));

    chart_->yAxis->setBasePen(axisPen);
    chart_->yAxis->setTickPen(axisPen);
    chart_->yAxis->setSubTickPen(Qt::NoPen);
    chart_->yAxis->setTickLengthOut(6);
    chart_->yAxis->setSubTicks(false);
    chart_->yAxis->setTickLabelColor(axisColor);
    chart_->yAxis->grid()->setPen(QPen(gridColor, 0.8));
    chart_->yAxis->grid()->setSubGridVisible(false);
    chart_->yAxis->setLabel(YUnitLabel);
    chart_->yAxis->setLabelColor(axisColor);
    chart_->yAxis->setLabelFont(QFont("Microsoft YaHei", 10));
    // Y-axis ticker: nice rounded values, ~5 ticks
    auto yTicker = QSharedPointer<QCPAxisTickerFixed>::create();
    yTicker->setTickStep(0);  // auto
    yTicker->setScaleStrategy(QCPAxisTickerFixed::ssMultiples);
    chart_->yAxis->setTicker(yTicker);
    chart_->yAxis->setNumberFormat("g");
    chart_->yAxis->setNumberPrecision(3);

    // Remove top/right axis (ggplot2 style)
    chart_->xAxis2->setVisible(true);
    chart_->xAxis2->setTicks(false);
    chart_->xAxis2->setTickLabels(false);
    chart_->xAxis2->setBasePen(QPen(axisColor, 0.5));
    chart_->yAxis2->setVisible(true);
    chart_->yAxis2->setTicks(false);
    chart_->yAxis2->setTickLabels(false);
    chart_->yAxis2->setBasePen(QPen(axisColor, 0.5));

    QVector<double> tickPositions;
    QVector<QString> tickLabels;
    const int maxTicks = 5;
    const int total = static_cast<int>(itemPoints.size());
    for (int tick = 0; tick < maxTicks && tick < total; ++tick) {
        int index = (maxTicks <= 1) ? 0
                    : static_cast<int>(std::llround(static_cast<double>(tick) * (total - 1) /
                                                    static_cast<double>(maxTicks - 1)));
        if (index >= total) continue;
        QString rt = fmt(itemPoints[static_cast<size_t>(index)]->report_time);
        QString datePart = (rt.length() >= 10) ? rt.mid(5, 5) : rt.left(10);   // "MM-DD"
        QString timePart = (rt.length() >= 16) ? rt.mid(11, 5) : "";             // "HH:MM"
        tickPositions.push_back(static_cast<double>(index));
        tickLabels.push_back(datePart + "\n" + timePart);
    }
    auto ticker = QSharedPointer<QCPAxisTickerText>::create();
    for (int i = 0; i < tickPositions.size(); ++i) {
        ticker->addTick(tickPositions[i], tickLabels[i]);
    }
    chart_->xAxis->setTicker(ticker);
    chart_->xAxis->setTickLabelRotation(0);

    // ── Title ─────────────────────────────────────────────
    auto title = fmt(itemPoints[0]->item_name);
    if (!itemPoints[0]->item_eng.empty()) {
        title += " (" + fmt(itemPoints[0]->item_eng) + ")";
    }
    if (!itemPoints[0]->unit.empty()) {
        title += " - " + fmt(itemPoints[0]->unit);
    }
    title += QString::fromWCharArray(L" 趋势图");
    // Ensure title row exists (only once, above axis rect)
    if (chart_->plotLayout()->rowCount() < 2) {
        chart_->plotLayout()->insertRow(0);
    }
    auto* existingTitle = dynamic_cast<QCPTextElement*>(chart_->plotLayout()->element(0, 0));
    if (existingTitle) {
        existingTitle->setText(title);
    } else {
        auto* el = new QCPTextElement(chart_, title,
                                       QFont("Microsoft YaHei", 12, QFont::Bold));
        el->setTextColor(QColor(0x33, 0x33, 0x33));
        chart_->plotLayout()->addElement(0, 0, el);
    }

    // ── Legend — outside plot, below chart (never overlaps) ──
    chart_->legend->setVisible(true);
    chart_->legend->setBrush(Qt::NoBrush);
    chart_->legend->setBorderPen(Qt::NoPen);
    chart_->legend->setFont(QFont("Microsoft YaHei", 8));
    chart_->legend->setIconSize(12, 10);
    chart_->legend->setSelectableParts(QCPLegend::spNone);
    // Move legend to a dedicated row below the axis rect
    chart_->plotLayout()->addElement(2, 0, chart_->legend);
    chart_->plotLayout()->setRowStretchFactor(2, 0.001);

    // ── Final ─────────────────────────────────────────────
    chart_->setBackground(QBrush(Qt::white));
    chart_->axisRect()->setBackground(QBrush(Qt::white));
    // Let QCustomPlot calculate margins automatically
    chart_->axisRect()->setAutoMargins(QCP::msAll);
    chart_->axisRect()->setMargins(QMargins(15, 15, 15, 15));
    chart_->replot();

    // Populate detail table
    detailModel_->removeRows(0, detailModel_->rowCount());
    for (const auto* p : itemPoints) {
        QList<QStandardItem*> row;
        row << item(fmt(p->report_time))
            << item(fmt(p->item_name))
            << item(fmt(p->result_text))
            << item(fmt(p->unit))
            << item(fmt(p->lower_bound))
            << item(fmt(p->upper_bound))
            << item(fmt(p->rep_no));
        QColor fg = Qt::black;
        if (p->normal == "1") fg = highColor;
        else if (p->normal == "5") fg = lowColor;
        for (auto* it : row) { it->setForeground(fg); }
        detailModel_->appendRow(row);
    }
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
        QString fileName = dir + "/" + base + ".csv";
        // Append item info if exporting multiple checked items
        if (i > 0 || itemModel_->rowCount() > 1) {
            fileName = dir + "/" + base + "-" + sanitize(code);
            if (!name.empty()) fileName += "-" + sanitize(name);
            fileName += ".csv";
        }
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) continue;

        QTextStream out(&file);
        out.setCodec("UTF-8");
        out << "\xEF\xBB\xBF";  // BOM
        out << QString::fromWCharArray(L"时间,项目,结果,单位,下限,上限,报告号\n");

        for (const auto& p : points_) {
            if (p.item_code != code) continue;
            out << fmt(p.report_time) << ","
                << fmt(p.item_name) << ","
                << fmt(p.result_text) << ","
                << fmt(p.unit) << ","
                << fmt(p.lower_bound) << ","
                << fmt(p.upper_bound) << ","
                << fmt(p.rep_no) << "\n";
        }
        file.close();
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
        QString fileName = base + "-" + sanitize(code);
        if (!name.empty()) fileName += "-" + sanitize(name);
        fileName += ".png";
        QString fullPath = dir + "/" + fileName;

        updateChart(code);
        chart_->savePng(fullPath, 0, 0, 2.0);
    }
    updateChart(currentItemCode_);

    QMessageBox::information(this, QString::fromWCharArray(L"导出完成"),
                             QString::fromWCharArray(L"已导出勾选项目的 PNG 图片。"));
}

