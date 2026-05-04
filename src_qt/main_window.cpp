#include "main_window.h"
#include "settings_dialog.h"
#include "search_controller.h"
#include "trend_window.h"

#include <QApplication>
#include <QComboBox>
#include <QGuiApplication>
#include <QScreen>
#include <QDateEdit>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTableView>
#include <QVBoxLayout>

// ── helpers ──────────────────────────────────────────────────

static QStandardItem* cell(const QString& text) {
    auto* item = new QStandardItem(text);
    item->setEditable(false);
    return item;
}

static QStandardItem* coloredCell(const QString& text, const QColor& bg, const QColor& fg = Qt::black) {
    auto* item = cell(text);
    item->setBackground(bg);
    item->setForeground(fg);
    return item;
}

static QString fromUtf8(const std::string& s) {
    return QString::fromUtf8(s.c_str(), static_cast<int>(s.size()));
}

// ── MainWindow ───────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QString::fromWCharArray(L"检验结果查询 — Qt 5.15"));
    auto screen = QGuiApplication::primaryScreen()->availableGeometry();
    resize(static_cast<int>(screen.width() * 0.85), static_cast<int>(screen.height() * 0.85));
    applySettings();
    setupUi();
    setupConnections();
    loadInitialData();
}

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    setCentralWidget(central);

    // Left query panel
    auto* queryPanel = new QWidget;
    queryPanel->setMinimumWidth(240);
    queryPanel->setMaximumWidth(400);
    setupQueryPanel(queryPanel);

    // Center report table
    setupReportTable();

    // Right result table
    setupResultTable();

    // Splitter
    splitter_ = new QSplitter(Qt::Horizontal);
    splitter_->addWidget(queryPanel);
    splitter_->addWidget(reportTable_);
    splitter_->addWidget(resultTable_);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setStretchFactor(2, 1);
    splitter_->restoreState(settings_.value("UI/SplitterState").toByteArray());

    // Bottom bar
    auto* bottom = new QWidget;
    bottom->setFixedHeight(44);
    setupButtonBar(bottom);

    // Main layout
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(4, 4, 4, 0);
    mainLayout->setSpacing(2);
    mainLayout->addWidget(splitter_);
    mainLayout->addWidget(bottom);

    statusLabel_ = new QLabel(QString::fromWCharArray(L"请输入条件后查询。"));
    statusBar()->addWidget(statusLabel_, 1);
}


void MainWindow::setupQueryPanel(QWidget* panel) {
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);

    auto makeLine = [&](const QString& label, QWidget* widget) {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel(label);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setMinimumWidth(90);
        lbl->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
        row->addWidget(lbl);
        widget->setMinimumWidth(140);
        row->addWidget(widget, 1);
        layout->addLayout(row);
        layout->addSpacing(2);
    };

    patientIdEdit_ = new QLineEdit;    makeLine(QString::fromWCharArray(L"诊疗卡号"), patientIdEdit_);
    barcodeEdit_ = new QLineEdit;      makeLine(QString::fromWCharArray(L"条码号"),   barcodeEdit_);
    nameEdit_ = new QLineEdit;         makeLine(QString::fromWCharArray(L"病人姓名"), nameEdit_);
    patientNoEdit_ = new QLineEdit;    makeLine(QString::fromWCharArray(L"病人号"),   patientNoEdit_);
    operEdit_ = new QLineEdit;         makeLine(QString::fromWCharArray(L"样本号"),   operEdit_);

    startDate_ = new QDateEdit(QDate::currentDate());
    startDate_->setCalendarPopup(true);
    startDate_->setDisplayFormat("yyyy-MM-dd");
    makeLine(QString::fromWCharArray(L"开始日期"), startDate_);

    endDate_ = new QDateEdit(QDate::currentDate());
    endDate_->setCalendarPopup(true);
    endDate_->setDisplayFormat("yyyy-MM-dd");
    makeLine(QString::fromWCharArray(L"结束日期"), endDate_);

    // Test condition group
    auto* testGroup = new QGroupBox(QString::fromWCharArray(L"检验条件"));
    auto* testLayout = new QVBoxLayout(testGroup);
    roomCombo_ = new QComboBox;        testLayout->addWidget(roomCombo_);
    machCombo_ = new QComboBox;        testLayout->addWidget(machCombo_);
    layout->addWidget(testGroup);

    // Patient condition group
    auto* patGroup = new QGroupBox(QString::fromWCharArray(L"病人条件"));
    auto* patLayout = new QVBoxLayout(patGroup);
    patientTypeCombo_ = new QComboBox;  patLayout->addWidget(patientTypeCombo_);
    reportStatusCombo_ = new QComboBox; patLayout->addWidget(reportStatusCombo_);
    layout->addWidget(patGroup);

    groupEdit_ = new QLineEdit;        makeLine(QString::fromWCharArray(L"组合项目"), groupEdit_);
    itemEdit_ = new QLineEdit;         makeLine(QString::fromWCharArray(L"项目代码"), itemEdit_);

    layout->addStretch();
}

void MainWindow::setupReportTable() {
    reportModel_ = new QStandardItemModel(0, 15, this);
    reportModel_->setHorizontalHeaderLabels({
        QString::fromWCharArray(L"样本号"),
        QString::fromWCharArray(L"姓名"),
        QString::fromWCharArray(L"条码号"),
        QString::fromWCharArray(L"上机时间"),
        QString::fromWCharArray(L"性别"),
        QString::fromWCharArray(L"年龄"),
        QString::fromWCharArray(L"床号"),
        QString::fromWCharArray(L"病人类型"),
        QString::fromWCharArray(L"检验者"),
        QString::fromWCharArray(L"审核者"),
        QString::fromWCharArray(L"项目名称"),
        QString::fromWCharArray(L"审核"),
        QString::fromWCharArray(L"确认"),
        QString::fromWCharArray(L"打印"),
        QString::fromWCharArray(L"自助机")
    });

    reportTable_ = new QTableView;
    reportTable_->setModel(reportModel_);
    reportTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    reportTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    reportTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    reportTable_->horizontalHeader()->setStretchLastSection(true);
    reportTable_->verticalHeader()->setVisible(false);
    reportTable_->setSortingEnabled(false);
}

void MainWindow::setupResultTable() {
    resultModel_ = new QStandardItemModel(0, 6, this);
    resultModel_->setHorizontalHeaderLabels({
        QString::fromWCharArray(L"项目名称"),
        QString::fromWCharArray(L"结果"),
        QString::fromWCharArray(L"下限"),
        QString::fromWCharArray(L"上限"),
        QString::fromWCharArray(L"单位"),
        QString::fromWCharArray(L"英文名称")
    });

    resultTable_ = new QTableView;
    resultTable_->setModel(resultModel_);
    resultTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    resultTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultTable_->horizontalHeader()->setStretchLastSection(true);
    resultTable_->verticalHeader()->setVisible(false);
}

void MainWindow::setupButtonBar(QWidget* panel) {
    auto* layout = new QHBoxLayout(panel);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(8);

    auto makeBtn = [&](const QString& text) {
        auto* btn = new QPushButton(text);
        btn->setFixedSize(96, 34);
        return btn;
    };

    layout->addStretch();
    auto* settingsBtn = makeBtn(QString::fromWCharArray(L"设置"));
    auto* queryBtn    = makeBtn(QString::fromWCharArray(L"查询"));
    auto* trendBtn    = makeBtn(QString::fromWCharArray(L"趋势图"));
    auto* exportBtn   = makeBtn(QString::fromWCharArray(L"导出"));
    auto* previewBtn  = makeBtn(QString::fromWCharArray(L"预览"));
    auto* printBtn    = makeBtn(QString::fromWCharArray(L"打印"));
    auto* exitBtn     = makeBtn(QString::fromWCharArray(L"退出"));

    layout->addWidget(settingsBtn);
    layout->addWidget(queryBtn);
    layout->addWidget(trendBtn);
    layout->addWidget(exportBtn);
    layout->addWidget(previewBtn);
    layout->addWidget(printBtn);
    layout->addWidget(exitBtn);

    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettings);
    connect(queryBtn,    &QPushButton::clicked, this, &MainWindow::onQuery);
    connect(trendBtn,    &QPushButton::clicked, this, &MainWindow::onShowTrend);
    connect(exitBtn,     &QPushButton::clicked, qApp, &QApplication::quit);
    // export, preview, print — unimplemented
    connect(exportBtn,  &QPushButton::clicked, this, [] {
        QMessageBox::information(nullptr, QString::fromWCharArray(L"提示"),
                                 QString::fromWCharArray(L"该功能暂未实现。"));
    });
    connect(previewBtn, &QPushButton::clicked, exportBtn, &QPushButton::clicked);
    connect(printBtn,   &QPushButton::clicked, exportBtn, &QPushButton::clicked);
}

void MainWindow::setupConnections() {
    connect(roomCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onRoomChanged);
    connect(reportTable_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onReportSelected);
}

void MainWindow::applySettings() {
    state_.settings.db.server = settings_.value("Database/Server").toString().toStdWString();
    state_.settings.db.initial_database = settings_.value("Database/InitialDatabase").toString().toStdWString();
    state_.settings.db.user = settings_.value("Database/User").toString().toStdWString();
    state_.settings.db.password = settings_.value("Database/Password").toString().toStdWString();
    state_.settings.ui.font_size = settings_.value("UI/FontSize", 9).toInt();

    int splitterX = settings_.value("UI/SplitterX", 0).toInt();
    if (splitterX > 0 && splitter_) {
        splitter_->restoreState(settings_.value("UI/SplitterState").toByteArray());
    }
}

void MainWindow::loadInitialData() {
    const auto& db = state_.settings.db;
    if (db.server.empty()) {
        return;
    }

    QString error;
    std::string err;

    state_.room_options.clear();
    if (search::load_room_options(db, state_.room_options, err)) {
        roomCombo_->addItem(QString::fromWCharArray(L"全部"));
        for (const auto& row : state_.room_options) {
            roomCombo_->addItem(fromUtf8(row.room_name));
        }
    }

    state_.patient_type_options.clear();
    if (search::load_patient_type_options(db, state_.patient_type_options, err)) {
        patientTypeCombo_->addItem(QString::fromWCharArray(L"全部"));
        for (const auto& row : state_.patient_type_options) {
            patientTypeCombo_->addItem(fromUtf8(row.type_code + "-" + row.type_name));
        }
    }

    state_.machine_options.clear();
    if (search::load_machine_options(db, "", state_.machine_options, err)) {
        machCombo_->addItem(QString::fromWCharArray(L"全部"));
        for (const auto& row : state_.machine_options) {
            machCombo_->addItem(fromUtf8(row.mach_name));
        }
    }

    reportStatusCombo_->addItem(QString::fromWCharArray(L"全部"));
    reportStatusCombo_->addItem(QString::fromWCharArray(L"已审核"));
    reportStatusCombo_->addItem(QString::fromWCharArray(L"未审核"));
    reportStatusCombo_->addItem(QString::fromWCharArray(L"已发送"));
    reportStatusCombo_->addItem(QString::fromWCharArray(L"未发送"));
}

// ── slots ────────────────────────────────────────────────────

void MainWindow::onQuery() {
    const auto& db = state_.settings.db;
    if (db.server.empty()) {
        QMessageBox::warning(this, QString::fromWCharArray(L"查询"),
                             QString::fromWCharArray(L"请先配置数据库连接。"));
        return;
    }

    const auto input = buildInput();
    lastQuery_ = input;
    std::string error;
    state_.report_rows.clear();
    if (!search::run_report_query(db, input, state_.report_rows, state_.connection_string, error)) {
        QMessageBox::critical(this, QString::fromWCharArray(L"查询失败"), fromUtf8(error));
        return;
    }

    // Populate report table
    reportModel_->removeRows(0, reportModel_->rowCount());
    for (const auto& row : state_.report_rows) {
        QList<QStandardItem*> items;
        items << cell(fromUtf8(row.oper_no))
              << cell(fromUtf8(row.name))
              << cell(fromUtf8(row.txm_no))
              << cell(fromUtf8(row.chk_date))
              << cell(fromUtf8(row.sex))
              << cell(fromUtf8(row.age))
              << cell(fromUtf8(row.bed_code))
              << cell(fromUtf8(row.patient_type))
              << cell(fromUtf8(row.requester))
              << cell(fromUtf8(row.reviewer))
              << cell(fromUtf8(row.group_name))
              << cell(fromUtf8(search::display_conf(row.conf)))
              << cell(fromUtf8(row.chk_flag))
              << cell(fromUtf8(search::display_binary_print_flag(row.zymz_print)))
              << cell(fromUtf8(search::display_binary_print_flag(row.zzj_print)));

        // Row background color
        QColor bg = Qt::white;
        switch (search::report_row_tone(row)) {
            case search::ReportRowTone::Printed:  bg = QColor(0xFF, 0xFF, 0xFF); break;
            case search::ReportRowTone::Reviewed: bg = QColor(0x75, 0xFB, 0xFD); break;
            case search::ReportRowTone::Pending:  bg = QColor(0xED, 0x6D, 0x52); break;
        }
        for (auto* item : items) {
            item->setBackground(bg);
        }
        reportModel_->appendRow(items);
    }

    resultModel_->removeRows(0, resultModel_->rowCount());
    statusLabel_->setText(fromUtf8(search::make_query_count_status(state_.report_rows.size())));
}

void MainWindow::onRoomChanged(int index) {
    if (index < 0 || state_.room_options.empty()) {
        return;
    }
    const auto& db = state_.settings.db;
    std::string roomCode;
    int dataIndex = index - 1;  // skip "全部"
    if (dataIndex >= 0 && static_cast<size_t>(dataIndex) < state_.room_options.size()) {
        roomCode = state_.room_options[static_cast<size_t>(dataIndex)].room_code;
    }

    std::string error;
    state_.machine_options.clear();
    machCombo_->clear();
    machCombo_->addItem(QString::fromWCharArray(L"全部"));
    if (search::load_machine_options(db, roomCode, state_.machine_options, error)) {
        for (const auto& row : state_.machine_options) {
            machCombo_->addItem(fromUtf8(row.mach_name));
        }
    }
}

void MainWindow::onReportSelected(const QModelIndex& current) {
    if (!current.isValid()) {
        return;
    }
    int row = current.row();
    if (row < 0 || row >= static_cast<int>(state_.report_rows.size())) {
        return;
    }

    const auto& repNo = state_.report_rows[static_cast<size_t>(row)].rep_no;

    std::string error;
    state_.result_rows.clear();
    if (!search::load_result_rows(state_.connection_string, repNo, state_.result_rows, error)) {
        return;
    }

    resultModel_->removeRows(0, resultModel_->rowCount());
    for (const auto& r : state_.result_rows) {
        QColor bg = Qt::white;
        QColor fg = Qt::black;
        switch (search::result_row_tone(r)) {
            case search::ResultRowTone::High: fg = QColor(220, 0, 0); break;
            case search::ResultRowTone::Low:  fg = QColor(0, 0, 220); break;
            default: break;
        }
        QList<QStandardItem*> items;
        items << coloredCell(fromUtf8(r.item_name),   bg, fg)
              << coloredCell(fromUtf8(r.result),      bg, fg)
              << coloredCell(fromUtf8(r.downbound),   bg, fg)
              << coloredCell(fromUtf8(r.upbound),     bg, fg)
              << coloredCell(fromUtf8(r.unit),        bg, fg)
              << coloredCell(fromUtf8(r.item_eng),    bg, fg);
        resultModel_->appendRow(items);
    }
}

void MainWindow::openSettings() {
    SettingsDialog dlg(state_.settings.db, state_.settings.ui.font_size, this);
    if (dlg.exec() == QDialog::Accepted) {
        state_.settings.db = dlg.dbSettings();
        state_.settings.ui.font_size = dlg.fontSize();

        settings_.setValue("Database/Server", QString::fromStdWString(state_.settings.db.server));
        settings_.setValue("Database/InitialDatabase", QString::fromStdWString(state_.settings.db.initial_database));
        settings_.setValue("Database/User", QString::fromStdWString(state_.settings.db.user));
        settings_.setValue("Database/Password", QString::fromStdWString(state_.settings.db.password));
        settings_.setValue("UI/FontSize", state_.settings.ui.font_size);

        roomCombo_->clear();
        machCombo_->clear();
        patientTypeCombo_->clear();
        reportStatusCombo_->clear();
        loadInitialData();

        statusBar()->showMessage(QString::fromWCharArray(L"设置已保存"), 3000);
    }
}

void MainWindow::onShowTrend() {
    if (lastQuery_.patient_name.empty() && lastQuery_.patient_no.empty()) {
        QMessageBox::information(this, QString::fromWCharArray(L"趋势图"),
                                 QString::fromWCharArray(L"请先使用病人姓名或病人号执行查询。"));
        return;
    }
    TrendWindow dlg(state_.settings.db, lastQuery_, this);
    dlg.exec();
}

// ── helpers ──────────────────────────────────────────────────

search::QueryInput MainWindow::buildInput() const {
    search::QueryInput in;
    auto s = [](const QString& q) { return q.toUtf8().toStdString(); };
    in.patient_id    = s(patientIdEdit_->text());
    in.barcode       = s(barcodeEdit_->text());
    in.patient_name  = s(nameEdit_->text());
    in.patient_no    = s(patientNoEdit_->text());
    in.oper_no       = s(operEdit_->text());
    in.start_date    = s(startDate_->date().toString("yyyy-MM-dd"));
    in.end_date      = s(endDate_->date().toString("yyyy-MM-dd"));
    in.group_code    = s(groupEdit_->text());
    in.item_code     = s(itemEdit_->text());
    in.limit         = 0;

    // Room
    int roomIdx = roomCombo_->currentIndex();
    if (roomIdx > 0 && static_cast<size_t>(roomIdx - 1) < state_.room_options.size()) {
        in.room_code = state_.room_options[static_cast<size_t>(roomIdx - 1)].room_code;
    }

    // Patient type
    int ptIdx = patientTypeCombo_->currentIndex();
    if (ptIdx > 0 && static_cast<size_t>(ptIdx - 1) < state_.patient_type_options.size()) {
        in.patient_type = state_.patient_type_options[static_cast<size_t>(ptIdx - 1)].type_code;
    }

    // Machine
    int machIdx = machCombo_->currentIndex();
    if (machIdx > 0 && static_cast<size_t>(machIdx - 1) < state_.machine_options.size()) {
        in.mach_code = state_.machine_options[static_cast<size_t>(machIdx - 1)].mach_code;
    }

    // Report status ("全部" → empty)
    QString statusText = reportStatusCombo_->currentText();
    in.report_status = (reportStatusCombo_->currentIndex() <= 0)
                           ? "" : statusText.toUtf8().toStdString();

    return in;
}
