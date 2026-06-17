#pragma once

#include <QWidget>
#include <QVector>
#include <vector>

class QLabel;
class QPushButton;
class QVBoxLayout;

class ProcessingPage : public QWidget {
    Q_OBJECT
public:
    explicit ProcessingPage(QWidget* parent = nullptr);
    void connectToWorker(class Worker* worker);

public slots:
    void onFrameCountUpdated(int current, int total);
    void onGaugeValuesUpdated(const QVector<double>& values);

signals:
    void restartClicked();
    void quitClicked();

private:
    QLabel* frameCountLabel_ = nullptr;
    QVBoxLayout* gaugeValuesLayout_ = nullptr;
    std::vector<QLabel*> gaugeValueLabels_;
    QPushButton* restartBtn_ = nullptr;
    QPushButton* quitBtn_ = nullptr;
};
