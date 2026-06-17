#include "main_window.h"
#include "detection_page.h"
#include "calibration_page.h"
#include "processing_page.h"
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

#include <opencv2/opencv.hpp>

#include <iostream>

// ═══════════════════════════════════════════════════════════════════
//  Constants
// ═══════════════════════════════════════════════════════════════════

constexpr int kControlPanelWidth = 300;

// ═══════════════════════════════════════════════════════════════════
//  VideoWidget
// ═══════════════════════════════════════════════════════════════════

VideoWidget::VideoWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 240);
    setMouseTracking(true);
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

static bool widgetToImage(const QImage& image, const QPixmap& scaled,
                          int w, int h, const QPoint& pos,
                          int& ix, int& iy) {
    if (image.isNull() || scaled.isNull()) return false;
    float sx = static_cast<float>(scaled.width()) / image.width();
    float sy = static_cast<float>(scaled.height()) / image.height();
    int ox = (w - scaled.width()) / 2;
    int oy = (h - scaled.height()) / 2;
    ix = static_cast<int>((pos.x() - ox) / sx);
    iy = static_cast<int>((pos.y() - oy) / sy);
    return ix >= 0 && ix < image.width() && iy >= 0 && iy < image.height();
}

void VideoWidget::mousePressEvent(QMouseEvent* event) {
    int ix, iy;
    if (widgetToImage(image_, scaled_, width(), height(), event->pos(), ix, iy))
        emit imageClicked(ix, iy);
}

void VideoWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    int ix, iy;
    if (widgetToImage(image_, scaled_, width(), height(), event->pos(), ix, iy))
        emit mouseMoved(ix, iy);
}

void VideoWidget::mouseReleaseEvent(QMouseEvent*) {
    emit mouseReleased();
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
                  "QLabel { color: #d0d0d0; }"
                  "QCheckBox { color: #d0d0d0; }");

    auto* mainLayout = new QHBoxLayout(this);
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

    // ── Register meta-types ─────────────────────────────────────
    qRegisterMetaType<CalibUIState>();

    // ── Worker + Thread ─────────────────────────────────────────
    worker_ = new Worker(videoPath);
    workerThread_ = new QThread(this);
    worker_->moveToThread(workerThread_);

    // Worker → Main
    connect(worker_, &Worker::frameReady, this,
            [this](const QImage& img) { videoWidget_->setImage(img); });
    connect(worker_, &Worker::modeChanged,
            this, &MainWindow::setMode);
    connect(worker_, &Worker::finished,
            this, []() { QApplication::quit(); });

    // Worker → Pages
    connect(worker_, &Worker::detectionUpdated,
            detectionPage_, &DetectionPage::onDetectionUpdated);
    connect(worker_, &Worker::calibUIUpdated,
            calibrationPage_, &CalibrationPage::onCalibUIUpdated);
    connect(worker_, &Worker::manualPlacementActive,
            detectionPage_, &DetectionPage::setManualPlacementActive);
    connect(worker_, &Worker::gaugeValuesUpdated,
            processingPage_, &ProcessingPage::onGaugeValuesUpdated);
    connect(worker_, &Worker::frameCountUpdated,
            processingPage_, &ProcessingPage::onFrameCountUpdated);

    // Pages → Worker (cross-thread, auto-queued)
    connect(detectionPage_, &DetectionPage::manualPlacementToggled,
            worker_, &Worker::setManualPlacement);
    connect(detectionPage_, &DetectionPage::cannyChanged,
            worker_, &Worker::setCanny);
    connect(detectionPage_, &DetectionPage::accChanged,
            worker_, &Worker::setAcc);
    connect(detectionPage_, &DetectionPage::confirmClicked,
            worker_, &Worker::confirmGauges);
    connect(calibrationPage_, &CalibrationPage::startCalibrationClicked,
            worker_, &Worker::startCalibration);
    connect(calibrationPage_, &CalibrationPage::confirmCalibClicked,
            worker_, &Worker::confirmCalib);
    connect(calibrationPage_, &CalibrationPage::cancelCalibClicked,
            worker_, &Worker::quit);
    connect(calibrationPage_, &CalibrationPage::minValChanged,
            worker_, &Worker::setCalibMin);
    connect(calibrationPage_, &CalibrationPage::maxValChanged,
            worker_, &Worker::setCalibMax);
    connect(processingPage_, &ProcessingPage::restartClicked,
            worker_, &Worker::restart);
    connect(processingPage_, &ProcessingPage::quitClicked,
            worker_, &Worker::quit);

    // Video → Worker
    connect(videoWidget_, &VideoWidget::mouseMoved,
            worker_, &Worker::handleDragMove);
    connect(videoWidget_, &VideoWidget::mouseReleased,
            worker_, &Worker::handleDragRelease);
    connect(videoWidget_, &VideoWidget::imageClicked,
            worker_, &Worker::handleClick);

    connect(this, &MainWindow::quitRequested,
            worker_, &Worker::quit);

    connect(workerThread_, &QThread::started,
            worker_, &Worker::start);

    // ── Start worker thread ─────────────────────────────────────
    workerThread_->start();

    // ── Initial UI state ────────────────────────────────────────
    setMode(AppMode::kDetection);
}

MainWindow::~MainWindow() {
    if (workerThread_ && workerThread_->isRunning()) {
        emit quitRequested();
        if (!workerThread_->wait(3000))
            workerThread_->terminate();
    }
    delete worker_;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (workerThread_ && workerThread_->isRunning())
        emit quitRequested();
    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (workerThread_ && workerThread_->isRunning())
            emit quitRequested();
    }
    QWidget::keyPressEvent(event);
}

void MainWindow::setMode(AppMode mode) {
    currentMode_ = mode;
    detectionPage_->setVisible(mode == AppMode::kDetection);
    calibrationPage_->setVisible(mode == AppMode::kCalibration);
    processingPage_->setVisible(mode == AppMode::kProcessing);
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
