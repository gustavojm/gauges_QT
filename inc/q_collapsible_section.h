/*
    Elypson/qt-collapsible-section
    (c) 2016 Michael A. Voelkel - michael.alexander.voelkel@gmail.com

    This file is part of Elypson/qt-collapsible section.

    Elypson/qt-collapsible-section is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Elypson/qt-collapsible-section is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Elypson/qt-collapsible-section. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QParallelAnimationGroup>
#include <QScrollArea>
#include <QToolButton>
#include <QWidget>

namespace ui
{
    /**
     * @class QCollapsibleSection
     * @brief A collapsible section widget with a toggle button and animated content area.
     *
     * Based on Elypson/qt-collapsible-section.  Provides a header with a
     * colour swatch, title, and toggle button that expands or collapses
     * the content layout with an optional animation.
     *
     * @see GaugeSectionWidgets
     */
    class QCollapsibleSection : public QWidget
    {
        Q_OBJECT
        Q_DISABLE_COPY_MOVE(QCollapsibleSection)
        
    private:
        QGridLayout* mainLayout_;             ///< Root grid layout.
        QLabel* colorLabel_;                  ///< Colour swatch label in the header.
        QToolButton* toggleButton_;           ///< Expand/collapse toggle button.
        QFrame* headerLine_;                  ///< Decorative line below the header.
        QParallelAnimationGroup* toggleAnimation_; ///< Animation group for expand/collapse.
        QScrollArea* contentArea_;            ///< Scroll area holding the content widget.
        int animationDuration_;               ///< Duration of the toggle animation in ms.
        int collapsedHeight_;                 ///< Height of the collapsed header.
        bool isExpanded_ = false;             ///< True when the section is expanded.
        
    public slots:
        /**
         * @brief Toggles the section between expanded and collapsed states.
         * @param collapsed  If true, forces collapse; if false, forces expand.
         */
        void toggle(bool collapsed);

    public:
        /// Default animation duration (0 = instant).
        static const int DEFAULT_DURATION = 0;
    
        /**
         * @brief Constructs a collapsible section.
         * @param title             Header title text.
         * @param animationDuration Duration of the expand/collapse animation in ms.
         * @param parent            Qt parent widget.
         */
        explicit QCollapsibleSection(const QString& title = "", const int animationDuration = DEFAULT_DURATION, QWidget* parent = nullptr);

        /**
         * @brief Sets the content layout (takes ownership of the layout and its children).
         * @param contentLayout  Layout to place inside the collapsible area.
         */
        void setContentLayout(QLayout* contentLayout);
        
        /**
         * @brief Updates the header title text.
         * @param title  New title string.
         */
        void setTitle(QString title);

        /**
         * @brief Sets a colour swatch character and colour in the header.
         * @param swatch  Single character to display (e.g. "■").
         * @param color   Background colour for the swatch.
         */
        void setColorSwatch(const QString& swatch, const QColor& color);
        
        /**
         * @brief Recalculates animation heights after content changes.
         */
        void updateHeights();
    };
}

