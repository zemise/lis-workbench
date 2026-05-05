#pragma once

#include "app_settings.h"
#include "search_app.h"
#include "search_core.h"
#include "search_view_state.h"

#include <QCoreApplication>
#include <QMainWindow>

class QLineEdit;
class QDateEdit;
class QComboBox;
class QTableView;
class QSplitter;
class QLabel;
class QStandardItemModel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onQuery();
    void onRoomChanged(int index);
    void onReportSelected(const QModelIndex& current);
    void openSettings();
    void onShowTrend();

private:
    void setupUi();
    void setupQueryPanel(QWidget* panel);
    void setupReportTable();
    void setupResultTable();
    void setupButtonBar(QWidget* panel);
    void setupConnections();
    void loadInitialData();
    void applySettings();

    search::QueryInput buildInput() const;

    // ── Widgets ──────────────────────────────────────
    // Query form
    QLineEdit* patientIdEdit_ = nullptr;
    QLineEdit* barcodeEdit_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QLineEdit* patientNoEdit_ = nullptr;
    QLineEdit* operEdit_ = nullptr;
    QDateEdit* startDate_ = nullptr;
    QDateEdit* endDate_ = nullptr;
    QComboBox* roomCombo_ = nullptr;
    QComboBox* machCombo_ = nullptr;
    QComboBox* patientTypeCombo_ = nullptr;
    QComboBox* reportStatusCombo_ = nullptr;
    QLineEdit* groupEdit_ = nullptr;
    QLineEdit* itemEdit_ = nullptr;

    // Tables
    QTableView* reportTable_ = nullptr;
    QTableView* resultTable_ = nullptr;
    QStandardItemModel* reportModel_ = nullptr;
    QStandardItemModel* resultModel_ = nullptr;

    // Layout
    QSplitter* splitter_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    // ── Data ─────────────────────────────────────────
    search::ViewState state_;
    search::QueryInput lastQuery_;

    static QString iniPath() {
        return QCoreApplication::applicationDirPath() + "/result_search.ini";
    }
};
