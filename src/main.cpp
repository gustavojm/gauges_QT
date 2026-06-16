#include "main_window.h"
#include "worker.h"

#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QThread>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QSpinBox>

#include <opencv2/opencv.hpp>

#include <iostream>

// ═══════════════════════════════════════════════════════════════════
//  Constants
// ═══════════════════════════════════════════════════════════════════

constexpr int kBtnWide = 120;
constexpr int kBtnNarrow = 80;
constexpr int kControlPanelWidth = 300;

// ═══════════════════════════════════════════════════════════════════
//  VideoWidget
// ═══════════════════════════════════════════════════════════════════

VideoWidget::VideoWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 240);
    setMouseTracking(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void VideoWidget::setImage(const QImage& img) {
    image_ = img.copy();
    updateScaled();
    update();
}

void VideoWidget::updateScaled() {
    if (image_.isNull()) {
        scaled_ = QPixmap();
        return;
    }
    scaled_ = QPixmap::fromImage(image_).scaled(
        size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void VideoWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(26, 26, 30));
    if (!scaled_.isNull()) {
        int x = (width() - scaled_.width()) / 2;
        int y = (height() - scaled_.height()) / 2;
        p.drawPixmap(x, y, scaled_);
    }
}

void VideoWidget::resizeEvent(QResizeEvent*) {
    updateScaled();
}

void VideoWidget::mousePressEvent(QMouseEvent* event) {
    if (image_.isNull() || scaled_.isNull()) return;
    float sx = static_cast<float>(scaled_.width()) / image_.width();
    float sy = static_cast<float>(scaled_.height()) / image_.height();
    int ox = (width() - scaled_.width()) / 2;
    int oy = (height() - scaled_.height()) / 2;
    int ix = static_cast<int>((event->pos().x() - ox) / sx);
    int iy = static_cast<int>((event->pos().y() - oy) / sy);
    if (ix >= 0 && ix < image_.width() && iy >= 0 && iy < image_.height())
        emit imageClicked(ix, iy);
}

// ═══════════════════════════════════════════════════════════════════
//  MainWindow
// ═══════════════════════════════════════════════════════════════════

MainWindow::MainWindow(const std::string& videoPath)
{
    setWindowTitle("Gauge Reader");
    resize(1600, 1000);
    setStyleSheet("QWidget { font-size: 13px; }"
                  "QWidget#controlPanel { background-color: #1e1e24; }"
                  "QLabel { color: #d0d0d0; }");

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── Video area ──────────────────────────────────────────────
    videoWidget_ = new VideoWidget(this);
    mainLayout->addWidget(videoWidget_, 1);

    // ── Control panel ───────────────────────────────────────────
    controlPanel_ = new QWidget(this);
    controlPanel_->setObjectName("controlPanel");
    controlPanel_->setFixedWidth(kControlPanelWidth);
    controlLayout_ = new QVBoxLayout(controlPanel_);
    controlLayout_->setContentsMargins(12, 12, 12, 12);
    controlLayout_->setSpacing(10);

    buildDetectionUI(controlLayout_);
    buildCalibrationUI(controlLayout_);
    buildProcessingUI(controlLayout_);

    controlLayout_->addStretch();
    mainLayout->addWidget(controlPanel_);

    // ── Register meta-types ─────────────────────────────────────
    qRegisterMetaType<CalibUIState>();

    // ── Worker + Thread ─────────────────────────────────────────
    worker_ = new Worker(videoPath);
    workerThread_ = new QThread(this);
    worker_->moveToThread(workerThread_);

    connect(worker_, &Worker::frameReady,
            this, &MainWindow::onFrameReady);
    connect(worker_, &Worker::gaugeValuesUpdated,
            this, &MainWindow::onGaugeValuesUpdated);
    connect(worker_, &Worker::frameCountUpdated,
            this, &MainWindow::onFrameCountUpdated);
    connect(worker_, &Worker::detectionUpdated,
            this, &MainWindow::onDetectionUpdated);
    connect(worker_, &Worker::calibUIUpdated,
            this, &MainWindow::onCalibUIUpdated);
    connect(worker_, &Worker::modeChanged,
            this, &MainWindow::onModeChanged);
    connect(worker_, &Worker::finished,
            this, &MainWindow::onWorkerFinished);

    connect(workerThread_, &QThread::started,
            worker_, &Worker::start);

    // ── Video click → Worker directly (cross-thread, auto-queued)
    connect(videoWidget_, &VideoWidget::imageClicked,
            worker_, &Worker::handleClick);

    // ── Start worker thread ─────────────────────────────────────
    workerThread_->start();

    // ── Initial UI state ────────────────────────────────────────
    setMode(AppMode::kDetection);
}

MainWindow::~MainWindow() {
    if (workerThread_ && workerThread_->isRunning()) {
        QMetaObject::invokeMethod(worker_, "quit", Qt::QueuedConnection);
        if (!workerThread_->wait(3000))
            workerThread_->terminate();
    }
    delete worker_;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (workerThread_ && workerThread_->isRunning())
        QMetaObject::invokeMethod(worker_, "quit", Qt::QueuedConnection);
    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (workerThread_ && workerThread_->isRunning())
            QMetaObject::invokeMethod(worker_, "quit", Qt::QueuedConnection);
    }
    QWidget::keyPressEvent(event);
}

// ═══════════════════════════════════════════════════════════════════
//  Build UI
// ═══════════════════════════════════════════════════════════════════

void MainWindow::buildDetectionUI(QVBoxLayout* parent) {
    detectionPage_ = new QWidget(this);
    auto* lay = new QVBoxLayout(detectionPage_);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);

    manualCb_ = new QCheckBox("Manual placement", this);
    connect(manualCb_, &QCheckBox::toggled,
            this, &MainWindow::onManualPlacementToggled);
    lay->addWidget(manualCb_);

    auto* cannyRow = new QWidget(this);
    auto* chl = new QHBoxLayout(cannyRow);
    chl->setContentsMargins(0, 0, 0, 0);
    chl->addWidget(new QLabel("Canny:", this));
    cannySlider_ = new QSlider(Qt::Horizontal, this);
    cannySlider_->setRange(1, 500);
    cannySlider_->setValue(320);
    cannyValLabel_ = new QLabel("320", this);
    cannyValLabel_->setFixedWidth(30);
    chl->addWidget(cannySlider_, 1);
    chl->addWidget(cannyValLabel_);
    connect(cannySlider_, &QSlider::valueChanged,
            this, &MainWindow::onCannyChanged);
    lay->addWidget(cannyRow);

    auto* accRow = new QWidget(this);
    auto* ahl = new QHBoxLayout(accRow);
    ahl->setContentsMargins(0, 0, 0, 0);
    ahl->addWidget(new QLabel("Accum:", this));
    accSlider_ = new QSlider(Qt::Horizontal, this);
    accSlider_->setRange(1, 500);
    accSlider_->setValue(40);
    accValLabel_ = new QLabel("40", this);
    accValLabel_->setFixedWidth(30);
    ahl->addWidget(accSlider_, 1);
    ahl->addWidget(accValLabel_);
    connect(accSlider_, &QSlider::valueChanged,
            this, &MainWindow::onAccChanged);
    lay->addWidget(accRow);

    gaugeCountLabel_ = new QLabel("Found 0 gauge(s)", this);
    lay->addWidget(gaugeCountLabel_);

    lay->addSpacing(8);

    auto* btnRow = new QWidget(this);
    auto* bhl = new QHBoxLayout(btnRow);
    bhl->setContentsMargins(0, 0, 0, 0);
    bhl->setSpacing(8);
    addManualBtn_ = new QPushButton("Add Manually", this);
    addManualBtn_->setFixedWidth(kBtnWide);
    confirmBtn_ = new QPushButton("Confirm", this);
    confirmBtn_->setFixedWidth(kBtnWide);
    bhl->addWidget(addManualBtn_);
    bhl->addWidget(confirmBtn_);
    connect(addManualBtn_, &QPushButton::clicked,
            this, &MainWindow::onAddManual);
    connect(confirmBtn_, &QPushButton::clicked,
            this, &MainWindow::onConfirmGauges);
    lay->addWidget(btnRow);

    lay->addStretch();
    parent->addWidget(detectionPage_);
}

void MainWindow::buildCalibrationUI(QVBoxLayout* parent) {
    calibrationPage_ = new QWidget(this);
    auto* lay = new QVBoxLayout(calibrationPage_);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);

    gaugeProgress_ = new QLabel(this);
    lay->addWidget(gaugeProgress_);

    calibInstruction_ = new QLabel(this);
    calibInstruction_->setWordWrap(true);
    calibInstruction_->setStyleSheet("color: #ffff00; font-weight: bold;");
    lay->addWidget(calibInstruction_);

    addAnotherBtn_ = new QPushButton("Add another gauge", this);
    addAnotherBtn_->setFixedWidth(kBtnWide);
    connect(addAnotherBtn_, &QPushButton::clicked,
            this, &MainWindow::onAddAnotherGauge);
    lay->addWidget(addAnotherBtn_);

    startCalibBtn_ = new QPushButton("Start calibration", this);
    startCalibBtn_->setFixedWidth(kBtnWide);
    connect(startCalibBtn_, &QPushButton::clicked,
            this, &MainWindow::onStartCalibration);
    lay->addWidget(startCalibBtn_);

    // Confirm sub-panel (min/max spinners + buttons)
    calibConfirmWidget_ = new QWidget(this);
    auto* cl = new QVBoxLayout(calibConfirmWidget_);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(8);

    auto* minRow = new QWidget(this);
    auto* mhl = new QHBoxLayout(minRow);
    mhl->setContentsMargins(0, 0, 0, 0);
    mhl->addWidget(new QLabel("Min value:", this));
    minValSpin_ = new QSpinBox(this);
    minValSpin_->setRange(0, 10000);
    mhl->addWidget(minValSpin_, 1);
    cl->addWidget(minRow);

    auto* maxRow = new QWidget(this);
    auto* Mhl = new QHBoxLayout(maxRow);
    Mhl->setContentsMargins(0, 0, 0, 0);
    Mhl->addWidget(new QLabel("Max value:", this));
    maxValSpin_ = new QSpinBox(this);
    maxValSpin_->setRange(0, 10000);
    maxValSpin_->setValue(1000);
    Mhl->addWidget(maxValSpin_, 1);
    cl->addWidget(maxRow);

    auto* calBtnRow = new QWidget(this);
    auto* cbl = new QHBoxLayout(calBtnRow);
    cbl->setContentsMargins(0, 0, 0, 0);
    cbl->setSpacing(8);
    confirmCalibBtn_ = new QPushButton("Confirm", this);
    confirmCalibBtn_->setFixedWidth(kBtnWide);
    cancelCalibBtn_ = new QPushButton("Cancel", this);
    cancelCalibBtn_->setFixedWidth(kBtnWide);
    cbl->addWidget(confirmCalibBtn_);
    cbl->addWidget(cancelCalibBtn_);
    connect(confirmCalibBtn_, &QPushButton::clicked,
            this, &MainWindow::onConfirmCalib);
    connect(cancelCalibBtn_, &QPushButton::clicked,
            this, &MainWindow::onCancelCalib);
    cl->addWidget(calBtnRow);

    connect(minValSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onMinValChanged);
    connect(maxValSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onMaxValChanged);

    lay->addWidget(calibConfirmWidget_);
    lay->addStretch();

    // Default hidden state
    calibrationPage_->setVisible(false);
    addAnotherBtn_->setVisible(false);
    startCalibBtn_->setVisible(false);
    calibConfirmWidget_->setVisible(false);

    parent->addWidget(calibrationPage_);
}

void MainWindow::buildProcessingUI(QVBoxLayout* parent) {
    processingPage_ = new QWidget(this);
    auto* lay = new QVBoxLayout(processingPage_);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);

    frameCountLabel_ = new QLabel("Frame 0 / 0", this);
    lay->addWidget(frameCountLabel_);

    gaugeValuesLayout_ = new QVBoxLayout;
    lay->addLayout(gaugeValuesLayout_);

    lay->addSpacing(12);

    auto* btnRow = new QWidget(this);
    auto* bhl = new QHBoxLayout(btnRow);
    bhl->setContentsMargins(0, 0, 0, 0);
    bhl->setSpacing(8);
    restartBtn_ = new QPushButton("Restart", this);
    restartBtn_->setFixedWidth(kBtnNarrow);
    quitBtn_ = new QPushButton("Quit", this);
    quitBtn_->setFixedWidth(kBtnNarrow);
    bhl->addWidget(restartBtn_);
    bhl->addWidget(quitBtn_);
    connect(restartBtn_, &QPushButton::clicked,
            this, &MainWindow::onRestart);
    connect(quitBtn_, &QPushButton::clicked,
            this, &MainWindow::onQuit);
    lay->addWidget(btnRow);

    lay->addStretch();
    processingPage_->setVisible(false);
    parent->addWidget(processingPage_);
}

// ═══════════════════════════════════════════════════════════════════
//  Worker Signal Slots
// ═══════════════════════════════════════════════════════════════════

void MainWindow::onFrameReady(const QImage& image) {
    videoWidget_->setImage(image);
}

void MainWindow::onGaugeValuesUpdated(const QVector<double>& values) {
    while (gaugeValueLabels_.size() < values.size()) {
        auto* lbl = new QLabel(this);
        lbl->setStyleSheet("color: #00ffff; font-size: 14px;");
        gaugeValuesLayout_->addWidget(lbl);
        gaugeValueLabels_.push_back(lbl);
    }
    for (size_t i = 0; i < gaugeValueLabels_.size(); ++i) {
        if (i < values.size()) {
            gaugeValueLabels_[i]->setText(
                QString("Gauge %1: %2")
                    .arg(i + 1)
                    .arg(values[i], 0, 'f', 2));
            gaugeValueLabels_[i]->setVisible(true);
        } else {
            gaugeValueLabels_[i]->setVisible(false);
        }
    }
}

void MainWindow::onFrameCountUpdated(int current, int total) {
    frameCountLabel_->setText(
        QString("Frame %1 / %2").arg(current).arg(total));
}

void MainWindow::onDetectionUpdated(size_t numGauges) {
    gaugeCountLabel_->setText(
        QString("Found %1 gauge(s)").arg(numGauges));
    bool hasGauges = numGauges > 0;
    addManualBtn_->setVisible(hasGauges);
    confirmBtn_->setVisible(hasGauges);
}

void MainWindow::onCalibUIUpdated(const CalibUIState& calib) {
    if (!calib.initialized) {
        calibrationPage_->setVisible(false);
        return;
    }

    bool showAddAnother = false;
    bool showStartCalib = false;
    bool showConfirm = false;

    switch (calib.state) {
    case GaugeState::kCircleManual:
        if (calib.circleStage == 1)
            calibInstruction_->setText(
                "Click on the CENTER of the gauge");
        else if (calib.circleStage == 2)
            calibInstruction_->setText(
                "Now click on the EDGE of the gauge face");
        else if (calib.circleStage == 3) {
            calibInstruction_->setText(
                QString("Gauge %1 placed").arg(calib.totalGauges));
            showAddAnother = true;
            showStartCalib = true;
        }
        break;

    case GaugeState::kCalibMin:
        if (calib.totalGauges > 1)
            gaugeProgress_->setText(
                QString("Gauge %1 / %2")
                    .arg(calib.currentGauge + 1)
                    .arg(calib.totalGauges));
        else
            gaugeProgress_->clear();
        calibInstruction_->setText(
            "Click on the MINIMUM value marking");
        break;

    case GaugeState::kCalibMax:
        calibInstruction_->setText(
            "Now click on the MAXIMUM value marking");
        break;

    case GaugeState::kCalibConfirm:
        if (calib.totalGauges > 1)
            gaugeProgress_->setText(
                QString("Gauge %1 / %2")
                    .arg(calib.currentGauge + 1)
                    .arg(calib.totalGauges));
        else
            gaugeProgress_->clear();
        calibInstruction_->setText(
            "Set min/max values and confirm");
        showConfirm = true;

        if (!calibConfirmInitialized_) {
            minValSpin_->setValue(calib.calibTrackMin);
            maxValSpin_->setValue(calib.calibTrackMax);
            calibConfirmInitialized_ = true;
        }
        break;

    default:
        break;
    }

    addAnotherBtn_->setVisible(showAddAnother);
    startCalibBtn_->setVisible(showStartCalib);
    calibConfirmWidget_->setVisible(showConfirm);

    if (calib.state != GaugeState::kCalibConfirm)
        calibConfirmInitialized_ = false;
}

void MainWindow::onModeChanged(AppMode mode) {
    setMode(mode);
}

void MainWindow::onWorkerFinished() {
    QApplication::quit();
}

// ═══════════════════════════════════════════════════════════════════
//  Mode Helper
// ═══════════════════════════════════════════════════════════════════

void MainWindow::setMode(AppMode mode) {
    currentMode_ = mode;
    detectionPage_->setVisible(mode == AppMode::kDetection);
    calibrationPage_->setVisible(mode == AppMode::kCalibration);
    processingPage_->setVisible(mode == AppMode::kProcessing);
}

// ═══════════════════════════════════════════════════════════════════
//  User Interaction Slots (forward to Worker)
// ═══════════════════════════════════════════════════════════════════

void MainWindow::onImageClick(int x, int y) {
    // Handled directly by connection videoWidget_ → Worker::handleClick
}

void MainWindow::onManualPlacementToggled(bool checked) {
    QMetaObject::invokeMethod(worker_, "setManualPlacement",
        Qt::QueuedConnection, Q_ARG(bool, checked));
}

void MainWindow::onCannyChanged(int value) {
    cannyValLabel_->setText(QString::number(value));
    QMetaObject::invokeMethod(worker_, "setCanny",
        Qt::QueuedConnection, Q_ARG(int, value));
}

void MainWindow::onAccChanged(int value) {
    accValLabel_->setText(QString::number(value));
    QMetaObject::invokeMethod(worker_, "setAcc",
        Qt::QueuedConnection, Q_ARG(int, value));
}

void MainWindow::onAddManual() {
    setMode(AppMode::kCalibration);
    QMetaObject::invokeMethod(worker_, "addManual", Qt::QueuedConnection);
}

void MainWindow::onConfirmGauges() {
    setMode(AppMode::kCalibration);
    QMetaObject::invokeMethod(worker_, "confirmGauges", Qt::QueuedConnection);
}

void MainWindow::onAddAnotherGauge() {
    QMetaObject::invokeMethod(worker_, "addAnotherGauge", Qt::QueuedConnection);
}

void MainWindow::onStartCalibration() {
    QMetaObject::invokeMethod(worker_, "startCalibration", Qt::QueuedConnection);
}

void MainWindow::onConfirmCalib() {
    QMetaObject::invokeMethod(worker_, "confirmCalib", Qt::QueuedConnection);
}

void MainWindow::onCancelCalib() {
    if (workerThread_ && workerThread_->isRunning())
        QMetaObject::invokeMethod(worker_, "quit", Qt::QueuedConnection);
}

void MainWindow::onMinValChanged(int value) {
    QMetaObject::invokeMethod(worker_, "setCalibMin",
        Qt::QueuedConnection, Q_ARG(int, value));
}

void MainWindow::onMaxValChanged(int value) {
    QMetaObject::invokeMethod(worker_, "setCalibMax",
        Qt::QueuedConnection, Q_ARG(int, value));
}

void MainWindow::onRestart() {
    QMetaObject::invokeMethod(worker_, "restart", Qt::QueuedConnection);
}

void MainWindow::onQuit() {
    if (workerThread_ && workerThread_->isRunning())
        QMetaObject::invokeMethod(worker_, "quit", Qt::QueuedConnection);
}

// ═══════════════════════════════════════════════════════════════════
//  main
// ═══════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_path>\n";
        return -1;
    }

    QApplication app(argc, argv);
    app.setApplicationName("Gauge Reader");

    MainWindow window(argv[1]);
    window.show();

    return app.exec();
}
