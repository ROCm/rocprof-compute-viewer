#include <QApplication>
#include <future>
#include <vector>
#include "include/custom_layouts.h"
#include "mainwindow.h"

void close_window(MainWindow& w)
{
    QTimer* timer = new QTimer();
    timer = new QTimer();
    timer->setSingleShot(true);
    timer->setInterval(200);
    timer->start();
    QObject::connect(timer, &QTimer::timeout, &w, &MainWindow::close);
}

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);

    const char* param1 = "";
    const char* param2 = "";

    if (argc >= 2 && argv[1]) {
        if (argv[1][0] == 'a') {
            param1 = "../tests/data/";
            param2 = "../tests/data/";
        }
        else if (argv[1][0] == 'b') {
            param1 = "../tests/data/";
        }
        else if (argv[1][0] == 'c') {
            param2 = "../tests/data/";
        }
        else {
            param1 = "../tests///data///";
            param2 = "../tests///data///";
        }
    }

    MainWindow w({param1, param2});
    // w.show();
    close_window(w);
    return a.exec();
}
