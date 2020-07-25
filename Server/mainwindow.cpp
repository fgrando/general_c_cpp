#include "mainwindow.h"
#include "ui_mainwindow.h"



#include <QDebug>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextDocument>
#include <QFont>

#include <thread>

#include "server.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setupGUI();

    resize(600, 300);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::Print(std::string text)
{
    //
    // ATENTION: Since this call may come from another thread
    //           we use a signals to perform the change in the text object
    //           since we cannot change GUI interfaces from other threads.
    //           (do not call plainTextEdit->appendPlainText(text))
    //
    QString data = QString::fromStdString(text);
    qDebug() << data;

    emit EmitText(data);
}

void MainWindow::setupGUI()
{
    QString compilationTime = QString("%1T%2").arg(__DATE__).arg(__TIME__);
    this->setWindowTitle("Server - v0.1 - " + compilationTime);

    QWidget *mainWidget = new QWidget(this);

    // add bar with IP and port settings
    QHBoxLayout *hbox = new QHBoxLayout();
    m_lineEditIp = new QLineEdit("127.0.0.1");
    m_lineEditPort = new QLineEdit("50001");
    m_pushButtonReload = new QPushButton("Start!");
    connect(m_pushButtonReload, SIGNAL(clicked(bool)), this, SLOT(setupServer()));

    hbox->addWidget(m_lineEditIp);
    hbox->addWidget(m_lineEditPort);
    hbox->addWidget(m_pushButtonReload);

    // Add the output text area (set font to monospace to see the dumps correctly
    m_plainTextEdit = new QPlainTextEdit(mainWidget);
    QTextDocument *doc = m_plainTextEdit->document();
    QFont font = doc->defaultFont();
    font.setFamily("Courier New");
    doc->setDefaultFont(font);


    // connect the signal to receive inputs from socket thread
    connect(this, SIGNAL(EmitText(QString)), m_plainTextEdit, SLOT(appendPlainText(QString)));
    emit EmitText("set ip and port and click Start!");

    QVBoxLayout *vbox= new QVBoxLayout(mainWidget);
    vbox->addLayout(hbox);
    vbox->addWidget(m_plainTextEdit);

    this->setCentralWidget(mainWidget);
}

void MainWindow::setupServer()
{
    m_plainTextEdit->clear();
    emit(EmitText("connecting to " + m_lineEditIp->text() + ":" + m_lineEditPort->text()));

    // create the socket server
    Server *srv = new Server(this);

    if (srv->Init(m_lineEditIp->text().toStdString(), m_lineEditPort->text().toInt()) < 0) {
        emit(EmitText("Failed to create connections... Check ip/port and try again..."));
        delete srv;

    } else {
        new std::thread(&Server::ServerForever, srv);
        m_pushButtonReload->setEnabled(false);
        m_lineEditIp->setEnabled(false);
        m_lineEditPort->setEnabled(false);
    }
}
