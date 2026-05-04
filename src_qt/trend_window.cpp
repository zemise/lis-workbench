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
    QString q = fmt(s);
    q.replace('/', '_').replace('\\', '_').replace(':', '_')
     .replace('*', '_').replace('?', '_').replace('"', '_')
     .replace('<', '_').replace('>', '_').replace('|', '_');
    return q;
}

// ── TrendWindow ──────────────────────────────────────────────

TrendWindow::TrendWindow(const search::DbSettings& db,
                         const search::QueryInput& lastQuery,
                         QWidget* parent)
    : QDialog(parent), db_(db), lastQuery_(lastQuery) {
    setWindowTitle(QString::fromWCharArray(L"检验结果趋势图"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    auto screen = QGuiApplication::primaryScreen()->availableGeometry();
    resize(static_cast<int>(screen.width() * 0.8), static_cast<int>(screen.height() * 0.75));
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
    chart_->setAntialiasedElements(QCP::aeAll);
    chart_->setMinimumHeight(250);
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
    chartContainer->setMinimumHeight(220);
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

    // Left panel: chart (top, ~55%) + detail table (bottom, ~45%)
    auto* leftSplitter = new QSplitter(Qt::Vertical);
    leftSplitter->addWidget(chartContainer);
    leftSplitter->addWidget(detailTable_);
    leftSplitter->setStretchFactor(0, 5);
    leftSplitter->setStretchFactor(1, 4);
    leftSplitter->setSizes({500, 350});

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

    // ── Styling constants ──────────────────────────────────
    const QColor lineColor(0x21, 0x6E, 0xC5);    // professional blue
    const QColor normalColor(0x55, 0x55, 0x55);   // dark gray
    const QColor highColor(0xDC, 0x32, 0x32);     // red
    const QColor lowColor(0x32, 0x64, 0xDC);      // blue
    const QColor refFill(0xE8, 0xE8, 0xE8);       // light gray reference band
    const QColor gridColor(0xE0, 0xE0, 0xE0);     // subtle grid
    const QColor axisColor(0x60, 0x60, 0x60);     // axis lines

    // ── Reference range band ───────────────────────────────
    if (hasRef) {
        QVector<double> bandX = {0.0, static_cast<double>(itemPoints.size()) - 1.0};
        QVector<double> bandHigh(bandX.size(), refHigh);
        QVector<double> bandLow(bandX.size(), refLow);
        auto* upper = chart_->addGraph();
        upper->setData(bandX, bandHigh);
        upper->setPen(Qt::NoPen);
        auto* lower = chart_->addGraph();
        lower->setData(bandX, bandLow);
        lower->setPen(QPen(axisColor, 1, Qt::DashLine));
        lower->setChannelFillGraph(upper);
        lower->setBrush(refFill);
        lower->setName(QString::fromWCharArray(L"参考区间"));
    }

    // ── Main trend line ───────────────────────────────────
    auto* line = chart_->addGraph();
    line->setData(x, y);
    line->setPen(QPen(lineColor, 2.5));
    line->setLineStyle(QCPGraph::lsLine);
    line->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssNone));
    line->setName(fmt(itemPoints[0]->item_name));

    // ── Scatter points by normal status ───────────────────
    auto addScatter = [&](const QVector<double>& xs, const QVector<double>& ys,
                          const QColor& color, const QString& name) {
        if (xs.isEmpty()) return;
        auto* g = chart_->addGraph();
        g->setData(xs, ys);
        g->setPen(Qt::NoPen);
        g->setLineStyle(QCPGraph::lsNone);
        g->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, color, 7));
        g->setName(name);
    };
    addScatter(xNormal, yNormal, normalColor, QString::fromWCharArray(L"正常"));
    addScatter(xHigh,   yHigh,   highColor,   QString::fromWCharArray(L"偏高"));
    addScatter(xLow,    yLow,    lowColor,    QString::fromWCharArray(L"偏低"));

    // ── Axes styling ──────────────────────────────────────
    chart_->xAxis->setRange(-0.5, static_cast<double>(itemPoints.size()) - 0.5);
    chart_->yAxis->setRange(yMin, yMax);

    // Y-axis: auto ticks with clean format
    chart_->yAxis->setBasePen(QPen(axisColor, 1));
    chart_->yAxis->setTickPen(QPen(axisColor, 1));
    chart_->yAxis->setSubTickPen(QPen(axisColor, 1));
    chart_->yAxis->setTickLabelColor(axisColor);
    chart_->yAxis->grid()->setPen(QPen(gridColor, 1, Qt::DotLine));
    chart_->yAxis->grid()->setSubGridVisible(false);
    chart_->yAxis->setLabel(QString::fromWCharArray(L"结果值"));
    chart_->yAxis->setLabelColor(axisColor);

    // X-axis: max 5 ticks, two-line format (date + time)
    chart_->xAxis->setBasePen(QPen(axisColor, 1));
    chart_->xAxis->setTickPen(QPen(axisColor, 1));
    chart_->xAxis->setSubTickPen(QPen(axisColor, 1));
    chart_->xAxis->setTickLabelColor(axisColor);
    chart_->xAxis->grid()->setVisible(false);
    chart_->xAxis->setLabel(QString::fromWCharArray(L"检测日期（按结果顺序）"));
    chart_->xAxis->setLabelColor(axisColor);

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
        title += " [" + fmt(itemPoints[0]->unit) + "]";
    }
    if (chart_->plotLayout()->rowCount() == 0) {
        chart_->plotLayout()->insertRow(0);
    }
    auto* existingTitle = dynamic_cast<QCPTextElement*>(chart_->plotLayout()->element(0, 0));
    if (existingTitle) {
        existingTitle->setText(title);
    } else {
        auto* el = new QCPTextElement(chart_, title, QFont("Microsoft YaHei", 12, QFont::Bold));
        el->setTextColor(QColor(0x33, 0x33, 0x33));
        chart_->plotLayout()->addElement(0, 0, el);
    }

    // ── Final ─────────────────────────────────────────────
    chart_->setBackground(QBrush(Qt::white));
    chart_->axisRect()->setBackground(QBrush(Qt::white));
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

    for (int i = 0; i < itemModel_->rowCount(); ++i) {
        auto* checkItem = itemModel_->item(i, 0);
        if (!checkItem || checkItem->checkState() != Qt::Checked) continue;
        if (i >= static_cast<int>(items_.size())) continue;

        const auto& code = items_[i].item_code;
        const auto& name = items_[i].item_name;
        QString fileName = dir + "/" + sanitize(name) + "_" + fmt(code) + ".csv";
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

QPixmap TrendWindow::chartPixmap(const std::string& itemCode) {
    // Save current state
    auto savedCode = currentItemCode_;
    updateChart(itemCode);

    QPixmap pix(1600, 1000);
    pix.setDevicePixelRatio(1.0);
    chart_->render(&pix);

    // Restore
    updateChart(savedCode);
    return pix;
}

void TrendWindow::onExportImages() {
    QString dir = QFileDialog::getExistingDirectory(this,
        QString::fromWCharArray(L"选择导出文件夹"));
    if (dir.isEmpty()) return;

    for (int i = 0; i < itemModel_->rowCount(); ++i) {
        auto* checkItem = itemModel_->item(i, 0);
        if (!checkItem || checkItem->checkState() != Qt::Checked) continue;
        if (i >= static_cast<int>(items_.size())) continue;

        const auto& code = items_[i].item_code;
        const auto& name = items_[i].item_name;
        QString fileName = dir + "/" + sanitize(name) + "_" + fmt(code) + ".png";

        updateChart(code);
        QPixmap pix(1600, 1000);
        pix.setDevicePixelRatio(1.0);
        chart_->render(&pix);
        pix.save(fileName, "PNG");
    }
    updateChart(currentItemCode_);
    chart_->replot();

    QMessageBox::information(this, QString::fromWCharArray(L"导出完成"),
                             QString::fromWCharArray(L"已导出勾选项目的 PNG 图片。"));
}

QString TrendWindow::defaultFileName(const QString& ext) const {
    QString name;
    if (!lastQuery_.patient_name.empty()) name += fmt(lastQuery_.patient_name);
    if (!lastQuery_.patient_no.empty()) name += "_" + fmt(lastQuery_.patient_no);
    if (name.isEmpty()) name = "trend";
    return name + "." + ext;
}
