/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Palette

import "internal"

//! A horizontal palette panel: its palettes are shown side by side, in columns.
//! Each column is a palette tree with its own vertical scrolling. Dropping a palette
//! on a column adds it there; dropping it between two columns creates a new column.
Item {
    id: root

    property NavigationSection navigationSection: null

    //! Index of this palette panel (8..10 = horizontal palette panels)
    property int panelIndex: 8
    property int navigationOrderStart: 1

    property alias contextMenuModel: contextMenuModel

    readonly property PaletteProvider paletteProvider: paletteRootModel.paletteProvider

    readonly property int columnWidth: 300
    readonly property int gapWidth: 12

    property int columnCount: 0

    implicitHeight: 4 * palettesPanelHeader.implicitHeight
    implicitWidth: columnWidth

    function refreshColumns() {
        root.columnCount = root.paletteProvider ? root.paletteProvider.panelColumnCount(root.panelIndex) : 0
    }

    Component.onCompleted: {
        console.warn("[palette-panels] HorizontalPalettesPanel completed, panel " + root.panelIndex)
        contextMenuModel.load()
        refreshColumns()
        console.warn("[palette-panels] HorizontalPalettesPanel columns: " + root.columnCount)
    }

    onPaletteProviderChanged: refreshColumns()

    Connections {
        target: root.paletteProvider

        function onPanelColumnsChanged() {
            root.refreshColumns()
        }
    }

    PalettesPanelContextMenuModel {
        id: contextMenuModel

        onExpandCollapseAllRequested: function(expand) {
            for (let i = 0; i < columnsRepeater.count; ++i) {
                const column = columnsRepeater.itemAt(i)
                if (column) {
                    column.tree.expandCollapseAll(expand)
                }
            }
        }
    }

    PaletteRootModel {
        id: paletteRootModel

        panelIndex: root.panelIndex
    }

    //! Drop zone between columns (and before the first / after the last one): creates a new column
    component ColumnGap: DropArea {
        id: gap

        property int position: 0
        // inline components don't see the ids of this file: pass what is needed explicitly
        property PaletteProvider provider: null
        property int panel: -1

        height: parent ? parent.height : 0
        keys: [ "application/musescore/palettetree" ]

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: 2
            color: ui.theme.accentColor
            opacity: gap.containsDrag ? 0.5 : 0
        }

        onDropped: function(drop) {
            if (!gap.provider || !gap.provider.isPaletteDragActive()) {
                return
            }
            if (gap.provider.dropPaletteToPanel(gap.panel, -1, -1, gap.position)) {
                drop.acceptProposedAction()
            }
        }
    }

    ColumnLayout {
        id: contentColumn

        readonly property int sideMargin: 12

        anchors.fill: parent
        anchors.leftMargin: sideMargin
        anchors.rightMargin: sideMargin

        spacing: sideMargin

        PalettesPanelHeader {
            id: palettesPanelHeader

            Layout.preferredWidth: root.columnWidth - 2 * contentColumn.sideMargin
            Layout.alignment: Qt.AlignTop | Qt.AlignLeft

            paletteProvider: root.paletteProvider
            panelIndex: root.panelIndex
            searchEnabled: false

            popupMaxHeight: contentColumn.height - palettesPanelHeader.height
            popupAnchorItem: root

            navigation.section: root.navigationSection
            navigation.order: root.navigationOrderStart

            onAddCustomPaletteRequested: function(paletteName) {
                root.paletteProvider.createCustomPaletteInPanel(root.panelIndex, paletteName)
            }
        }

        StyledFlickable {
            id: columnsFlickable

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: -contentColumn.sideMargin
            Layout.rightMargin: -contentColumn.sideMargin

            // dragging with the mouse is used to move palettes: scroll with the scrollbar instead
            interactive: false
            flickableDirection: Flickable.HorizontalFlick

            contentWidth: columnsRow.width
            contentHeight: height

            Row {
                id: columnsRow

                height: columnsFlickable.height

                Repeater {
                    id: columnsRepeater

                    model: root.columnCount

                    delegate: Row {
                        id: columnDelegate

                        required property int index
                        property alias tree: columnTree

                        height: columnsRow.height

                        ColumnGap {
                            width: root.gapWidth
                            position: columnDelegate.index
                            provider: root.paletteProvider
                            panel: root.panelIndex
                        }

                        Item {
                            width: root.columnWidth
                            height: parent.height

                            // Catches palettes dropped on the empty part of the column
                            DropArea {
                                anchors.fill: parent
                                keys: [ "application/musescore/palettetree" ]

                                onEntered: {
                                    if (columnTree.isForeignPaletteDrag()) {
                                        columnTree.foreignDragEntered()
                                    }
                                }

                                onExited: {
                                    if (columnTree.isForeignPaletteDrag()) {
                                        columnTree.foreignDragExited()
                                    }
                                }

                                onDropped: function(drop) {
                                    if (!columnTree.isForeignPaletteDrag()) {
                                        return
                                    }
                                    const destRow = columnTree.placeholderActive() ? columnTree.placeholderIndex() : columnTree.count
                                    if (columnTree.dropForeignPalette(destRow)) {
                                        drop.acceptProposedAction()
                                    }
                                }
                            }

                            PaletteTree {
                                id: columnTree

                                anchors.fill: parent

                                clip: true
                                paletteProvider: root.paletteProvider
                                panelIndex: root.panelIndex
                                columnIndex: columnDelegate.index

                                navigation.section: root.navigationSection
                                navigation.order: palettesPanelHeader.navigation.order + 1 + columnDelegate.index
                            }
                        }
                    }
                }

                // after the last column: fills the remaining width (the whole panel when it is empty)
                ColumnGap {
                    position: root.columnCount
                    provider: root.paletteProvider
                    panel: root.panelIndex
                    width: Math.max(root.gapWidth,
                                    columnsFlickable.width - root.columnCount * (root.columnWidth + root.gapWidth))
                }
            }
        }
    }
}
