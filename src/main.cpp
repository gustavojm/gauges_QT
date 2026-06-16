#include "main_window.h"
#include "worker.h"

#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>

#include <opencv2/opencv.hpp>

#include <iostream>
#include <thread>

// ═══════════════════════════════════════════════════════════════════
//  Constants
// ═══════════════════════════════════════════════════════════════════

constexpr int kBtnWide = 120;
constexpr int kBtnNarrow = 80;
constexpr int kControlPanelWidth = 300;
constexpr int kPollIntervalMs = 16;

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
    image_ = img.copy(); // deep copy – own the pixel data
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
    : videoPath_(videoPath)
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

    // ── Connections ─────────────────────────────────────────────
    connect(videoWidget_, &VideoWidget::imageClicked,
            this, &MainWindow::onImageClick);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::onPollTimer);
    timer_->start(kPollIntervalMs);

    // ── Start worker ────────────────────────────────────────────
    worker_ = std::thread(WorkerMain, videoPath_, std::ref(shared_));
}

MainWindow::~MainWindow() {
    timer_->stop();
    {
        std::lock_guard<std::mutex> lk(shared_.mtx);
        shared_.quit = true;
    }
    if (worker_.joinable())
        worker_.join();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    {
        std::lock_guard<std::mutex> lk(shared_.mtx);
        shared_.quit = true;
    }
    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        std::lock_guard<std::mutex> lk(shared_.mtx);
        shared_.quit = true;
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
//  Poll Timer
// ═══════════════════════════════════════════════════════════════════

void MainWindow::onPollTimer() {
    AppMode prevMode = currentMode_;
    cv::Mat frame;
    std::vector<double> values;
    int fCount = 0, tFrames = 0;
    CalibUIState calib;
    bool doQuit = false;
    size_t numGauges = 0;

    {
        std::lock_guard<std::mutex> lk(shared_.mtx);
        if (shared_.quit) {
            doQuit = true;
        } else {
            currentMode_ = shared_.mode;
            if (shared_.frameReady) {
                shared_.displayFrame.copyTo(frame);
                shared_.frameReady = false;
            }
            values = shared_.gaugeValues;
            fCount = shared_.frameCount;
            tFrames = shared_.totalFrames;
            calib = shared_.calibUI;
            numGauges = shared_.detectedGauges.size();
        }
    }

    if (doQuit) {
        QApplication::quit();
        return;
    }

    // Mode switch
    if (currentMode_ != prevMode) {
        detectionPage_->setVisible(currentMode_ == AppMode::kDetection);
        calibrationPage_->setVisible(currentMode_ == AppMode::kCalibration);
        processingPage_->setVisible(currentMode_ == AppMode::kProcessing);
    }

    // Video display
    if (!frame.empty()) {
        cv::Mat rgb;
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
        QImage img(rgb.data, rgb.cols, rgb.rows, rgb.step,
                   QImage::Format_RGB888);
        videoWidget_->setImage(img.copy());
    }

    // Mode-specific UI updates
    if (currentMode_ == AppMode::kDetection) {
        gaugeCountLabel_->setText(
            QString("Found %1 gauge(s)").arg(numGauges));
        bool hasGauges = numGauges > 0;
        addManualBtn_->setVisible(hasGauges);
        confirmBtn_->setVisible(hasGauges);

    } else if (currentMode_ == AppMode::kCalibration) {
        updateCalibrationHelp();

    } else if (currentMode_ == AppMode::kProcessing) {
        frameCountLabel_->setText(
            QString("Frame %1 / %2").arg(fCount).arg(tFrames));
        updateGaugeValueLabels();
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Calibration Help
// ═══════════════════════════════════════════════════════════════════

void MainWindow::updateCalibrationHelp() {
    CalibUIState calib;
    {
        std::lock_guard<std::mutex> lk(shared_.mtx);
        calib = shared_.calibUI;
    }

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

// ═══════════════════════════════════════════════════════════════════
//  Gauge Value Labels
// ═══════════════════════════════════════════════════════════════════

void MainWindow::updateGaugeValueLabels() {
    std::vector<double> values;
    {
        std::lock_guard<std::mutex> lk(shared_.mtx);
        values = shared_.gaugeValues;
    }

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

// ═══════════════════════════════════════════════════════════════════
//  Detection Slots
// ═══════════════════════════════════════════════════════════════════

void MainWindow::onImageClick(int x, int y) {
    std::lock_guard<std::mutex> lk(shared_.mtx);
    shared_.hasClick = true;
    shared_.clickX = x;
    shared_.clickY = y;
}

void MainWindow::onManualPlacementToggled(bool checked) {
    std::lock_guard<std::mutex> lk(shared_.mtx);
    shared_.manualPlacement = checked;
    if (!checked)
        shared_.runDetection = true;
}

void MainWindow::onCannyChanged(int value) {
    cannyValLabel_->setText(QString::number(value));
    std::lock_guard<std::mutex> lk(shared_.mtx);
    shared_.detectCanny = value;
    shared_.runDetection = true;
}

void MainWindow::onAccChanged(int value) {
    accValLabel_->setText(QString::number(value));
    std::lock_guard<std::mutex> lk(shared_.mtx);
    shared_.detectAcc = value;
    shared_.runDetection = true;
}

void MainWindow::onAddManual() {
    std::lock_guard<std::mutex> lk(shared_.mtx);
    shared_.command = WorkerCommand::kConfirmAndAddManual;
    shared_.mode = AppMode::kCalibration;
}

void MainWindow::onConfirmGauges() {
    std::lock_guard<std::mutex> lk(shared_.mtx);
    shared_.command = WorkerCommand::kConfirmGauges;
    shared_.mode = AppMode::kCalibration;
}

// ═══════════════════════════════════════════════════════════════════
//  Calibration Slots
// ═══════════════════════════════════════════════════════════════════

void MainWindow::onAddAnotherGauge() {
    std::lock_guard<std::mutex> lk(shared_.mtx);
    shared_.command = WorkerCommand::kAddManualGauge;
}

void MainWindow::onStartCalibration() {
    std::lock_guard<std::mutex> lk(shared_.mtx);
    shared_.command = WorkerCommand::kConfirmManualCircles;
}

void MainWindow::onConfirmCalib() {
    std::lock_guard<std::mutex> lk(shared_.mtx);
    shared_.command = WorkerCommand::kConfirmCalib;
}

void MainWindow::onCancelCalib() {
    std::lock_guard<std::mutex> lk(shared_.mtx);
    shared_.quit = true;
}

void MainWindow::onMinValChanged(int value) {
    std::lock_guard<std::mutex> lk(shared_.mtx);
    shared_.calibMinVal = value;
}

void MainWindow::onMaxValChanged(int value) {
    std::lock_guard<std::mutex> lk(shared_.mtx);
    shared_.calibMaxVal = value;
}

// ═══════════════════════════════════════════════════════════════════
//  Processing Slots
// ═══════════════════════════════════════════════════════════════════

void MainWindow::onRestart() {
    std::lock_guard<std::mutex> lk(shared_.mtx);
    shared_.command = WorkerCommand::kRestart;
}

void MainWindow::onQuit() {
    std::lock_guard<std::mutex> lk(shared_.mtx);
    shared_.quit = true;
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
