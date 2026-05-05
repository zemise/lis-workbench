#include "trend_window.h"
#include "trend_core.h"

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

QString TrendWindow::gnuplotPath() const {
    QString l = QCoreApplication::applicationDirPath() + "/gnuplot/bin/gnuplot.exe";
    if (QFile::exists(l)) return l;
    for (auto* p : {"C:/Program Files/gnuplot/bin/gnuplot.exe",
                     "C:/Program Files (x86)/gnuplot/bin/gnuplot.exe"})
        if (QFile::exists(p)) return p;
    return "gnuplot";
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

    chartLabel_ = new QLabel(QString::fromWCharArray(L"请在左侧选择项目"));
    chartLabel_->setAlignment(Qt::AlignCenter);
    chartLabel_->setMinimumSize(600,400);
    chartLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    chartLabel_->setStyleSheet("QLabel{background:white;}");

    loadingLabel_ = new QLabel(QString::fromWCharArray(L"正在加载趋势数据..."));
    loadingLabel_->setAlignment(Qt::AlignCenter);

    auto* ca = new QVBoxLayout; ca->setContentsMargins(0,0,0,0);
    ca->addWidget(chartLabel_); ca->addWidget(loadingLabel_);
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
    if (points_.empty()) { chartLabel_->setText(QString::fromWCharArray(L"无数据")); return; }
    items_ = search::trend_item_options(points_);
    itemModel_->removeRows(0, itemModel_->rowCount());
    for (size_t i=0; i<items_.size(); ++i) {
        auto* chk = ci(s8(items_[i].item_name));
        chk->setCheckable(true); chk->setCheckState(Qt::Unchecked);
        itemModel_->appendRow(chk);
    }
    exportCsvBtn_->setEnabled(true); exportImageBtn_->setEnabled(true);
    if (!items_.empty()) itemTable_->selectRow(0);
}

void TrendWindow::onItemClicked(const QModelIndex& idx) {
    if (!idx.isValid()) return;
    size_t i = static_cast<size_t>(idx.row());
    if (i >= items_.size()) return;
    currentItemCode_ = items_[i].item_code;
    renderChart(currentItemCode_);
}

// ── gnuplot script builder (mirrors Win32 trend_chart_renderer) ─

static void writeGp(QTextStream& gp, const std::vector<const search::TrendPoint*>& pts,
                    int w, int h, const QString& outPath) {
    // Ranges (Win32: detect_bounds + value_range)
    double yMin=pts[0]->result_value, yMax=pts[0]->result_value;
    bool ref=false; double rLo=0, rHi=0;
    for (const auto* p : pts) {
        yMin=std::min(yMin,p->result_value); yMax=std::max(yMax,p->result_value);
        if (!p->lower_bound.empty()||!p->upper_bound.empty()) {
            ref=true;
            try { rLo=std::stod(p->lower_bound); } catch(...){}
            try { rHi=std::stod(p->upper_bound); } catch(...){}
        }
    }
    double pad=(yMax-yMin)*0.2; if(pad<0.01) pad=1.0;
    if(ref){yMin=std::min(yMin,rLo)-pad; yMax=std::max(yMax,rHi)+pad;}
    else   {yMin-=pad; yMax+=pad;}

    // Title (Win32: item_name (item_eng) - unit 趋势图)
    std::string t=pts[0]->item_name;
    if(!pts[0]->item_eng.empty()) t+=" ("+pts[0]->item_eng+")";
    if(!pts[0]->unit.empty()) t+=" ["+pts[0]->unit+"]";
    t+=" Trend";

    std::string yl="Value";
    if(!pts[0]->unit.empty()) yl+=" ("+pts[0]->unit+")";

    gp << "set terminal pngcairo enhanced size "<<w<<","<<h<<" font ',10'\n";
    gp << "set output '"<<outPath.toStdString().c_str()<<"'\n";
    gp << "set title '"<<t.c_str()<<"' font ',13'\n";
    gp << "set xlabel 'Date (by order)' font ',10'\n";
    gp << "set ylabel '"<<yl.c_str()<<"' font ',10'\n";
    gp << "set yrange ["<<yMin<<":"<<yMax<<"]\n";
    gp << "set grid ytics lc rgb '#E8E8E8'\n";
    gp << "set key inside right top font ',8'\n";
    gp << "set format y '%.4g'\n";
    gp << "set style fill transparent solid 0.3\n";

    // Reference band (Win32: fill_solid_rect)
    if(ref)
        gp<<"set object 1 rect from graph 0, first "<<rLo
          <<" to graph 1, first "<<rHi
          <<" fc rgb '#F2F2F2' fs solid 0.5 behind\n";

    // X-ticks: max 5, date+time two-line (Win32: format_time_part)
    int nT=std::min(5,(int)pts.size());
    gp<<"set xtics (";
    for(int t=0; t<nT; ++t){
        int i=(nT==1)?0:(int)std::llround(t*(pts.size()-1.0)/(nT-1.0));
        if(t>0) gp<<", ";
        QString rt=s8(pts[i]->report_time);
        QString lb=rt.length()>=16?rt.mid(5,5)+"\\n"+rt.mid(11,5)
                  :rt.length()>=10?rt.mid(5,5):rt;
        gp<<"\""<<lb.toStdString().c_str()<<"\" "<<i;
    }
    gp<<") font ',9' rotate by 0 offset 0,-0.8\n";

    // Plot: single inline data block (Win32: LineTo + Ellipse)
    gp<<"plot '-' u 1:2 w lines lw 2.5 lc rgb '#1E5FB4' ti 'Result'\n";

    for(size_t i=0; i<pts.size(); ++i)
        gp<<i<<" "<<pts[i]->result_value<<"\n";
    gp<<"e\n";
    gp.flush();
}

// ── renderChart (Win32: draw_trend_chart_to_rect) ────────────

void TrendWindow::renderChart(const std::string& itemCode) {
    std::vector<const search::TrendPoint*> pts;
    for(const auto& p : points_)
        if(p.item_code==itemCode && p.has_numeric_value) pts.push_back(&p);

    // Detail table (Win32: fill_trend_list)
    detailModel_->removeRows(0,detailModel_->rowCount());
    for(const auto* p : pts){
        QList<QStandardItem*> r;
        r<<ci(s8(p->report_time))<<ci(s8(p->item_name))<<ci(s8(p->result_text))
         <<ci(s8(p->unit))<<ci(s8(p->lower_bound))<<ci(s8(p->upper_bound))
         <<ci(s8(p->rep_no));
        QColor fg=Qt::black;
        if(p->normal=="1") fg=QColor(0xD2,0x28,0x28);
        else if(p->normal=="5") fg=QColor(0x28,0x50,0xD2);
        for(auto* it:r) it->setForeground(fg);
        detailModel_->appendRow(r);
    }
    if(pts.size()<2){
        chartLabel_->setText(QString::fromWCharArray(L"该项目不足两个有效数值点，暂无法绘制趋势线"));
        return;
    }

    QString gp=gnuplotPath();
    QString sfp=QDir::tempPath()+"/trend_"+QString::number(QCoreApplication::applicationPid())+".gp";
    QString pfp=QDir::tempPath()+"/trend_"+QString::number(QCoreApplication::applicationPid())+".png";
    QFile::remove(pfp);

    { // Write script
        QFile sf(sfp);
        if(!sf.open(QIODevice::WriteOnly|QIODevice::Text)) return;
        QTextStream out(&sf); out.setCodec("UTF-8");
        writeGp(out, pts, 1000, 600, pfp);
        sf.close();
    }

    QProcess proc; proc.start(gp,{sfp});
    if(!proc.waitForStarted(3000)){QFile::remove(sfp);return;}
    proc.waitForFinished(30000);
    QFile::remove(sfp);

    if(!QFile::exists(pfp)||!chartPixmap_.load(pfp)){
        chartLabel_->setText(QString::fromWCharArray(L"生成失败：")
                             +QString::fromUtf8(proc.readAllStandardError()));
        QFile::remove(pfp); return;
    }
    QFile::remove(pfp);
    chartLabel_->setPixmap(chartPixmap_.scaled(chartLabel_->size(),
                                                Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// ── export ───────────────────────────────────────────────────

void TrendWindow::onExportCsv() {
    QString d=QFileDialog::getExistingDirectory(this,QString::fromWCharArray(L"选择导出文件夹"));
    if(d.isEmpty()) return;
    QString base=ebn(lastQuery_);
    for(int i=0;i<itemModel_->rowCount();++i){
        auto* chk=itemModel_->item(i,0);
        if(!chk||chk->checkState()!=Qt::Checked) continue;
        if(i>=(int)items_.size()) continue;
        const auto& code=items_[i].item_code; const auto& name=items_[i].item_name;
        QString fn=base+"-"+san(code);
        if(!name.empty()) fn+="-"+san(name);
        fn+=".csv";
        QFile f(d+"/"+fn); if(!f.open(QIODevice::WriteOnly|QIODevice::Text)) continue;
        QTextStream out(&f); out.setCodec("UTF-8");
        out<<"\xEF\xBB\xBF"<<QString::fromWCharArray(L"时间,项目,结果,单位,下限,上限,报告号\n");
        for(const auto& p:points_){
            if(p.item_code!=code) continue;
            out<<s8(p.report_time)<<","<<s8(p.item_name)<<","<<s8(p.result_text)<<","
              <<s8(p.unit)<<","<<s8(p.lower_bound)<<","<<s8(p.upper_bound)<<","
              <<s8(p.rep_no)<<"\n";
        }
    }
    QMessageBox::information(this,QString::fromWCharArray(L"导出完成"),
                             QString::fromWCharArray(L"已导出勾选项目的 CSV 文件。"));
}

void TrendWindow::onExportImages() {
    QString d=QFileDialog::getExistingDirectory(this,QString::fromWCharArray(L"选择导出文件夹"));
    if(d.isEmpty()) return;
    QString base=ebn(lastQuery_);
    QString gp=gnuplotPath();
    for(int i=0;i<itemModel_->rowCount();++i){
        auto* chk=itemModel_->item(i,0);
        if(!chk||chk->checkState()!=Qt::Checked) continue;
        if(i>=(int)items_.size()) continue;
        const auto& code=items_[i].item_code; const auto& name=items_[i].item_name;
        QString fn=base+"-"+san(code);
        if(!name.empty()) fn+="-"+san(name);
        fn+=".png";
        QString outPath=d+"/"+fn;

        std::vector<const search::TrendPoint*> pts;
        for(const auto& p:points_)
            if(p.item_code==code&&p.has_numeric_value) pts.push_back(&p);
        if(pts.size()<2) continue;

        QString sfp=QDir::tempPath()+"/trend_exp_"+QString::number(QCoreApplication::applicationPid())+".gp";
        QFile sf(sfp);
        if(!sf.open(QIODevice::WriteOnly|QIODevice::Text)) continue;
        QTextStream out(&sf); out.setCodec("UTF-8");
        writeGp(out, pts, 1600, 900, outPath);
        sf.close();

        QProcess proc; proc.start(gp,{sfp});
        proc.waitForStarted(3000); proc.waitForFinished(30000);
        QFile::remove(sfp);
    }
    QMessageBox::information(this,QString::fromWCharArray(L"导出完成"),
                             QString::fromWCharArray(L"已导出勾选项目的 PNG 图片。"));
}
