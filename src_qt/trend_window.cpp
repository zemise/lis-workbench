#include "trend_window.h"
#include "search_text.h"
#include "trend_core.h"

#include <QApplication>
#include <QCheckBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

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
    resize(1100, 720);
    setAttribute(Qt::WA_DeleteOnClose);
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
    itemTable_->setMaximumWidth(220);

    // Chart (center)
    chart_ = new QCustomPlot;
    chart_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    chart_->xAxis->setLabel(QString::fromWCharArray(L"检测日期（按结果顺序等距）"));
    chart_->yAxis->setLabel(QString::fromWCharArray(L"结果值"));
    chart_->legend->setVisible(true);
    chart_->legend->setFont(QFont("Microsoft YaHei", 8));

    loadingLabel_ = new QLabel(QString::fromWCharArray(L"正在加载趋势数据..."));
    loadingLabel_->setAlignment(Qt::AlignCenter);
    auto* chartArea = new QVBoxLayout;
    chartArea->addWidget(chart_);
    chartArea->addWidget(loadingLabel_);
    auto* chartContainer = new QWidget;
    chartContainer->setLayout(chartArea);

    // Detail table (right/bottom)
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

    // Buttons
    exportCsvBtn_ = new QPushButton(QString::fromWCharArray(L"导出勾选项目"));
    exportImageBtn_ = new QPushButton(QString::fromWCharArray(L"导出勾选图片"));
    exportCsvBtn_->setEnabled(false);
    exportImageBtn_->setEnabled(false);
    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(exportCsvBtn_);
    btnLayout->addWidget(exportImageBtn_);

    // Main splitter: left items | center chart | detail table
    mainSplitter_ = new QSplitter(Qt::Horizontal);
    mainSplitter_->addWidget(itemTable_);
    mainSplitter_->addWidget(chartContainer);

    auto* rightPanel = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(detailTable_);
    rightLayout->addLayout(btnLayout);
    mainSplitter_->addWidget(rightPanel);
    mainSplitter_->setStretchFactor(0, 0);
    mainSplitter_->setStretchFactor(1, 1);
    mainSplitter_->setStretchFactor(2, 1);

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
        checkItem->setCheckState(Qt::Checked);
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
    QVector<QString> labels;
    double minVal = itemPoints[0]->result_value;
    double maxVal = itemPoints[0]->result_value;
    bool hasRef = false;
    double refLow = 0.0, refHigh = 0.0;

    for (const auto* p : itemPoints) {
        x.push_back(static_cast<double>(x.size()));
        y.push_back(p->result_value);
        labels.push_back(fmt(p->report_time));
        minVal = std::min(minVal, p->result_value);
        maxVal = std::max(maxVal, p->result_value);

        // Parse reference bounds
        if (!p->lower_bound.empty() || !p->upper_bound.empty()) {
            hasRef = true;
            try { refLow = std::stod(p->lower_bound); } catch (...) {}
            try { refHigh = std::stod(p->upper_bound); } catch (...) {}
        }
    }

    double padding = (maxVal - minVal) * 0.15;
    if (padding < 1e-9) padding = 1.0;
    double yMin = minVal - padding;
    double yMax = maxVal + padding;
    if (hasRef) {
        yMin = std::min(yMin, refLow - padding);
        yMax = std::max(yMax, refHigh + padding);
    }

    // Reference range band
    if (hasRef) {
        auto* band = new QCPGraph(chart_->xAxis, chart_->yAxis);
        QVector<double> bandX = {0.0, static_cast<double>(itemPoints.size()) - 1.0};
        QVector<double> bandHigh(bandX.size(), refHigh);
        QVector<double> bandLow(bandX.size(), refLow);
        // Draw reference band as a filled area between two graphs
        auto* upper = chart_->addGraph();
        upper->setData(bandX, bandHigh);
        upper->setPen(Qt::NoPen);
        auto* lower = chart_->addGraph();
        lower->setData(bandX, bandLow);
        lower->setPen(QPen(Qt::gray, 1, Qt::DashLine));
        auto* refBrush = new QCPGraph(chart_->xAxis, chart_->yAxis);
        // Use fill between: lower channel fills to upper
        lower->setChannelFillGraph(upper);
        lower->setBrush(QColor(200, 200, 200, 80));
        lower->setName(QString::fromWCharArray(L"参考区间"));
    }

    // Main trend line
    auto* graph = chart_->addGraph();
    graph->setData(x, y);
    graph->setPen(QPen(QColor(0x33, 0x6A, 0xE8), 2));
    graph->setLineStyle(QCPGraph::lsLine);
    graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 6));
    graph->setName(fmt(items_[0].item_name));

    // Color points by normal status
    for (int i = 0; i < y.size(); ++i) {
        QColor color = Qt::darkGray;
        if (i < static_cast<int>(itemPoints.size())) {
            if (itemPoints[i]->normal == "1") color = Qt::red;
            else if (itemPoints[i]->normal == "5") color = Qt::blue;
        }
        graph->selectionDecorator()->setPen(QPen(color, 2));
    }

    // Axes
    chart_->xAxis->setRange(-0.5, static_cast<double>(itemPoints.size()) - 0.5);
    chart_->yAxis->setRange(yMin, yMax);

    // X-axis ticks
    QVector<double> tickPositions;
    QVector<QString> tickLabels;
    int step = std::max(1, static_cast<int>(itemPoints.size()) / 10);
    for (int i = 0; i < labels.size(); i += step) {
        tickPositions.push_back(i);
        tickLabels.push_back(labels[i]);
    }
    chart_->xAxis->setAutoTicks(false);
    chart_->xAxis->setAutoTickLabels(false);
    chart_->xAxis->setTickVector(tickPositions);
    chart_->xAxis->setTickVectorLabels(tickLabels);
    chart_->xAxis->setTickLabelRotation(45);

    auto title = fmt(itemPoints[0]->item_name);
    if (!itemPoints[0]->item_eng.empty()) {
        title += " (" + fmt(itemPoints[0]->item_eng) + ")";
    }
    if (!itemPoints[0]->unit.empty()) {
        title += " [" + fmt(itemPoints[0]->unit) + "]";
    }
    chart_->plotLayout()->insertRow(0);
    chart_->plotLayout()->addElement(0, 0, new QCPTextElement(chart_, title, QFont("Microsoft YaHei", 11, QFont::Bold)));

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
        QColor bg = Qt::white;
        QColor fg = Qt::black;
        if (p->normal == "1") fg = Qt::red;
        else if (p->normal == "5") fg = Qt::blue;
        for (auto* it : row) {
            it->setBackground(bg);
            it->setForeground(fg);
        }
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
