#include "trend_window.h"
#include "trend_core.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QGuiApplication>
#include <QScreen>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
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

// ── TrendChartWidget ─────────────────────────────────────────

TrendChartWidget::TrendChartWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(600, 400);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void TrendChartWidget::setData(const std::vector<const search::TrendPoint*>& pts) {
    pts_ = pts;
    if (pts_.size() < 2) { update(); return; }

    yMin_ = pts_[0]->result_value; yMax_ = pts_[0]->result_value;
    hasRef_ = false; refLo_ = 0; refHi_ = 0;
    for (const auto* p : pts_) {
        yMin_ = std::min(yMin_, p->result_value);
        yMax_ = std::max(yMax_, p->result_value);
        if (!p->lower_bound.empty() || !p->upper_bound.empty()) {
            hasRef_ = true;
            try { refLo_ = std::stod(p->lower_bound); } catch(...) {}
            try { refHi_ = std::stod(p->upper_bound); } catch(...) {}
        }
    }
    double pad = (yMax_ - yMin_) * 0.2;
    if (pad < 0.01) pad = 1.0;
    if (hasRef_) { yMin_ = std::min(yMin_, refLo_) - pad; yMax_ = std::max(yMax_, refHi_) + pad; }
    else         { yMin_ -= pad; yMax_ += pad; }
    update();
}

QPixmap TrendChartWidget::exportPixmap(int w, int h) {
    QSize orig = size();
    resize(w, h);
    QPixmap pix = grab();
    resize(orig);
    return pix;
}

void TrendChartWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white);

    if (pts_.size() < 2) {
        p.setPen(QColor(0x99,0x99,0x99));
        p.setFont(QFont("Microsoft YaHei", 14));
        p.drawText(rect(), Qt::AlignCenter,
                   QString::fromWCharArray(L"该项目不足两个有效数值点，暂无法绘制趋势线"));
        return;
    }

    int w = width(), h = height();

    // ── Fonts ───────────────────────────────────
    QFont titleFont("Microsoft YaHei", 13, QFont::Bold);
    QFont labelFont("Microsoft YaHei", 10);
    QFont tickFont("Microsoft YaHei", 9);
    QFont legendFont("Microsoft YaHei", 8);
    QFontMetrics titleFm(titleFont), labelFm(labelFont), tickFm(tickFont);

    // ── Measure text sizes ──────────────────────
    std::string titleStr = pts_[0]->item_name;
    if (!pts_[0]->item_eng.empty()) titleStr += " (" + pts_[0]->item_eng + ")";
    if (!pts_[0]->unit.empty()) titleStr += " [" + pts_[0]->unit + "]";
    titleStr += " 趋势图";
    QString qTitle = s8(titleStr);
    int titleH = titleFm.height() + gap_;

    std::string yLabelStr = pts_[0]->unit.empty() ? "结果值" : "结果值 (" + pts_[0]->unit + ")";
    QString qYLabel = s8(yLabelStr);
    int yLabelW = labelFm.height() + 4;  // rotated text: width = font height + margin

    // Widest Y-axis tick label
    double yRange = yMax_ - yMin_;
    int maxYTickW = 0;
    for (int i = 0; i <= 5; ++i) {
        double val = yMin_ + yRange * i / 5;
        maxYTickW = std::max(maxYTickW,
            tickFm.horizontalAdvance(QString::number(val, 'g', 4)));
    }
    // yLabelW column on left, tick labels on right, gap to axis
    int tickPad = 6;  // match X-axis spacing
    int yAxisW = yLabelW + gap_ + maxYTickW + tickPad;

    QString qXLabel = QString::fromWCharArray(L"检测日期（按结果顺序）");
    int xTickH = tickFm.height() * 2 + gap_;   // date + time
    int xLabelH = labelFm.height() + gap_;
    int xAxisH = xTickH + xLabelH;

    QFontMetrics legendFm(legendFont);
    QString legendTexts[] = {
        QString::fromWCharArray(L"结果线"),
        QString::fromWCharArray(L"参考范围"),
        QString::fromWCharArray(L"高值"),
        QString::fromWCharArray(L"低值")
    };
    int legendTextW = 0;
    for (const auto& s : legendTexts)
        legendTextW = std::max(legendTextW, legendFm.horizontalAdvance(s));
    int legendW = legendTextW + 28;

    // ── Layout areas ───────────────────────────
    QRect titleArea(0, 0, w, titleH);
    QRect plotArea(yAxisW, titleH + gap_,
                   w - yAxisW - legendW - gap_ * 3,
                   h - titleH - xAxisH - gap_);
    QRect legendArea(w - legendW - gap_, titleH + gap_,
                     legendW, h - titleH - xAxisH - gap_);

    // ── Title ──────────────────────────────────
    p.setFont(titleFont);
    p.setPen(Qt::black);
    p.drawText(titleArea, Qt::AlignHCenter | Qt::AlignBottom, qTitle);

    // ── Y-axis tick labels + grid ──────────────
    p.setFont(tickFont);
    for (int i = 0; i <= 5; ++i) {
        double val = yMin_ + yRange * i / 5;
        int yy = plotArea.bottom()
               - static_cast<int>((val - yMin_) / yRange * plotArea.height());
        // Grid line
        p.setPen(QPen(QColor(0xE8,0xE8,0xE8), 1, Qt::DotLine));
        p.drawLine(plotArea.left(), yy, plotArea.right(), yy);
        // Tick mark (outward, 6px left of axis)
        p.setPen(QPen(QColor(0x55,0x55,0x55), 1.2));
        p.drawLine(plotArea.left() - 6, yy, plotArea.left(), yy);
        int tickLeft = yLabelW + gap_;
        p.drawText(QRect(tickLeft, yy - tickFm.height()/2, maxYTickW, tickFm.height()),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(val, 'g', 4));
    }
    // Y-axis label (vertical, in its own left column)
    p.save();
    p.setFont(labelFont);
    p.translate(yLabelW / 2, plotArea.center().y());
    p.rotate(-90);
    p.drawText(QRect(-plotArea.height()/2, 0,
                     plotArea.height(), yLabelW), Qt::AlignCenter, qYLabel);
    p.restore();

    // ── X-axis ──────────────────────────────────
    p.setFont(tickFont);
    int maxTicks = std::min(5, static_cast<int>(pts_.size()));
    for (int t = 0; t < maxTicks; ++t) {
        size_t idx = (maxTicks <= 1) ? 0
            : static_cast<size_t>(std::llround(t * (pts_.size() - 1.0) / (maxTicks - 1)));
        int xx = plotArea.left()
               + static_cast<int>(idx * plotArea.width() / std::max<size_t>(1, pts_.size() - 1));
        p.setPen(QColor(0x55,0x55,0x55));
        p.drawLine(xx, plotArea.bottom(), xx, plotArea.bottom() + 6);
        QString rt = s8(pts_[idx]->report_time);
        QString datePart = rt.length() >= 10 ? rt.mid(5, 5) : rt;
        QString timePart = rt.length() >= 16 ? rt.mid(11, 5) : "";
        int lw = std::min(60, plotArea.width() / maxTicks - 4);
        p.setClipping(false);
        p.drawText(QRect(xx - lw/2, plotArea.bottom() + 6, lw, tickFm.height()),
                   Qt::AlignHCenter | Qt::TextDontClip, datePart);
        if (!timePart.isEmpty())
            p.drawText(QRect(xx - lw/2, plotArea.bottom() + 8 + tickFm.height(),
                             lw, tickFm.height()), Qt::AlignHCenter | Qt::TextDontClip, timePart);
        p.setClipping(true);
    }
    p.setFont(labelFont);
    p.drawText(QRect(plotArea.left(), plotArea.bottom() + xTickH + gap_,
                     plotArea.width(), labelFm.height()),
               Qt::AlignHCenter, qXLabel);

    // ── Reference band ──────────────────────────
    if (hasRef_) {
        int yLo = plotArea.bottom() - static_cast<int>((refLo_ - yMin_) / yRange * plotArea.height());
        int yHi = plotArea.bottom() - static_cast<int>((refHi_ - yMin_) / yRange * plotArea.height());
        QRect refRect(plotArea.left(), std::min(yLo, yHi),
                      plotArea.width(), std::abs(yHi - yLo));
        p.fillRect(refRect, QColor(0xF2,0xF2,0xF2));
        p.setPen(QPen(QColor(0xAA,0xAA,0xAA), 1, Qt::DashLine));
        p.drawLine(plotArea.left(), yLo, plotArea.right(), yLo);
        p.drawLine(plotArea.left(), yHi, plotArea.right(), yHi);
    }

    // ── Axis frame ──────────────────────────────
    p.setPen(QPen(QColor(0x55,0x55,0x55), 1.2));
    p.drawLine(plotArea.topLeft(), plotArea.bottomLeft());
    p.drawLine(plotArea.bottomLeft(), plotArea.bottomRight());

    // ── Trend line ──────────────────────────────
    p.setRenderHint(QPainter::Antialiasing, false);
    QPen linePen(QColor(0x1E,0x5F,0xB4), 3.0);
    linePen.setJoinStyle(Qt::RoundJoin);
    p.setPen(linePen);
    for (size_t i = 0; i < pts_.size(); ++i) {
        int x = plotArea.left() + static_cast<int>(i * plotArea.width() / std::max<size_t>(1, pts_.size() - 1));
        int y = plotArea.bottom() - static_cast<int>((pts_[i]->result_value - yMin_) / yRange * plotArea.height());
        if (i == 0) continue;
        QPoint prev(plotArea.left() + static_cast<int>((i-1) * plotArea.width() / std::max<size_t>(1, pts_.size()-1)),
                     plotArea.bottom() - static_cast<int>((pts_[i-1]->result_value - yMin_) / yRange * plotArea.height()));
        p.drawLine(prev, QPoint(x, y));
    }
    p.setRenderHint(QPainter::Antialiasing, true);

    // ── Scatter points ──────────────────────────
    for (size_t i = 0; i < pts_.size(); ++i) {
        int x = plotArea.left() + static_cast<int>(i * plotArea.width() / std::max<size_t>(1, pts_.size() - 1));
        int y = plotArea.bottom() - static_cast<int>((pts_[i]->result_value - yMin_) / yRange * plotArea.height());
        QColor fill = QColor(0x23,0x23,0x23);
        if (pts_[i]->normal == "1") fill = QColor(0xD2,0x28,0x28);
        else if (pts_[i]->normal == "5") fill = QColor(0x28,0x50,0xD2);
        p.setPen(QPen(Qt::white, 2.5));
        p.setBrush(fill);
        p.drawEllipse(QPoint(x, y), 6, 6);
    }

    // ── Legend (dynamic sizing) ─────────────────
    int lx = legendArea.left() + 4;
    int itemH = legendFm.height() + 10;  // text + generous spacing
    int legendH = itemH * 4 + 8;

    p.setPen(QPen(QColor(0xDD,0xDD,0xDD), 0.5));
    p.setBrush(QColor(0xFF,0xFF,0xFF,0xE0));
    p.drawRect(lx - 2, legendArea.top() + 2, legendW, legendH);

    int ly = legendArea.top() + 6;
    int iconSz = 10;
    auto drawItem = [&](const QColor& c, const QString& text, bool isLine, bool isRect) {
        int midY = ly + itemH / 2;
        p.setPen(Qt::NoPen);
        if (isLine) {
            p.setPen(QPen(c, 2.5));
            p.drawLine(lx, midY, lx + 18, midY);
            p.setPen(Qt::NoPen);
        } else if (isRect) {
            p.fillRect(lx + 2, midY - 5, 14, 10, c);
            p.setPen(QPen(QColor(0x99,0x99,0x99), 0.8, Qt::DashLine));
            p.drawRect(lx + 2, midY - 5, 14, 10);
            p.setPen(Qt::NoPen);
        } else {
            p.setPen(QPen(Qt::white, 2));
            p.setBrush(c);
            p.drawEllipse(QPoint(lx + 11, midY), 5, 5);
            p.setPen(Qt::NoPen);
        }
        p.setPen(Qt::black);
        p.setFont(legendFont);
        p.drawText(lx + 24, ly, legendW - 28, itemH, Qt::AlignVCenter, text);
        ly += itemH;
    };
    drawItem(QColor(0x1E,0x5F,0xB4), QString::fromWCharArray(L"结果线"), true, false);
    drawItem(QColor(0xF2,0xF2,0xF2), QString::fromWCharArray(L"参考范围"), false, true);
    drawItem(QColor(0xD2,0x28,0x28), QString::fromWCharArray(L"高值"), false, false);
    drawItem(QColor(0x28,0x50,0xD2), QString::fromWCharArray(L"低值"), false, false);
}

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

    chart_ = new TrendChartWidget(this);

    loadingLabel_ = new QLabel(QString::fromWCharArray(L"正在加载趋势数据..."));
    loadingLabel_->setAlignment(Qt::AlignCenter);

    auto* ca = new QVBoxLayout; ca->setContentsMargins(0,0,0,0);
    ca->addWidget(chart_); ca->addWidget(loadingLabel_);
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

void TrendWindow::setMockData(const std::vector<search::TrendPoint>& data) {
    mockMode_ = true;
    points_ = data;
    onTrendDataLoaded();
}

void TrendWindow::loadTrendData() {
    if (mockMode_) return;
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
    exportImageBtn_->setEnabled(true);
    if (!items_.empty()) itemTable_->selectRow(0);
}

void TrendWindow::onItemClicked(const QModelIndex& idx) {
    if (!idx.isValid()) return;
    size_t i = static_cast<size_t>(idx.row());
    if (i >= items_.size()) return;
    currentItemCode_ = items_[i].item_code;

    std::vector<const search::TrendPoint*> pts;
    for (const auto& p : points_)
        if (p.item_code == currentItemCode_ && p.has_numeric_value) pts.push_back(&p);

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

    chart_->setData(pts);
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
        const auto& code = items_[i].item_code; const auto& name = items_[i].item_name;
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
    QString d = QFileDialog::getExistingDirectory(this,
        QString::fromWCharArray(L"选择导出文件夹"));
    if (d.isEmpty()) return;
    QString base = ebn(lastQuery_);
    std::string savedCode = currentItemCode_;

    for (int i=0; i<itemModel_->rowCount(); ++i) {
        auto* chk = itemModel_->item(i,0);
        if (!chk || chk->checkState() != Qt::Checked) continue;
        if (i >= (int)items_.size()) continue;
        const auto& code = items_[i].item_code; const auto& name = items_[i].item_name;
        QString fn = base + "-" + san(code);
        if (!name.empty()) fn += "-" + san(name);
        fn += ".png";

        std::vector<const search::TrendPoint*> pts;
        for (const auto& p : points_)
            if (p.item_code == code && p.has_numeric_value) pts.push_back(&p);
        if (pts.size() < 2) continue;
        chart_->setData(pts);
        QPixmap pix = chart_->exportPixmap(3200, 1800);
        pix.save(d + "/" + fn, "PNG");
    }

    // Restore
    currentItemCode_ = savedCode;
    std::vector<const search::TrendPoint*> pts;
    for (const auto& p : points_)
        if (p.item_code == savedCode && p.has_numeric_value) pts.push_back(&p);
    if (pts.size() >= 2) chart_->setData(pts);

    QMessageBox::information(this, QString::fromWCharArray(L"导出完成"),
                             QString::fromWCharArray(L"已导出勾选项目的 PNG 图片。"));
}
