#pragma once

#include <QWidget>
#include <QImage>
#include <QString>

#include <thread>
#include <string>
#include <vector>

#include "shared_state.h"

class QLabel;
class QPushButton;
class QCheckBox;
class QSlider;
class QSpinBox;
class QTimer;
class QVBoxLayout;
class QCloseEvent;
class QKeyEvent;

// ─── Video Widget ─────────────────────────────────────────────────

class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    void setImage(const QImage& img);

signals:
    void imageClicked(int imageX, int imageY);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QImage image_;
    QPixmap scaled_;
    void updateScaled();
};

// ─── Main Window ──────────────────────────────────────────────────

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(const std::string& videoPath);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onPollTimer();
    void onImageClick(int x, int y);
    void onManualPlacementToggled(bool checked);
    void onCannyChanged(int value);
    void onAccChanged(int value);
    void onAddManual();
    void onConfirmGauges();
    void onAddAnotherGauge();
    void onStartCalibration();
    void onConfirmCalib();
    void onCancelCalib();
    void onMinValChanged(int value);
    void onMaxValChanged(int value);
    void onRestart();
    void onQuit();

private:
    void buildDetectionUI(QVBoxLayout* parent);
    void buildCalibrationUI(QVBoxLayout* parent);
    void buildProcessingUI(QVBoxLayout* parent);
    void updateCalibrationHelp();
    void updateGaugeValueLabels();

    SharedState shared_;
    std::thread worker_;
    const std::string videoPath_;

    VideoWidget* videoWidget_;
    QWidget* controlPanel_;
    QVBoxLayout* controlLayout_;
    QTimer* timer_;
    AppMode currentMode_ = AppMode::kDetection;
    bool calibConfirmInitialized_ = false;

    // Detection
    QWidget* detectionPage_;
    QCheckBox* manualCb_;
    QSlider* cannySlider_;
    QLabel* cannyValLabel_;
    QSlider* accSlider_;
    QLabel* accValLabel_;
    QLabel* gaugeCountLabel_;
    QPushButton* addManualBtn_;
    QPushButton* confirmBtn_;

    // Calibration
    QWidget* calibrationPage_;
    QLabel* calibInstruction_;
    QLabel* gaugeProgress_;
    QPushButton* addAnotherBtn_;
    QPushButton* startCalibBtn_;
    QWidget* calibConfirmWidget_;
    QSpinBox* minValSpin_;
    QSpinBox* maxValSpin_;
    QPushButton* confirmCalibBtn_;
    QPushButton* cancelCalibBtn_;

    // Processing
    QWidget* processingPage_;
    QLabel* frameCountLabel_;
    QVBoxLayout* gaugeValuesLayout_;
    std::vector<QLabel*> gaugeValueLabels_;
    QPushButton* restartBtn_;
    QPushButton* quitBtn_;
};
