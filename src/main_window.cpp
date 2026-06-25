#include "main_window.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QThread>
#include <QTime>
#include <QVBoxLayout>

constexpr int kControlPanelWidth = 300;
constexpr int kAlarmTableHeight = 200;

MainWindow::MainWindow(const std::string& videoPath)
{
    setWindowTitle("Gauge Reader");
    resize(1600, 1000);
    setStyleSheet("QWidget { font-size: 13px; }"
                  "QWidget#controlPanel { background-color: #1e1e24; }"
                  "QLabel { color: #d0d0d0; }"
                  "QCheckBox { color: #d0d0d0; }");

    // ── Menu bar ────────────────────────────────────────────────
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction("E&xit");
    connect(exitAction, &QAction::triggered, this, [this]() {
        close();
    });

    auto* helpMenu = menuBar()->addMenu("&Help");
    auto* aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About Gauge Reader",
            "Gauge Reader\n\n"
            "A tool for reading circular and edgewise gauges from video.");
    });

    // ── Central widget ──────────────────────────────────────────
    auto* central = new QWidget(this);
    setCentralWidget(central);

    statusBar()->showMessage("Ready");

    auto* verticalLayout = new QVBoxLayout(central);
    verticalLayout->setContentsMargins(0, 0, 0, 0);
    verticalLayout->setSpacing(0);

    // ── Top area: video + control panel ─────────────────────────
    auto* topWidget = new QWidget(this);
    auto* mainLayout = new QHBoxLayout(topWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── Video area ──────────────────────────────────────────────
    videoWidget_ = new VideoWidget(this);
    mainLayout->addWidget(videoWidget_, 1);

    // ── Control panel ───────────────────────────────────────────
    auto* controlPanel = new QWidget(this);
    controlPanel->setObjectName("controlPanel");
    controlPanel->setFixedWidth(kControlPanelWidth);
    auto* controlLayout = new QVBoxLayout(controlPanel);
    controlLayout->setContentsMargins(12, 12, 12, 12);
    controlLayout->setSpacing(10);

    detectionPage_ = new DetectionPage(this);
    calibrationPage_ = new CalibrationPage(this);
    processingPage_ = new ProcessingPage(this);

    controlLayout->addWidget(detectionPage_);
    controlLayout->addWidget(calibrationPage_);
    controlLayout->addWidget(processingPage_);
    controlLayout->addStretch();
    mainLayout->addWidget(controlPanel);

    verticalLayout->addWidget(topWidget, 1);

    // ── Alarm table ─────────────────────────────────────────────
    alarmTable_ = new QTableWidget(this);
    alarmTable_->setFixedHeight(kAlarmTableHeight);
    alarmTable_->setColumnCount(10);
    alarmTable_->setHorizontalHeaderLabels({
        "Timestamp", "Tag", "Value", "Min", "Max", "Color",
        "Enabled", "Direction", "Threshold", "Triggered"
    });
    alarmTable_->horizontalHeader()->setStretchLastSection(true);
    alarmTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    alarmTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    alarmTable_->setAlternatingRowColors(true);
    alarmTable_->verticalHeader()->hide();
    verticalLayout->addWidget(alarmTable_);

    // ── Register meta-types ─────────────────────────────────────
    qRegisterMetaType<AppMode>();
    qRegisterMetaType<QVector<GaugeCalibData>>();

    // ── Worker + Thread ─────────────────────────────────────────
    worker_ = new Worker(videoPath);
    workerThread_ = new QThread(this);
    worker_->moveToThread(workerThread_);

    // Worker self-deletes after its thread finishes — no manual delete needed
    connect(workerThread_, &QThread::finished,
            worker_, &QObject::deleteLater);

    // Worker stops its event loop when we ask it to quit
    connect(this, &MainWindow::quitRequested,
            worker_, &Worker::quit);
    connect(workerThread_, &QThread::started,
            worker_, &Worker::start);

    // Worker → Main
    connect(worker_, &Worker::frameReady, this,
            [this](const QImage& img) { videoWidget_->setImage(img); });
    connect(worker_, &Worker::modeChanged,
            this, &MainWindow::setMode);
    connect(worker_, &Worker::finished,
            this, []() { QApplication::quit(); });
    connect(worker_, &Worker::alarmTriggered,
            this, &MainWindow::onAlarmTriggered);

    // Pages ↔ Worker
    detectionPage_->connectToWorker(worker_);
    calibrationPage_->connectToWorker(worker_);
    processingPage_->connectToWorker(worker_);

    // Video → Worker
    connect(videoWidget_, &VideoWidget::mouseMoved,
            worker_, &Worker::onDragMove);
    connect(videoWidget_, &VideoWidget::imageMouseReleased,
            worker_, &Worker::onDragRelease);
    connect(videoWidget_, &VideoWidget::imageClicked,
            worker_, &Worker::onImageClicked);

    // ── Start worker thread ─────────────────────────────────────
    workerThread_->start();

    // ── Initial UI state ────────────────────────────────────────
    setMode(AppMode::kDetection);
}

MainWindow::~MainWindow() {
    if (workerThread_ && workerThread_->isRunning()) {
        emit quitRequested();
        if (!workerThread_->wait(3000)) {
            qWarning("Worker thread did not stop in time — abandoning.");
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (workerThread_ && workerThread_->isRunning())
            emit quitRequested();
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::setMode(AppMode mode) {
    currentMode_ = mode;
    detectionPage_->setVisible(mode == AppMode::kDetection);
    calibrationPage_->setVisible(mode == AppMode::kCalibration);
    processingPage_->setVisible(mode == AppMode::kProcessing);

    switch (mode) {
        case AppMode::kDetection:
            statusBar()->showMessage("Detection mode");
            break;
        case AppMode::kCalibration:
            statusBar()->showMessage("Calibration mode");
            break;
        case AppMode::kProcessing:
            statusBar()->showMessage("Processing mode");
            break;
    }
}

void MainWindow::setStatus(const QString& message) {
    statusBar()->showMessage(message);
}

void MainWindow::onAlarmTriggered(int gaugeIdx, bool triggered) {
    if (!triggered) return;

    const auto& calibData = worker_->calibData();
    if (gaugeIdx < 0 || gaugeIdx >= calibData.size()) return;

    const auto& data = calibData[gaugeIdx];
    int row = alarmTable_->rowCount();
    alarmTable_->insertRow(row);

    QString timestamp = QTime::currentTime().toString("hh:mm:ss.zzz");

    auto setItem = [&](int col, const QString& text) {
        alarmTable_->setItem(row, col, new QTableWidgetItem(text));
    };

    setItem(0, timestamp);
    setItem(1, data.tag);
    setItem(2, data.value.has_value() ? QString::number(*data.value) : "N/A");
    setItem(3, QString::number(data.min_value));
    setItem(4, QString::number(data.max_value));
    setItem(5, QString::number(data.color_rgb, 16).rightJustified(6, '0'));
    setItem(6, data.alarm_enabled ? "Yes" : "No");
    setItem(7, data.alarm_direction == AlarmDirection::kGreaterThan ? ">" : "<");
    setItem(8, QString::number(data.alarm_threshold));
    setItem(9, data.alarm_triggered ? "YES" : "No");

    alarmTable_->scrollToBottom();

    if (row >= kMaxAlarmRows) {
        alarmTable_->removeRow(0);
    }

}
