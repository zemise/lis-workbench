#include <QApplication>
#include <QLabel>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QLabel label("检验结果查询 — Qt 5.15 迁移起点");
    label.setMinimumSize(400, 200);
    label.setAlignment(Qt::AlignCenter);
    label.show();
    return app.exec();
}
