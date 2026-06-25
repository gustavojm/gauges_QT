#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QString>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <optional>
#include <vector>

#include "q_collapsible_section.h"
#include "worker.h"

/**
 * @struct GaugeSectionWidgets
 * @brief Bundle of UI widgets for a single gauge's collapsible section.
 *
 * Used by CalibrationPage and ProcessingPage to track the per-gauge
 * controls created by the rebuildCollapsibleSections template.
 */
struct GaugeSectionWidgets {
    ui::QCollapsibleSection* section = nullptr;  ///< The collapsible section container.
    QDoubleSpinBox* minSpin = nullptr;           ///< Minimum calibration value spinner.
    QDoubleSpinBox* maxSpin = nullptr;           ///< Maximum calibration value spinner.
    QLineEdit* tagEdit = nullptr;                ///< Instrument tag text field.
    QCheckBox* alarmEnableCheck = nullptr;       ///< Alarm enable checkbox.
    QComboBox* alarmDirCombo = nullptr;          ///< Alarm direction combo box (< / >).
    QDoubleSpinBox* alarmThresholdSpin = nullptr; ///< Alarm threshold value spinner.
};

/**
 * @brief Builds a display title string for a gauge section.
 *
 * Includes a warning prefix when the alarm is triggered.
 * @param index          0-based gauge index.
 * @param value          Current smoothed gauge value.
 * @param alarmTriggered True if the alarm is currently triggered.
 * @return Formatted QString like "Gauge 1: 123.45" or "⚠ Gauge 1: 123.45".
 */
inline QString GaugeTitle(int index, const std::optional<double>& value,
                           bool alarmTriggered = false) {
    QString prefix = alarmTriggered ? "\u26A0 " : "";
    if (value.has_value())
        return QString("%1Gauge %2: %3")
            .arg(prefix)
            .arg(index + 1)
            .arg(*value, 0, 'f', 2);
    return QString("%1Gauge %2: ---").arg(prefix).arg(index + 1);
}

/**
 * @brief Rebuilds the collapsible section UI for all gauges.
 *
 * Destroys existing sections, creates new ones from the calibration data,
 * and connects all spin boxes, combo boxes, checkboxes, and tag fields
 * to the page's signals via lambdas.
 *
 * @tparam Page  The page class (CalibrationPage or ProcessingPage) that
 *               owns the signals connected to the Worker.
 * @param[out] sections      Vector of GaugeSectionWidgets to repopulate.
 * @param sectionsLayout     QVBoxLayout to which sections are added.
 * @param scrollContent      Parent widget for the created sections.
 * @param calib              Current calibration data vector.
 * @param page               Pointer to the page (used for signal emission).
 *
 * @note This is a header-only template function to avoid explicit
 *       instantiations for each page type.
 */

template<typename Page>
void RebuildCollapsibleSections(
    std::vector<GaugeSectionWidgets>& sections,
    QVBoxLayout* sectionsLayout,
    QWidget* scrollContent,
    const QVector<GaugeCalibData>& calib,
    Page* page)
{
    while (!sections.empty()) {
        auto& s = sections.back();
        sectionsLayout->removeWidget(s.section);
        delete s.section;
        sections.pop_back();
    }

    for (qsizetype i = 0; i < calib.size(); i++) {
        auto* sec = new ui::QCollapsibleSection(
            GaugeTitle(i, calib[i].value),
            0, scrollContent);
        sec->SetColorSwatch("\u25A0", QColor::fromRgb(calib[i].color_rgb));
        sec->setStyleSheet(
            "ui--QCollapsibleSection { background: #2a2a32; border-radius: 4px; }"
            "ui--QCollapsibleSection QToolButton { color: black; }");

        auto* cl = new QVBoxLayout();
        cl->setContentsMargins(8, 4, 8, 8);
        cl->setSpacing(6);

        auto* tagRow = new QWidget();
        auto* tagLay = new QHBoxLayout(tagRow);
        tagLay->setContentsMargins(0, 0, 0, 0);
        tagLay->addWidget(new QLabel("TAG:"));
        auto* tagEdit = new QLineEdit();
        tagEdit->setPlaceholderText("e.g. 64323-PI-165");
        tagEdit->setText(calib[i].tag);
        tagLay->addWidget(tagEdit, 1);
        cl->addWidget(tagRow);

        auto* minRow = new QWidget();
        auto* minLay = new QHBoxLayout(minRow);
        minLay->setContentsMargins(0, 0, 0, 0);
        minLay->addWidget(new QLabel("Min:"));
        auto* minSpin = new QDoubleSpinBox();
        minSpin->setRange(-99999, 99999);
        minSpin->setDecimals(1);
        minSpin->setValue(calib[i].min_value);
        minLay->addWidget(minSpin, 1);
        cl->addWidget(minRow);

        auto* maxRow = new QWidget();
        auto* maxLay = new QHBoxLayout(maxRow);
        maxLay->setContentsMargins(0, 0, 0, 0);
        maxLay->addWidget(new QLabel("Max:"));
        auto* maxSpin = new QDoubleSpinBox();
        maxSpin->setRange(-99999, 99999);
        maxSpin->setDecimals(1);
        maxSpin->setValue(calib[i].max_value);
        maxLay->addWidget(maxSpin, 1);
        cl->addWidget(maxRow);

        auto* alarmDirRow = new QWidget();
        auto* alarmDirLay = new QHBoxLayout(alarmDirRow);
        alarmDirLay->setContentsMargins(0, 0, 0, 0);
        alarmDirLay->addWidget(new QLabel("Alarm:"));
        auto* alarmDirCombo = new QComboBox();
        alarmDirCombo->addItem("<");
        alarmDirCombo->addItem(">");
        alarmDirCombo->setCurrentIndex(
            calib[i].alarm_direction == AlarmDirection::kGreaterThan ? 0 : 1);
        alarmDirLay->addWidget(alarmDirCombo, 1);
        cl->addWidget(alarmDirRow);

        auto* alarmThreshRow = new QWidget();
        auto* alarmThreshLay = new QHBoxLayout(alarmThreshRow);
        alarmThreshLay->setContentsMargins(0, 0, 0, 0);
        alarmThreshLay->addWidget(new QLabel("Threshold:"));
        auto* alarmThresholdSpin = new QDoubleSpinBox();
        alarmThresholdSpin->setRange(-99999, 99999);
        alarmThresholdSpin->setDecimals(1);
        alarmThresholdSpin->setValue(calib[i].alarm_threshold);
        alarmThreshLay->addWidget(alarmThresholdSpin, 1);
        cl->addWidget(alarmThreshRow);

        auto* alarmEnableRow = new QWidget();
        auto* alarmEnableLay = new QHBoxLayout(alarmEnableRow);
        alarmEnableLay->setContentsMargins(0, 0, 0, 0);
        auto* alarmEnableCheck = new QCheckBox("Enable alarm");
        alarmEnableCheck->setChecked(calib[i].alarm_enabled);
        alarmEnableLay->addWidget(alarmEnableCheck);
        alarmEnableLay->addStretch();
        cl->addWidget(alarmEnableRow);

        sec->SetContentLayout(cl);
        sectionsLayout->addWidget(sec);

        QObject::connect(minSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), page,
                [&sections, i, page](double v) {
                    emit page->gaugeCalibRangeChanged(
                        i, v,
                        sections[i].maxSpin->value());
                });
        QObject::connect(maxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), page,
                [&sections, i, page](double v) {
                    emit page->gaugeCalibRangeChanged(
                        i, sections[i].minSpin->value(),
                        v);
                });

        QObject::connect(alarmDirCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), page,
                [&sections, i, page](int index) {
                    emit page->alarmDirectionChanged(
                        i, index == 0 ? AlarmDirection::kLessThan
                                      : AlarmDirection::kGreaterThan);
                });

        QObject::connect(alarmThresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), page,
                [&sections, i, page](double v) {
                    emit page->alarmThresholdChanged(i, v);
                });

        QObject::connect(alarmEnableCheck, &QCheckBox::toggled, page,
                [&sections, i, page](bool checked) {
                    emit page->alarmEnableChanged(i, checked);
                });

        QObject::connect(tagEdit, &QLineEdit::editingFinished, page,
                [&sections, i, page]() {
                    emit page->tagChanged(i, sections[i].tagEdit->text());
                });

        sections.push_back({sec, minSpin, maxSpin, tagEdit, alarmEnableCheck, alarmDirCombo, alarmThresholdSpin});
    }
}
