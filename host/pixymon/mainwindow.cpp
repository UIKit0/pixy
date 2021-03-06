#include <stdexcept>
#include <QDebug>
#include <QMessageBox>
#include <QFileDialog>
#include <QMetaType>
#include "mainwindow.h"
#include "videowidget.h"
#include "console.h"
#include "interpreter.h"
#include "chirpmon.h"
#include "dfu.h"
#include "flash.h"
#include "ui_mainwindow.h"

extern ChirpProc c_grabFrame;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    m_ui(new Ui::MainWindow)
{
    qRegisterMetaType<Device>("Device");

    m_ui->setupUi(this);
    setWindowTitle(PIXYMON_TITLE);

    m_interpreter = 0;
    m_flash = NULL;
    m_pixyConnected = false;
    m_pixyDFUConnected = false;

    m_console = new ConsoleWidget(this);
    m_video = new VideoWidget(this);

    m_ui->imageLayout->addWidget(m_video);
    m_ui->imageLayout->addWidget(m_console);

    m_ui->toolBar->addAction(m_ui->actionPlay_Pause);

    m_ui->actionConfigure->setIcon(QIcon(":/icons/icons/config.png"));
    m_ui->toolBar->addAction(m_ui->actionConfigure);

    updateButtons();

    // start looking for devices
    m_connect = new ConnectEvent(this);
    if (m_connect->getConnected()==NONE)
        m_console->error("No Pixy devices have been detected.\n");
}

MainWindow::~MainWindow()
{
    if (m_interpreter)
        delete m_interpreter;
    if (m_connect)
        delete m_connect;
    delete m_ui;
}

void MainWindow::updateButtons()
{
    if (m_interpreter && m_interpreter->programRunning())
    {
        m_ui->actionPlay_Pause->setIcon(QIcon(":/icons/icons/stop.png"));
    }
    else
    {
        m_ui->actionPlay_Pause->setIcon(QIcon(":/icons/icons/play.png"));
    }

    if (m_pixyDFUConnected && m_pixyConnected) // we're in programming mode
    {
        m_ui->actionPlay_Pause->setEnabled(false);
    }
    else if (m_pixyConnected)
    {
        m_ui->actionPlay_Pause->setEnabled(true);
    }
    else // nothing connected
    {
        m_ui->actionPlay_Pause->setEnabled(false);
    }
}

void MainWindow::close()
{
    QCoreApplication::exit();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    close();
}

void MainWindow::handleRunState(bool state)
{
    updateButtons();
}

void MainWindow::connectPixyDFU(bool state)
{
    if (state) // connect
    {
        m_console->print("Pixy programming state detected.\n");
        try
        {
            Dfu *dfu;
            dfu = new Dfu();
            dfu->download("pixyflash.bin.hdr");
            m_pixyDFUConnected = true;
            delete dfu;
            m_connect = new ConnectEvent(this);
        }
        catch (std::runtime_error &exception)
        {
            m_pixyDFUConnected = false;
            m_console->error(QString(exception.what())+"\n");
        }
    }
    else // disconnect
    {
        m_console->error("Pixy DFU has stopped working.\n");
        // if we're disconnected, start the connect thread
        m_connect = new ConnectEvent(this);
        m_pixyDFUConnected = false;
    }

    updateButtons();
}

void MainWindow::connectPixy(bool state)
{
    if (state) // connect
    {
        try
        {
            if (m_pixyDFUConnected) // we're in programming mode
            {
                try
                {
                    m_flash = new Flash();
                }
                catch (std::runtime_error &exception)
                {
                    m_console->error(QString(exception.what())+"\n");
                    m_flash = NULL;
                    m_pixyDFUConnected = false;
                }
            }
            else
            {
                m_console->print("Pixy detected.\n");
                m_interpreter = new Interpreter(m_console, m_video, this);
#if 0
                // start with a program (normally would be read from a config file instead of hard-coded)
                m_interpreter->beginProgram();
                m_interpreter->call("cam_getFrame 33, 0, 0, 320, 200");
                m_interpreter->endProgram();
                m_interpreter->runProgram();
#endif
                connect(m_interpreter, SIGNAL(runState(bool)), this, SLOT(handleRunState(bool)));
                m_console->acceptInput(true);
            }
            m_pixyConnected = true;
        }
        catch (std::runtime_error &exception)
        {
            m_console->error(QString(exception.what())+"\n");
            m_pixyConnected = false;
        }
    }
    else // disconnect
    {
        m_console->error("Pixy has stopped working.\n");
        if (m_interpreter)
        {
            delete m_interpreter;
            m_interpreter = NULL;
        }
        m_video->clear();
        m_console->acceptInput(false);
        // if we're disconnected, start the connect thread
        m_connect = new ConnectEvent(this);
        m_pixyConnected = false;
    }

    updateButtons();
}

void MainWindow::handleConnected(Device device, bool state)
{
    // kill connect thread
    if (m_connect)
    {
        delete m_connect;
        m_connect = NULL;
    }
    if (device==PIXY)
        connectPixy(state);
    else if (device==PIXY_DFU)
        connectPixyDFU(state);
}

void MainWindow::on_actionPlay_Pause_triggered()
{
    if (m_interpreter->programRunning())
        m_interpreter->stopProgram();
    else
        m_interpreter->runRemoteProgram();
    updateButtons();
}


void MainWindow::on_actionProgram_triggered()
{
    if (!m_pixyDFUConnected || !m_pixyConnected)
    {
        m_console->print("Pixy needs to be put into programming mode.\n"
                         "Do this by unplugging the USB cable, holding down the button,\n"
                         "and then plugging the USB cable back in while continuing to\n"
                         "hold down the button. You may do this now.\n");
    }
    else
    {
        QFileDialog fd(this);
        fd.setWindowTitle("Select a Firmware File");
        fd.setDirectory(QDir("\\"));
        fd.setNameFilter("Firmware (*.hex)");

        if (fd.exec())
        {
            QStringList slist = fd.selectedFiles();
            if (slist.size()==1)
            {
                if (m_flash)
                {
                    try
                    {
                        m_console->print("Programming...\n");
                        QApplication::processEvents(); // render message before we continue
                        m_flash->program(slist.at(0));
                        m_console->print("done! (Reset Pixy by unplugging/plugging USB cable.)\n");

                    }
                    catch (std::runtime_error &exception)
                    {
                        m_console->error(QString(exception.what())+"\n");
                    }

                    // clean up...
                    delete m_flash;
                    m_flash = NULL;
                    m_pixyDFUConnected = false;
                    m_pixyConnected = false;

                    // start looking for devices again (wait 4 seconds before we start looking though)
                    m_connect = new ConnectEvent(this, 4000);
                }
            }
        }
    }
}

void MainWindow::on_actionExit_triggered()
{
    close();
}
