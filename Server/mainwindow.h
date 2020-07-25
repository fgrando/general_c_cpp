#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>

#include "textoutput.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow, public TextOutput
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void Print(std::string text);

signals:
    void EmitText(QString text);

private slots:
    void setupServer();

private:
    Ui::MainWindow *ui;

    // GUI elements (I didnt add elements to *ui)
    void setupGUI();
    QPlainTextEdit *m_plainTextEdit;
    QLineEdit *m_lineEditIp;
    QLineEdit *m_lineEditPort;
    QPushButton *m_pushButtonReload;
};

#endif // MAINWINDOW_H
