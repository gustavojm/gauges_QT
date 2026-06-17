#pragma once

#include <QWidget>

#include <cstddef>

class QCheckBox;
class QSlider;
class QLabel;
class QPushButton;

class DetectionPage : public QWidget {
    Q_OBJECT
public:
    explicit DetectionPage(QWidget* parent = nullptr);

public slots:
    void onDetectionUpdated(size_t numGauges);
    void setManualPlacementActive(bool active);

signals:
    void manualPlacementToggled(bool checked);
    void cannyChanged(int value);
    void accChanged(int value);
    void confirmClicked();

private:
    QCheckBox* manualCb_ = nullptr;
    QWidget* cannyRow_ = nullptr;
    QSlider* cannySlider_ = nullptr;
    QLabel* cannyValLabel_ = nullptr;
    QWidget* accRow_ = nullptr;
    QSlider* accSlider_ = nullptr;
    QLabel* accValLabel_ = nullptr;
    QLabel* gaugeCountLabel_ = nullptr;
    QPushButton* confirmBtn_ = nullptr;
};
