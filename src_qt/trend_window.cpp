#include "trend_window.h"
#include "trend_core.h"

#ifdef HAS_QWT
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_plot_zoneitem.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_layout.h>
#include <qwt_legend.h>
#include <qwt_symbol.h>
#include <qwt_scale_draw.h>
#include <qwt_scale_widget.h>
#endif

#include <QCoreApplication>
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

// ── helpers ──────────────────────────────────────────────────

static QString s8(const std::string& s) {
    return QString::fromUtf8(s.c_str(), static_cast<int>(s.size()));
}
static QStandardItem* ci(const QString& t) {
    auto* it = new QStandardItem(t); it->setEditable(false); return it;
}
static QString san(const std::string& s) {
    QString q = s8(s).trimmed();
    for (auto& ch : q) {
        switch (ch.unicode()) {
            case '\\': case '/': case ':': case '*':
            case '?':  case '"': case '<': case '>': case '|': ch = '_'; break;
        }
    }
    return q;
}
static QString ebn(const search::QueryInput& in) {
    QStringList p;
    auto a = san(in.patient_name), b = san(in.patient_no);
    auto c = san(in.start_date), d = san(in.end_date);
    QString dt;
    if (!c.isEmpty() && !d.isEmpty() && c != d) dt = c + "_" + d;
    else if (!c.isEmpty()) dt = c; else dt = d;
    if (!a.isEmpty()) p << a; if (!b.isEmpty()) p << b;
    if (!dt.isEmpty()) p << dt;
    if (p.isEmpty()) p << "trend_export";
    return p.join("-");
}

#ifdef HAS_QWT
// Custom X-axis tick labels: date + time, two-line
class TrendScaleDraw : public QwtScaleDraw {
public:
    TrendScaleDraw(const std::vector<const search::TrendPoint*>& pts)
        : pts_(pts) {}
    QwtText label(double v) const override {
        int i = static_cast<int>(v + 0.5);
        if (i < 0 || i >= static_cast<int>(pts_.size())) return QwtText();
        QString rt = s8(pts_[i]->report_time);
        QString datePart = rt.length() >= 10 ? rt.mid(5, 5) : rt;
        QString timePart = rt.length() >= 16 ? rt.mid(11, 5) : QString();
        return timePart.isEmpty() ? QwtText(datePart)
                                  : QwtText(datePart + "\n" + timePart);
    }
private:
    const std::vector<const search::TrendPoint*>& pts_;
};
#endif

// ── TrendWindow ──────────────────────────────────────────────

TrendWindow::TrendWindow(const search::DbSettings& db,
                         const search::QueryInput& lastQuery, QWidget* parent)
    : QDialog(parent), db_(db), lastQuery_(lastQuery) {
    setWindowTitle(QString::fromWCharArray(L"检验结果趋势图"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    auto scr = QGuiApplication::primaryScreen()->availableGeometry();
    resize(std::max(static_cast<int>(scr.width() * 0.8), 1200),
           std::max(static_cast<int>(scr.height() * 0.75), 700));
    setupUi();
    loadTrendData();
}

void TrendWindow::setupUi() {
    itemModel_ = new QStandardItemModel(0,1,this);
    itemModel_->setHorizontalHeaderLabels({QString::fromWCharArray(L"项目")});
    itemTable_ = new QTableView;
    itemTable_->setModel(itemModel_);
    itemTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    itemTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    itemTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    itemTable_->horizontalHeader()->setStretchLastSection(true);
    itemTable_->verticalHeader()->setVisible(false);

#ifdef HAS_QWT
    plot_ = new QwtPlot(this);
    plot_->setCanvasBackground(Qt::white);
    plot_->setMinimumHeight(300);
    plot_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Legend
    auto* legend = new QwtLegend;
    legend->setDefaultItemMode(QwtLegendData::ReadOnly);
    plot_->insertLegend(legend, QwtPlot::RightLegend);

    // Grid
    grid_ = new QwtPlotGrid;
    grid_->enableY(true);
    grid_->enableX(false);
    grid_->setMajorPen(QPen(QColor(0xE8,0xE8,0xE8), 1, Qt::DotLine));
    grid_->attach(plot_);

    // Curves (created in renderChart)
    lineCurve_ = new QwtPlotCurve(QString::fromWCharArray(L"结果线"));
    lineCurve_->setPen(QPen(QColor(0x1E,0x5F,0xB4), 2.5));
    lineCurve_->setStyle(QwtPlotCurve::Lines);
    lineCurve_->attach(plot_);

    normalScatter_ = new QwtPlotCurve(QString::fromWCharArray(L"正常"));
    highScatter_   = new QwtPlotCurve(QString::fromWCharArray(L"偏高"));
    lowScatter_    = new QwtPlotCurve(QString::fromWCharArray(L"低值"));

    // Reference zone (attached in renderChart)
    refZone_ = new QwtPlotZoneItem;
    refZone_->setOrientation(Qt::Horizontal);
    refZone_->setBrush(QColor(0xF2,0xF2,0xF2));

    chartWidget_ = plot_;
#else
    auto* label = new QLabel(QString::fromWCharArray(L"趋势图（待实现）\n\n请先在左侧选择项目"));
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumSize(600,400);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    label->setStyleSheet("QLabel{background:white; font-size:18px; color:#888;}");
    chartWidget_ = label;
#endif

    loadingLabel_ = new QLabel(QString::fromWCharArray(L"正在加载趋势数据..."));
    loadingLabel_->setAlignment(Qt::AlignCenter);

    auto* ca = new QVBoxLayout; ca->setContentsMargins(0,0,0,0);
    ca->addWidget(chartWidget_); ca->addWidget(loadingLabel_);
    auto* cc = new QWidget; cc->setMinimumHeight(300); cc->setLayout(ca);

    detailModel_ = new QStandardItemModel(0,7,this);
    detailModel_->setHorizontalHeaderLabels({
        QString::fromWCharArray(L"时间"),QString::fromWCharArray(L"项目"),
        QString::fromWCharArray(L"结果"),QString::fromWCharArray(L"单位"),
        QString::fromWCharArray(L"下限"),QString::fromWCharArray(L"上限"),
        QString::fromWCharArray(L"报告号")});
    detailTable_ = new QTableView;
    detailTable_->setModel(detailModel_);
    detailTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    detailTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    detailTable_->horizontalHeader()->setStretchLastSection(true);
    detailTable_->verticalHeader()->setVisible(false);

    auto* ls = new QSplitter(Qt::Vertical);
    ls->addWidget(cc); ls->addWidget(detailTable_);
    ls->setStretchFactor(0,2); ls->setStretchFactor(1,1);

    exportCsvBtn_ = new QPushButton(QString::fromWCharArray(L"导出勾选项目"));
    exportImageBtn_ = new QPushButton(QString::fromWCharArray(L"导出勾选图片"));
    exportCsvBtn_->setEnabled(false); exportImageBtn_->setEnabled(false);
    exportCsvBtn_->setFixedHeight(32); exportImageBtn_->setFixedHeight(32);

    auto* rp = new QWidget; auto* rl = new QVBoxLayout(rp);
    rl->setContentsMargins(0,0,0,0); rl->setSpacing(8);
    rl->addWidget(itemTable_,1);
    rl->addWidget(exportImageBtn_); rl->addWidget(exportCsvBtn_);
    rp->setMinimumWidth(200); rp->setMaximumWidth(350);

    mainSplitter_ = new QSplitter(Qt::Horizontal);
    mainSplitter_->addWidget(ls); mainSplitter_->addWidget(rp);
    mainSplitter_->setStretchFactor(0,1); mainSplitter_->setStretchFactor(1,0);
    mainSplitter_->setSizes({800,350});

    auto* ml = new QVBoxLayout(this); ml->addWidget(mainSplitter_);

    connect(itemTable_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &TrendWindow::onItemClicked);
    connect(exportCsvBtn_, &QPushButton::clicked, this, &TrendWindow::onExportCsv);
    connect(exportImageBtn_, &QPushButton::clicked, this, &TrendWindow::onExportImages);
}

void TrendWindow::loadTrendData() {
    auto& pts = points_; auto db = db_; auto in = lastQuery_;
    QtConcurrent::run([this,db,in,&pts]() {
        std::string err; std::vector<search::TrendPoint> r;
        if (search::query_trend_points(db,in,r,err)) pts = std::move(r);
        QMetaObject::invokeMethod(this, "onTrendDataLoaded", Qt::QueuedConnection);
    });
}

void TrendWindow::onTrendDataLoaded() {
    loadingLabel_->hide();
    if (points_.empty()) return;

    items_ = search::trend_item_options(points_);
    itemModel_->removeRows(0, itemModel_->rowCount());
    for (size_t i=0; i<items_.size(); ++i) {
        auto* chk = ci(s8(items_[i].item_name));
        chk->setCheckable(true); chk->setCheckState(Qt::Unchecked);
        itemModel_->appendRow(chk);
    }
    exportCsvBtn_->setEnabled(true);
#ifdef HAS_QWT
    exportImageBtn_->setEnabled(true);
#endif
    if (!items_.empty()) itemTable_->selectRow(0);
}

void TrendWindow::onItemClicked(const QModelIndex& idx) {
    if (!idx.isValid()) return;
    size_t i = static_cast<size_t>(idx.row());
    if (i >= items_.size()) return;
    currentItemCode_ = items_[i].item_code;
    renderChart(currentItemCode_);
}

#ifdef HAS_QWT
void TrendWindow::renderQwtChart(const std::vector<const search::TrendPoint*>& pts) {
    // Build data vectors
    QVector<double> x(pts.size()), y(pts.size());
    QVector<double> xn, yn, xh, yh, xlVals, ylVals;
    double yMin=pts[0]->result_value, yMax=pts[0]->result_value;
    bool hasRef=false; double refLo=0, refHi=0;

    for (size_t i=0; i<pts.size(); ++i) {
        double val = pts[i]->result_value;
        x[i] = static_cast<double>(i);
        y[i] = val;
        yMin = std::min(yMin, val); yMax = std::max(yMax, val);

        if (pts[i]->normal == "1")      { xh.push_back(i); yh.push_back(val); }
        else if (pts[i]->normal == "5") { xlVals.push_back(i); ylVals.push_back(val); }
        else                            { xn.push_back(i); yn.push_back(val); }

        if (!pts[i]->lower_bound.empty() || !pts[i]->upper_bound.empty()) {
            hasRef = true;
            try { refLo = std::stod(pts[i]->lower_bound); } catch(...) {}
            try { refHi = std::stod(pts[i]->upper_bound); } catch(...) {}
        }
    }

    double pad = (yMax - yMin) * 0.2;
    if (pad < 0.01) pad = 1.0;
    if (hasRef) { yMin = std::min(yMin,refLo) - pad; yMax = std::max(yMax,refHi) + pad; }
    else        { yMin -= pad; yMax += pad; }

    plot_->setAxisScale(QwtPlot::xBottom, -0.3, pts.size() - 0.7);
    plot_->setAxisScale(QwtPlot::yLeft, yMin, yMax);

    // Title
    std::string title = pts[0]->item_name;
    if (!pts[0]->item_eng.empty()) title += " (" + pts[0]->item_eng + ")";
    if (!pts[0]->unit.empty()) title += " [" + pts[0]->unit + "]";
    title += " 趋势图";
    plot_->setTitle(s8(title));

    // Axis labels
    plot_->setAxisTitle(QwtPlot::xBottom,
        QString::fromWCharArray(L"检测日期（按结果顺序）"));
    std::string yLabel = pts[0]->unit.empty() ? "结果值" : "结果值 (" + pts[0]->unit + ")";
    plot_->setAxisTitle(QwtPlot::yLeft, s8(yLabel));

    // Trend line
    lineCurve_->setSamples(x, y);

    // Scatter points — white-bordered filled circles (Win32 style)
    auto setScatter = [](QwtPlotCurve* c, const QVector<double>& xs,
                         const QVector<double>& ys, const QColor& fill) {
        if (xs.isEmpty()) { c->detach(); return; }
        c->setSamples(xs, ys);
        c->setStyle(QwtPlotCurve::Dots);
        QwtSymbol* sym = new QwtSymbol(QwtSymbol::Ellipse,
                                       QBrush(fill), QPen(Qt::white, 1.0), QSize(9,9));
        c->setSymbol(sym);
        c->attach(c->plot());
    };
    setScatter(normalScatter_, xn, yn, QColor(0x23,0x23,0x23));
    setScatter(highScatter_,   xh, yh, QColor(0xD2,0x28,0x28));
    setScatter(lowScatter_,    xlVals, ylVals, QColor(0x28,0x50,0xD2));

    // Reference zone
    refZone_->detach();
    if (hasRef) {
        refZone_->setInterval(std::min(refLo,refHi), std::max(refLo,refHi));
        refZone_->attach(plot_);
    }

    // X-axis ticks — max 5, date+time
    plot_->setAxisScaleDraw(QwtPlot::xBottom, new TrendScaleDraw(pts));
    plot_->setAxisMaxMajor(QwtPlot::xBottom, 5);

    // Fonts
    QFont axisFont("Microsoft YaHei", 9);
    plot_->setAxisFont(QwtPlot::xBottom, axisFont);
    plot_->setAxisFont(QwtPlot::yLeft, axisFont);

    plot_->replot();
}

QPixmap TrendWindow::renderQwtToPixmap(int w, int h) {
    QPixmap pix(w, h);
    pix.fill(Qt::white);
    QwtPlotRenderer renderer;
    renderer.renderTo(plot_, pix);
    return pix;
}
#endif

void TrendWindow::renderChart(const std::string& itemCode) {
    // Collect points for this item
    std::vector<const search::TrendPoint*> pts;
    for (const auto& p : points_)
        if (p.item_code == itemCode && p.has_numeric_value) pts.push_back(&p);

    // Detail table
    detailModel_->removeRows(0, detailModel_->rowCount());
    for (const auto* p : pts) {
        QList<QStandardItem*> row;
        row << ci(s8(p->report_time)) << ci(s8(p->item_name))
            << ci(s8(p->result_text)) << ci(s8(p->unit))
            << ci(s8(p->lower_bound)) << ci(s8(p->upper_bound))
            << ci(s8(p->rep_no));
        QColor fg = Qt::black;
        if (p->normal == "1") fg = QColor(0xD2,0x28,0x28);
        else if (p->normal == "5") fg = QColor(0x28,0x50,0xD2);
        for (auto* it : row) it->setForeground(fg);
        detailModel_->appendRow(row);
    }

    if (pts.size() < 2) return;  // keep previous chart

#ifdef HAS_QWT
    renderQwtChart(pts);
#endif
}

void TrendWindow::onExportCsv() {
    QString d = QFileDialog::getExistingDirectory(this,
        QString::fromWCharArray(L"选择导出文件夹"));
    if (d.isEmpty()) return;
    QString base = ebn(lastQuery_);
    for (int i=0; i<itemModel_->rowCount(); ++i) {
        auto* chk = itemModel_->item(i,0);
        if (!chk || chk->checkState() != Qt::Checked) continue;
        if (i >= (int)items_.size()) continue;
        const auto& code = items_[i].item_code;
        const auto& name = items_[i].item_name;
        QString fn = base + "-" + san(code);
        if (!name.empty()) fn += "-" + san(name);
        fn += ".csv";
        QFile f(d + "/" + fn);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) continue;
        QTextStream out(&f); out.setCodec("UTF-8");
        out << "\xEF\xBB\xBF"
            << QString::fromWCharArray(L"时间,项目,结果,单位,下限,上限,报告号\n");
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
#ifdef HAS_QWT
    QString d = QFileDialog::getExistingDirectory(this,
        QString::fromWCharArray(L"选择导出文件夹"));
    if (d.isEmpty()) return;
    QString base = ebn(lastQuery_);

    // Save current chart state
    std::string savedCode = currentItemCode_;

    for (int i=0; i<itemModel_->rowCount(); ++i) {
        auto* chk = itemModel_->item(i,0);
        if (!chk || chk->checkState() != Qt::Checked) continue;
        if (i >= (int)items_.size()) continue;
        const auto& code = items_[i].item_code;
        const auto& name = items_[i].item_name;
        QString fn = base + "-" + san(code);
        if (!name.empty()) fn += "-" + san(name);
        fn += ".png";

        // Generate chart for this item
        std::vector<const search::TrendPoint*> pts;
        for (const auto& p : points_)
            if (p.item_code == code && p.has_numeric_value) pts.push_back(&p);
        if (pts.size() < 2) continue;

        renderQwtChart(pts);
        QPixmap pix = renderQwtToPixmap(3200, 1800);
        pix.save(d + "/" + fn, "PNG");
    }

    // Restore current chart
    if (!savedCode.empty()) {
        currentItemCode_ = savedCode;
        std::vector<const search::TrendPoint*> pts;
        for (const auto& p : points_)
            if (p.item_code == savedCode && p.has_numeric_value) pts.push_back(&p);
        if (pts.size() >= 2) renderQwtChart(pts);
    }

    QMessageBox::information(this, QString::fromWCharArray(L"导出完成"),
                             QString::fromWCharArray(L"已导出勾选项目的 PNG 图片。"));
#else
    QMessageBox::information(this, QString::fromWCharArray(L"导出图片"),
                             QString::fromWCharArray(L"Qwt 未安装，暂不支持导出图片。"));
#endif
}
