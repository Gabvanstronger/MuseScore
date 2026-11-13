/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#include "palettemodel.h"

#include <QMimeData>

#include <algorithm>
#include <map>
#include <set>

#include "internal/palettetree.h"
#include "internal/palettecelliconengine.h"

#include "engraving/dom/actionicon.h"
#include "engraving/dom/beam.h"
#include "engraving/dom/chordrest.h"
#include "engraving/dom/select.h"

#include "modularity/ioc.h"
#include "translation.h"

using namespace mu;
using namespace mu::palette;
using namespace mu::engraving;

//---------------------------------------------------------
//   PaletteTreeModel::PaletteTreeModel
//---------------------------------------------------------

PaletteTreeModel::PaletteTreeModel(PaletteTreePtr tree, const muse::modularity::ContextPtr& ctx, QObject* parent)
    : QAbstractItemModel(parent), muse::Contextable(ctx), _paletteTree(tree)
{
    connect(this, &QAbstractItemModel::dataChanged, this, &PaletteTreeModel::onDataChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &PaletteTreeModel::setTreeChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &PaletteTreeModel::setTreeChanged);
    connect(this, &QAbstractItemModel::rowsInserted, this, &PaletteTreeModel::setTreeChanged);
    connect(this, &QAbstractItemModel::rowsMoved, this, &PaletteTreeModel::setTreeChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &PaletteTreeModel::setTreeChanged);

    configuration()->colorsChanged().onNotify(this, [this]() {
        notifyAboutCellsChanged(Qt::DecorationRole);
    });
}

//---------------------------------------------------------
//   PaletteTreeModel::onDataChanged
//---------------------------------------------------------

void PaletteTreeModel::onDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight,
                                     const QVector<int>& roles)
{
    Q_UNUSED(topLeft);
    Q_UNUSED(bottomRight);
    static const std::set<int> nonPersistentRoles({ CellActiveRole, PaletteExpandedRole, PanelExpandedRole, Qt::DecorationRole });

    bool treeChanged = false;
    for (int role : roles) {
        if (!nonPersistentRoles.count(role)) {
            treeChanged = true;
            break;
        }
    }

    if (treeChanged) {
        setTreeChanged();
    }
}

//---------------------------------------------------------
//   PaletteTreeModel::setTreeChanged
//---------------------------------------------------------

void PaletteTreeModel::setTreeChanged()
{
    _treeChanged = true;
    if (!_treeChangedSignalBlocked) {
        emit treeChanged();
    }
}

//---------------------------------------------------------
//   PaletteTreeModel::blockTreeChanged
//---------------------------------------------------------

bool PaletteTreeModel::blockTreeChanged(bool block)
{
    const bool wasBlocked = _treeChangedSignalBlocked;
    _treeChangedSignalBlocked = block;

    if (wasBlocked && !block && _treeChanged) {
        emit treeChanged();
    }

    return wasBlocked;
}

//---------------------------------------------------------
//   PaletteTreeModel::iptrToPalette
//---------------------------------------------------------

Palette* PaletteTreeModel::iptrToPalette(void* iptr, int* idx)
{
    const auto palette = std::find_if(palettes().begin(), palettes().end(), [iptr](const PalettePtr& p) {
        return iptr == p.get();
    });

    if (idx) {
        (*idx) = palette - palettes().begin();
    }

    if (palette != palettes().end()) {
        return static_cast<Palette*>(iptr);
    }
    return nullptr;
}

void PaletteTreeModel::notifyAboutCellsChanged(int changedRole)
{
    const size_t npalettes = palettes().size();
    for (size_t row = 0; row < npalettes; ++row) {
        Palette* palette = palettes()[row].get();

        const QModelIndex parent = index(int(row), 0, QModelIndex());
        const QModelIndex first = index(0, 0, parent);
        const QModelIndex last = index(palette->cellsCount() - 1, 0, parent);
        emit dataChanged(first, last, { changedRole });
    }
}

//---------------------------------------------------------
//   PaletteTreeModel::findPalette
//---------------------------------------------------------

const Palette* PaletteTreeModel::findPalette(const QModelIndex& index) const
{
    if (index.internalPointer() != _paletteTree.get()) {
        return nullptr;
    }

    const int row = index.row();
    if (index.column() != 0 || row < 0 || row > int(palettes().size())) {
        return nullptr;
    }

    return palettes()[row].get();
}

//---------------------------------------------------------
//   PaletteTreeModel::findPalette
//---------------------------------------------------------

Palette* PaletteTreeModel::findPalette(const QModelIndex& index)
{
    return const_cast<Palette*>(const_cast<const PaletteTreeModel*>(this)->findPalette(index));
}

//---------------------------------------------------------
//   PaletteTreeModel::findCell
//---------------------------------------------------------

PaletteCellConstPtr PaletteTreeModel::findCell(const QModelIndex& index) const
{
    if (const Palette* pp = iptrToPalette(index.internalPointer())) {
        const int row = index.row();
        if (index.column() != 0 || row < 0 || row >= pp->cellsCount()) {
            return nullptr;
        }
        return pp->cellAt(row);
    }

    return nullptr;
}

//---------------------------------------------------------
//   PaletteTreeModel::findCell
//---------------------------------------------------------

PaletteCellPtr PaletteTreeModel::findCell(const QModelIndex& index)
{
    return std::const_pointer_cast<PaletteCell>(const_cast<const PaletteTreeModel*>(this)->findCell(index));
}

//---------------------------------------------------------
//   PaletteTreeModel::setPaletteTree
//---------------------------------------------------------

void PaletteTreeModel::setPaletteTree(PaletteTreePtr newTree)
{
    beginResetModel();
    _paletteTree = newTree;
    endResetModel();

    _treeChanged = false;
}

//---------------------------------------------------------
//   PaletteTreeModel::index
//---------------------------------------------------------

QModelIndex PaletteTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }

    if (!parent.isValid()) {
        return createIndex(row, column, const_cast<void*>(static_cast<const void*>(_paletteTree.get())));
    }

    void* iptr = parent.internalPointer();

    if (iptr == _paletteTree.get()) {
        return createIndex(row, column, palettes()[parent.row()].get());
    }

    return QModelIndex();
}

//---------------------------------------------------------
//   PaletteTreeModel::parent
//---------------------------------------------------------

QModelIndex PaletteTreeModel::parent(const QModelIndex& modelIndex) const
{
    void* iptr = modelIndex.internalPointer();

    if (iptr == _paletteTree.get()) {
        return QModelIndex();
    }

    int row;
    if (iptrToPalette(iptr, &row)) {
        return index(row, /* column */ 0, QModelIndex());
    }

    return QModelIndex();
}

//---------------------------------------------------------
//   PaletteTreeModel::rowCount
//---------------------------------------------------------

int PaletteTreeModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid()) {
        return int(palettes().size());
    }

    void* iptr = parent.internalPointer();

    if (iptr == _paletteTree.get()) {
        const int row = parent.row();
        if (parent.column() != 0 || row < 0 || row >= int(palettes().size())) {
            return 0;
        }
        return palettes()[row]->cellsCount();
    }

    return 0;
}

//---------------------------------------------------------
//   PaletteTreeModel::columnCount
//---------------------------------------------------------

int PaletteTreeModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 1;
}

//---------------------------------------------------------
//   PaletteTreeModel::data
//---------------------------------------------------------

QVariant PaletteTreeModel::data(const QModelIndex& index, int role) const
{
    if (const Palette* pp = findPalette(index)) {
        switch (role) {
        case Qt::DisplayRole:
        case Qt::ToolTipRole:
            return pp->translatedName();
        case Qt::AccessibleTextRole:
            return QString("%1 palette").arg(pp->translatedName());
        case VisibleRole:
            return pp->isVisible();
        case CustomRole:
            return pp->isCustom();
        case EditableRole:
            return pp->isEditable();
        case GridSizeRole:
            return pp->scaledGridSize();
        case DrawGridRole:
            return pp->drawGrid();
        case PaletteExpandedRole:
            return pp->isExpanded();
        // TODO showMore?
        case PaletteTypeRole:
            return QVariant::fromValue(pp->type());
        case PaletteContentTypeRole:
            return QVariant::fromValue(pp->contentType());
        }
        return QVariant();
    }

    if (PaletteCellConstPtr cell = findCell(index)) {
        switch (role) {
        case Qt::DisplayRole:
            return QVariant();             // Don't show element names in
        // item views (i.e. just show icons). If you need
        // to know the name, use the ToolTip instead.
        case Qt::ToolTipRole:
            return cell->translatedName();
        case Qt::AccessibleTextRole: {
            QString name = cell->translatedName();
            //ScoreAccessibility::makeReadable(name); //! TODO
            return name;
        }
        case Qt::DecorationRole: {
            qreal extraMag = 1.0;
            if (const Palette* pp = iptrToPalette(index.internalPointer())) {
                extraMag = pp->mag();
            }
            return QIcon(new PaletteCellIconEngine(cell, iocContext(), extraMag * configuration()->paletteScaling()));
        }
        case PaletteCellRole:
            return QVariant::fromValue(cell.get());
        case VisibleRole:
            return cell->visible;
        case CustomRole:
            return cell->custom;
        case EditableRole: {
            if (const Palette* pp = iptrToPalette(index.internalPointer())) {
                return pp->isEditable();
            }
            return false;
        }
        case MimeDataRole: {
            QVariantMap map;
            if (cell->element) {
                map[mimeSymbolFormat] = cell->element->mimeData().toQByteArray();
            }
            map[PaletteCell::mimeDataFormat] = cell->toMimeData();
            return map;
        }
        case CellActiveRole:
            return cell->active;
        default:
            break;
        }
        return QVariant();
    }

    // data for root item
    switch (role) {
    case EditableRole:
        return true;
    default:
        break;
    }

    return QVariant();
}

//---------------------------------------------------------
//   PaletteTreeModel::setData
//---------------------------------------------------------

bool PaletteTreeModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (Palette* pp = findPalette(index)) {
        switch (role) {
        case VisibleRole:
            if (value.canConvert<bool>()) {
                const bool val = value.toBool();
                if (val != pp->isVisible()) {
                    pp->setVisible(val);
                    emit dataChanged(index, index, { VisibleRole });
                }
                return true;
            }
            return false;
        case EditableRole:
            if (value.canConvert<bool>()) {
                const bool val = value.toBool();
                if (val != pp->isEditable()) {
                    pp->setEditable(val);
                    emit dataChanged(index, index, { EditableRole });
                    // notify cells editability changed too
                    const QModelIndex childFirstIndex = PaletteTreeModel::index(0, 0, index);
                    const int rows = rowCount(index);
                    const QModelIndex childLastIndex = PaletteTreeModel::index(rows - 1, 0, index);
                    emit dataChanged(childFirstIndex, childLastIndex, { EditableRole });
                }
                return true;
            }
            return false;
        case PaletteExpandedRole:
            if (value.canConvert<bool>()) {
                const bool val = value.toBool();
                if (val != pp->isExpanded()) {
                    const bool singlePalette = configuration()->isSinglePalette().val;

                    if (singlePalette && val) {
                        for (auto& palette : palettes()) {
                            palette->setExpanded(false);
                        }
                        pp->setExpanded(val);

                        const QModelIndex parent = index.parent();
                        const int rows = rowCount(parent);
                        const QModelIndex first = PaletteTreeModel::index(0, 0, parent);
                        const QModelIndex last = PaletteTreeModel::index(rows - 1, 0, parent);
                        emit dataChanged(first, last, { PaletteExpandedRole });
                    } else {
                        pp->setExpanded(val);
                        emit dataChanged(index, index, { PaletteExpandedRole });
                    }
                }
                return true;
            }
            return false;
        case Qt::DisplayRole:
            pp->setName(value.toString());
            emit dataChanged(index, index, { Qt::DisplayRole, Qt::AccessibleTextRole });
            return true;
//                   case CustomRole:
//                         if (value.canConvert<bool>()) {
//                               const bool val = value.toBool();
//                               if (val != pp->custom()) {
//                                     pp->setCustom(val);
//                                     emit dataChanged(index, index, { CustomRole });
//                                     }
//                               return true;
//                               }
//                         return false;
//                   case gridSizeRole:
//                         return pp->gridSize();
//                   case drawGridRole:
//                         return pp->drawGrid();
        default:
            break;
        }
        return false;
    }

    if (PaletteCellPtr cell = findCell(index)) {
        switch (role) {
//                   case Qt::DisplayRole:
//                   case Qt::ToolTipRole:
        // or EditRole?
//                         return cell->name;
        case PaletteCellRole: {
            PaletteCell* newCell = value.value<PaletteCell*>();
            if (!newCell) {
                return false;
            }
            cell->element = newCell->element;
            cell->untranslatedElement = newCell->untranslatedElement;
            cell->name = newCell->name;
            cell->id = newCell->id;
            emit dataChanged(index, index);
            return true;
        };
        case VisibleRole:
            if (value.canConvert<bool>()) {
                const bool val = value.toBool();
                if (val != cell->visible) {
                    cell->visible = val;
                    emit dataChanged(index, index, { VisibleRole });
                }
                return true;
            }
            return false;
        case CustomRole:
            if (value.canConvert<bool>()) {
                const bool val = value.toBool();
                if (val != cell->custom) {
                    cell->custom = val;
                    emit dataChanged(index, index, { CustomRole });
                }
                return true;
            }
            return false;
        case MimeDataRole: {
            const QVariantMap map = value.toMap();

            if (map.contains(PaletteCell::mimeDataFormat)) {
                const QByteArray cellMimeData = map[PaletteCell::mimeDataFormat].toByteArray();
                PaletteCellPtr newCell(PaletteCell::fromMimeData(cellMimeData, iocContext()));
                if (!newCell) {
                    return false;
                }

                cell->element = newCell->element;
                cell->untranslatedElement = newCell->untranslatedElement;
                cell->name = newCell->name;
                cell->id = newCell->id;
            } else if (map.contains(mimeSymbolFormat)) {
                const QByteArray elementMimeData = map[mimeSymbolFormat].toByteArray();
                PaletteCellPtr newCell = PaletteCell::fromElementMimeData(elementMimeData, iocContext());

                cell->element = newCell->element;
                cell->untranslatedElement = newCell->untranslatedElement;
                cell->name = newCell->name;
                cell->id = newCell->id;

                cell->custom = true;               // mark the updated cell custom
            } else {
                return false;
            }

            emit dataChanged(index, index);
            return true;
        }
        default:
            break;
        }
        return false;
    }

    return false;
}

//---------------------------------------------------------
//   PaletteTreeModel::roleNames
//---------------------------------------------------------

QHash<int, QByteArray> PaletteTreeModel::roleNames() const
{
    QHash<int, QByteArray> roles(QAbstractItemModel::roleNames());
    roles[MimeDataRole] = "mimeData";
    roles[GridSizeRole] = "gridSize";
    roles[DrawGridRole] = "drawGrid";
    roles[CustomRole] = "custom";
    roles[EditableRole] = "editable";
    roles[PaletteExpandedRole] = "expanded";
    roles[CellActiveRole] = "cellActive";
    roles[Qt::AccessibleTextRole] = "accessibleText";
    return roles;
}

//---------------------------------------------------------
//   PaletteTreeModel::flags
//---------------------------------------------------------

Qt::ItemFlags PaletteTreeModel::flags(const QModelIndex& index) const
{
    return QAbstractItemModel::flags(index) | Qt::ItemIsDragEnabled;
}

//---------------------------------------------------------
//   PaletteTreeModel::supportedDropActions
//---------------------------------------------------------

Qt::DropActions PaletteTreeModel::supportedDropActions() const
{
    return Qt::DropActions(Qt::CopyAction | Qt::MoveAction);
}

//---------------------------------------------------------
//   PaletteTreeModel::mimeData
//---------------------------------------------------------

QMimeData* PaletteTreeModel::mimeData(const QModelIndexList& indexes) const
{
    QMimeData* mime = QAbstractItemModel::mimeData(indexes);   // TODO: needed or use only "our" MIME data?

    if (indexes.empty() || indexes.size() > 1) {
        return mime;
    }

    if (const Palette* pp = findPalette(indexes[0])) {
        mime->setData(Palette::mimeDataFormat, pp->toMimeData());
    } else if (PaletteCellConstPtr cell = findCell(indexes[0])) {
        mime->setData(mimeSymbolFormat, cell->element->mimeData().toQByteArray());
    }

    return mime;
}

//---------------------------------------------------------
//   PaletteTreeModel::mimeTypes
//---------------------------------------------------------

QStringList PaletteTreeModel::mimeTypes() const
{
    QStringList types = QAbstractItemModel::mimeTypes();
    types << mimeSymbolFormat;
    return types;
}

//---------------------------------------------------------
//   PaletteTreeModel::canDropMimeData
//---------------------------------------------------------

bool PaletteTreeModel::canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                                       const QModelIndex& parent) const
{
    Q_UNUSED(column);

    if (!parent.isValid()) {
        return (action == Qt::CopyAction) && data->hasFormat(Palette::mimeDataFormat);
    }

    if (const Palette* pp = findPalette(parent)) {
        if (row < 0 || row > int(pp->cellsCount())) {
            return false;
        }

        if (data->hasFormat(PaletteCell::mimeDataFormat)) {
            return action & (Qt::CopyAction | Qt::MoveAction);
        } else if (data->hasFormat(mimeSymbolFormat)) {
            return action == Qt::CopyAction;
        }
    }
    return false;
}

//---------------------------------------------------------
//   PaletteTreeModel::dropMimeData
//---------------------------------------------------------

bool PaletteTreeModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                                    const QModelIndex& parent)
{
    Q_UNUSED(column);   // when dropping at the end of palette, column == -1. Probably an effect of proxy models

    if (!parent.isValid()) {
        if (action != Qt::CopyAction || !data->hasFormat(Palette::mimeDataFormat)) {
            return false;
        }
        if (row < 0 || row > int(palettes().size())) {
            return false;
        }

        auto palette = Palette::fromMimeData(data->data(Palette::mimeDataFormat), iocContext());
        if (!palette) {
            return false;
        }

        beginInsertRows(parent, row, row);
        palettes().insert(palettes().begin() + row, palette);
        endInsertRows();
        return true;
    }

    if (Palette* pp = findPalette(parent)) {
        if (row < 0 || row > int(pp->cellsCount())) {
            return false;
        }

        PaletteCellPtr cell;

        if (data->hasFormat(PaletteCell::mimeDataFormat)) {
            cell = PaletteCell::fromMimeData(data->data(PaletteCell::mimeDataFormat), iocContext());
            if (action == Qt::CopyAction) {
                cell->custom = true;
            }
        } else if (data->hasFormat(mimeSymbolFormat)) {
            cell = PaletteCell::fromElementMimeData(data->data(mimeSymbolFormat), iocContext());
            cell->custom = true;       // the cell is created by dropping an element so it is custom
        }

        if (!cell) {
            return false;
        }

        beginInsertRows(parent, row, row);
        pp->insertCell(row, cell);
        endInsertRows();
        return true;
    }
    return false;
}

//---------------------------------------------------------
//   PaletteTreeModel::moveRows
//---------------------------------------------------------

bool PaletteTreeModel::moveRows(const QModelIndex& sourceParent, int sourceRow, int count,
                                const QModelIndex& destinationParent, int destinationChild)
{
    const bool sameParent = sourceParent == destinationParent;

    if (!sourceParent.isValid()) {
        // moving palettes
        if (sourceRow + count > int(palettes().size()) || destinationChild >= int(palettes().size())) {
            return false;
        }

        if (!sameParent) {
            // Cannot move between different parents, at least for now
            // TODO: reconsider?
            return false;
        }

        // The moved rows are considered to be inserted *before* destinationRow,
        // so if we want to move a row down in the list within the same parent
        // then we should increment the destinationChild index.
        const int destinationRow = (destinationChild >= sourceRow) ? destinationChild + 1 : destinationChild;

        if (sameParent && sourceRow == destinationRow) {
            return false;
        }

        std::vector<PalettePtr> movedPalettes;

        auto srcBegin = palettes().begin() + sourceRow;
        auto srcEnd = srcBegin + count;

        // As of Qt 5.12, Qt proxy models rebuild the entire index mapping if
        // layoutChanged() gets emitted (i.e. if begin/endMoveRows() gets called).
        // Performance is much better when doing remove + insert rows instead.
        beginRemoveRows(sourceParent, sourceRow, sourceRow + count - 1);
        movedPalettes.reserve(count);
        movedPalettes.insert(movedPalettes.end(), std::make_move_iterator(srcBegin), std::make_move_iterator(srcEnd));
        palettes().erase(srcBegin, srcEnd);
        endRemoveRows();

        const int destIdx = (destinationRow < sourceRow) ? destinationRow : (destinationRow - count);
        auto dest = palettes().begin() + destIdx;

        beginInsertRows(destinationParent, destIdx, destIdx + count - 1);
        palettes().insert(dest, std::make_move_iterator(movedPalettes.begin()),
                          std::make_move_iterator(movedPalettes.end()));
        endInsertRows();

        return true;
    }

    Palette* sourcePalette = findPalette(sourceParent);
    Palette* destPalette = sameParent ? sourcePalette : findPalette(destinationParent);
    if (sourcePalette && destPalette) {
        // moving palette cells
        if (sourceRow + count > int(sourcePalette->cellsCount()) || destinationChild > int(destPalette->cellsCount())) {
            return false;
        }

        // The moved rows are considered to be inserted *before* destinationRow,
        // so if we want to move a row down in the list within the same parent
        // then we should increment the destinationChild index.
        const int destinationRow
            = (sameParent && destinationChild >= sourceRow) ? destinationChild + 1 : destinationChild;

        if (sameParent && sourceRow == destinationRow) {
            return false;
        }

        // As of Qt 5.12, Qt proxy models rebuild the entire index mapping if
        // layoutChanged() gets emitted (i.e. if begin/endMoveRows() gets called).
        // Performance is much better when doing remove + insert rows instead.
        beginRemoveRows(sourceParent, sourceRow, sourceRow + count - 1);
        auto movedCells(sourcePalette->takeCells(sourceRow, count));
        endRemoveRows();

        const int destIdx = (sameParent && destinationRow >= sourceRow) ? (destinationRow - count) : destinationRow;

        beginInsertRows(destinationParent, destIdx, destIdx + count - 1);
        destPalette->insertCells(destIdx, movedCells);
        endInsertRows();

        return true;
    }

    return false;
}

//---------------------------------------------------------
//   PaletteTreeModel::removeRows
//---------------------------------------------------------

bool PaletteTreeModel::removeRows(int row, int count, const QModelIndex& parent)
{
    if (!parent.isValid()) {
        // removing palettes
        if (row < 0 || row + count > int(palettes().size())) {
            return false;
        }

        beginRemoveRows(parent, row, row + count - 1);
        auto rangeBegin = palettes().begin() + row;
        auto rangeEnd = rangeBegin + count;
        palettes().erase(rangeBegin, rangeEnd);
        endRemoveRows();
        return true;
    }

    if (Palette* palette = findPalette(parent)) {
        // removing palette cells
        if (row < 0 || row + count > palette->cellsCount()) {
            return false;
        }

        beginRemoveRows(parent, row, row + count - 1);
        palette->takeCells(row, count);
        endRemoveRows();
        return true;
    }

    return false;
}

//---------------------------------------------------------
//   PaletteTreeModel::insertRows
//---------------------------------------------------------

bool PaletteTreeModel::insertRows(int row, int count, const QModelIndex& parent)
{
    if (!parent.isValid()) {
        // inserting palettes
        if (row < 0 || row > int(palettes().size())) {
            return false;
        }

        beginInsertRows(parent, row, row + count - 1);
        for (int i = 0; i < count; ++i) {
            PalettePtr p = std::make_shared<Palette>(iocContext(), Palette::Type::Custom);
            p->setName(QT_TRANSLATE_NOOP("palette", "Untitled palette"));
            p->setGridSize(QSize(48, 48));
            p->setExpanded(true);
            palettes().insert(palettes().begin() + row, p);
        }
        endInsertRows();
        return true;
    }

    if (Palette* palette = findPalette(parent)) {
        // inserting palette cells
        if (row < 0 || row > palette->cellsCount()) {
            return false;
        }

        beginInsertRows(parent, row, row + count - 1);
        for (int i = 0; i < count; ++i) {
            PaletteCellPtr cell = std::make_shared<PaletteCell>(iocContext(), palette);
            palette->insertCell(row, cell);
        }
        endInsertRows();
        return true;
    }

    return false;
}

//---------------------------------------------------------
//   PaletteTreeModel::insertPalette
//---------------------------------------------------------

bool PaletteTreeModel::insertPalette(PalettePtr pp, int row, const QModelIndex& parent)
{
    if (row < 0 || row > int(palettes().size()) || parent != QModelIndex()) {
        return false;
    }
    beginInsertRows(parent, row, row);
    palettes().insert(palettes().begin() + row, pp);
    endInsertRows();
    return true;
}

//---------------------------------------------------------
//   PaletteTreeModel::updateCellsState
//---------------------------------------------------------

void PaletteTreeModel::updateCellsState(const Selection& sel)
{
    const ChordRest* cr = sel.firstChordRest();
    const BeamMode bm = cr ? cr->beamMode() : BeamMode::NONE;
    const ActionIconType beamActionType = Beam::actionIconTypeForBeamMode(bm);
    bool deactivateAll = !cr;

    for (EngravingItem* e : sel.elements()) {
        if (e->isNote()) {
            e = e->parentItem();
        }
        if (e->isChordRest()) {
            if (toChordRest(e)->beamMode() != bm) {
                deactivateAll = true;
            }
        }
    }

    const size_t npalettes = palettes().size();
    for (size_t row = 0; row < npalettes; ++row) {
        Palette* palette = palettes()[row].get();
        // TODO: should this be turned on for all palettes?
        if (palette->type() != Palette::Type::Beam) {
            continue;
        }

        for (int ci = 0; ci < palette->cellsCount(); ++ci) {
            PaletteCellPtr cell = palette->cellAt(ci);
            if (deactivateAll) {
                cell->active = false;
            } else if (cell->element && cell->element->isActionIcon()) {
                const ActionIcon* action = toActionIcon(cell->element.get());
                cell->active = (action->actionType() == beamActionType);
            }
        }
    }

    notifyAboutCellsChanged(CellActiveRole);
}

//---------------------------------------------------------
//   PaletteTreeModel::retranslate
//---------------------------------------------------------

void PaletteTreeModel::retranslate()
{
    _paletteTree->retranslate();
}

//---------------------------------------------------------
//   PaletteTreeModel::findPaletteCell
//---------------------------------------------------------

QModelIndex PaletteTreeModel::findPaletteCell(const PaletteCell& cell, const QModelIndex& parent) const
{
    if (const Palette* pp = findPalette(parent)) {
        const int idx = pp->indexOfCell(cell);
        if (idx == -1) {
            return QModelIndex();
        }
        return index(idx, /* column */ 0, parent);
    }
    return QModelIndex();
}

//---------------------------------------------------------
//   PaletteTreeModel::match
///   Currently only searching for a given palette cell is
///   implemented, for anything else the corresponding
///   member function of QAbstractItemModel is used.
//---------------------------------------------------------

QModelIndexList PaletteTreeModel::match(const QModelIndex& start, int role, const QVariant& value, int hits,
                                        Qt::MatchFlags flags) const
{
    if (role != PaletteCellRole || flags != Qt::MatchExactly || hits != 1
        || !value.canConvert<const PaletteCell*>()
        || !findPalette(start.parent())
        ) {
        return QAbstractItemModel::match(start, role, value, hits, flags);
    }

    const PaletteCell* cell = value.value<const PaletteCell*>();
    if (!cell) {
        return QModelIndexList();
    }

    return QModelIndexList({ findPaletteCell(*cell, start.parent()) });
}

//---------------------------------------------------------
//   PaletteTreeModel::itemDataChanged
//---------------------------------------------------------

void PaletteTreeModel::itemDataChanged(const QModelIndex& idx)
{
    emit dataChanged(idx, idx);
    if (findPalette(idx)) {
        // palette cells appearance depends on palette settings
        const QModelIndex childFirstIndex = index(0, 0, idx);
        const int rows = rowCount(idx);
        const QModelIndex childLastIndex = index(rows - 1, 0, idx);
        emit dataChanged(childFirstIndex, childLastIndex, { Qt::DecorationRole });
    }
}

//---------------------------------------------------------
//   PaletteCellFilter::addChainedFilter
///   Ownership over the added filter is passed to this
///   filter.
//---------------------------------------------------------

void PaletteCellFilter::addChainedFilter(PaletteCellFilter* newFilter)
{
    PaletteCellFilter* f = this;
    while (f->chainedFilter) {
        f = f->chainedFilter;
    }

    newFilter->setParent(f);
    f->chainedFilter = newFilter;
    connect(newFilter, &PaletteCellFilter::filterChanged, f, &PaletteCellFilter::filterChanged);
}

//---------------------------------------------------------
//   PaletteCellFilter::accept
//---------------------------------------------------------

bool PaletteCellFilter::accept(const PaletteCell& cell) const
{
    const PaletteCellFilter* f = this;
    while (f) {
        if (!f->acceptCell(cell)) {
            return false;
        }
        f = f->chainedFilter;
    }
    return true;
}

//---------------------------------------------------------
//   PaletteCellFilter::connectToModel
//---------------------------------------------------------

void PaletteCellFilter::connectToModel(const QAbstractItemModel* model)
{
//       TODO: are all these needed?
//       columnsInserted(const QModelIndex &parent, int first, int last)
//       columnsMoved(const QModelIndex &parent, int start, int end, const QModelIndex &destination, int column)
//       columnsRemoved(const QModelIndex &parent, int first, int last)
    connect(model, &QAbstractItemModel::dataChanged, this, &PaletteCellFilter::filterChanged);
//       dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles = ...)
    connect(model, &QAbstractItemModel::layoutChanged, this, &PaletteCellFilter::filterChanged);
//       layoutChanged(const QList<QPersistentModelIndex> &parents = ..., QAbstractItemModel::LayoutChangeHint hint = ...)
    connect(model, &QAbstractItemModel::modelReset, this, &PaletteCellFilter::filterChanged);
//       modelReset()
    connect(model, &QAbstractItemModel::rowsInserted, this, &PaletteCellFilter::filterChanged);
//       rowsInserted(const QModelIndex &parent, int first, int last)
    connect(model, &QAbstractItemModel::rowsMoved, this, &PaletteCellFilter::filterChanged);
//       rowsMoved(const QModelIndex &parent, int start, int end, const QModelIndex &destination, int row)
    connect(model, &QAbstractItemModel::rowsRemoved, this, &PaletteCellFilter::filterChanged);
//       rowsRemoved(const QModelIndex &parent, int first, int last)
}

//---------------------------------------------------------
//   ExcludePaletteCellFilter
//---------------------------------------------------------

class ExcludePaletteCellFilter : public PaletteCellFilter
{
    const Palette* excludePalette;
    const QPersistentModelIndex paletteIndex; // filter is valid as long as this index is valid too

public:
    ExcludePaletteCellFilter(const Palette* p, QPersistentModelIndex index, QObject* parent = nullptr)
        : PaletteCellFilter(parent), excludePalette(p), paletteIndex(index) {}

    bool acceptCell(const PaletteCell& cell) const override
    {
        return paletteIndex.isValid() && -1 == excludePalette->indexOfCell(cell, /* matchName */ false);
    }
};

//---------------------------------------------------------
//   PaletteTreeModel::getFilter
///   The ownership of the returned filter is passed to a
///   caller.
//---------------------------------------------------------

PaletteCellFilter* PaletteTreeModel::getFilter(const QModelIndex& index) const
{
    if (const Palette* pp = findPalette(index)) {
        ExcludePaletteCellFilter* filter = new ExcludePaletteCellFilter(pp, index);
        filter->connectToModel(this);
        return filter;
    }

    // TODO: make a filter for a single cell?

    return nullptr;
}

//---------------------------------------------------------
//   FilterPaletteTreeModel::FilterPaletteTreeModel
//---------------------------------------------------------

FilterPaletteTreeModel::FilterPaletteTreeModel(PaletteCellFilter* filter, PaletteTreeModel* model, QObject* parent)
    : QSortFilterProxyModel(parent), cellFilter(filter)
{
    cellFilter->setParent(this);
//       connect(cellFilter, &PaletteCellFilter::filterChanged, this, &QSortFilterProxyModel::invalidate);
    connect(cellFilter, &PaletteCellFilter::filterChanged, this, &FilterPaletteTreeModel::invalidateFilter);

    setSourceModel(model);
}

//---------------------------------------------------------
//   FilterPaletteTreeModel::filterAcceptsRow
//---------------------------------------------------------

bool FilterPaletteTreeModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const QModelIndex& cellIndex = sourceModel()->index(sourceRow, /* column */ 0, sourceParent);
    const QVariant cellData = sourceModel()->data(cellIndex, PaletteTreeModel::PaletteCellRole);
    const PaletteCell* cell = cellData.value<const PaletteCell*>();
    if (!cell) { // a palette or just an unrelated model
        return true;
    }
    return cellFilter->accept(*cell);
}

//---------------------------------------------------------
//   PaletteCellFilterProxyModel::PaletteCellFilterProxyModel
//---------------------------------------------------------

PaletteCellFilterProxyModel::PaletteCellFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setFilterRole(Qt::ToolTipRole);   // palette cells have no data for DisplayRole
}

//---------------------------------------------------------
//   PaletteCellFilterProxyModel::filterAcceptsRow
//---------------------------------------------------------

bool PaletteCellFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const QAbstractItemModel* model = sourceModel();
    const QModelIndex rowIndex = model->index(sourceRow, 0, sourceParent);
    const int rowCount = model->rowCount(rowIndex);

    if (rowCount == 0) {
        if (QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent)) {
            return true;
        }
        // accept row if its parent is accepted by filter: necessary to be able to search by palette name
        if (sourceParent.isValid()
            && QSortFilterProxyModel::filterAcceptsRow(sourceParent.row(), sourceParent.parent())) {
            return true;
        }
        return false;
    }

    for (int i = 0; i < rowCount; ++i) {
        if (filterAcceptsRow(i, rowIndex)) {
            return true;
        }
    }

    return false;
}

//---------------------------------------------------------
//   Palette panels
//---------------------------------------------------------

Palette::PanelStates PaletteTreeModel::palettePanelStates(const QModelIndex& paletteIndex) const
{
    const Palette* palette = findPalette(paletteIndex);
    return palette ? palette->panelStates() : Palette::PanelStates();
}

void PaletteTreeModel::setPalettePanelStates(const QModelIndex& paletteIndex, const Palette::PanelStates& states)
{
    Palette* palette = findPalette(paletteIndex);
    if (!palette) {
        return;
    }

    palette->setPanelStates(states);

    const bool visible = !states.empty();
    if (palette->isVisible() != visible) {
        palette->setVisible(visible);
        emit dataChanged(paletteIndex, paletteIndex, { VisibleRole, PanelsRole });
    } else {
        emit dataChanged(paletteIndex, paletteIndex, { PanelsRole });
    }
}

bool PaletteTreeModel::isPaletteInPanel(const QModelIndex& paletteIndex, int panel) const
{
    const Palette* palette = findPalette(paletteIndex);
    return palette && palette->isVisible() && palette->isInPanel(panel);
}

int PaletteTreeModel::palettePanelOrder(const QModelIndex& paletteIndex, int panel) const
{
    const Palette::PanelStates states = palettePanelStates(paletteIndex);
    auto it = states.find(panel);
    return it != states.end() ? it->second.order : -1;
}

bool PaletteTreeModel::palettePanelExpanded(const QModelIndex& paletteIndex, int panel) const
{
    const Palette::PanelStates states = palettePanelStates(paletteIndex);
    auto it = states.find(panel);
    return it != states.end() ? it->second.expanded : false;
}

void PaletteTreeModel::addPaletteToPanel(const QModelIndex& paletteIndex, int panel, bool expanded, int column)
{
    const Palette* palette = findPalette(paletteIndex);
    if (!palette) {
        return;
    }

    // a hidden palette is in no panel: forget any stale panel assignment
    Palette::PanelStates states = palette->isVisible() ? palette->panelStates() : Palette::PanelStates();
    Palette::PanelState state;
    state.expanded = expanded;
    state.column = column;
    states[panel] = state;
    setPalettePanelStates(paletteIndex, states);
}

int PaletteTreeModel::palettePanelColumn(const QModelIndex& paletteIndex, int panel) const
{
    const Palette::PanelStates states = palettePanelStates(paletteIndex);
    auto it = states.find(panel);
    return it != states.end() ? it->second.column : 0;
}

int PaletteTreeModel::panelColumnCount(int panel) const
{
    int count = 0;
    for (int row = 0; row < rowCount(); ++row) {
        const QModelIndex paletteIndex = index(row, 0);
        if (isPaletteInPanel(paletteIndex, panel)) {
            count = std::max(count, palettePanelColumn(paletteIndex, panel) + 1);
        }
    }
    return count;
}

void PaletteTreeModel::insertPanelColumn(int panel, int column)
{
    for (int row = 0; row < rowCount(); ++row) {
        const QModelIndex paletteIndex = index(row, 0);
        Palette* palette = findPalette(paletteIndex);
        if (!palette || !isPaletteInPanel(paletteIndex, panel)) {
            continue;
        }

        Palette::PanelStates states = palette->panelStates();
        auto it = states.find(panel);
        if (it == states.end() || it->second.column < column) {
            continue;
        }

        it->second.column++;
        palette->setPanelStates(states);
        emit dataChanged(paletteIndex, paletteIndex, { PanelsRole });
    }
}

void PaletteTreeModel::normalizePanelColumns(int panel)
{
    std::set<int> columns;
    for (int row = 0; row < rowCount(); ++row) {
        const QModelIndex paletteIndex = index(row, 0);
        if (isPaletteInPanel(paletteIndex, panel)) {
            columns.insert(palettePanelColumn(paletteIndex, panel));
        }
    }

    std::map<int, int> newColumns;
    int next = 0;
    for (int column : columns) {
        newColumns[column] = next++;
    }

    for (int row = 0; row < rowCount(); ++row) {
        const QModelIndex paletteIndex = index(row, 0);
        Palette* palette = findPalette(paletteIndex);
        if (!palette || !isPaletteInPanel(paletteIndex, panel)) {
            continue;
        }

        Palette::PanelStates states = palette->panelStates();
        auto it = states.find(panel);
        if (it == states.end() || newColumns[it->second.column] == it->second.column) {
            continue;
        }

        it->second.column = newColumns[it->second.column];
        palette->setPanelStates(states);
        emit dataChanged(paletteIndex, paletteIndex, { PanelsRole });
    }
}

void PaletteTreeModel::removePaletteFromPanel(const QModelIndex& paletteIndex, int panel)
{
    Palette::PanelStates states = palettePanelStates(paletteIndex);
    if (states.erase(panel)) {
        setPalettePanelStates(paletteIndex, states);
        normalizePanelColumns(panel); // a column of a horizontal panel may have become empty
    }
}

void PaletteTreeModel::setPalettePanelExpanded(const QModelIndex& paletteIndex, int panel, bool expanded)
{
    Palette* palette = findPalette(paletteIndex);
    if (!palette) {
        return;
    }

    Palette::PanelStates states = palette->panelStates();
    auto it = states.find(panel);
    if (it == states.end() || it->second.expanded == expanded) {
        return;
    }

    it->second.expanded = expanded;
    palette->setPanelStates(states);
    emit dataChanged(paletteIndex, paletteIndex, { PanelExpandedRole });
}

void PaletteTreeModel::setPanelOrder(int panel, const std::vector<int>& rows)
{
    for (size_t i = 0; i < rows.size(); ++i) {
        const QModelIndex paletteIndex = index(rows[i], 0);
        Palette* palette = findPalette(paletteIndex);
        if (!palette) {
            continue;
        }

        Palette::PanelStates states = palette->panelStates();
        auto it = states.find(panel);
        if (it == states.end() || it->second.order == int(i)) {
            continue;
        }

        it->second.order = int(i);
        palette->setPanelStates(states);
        emit dataChanged(paletteIndex, paletteIndex, { PanelsRole });
    }
}

//---------------------------------------------------------
//   PanelPaletteFilterProxyModel
//---------------------------------------------------------

PanelPaletteFilterProxyModel::PanelPaletteFilterProxyModel(int panel, int column, QObject* parent)
    : QSortFilterProxyModel(parent), m_panel(panel), m_column(column)
{
    setFilterRole(PaletteTreeModel::VisibleRole);
    setFilterFixedString("true");
}

void PanelPaletteFilterProxyModel::setSourceModel(QAbstractItemModel* model)
{
    if (sourceModel()) {
        disconnect(sourceModel(), &QAbstractItemModel::dataChanged, this, &PanelPaletteFilterProxyModel::onSourceDataChanged);
    }

    QSortFilterProxyModel::setSourceModel(model);

    if (model) {
        connect(model, &QAbstractItemModel::dataChanged, this, &PanelPaletteFilterProxyModel::onSourceDataChanged);
    }

    sort(0);
}

PaletteTreeModel* PanelPaletteFilterProxyModel::treeModel() const
{
    return qobject_cast<PaletteTreeModel*>(sourceModel());
}

void PanelPaletteFilterProxyModel::onSourceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight,
                                                       const QList<int>& roles)
{
    Q_UNUSED(bottomRight);

    if (topLeft.parent().isValid()) {
        return;
    }

    if (roles.contains(PaletteTreeModel::PanelsRole)) {
        // membership or order in this panel may have changed
        invalidate();
        return;
    }

    if (roles.contains(PaletteTreeModel::PanelExpandedRole)) {
        const QModelIndex proxyIndex = mapFromSource(topLeft);
        if (proxyIndex.isValid()) {
            emit dataChanged(proxyIndex, proxyIndex, { PaletteTreeModel::PaletteExpandedRole });
        }
    }
}

bool PanelPaletteFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    if (!QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent)) {
        return false;
    }

    if (sourceParent.isValid()) {
        return true; // palette cell: visibility is enough
    }

    const PaletteTreeModel* tree = treeModel();
    const QModelIndex paletteIndex = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!tree || !tree->isPaletteInPanel(paletteIndex, m_panel)) {
        return false;
    }
    return m_column < 0 || tree->palettePanelColumn(paletteIndex, m_panel) == m_column;
}

bool PanelPaletteFilterProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const PaletteTreeModel* tree = treeModel();
    if (!tree || left.parent().isValid() || right.parent().isValid()) {
        return left.row() < right.row(); // keep cells in their original order
    }

    const int leftColumn = tree->palettePanelColumn(left, m_panel);
    const int rightColumn = tree->palettePanelColumn(right, m_panel);
    if (leftColumn != rightColumn) {
        return leftColumn < rightColumn;
    }

    const int leftOrder = tree->palettePanelOrder(left, m_panel);
    const int rightOrder = tree->palettePanelOrder(right, m_panel);
    if (leftOrder != rightOrder) {
        return leftOrder < rightOrder;
    }
    return left.row() < right.row();
}

QVariant PanelPaletteFilterProxyModel::data(const QModelIndex& index, int role) const
{
    if (role == PaletteTreeModel::PaletteExpandedRole && index.isValid() && !index.parent().isValid()) {
        const PaletteTreeModel* tree = treeModel();
        return tree ? tree->palettePanelExpanded(mapToSource(index), m_panel) : false;
    }
    return QSortFilterProxyModel::data(index, role);
}

bool PanelPaletteFilterProxyModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    PaletteTreeModel* tree = treeModel();
    if (!tree || !index.isValid() || index.parent().isValid()) {
        return QSortFilterProxyModel::setData(index, value, role);
    }

    const QModelIndex srcIndex = mapToSource(index);

    if (role == PaletteTreeModel::PaletteExpandedRole) {
        tree->setPalettePanelExpanded(srcIndex, m_panel, value.toBool());
        return true;
    }

    if (role == PaletteTreeModel::VisibleRole) {
        // hiding a palette only removes it from this panel
        if (!value.toBool()) {
            tree->removePaletteFromPanel(srcIndex, m_panel);
        }
        return true;
    }

    return QSortFilterProxyModel::setData(index, value, role);
}

std::vector<int> PanelPaletteFilterProxyModel::sourceRowsInOrder() const
{
    std::vector<int> rows;
    for (int i = 0; i < rowCount(); ++i) {
        rows.push_back(mapToSource(this->index(i, 0)).row());
    }
    return rows;
}

bool PanelPaletteFilterProxyModel::movePalette(int srcRow, int destRow)
{
    PaletteTreeModel* tree = treeModel();
    std::vector<int> rows = sourceRowsInOrder();
    if (!tree || srcRow < 0 || srcRow >= int(rows.size())) {
        return false;
    }

    destRow = std::clamp(destRow, 0, int(rows.size()) - 1);
    if (srcRow == destRow) {
        return false;
    }

    const int moved = rows[srcRow];
    rows.erase(rows.begin() + srcRow);
    rows.insert(rows.begin() + destRow, moved);
    tree->setPanelOrder(m_panel, rows);
    return true;
}

void PanelPaletteFilterProxyModel::insertPalette(int sourcePaletteRow, int destRow, bool expanded)
{
    PaletteTreeModel* tree = treeModel();
    if (!tree) {
        return;
    }

    std::vector<int> rows = sourceRowsInOrder();
    rows.erase(std::remove(rows.begin(), rows.end(), sourcePaletteRow), rows.end());
    if (destRow < 0 || destRow > int(rows.size())) {
        destRow = int(rows.size());
    }
    rows.insert(rows.begin() + destRow, sourcePaletteRow);

    tree->addPaletteToPanel(tree->index(sourcePaletteRow, 0), m_panel, expanded, std::max(m_column, 0));
    tree->setPanelOrder(m_panel, rows);
}
