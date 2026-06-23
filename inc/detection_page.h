#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QSlider;
class QLabel;
class QPushButton;

class DetectionPage : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DetectionPage)
    
public:
    explicit DetectionPage(QWidget* parent = nullptr);
    void connectToWorker(class Worker* worker);

public slots:
    void onDetectionCountChanged(int numGauges);
    void onManualPlacementActivated(bool active);
    void onManualInstructionChanged(int stage);

signals:
    void manualPlacementToggled(bool checked);
    void gaugeTypeChanged(int typeIndex);
    void cannyChanged(int value);
    void accChanged(int value);
    void confirmClicked();

private:
    QCheckBox* manualCb_ = nullptr;
    QComboBox* gaugeTypeCombo_ = nullptr;
    QWidget* cannyRow_ = nullptr;
    QSlider* cannySlider_ = nullptr;
    QLabel* cannyValLabel_ = nullptr;
    QWidget* accRow_ = nullptr;
    QSlider* accSlider_ = nullptr;
    QLabel* accValLabel_ = nullptr;
    QLabel* gaugeCountLabel_ = nullptr;
    QLabel* instructionLabel_ = nullptr;
    QPushButton* confirmBtn_ = nullptr;
};
