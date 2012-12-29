/*
 * tilesetview.cpp
 * Copyright 2008-2010, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "tilesetview.h"

#include "map.h"
#include "mapdocument.h"
#include "preferences.h"
#include "propertiesdialog.h"
#include "tmxmapwriter.h"
#include "tile.h"
#include "tileset.h"
#include "tilesetmodel.h"
#ifdef ZOMBOID
#include "mapcomposite.h"
#include "tilelayer.h"
#include "tilesetmanager.h"
#endif
#include "utils.h"
#include "zoomable.h"

#include <QAbstractItemDelegate>
#include <QCoreApplication>
#include <QFileDialog>
#include <QHeaderView>
#include <QMenu>
#include <QPainter>
#include <QUndoCommand>
#include <QWheelEvent>

using namespace Tiled;
using namespace Tiled::Internal;

namespace {

/**
 * The delegate for drawing tile items in the tileset view.
 */
class TileDelegate : public QAbstractItemDelegate
{
public:
    TileDelegate(TilesetView *tilesetView, QObject *parent = 0)
        : QAbstractItemDelegate(parent)
        , mTilesetView(tilesetView)
    { }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const;

private:
    TilesetView *mTilesetView;
};

void TileDelegate::paint(QPainter *painter,
                         const QStyleOptionViewItem &option,
                         const QModelIndex &index) const
{
    // Draw the tile image
    const QVariant display = index.model()->data(index, Qt::DisplayRole);
    const QPixmap tileImage = display.value<QPixmap>();
    const int extra = mTilesetView->drawGrid() ? 1 : 0;

    if (mTilesetView->zoomable()->smoothTransform())
        painter->setRenderHint(QPainter::SmoothPixmapTransform);

#ifdef ZOMBOID
    const QFontMetrics fm = painter->fontMetrics();
    const int labelHeight = mTilesetView->showLayerNames() ? fm.lineSpacing() : 0;
    painter->drawPixmap(option.rect.adjusted(0, 0, -extra, -extra - labelHeight), tileImage);

    if (mTilesetView->showLayerNames()) {
        const QVariant decoration = index.model()->data(index, Qt::DecorationRole);
        QString layerName = decoration.toString();
        if (layerName.isEmpty())
            layerName = QLatin1String("???");

        QString name = fm.elidedText(layerName, Qt::ElideRight, option.rect.width());
        painter->drawText(option.rect.left(), option.rect.bottom() - labelHeight, option.rect.width(), labelHeight, Qt::AlignHCenter, name);
    }
#else
    painter->drawPixmap(option.rect.adjusted(0, 0, -extra, -extra), tileImage);
#endif

    // Overlay with highlight color when selected
    if (option.state & QStyle::State_Selected) {
        const qreal opacity = painter->opacity();
        painter->setOpacity(0.5);
        painter->fillRect(option.rect.adjusted(0, 0, -extra, -extra),
                          option.palette.highlight());
        painter->setOpacity(opacity);
    }
}

QSize TileDelegate::sizeHint(const QStyleOptionViewItem & option,
                             const QModelIndex &index) const
{
    const TilesetModel *m = static_cast<const TilesetModel*>(index.model());
    const Tileset *tileset = m->tileset();
    const qreal zoom = mTilesetView->zoomable()->scale();
    const int extra = mTilesetView->drawGrid() ? 1 : 0;
#ifdef ZOMBOID
    const QFontMetrics &fm = option.fontMetrics;
    const int labelHeight = mTilesetView->showLayerNames() ? fm.lineSpacing() : 0;
    return QSize(tileset->tileWidth() * zoom + extra,
                 tileset->tileHeight() * zoom + extra + labelHeight);
#else
    return QSize(tileset->tileWidth() * zoom + extra,
                 tileset->tileHeight() * zoom + extra);
#endif
}



} // anonymous namespace

#ifdef ZOMBOID
TilesetView::TilesetView(Zoomable *zoomable, QWidget *parent)
    : QTableView(parent)
    , mZoomable(zoomable)
    , mMapDocument(0)
#else
TilesetView::TilesetView(MapDocument *mapDocument, Zoomable *zoomable, QWidget *parent)
    : QTableView(parent)
    , mZoomable(zoomable)
    , mMapDocument(mapDocument)
#endif
{
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setItemDelegate(new TileDelegate(this, this));
    setShowGrid(false);

    QHeaderView *header = horizontalHeader();
    header->hide();
    header->setResizeMode(QHeaderView::ResizeToContents);
    header->setMinimumSectionSize(1);

    header = verticalHeader();
    header->hide();
    header->setResizeMode(QHeaderView::ResizeToContents);
    header->setMinimumSectionSize(1);

    // Hardcode this view on 'left to right' since it doesn't work properly
    // for 'right to left' languages.
    setLayoutDirection(Qt::LeftToRight);

    Preferences *prefs = Preferences::instance();
    mDrawGrid = prefs->showTilesetGrid();

    connect(mZoomable, SIGNAL(scaleChanged(qreal)), SLOT(adjustScale()));
    connect(prefs, SIGNAL(showTilesetGridChanged(bool)),
            SLOT(setDrawGrid(bool)));
#ifdef ZOMBOID
    mShowLayerNames = prefs->autoSwitchLayer();
    connect(prefs, SIGNAL(autoSwitchLayerChanged(bool)),
            SLOT(autoSwitchLayerChanged(bool)));
#endif
}

#ifdef ZOMBOID
void TilesetView::setMapDocument(MapDocument *mapDocument)
{
    if (mMapDocument)
        mMapDocument->disconnect(this);

    mMapDocument = mapDocument;
    tilesetModel()->setMapDocument(mapDocument);

    if (mMapDocument) {
    }
}
#endif

QSize TilesetView::sizeHint() const
{
    return QSize(130, 100);
}

/**
 * Override to support zooming in and out using the mouse wheel.
 */
void TilesetView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier
        && event->orientation() == Qt::Vertical)
    {
        mZoomable->handleWheelDelta(event->delta());
        return;
    }

    QTableView::wheelEvent(event);
}

#ifdef ZOMBOID
class ChangeTileLayerName : public QUndoCommand
{
public:
    ChangeTileLayerName(MapDocument *mapDocument,
                  Tile *tile,
                  const QString &oldName,
                  const QString &newName)
        : QUndoCommand(QCoreApplication::translate("Undo Commands",
                                                   "Change Tile Layer Name"))
        , mMapDocument(mapDocument)
        , mTile(tile)
        , mOldName(oldName)
        , mNewName(newName)
    {
        redo();
    }

    void undo()
    {
        mMapDocument->setTileLayerName(mTile, mOldName);
    }

    void redo()
    {
        mMapDocument->setTileLayerName(mTile, mNewName);
    }

private:
    MapDocument *mMapDocument;
    Tile *mTile;
    QString mOldName;
    QString mNewName;
};
#endif // ZOMBOID

/**
 * Allow changing tile properties through a context menu.
 */
void TilesetView::contextMenuEvent(QContextMenuEvent *event)
{
    const QModelIndex index = indexAt(event->pos());
    const TilesetModel *m = tilesetModel();
    Tile *tile = m->tileAt(index);

    const bool isExternal = m->tileset()->isExternal();
    QMenu menu;

    QIcon propIcon(QLatin1String(":images/16x16/document-properties.png"));

#ifdef ZOMBOID
    QAction *actionProperties = 0;
    if (tile) {
        actionProperties = menu.addAction(propIcon,
                                          tr("Tile &Properties..."));
        actionProperties->setEnabled(!isExternal);
        Utils::setThemeIcon(actionProperties, "document-properties");
        menu.addSeparator();
    }
#else
    if (tile) {
        // Select this tile to make sure it is clear that only the properties
        // of a single tile are being edited.
        selectionModel()->setCurrentIndex(index,
                                          QItemSelectionModel::SelectCurrent |
                                          QItemSelectionModel::Clear);

        QAction *tileProperties = menu.addAction(propIcon,
                                                 tr("Tile &Properties..."));
        tileProperties->setEnabled(!isExternal);
        Utils::setThemeIcon(tileProperties, "document-properties");
        menu.addSeparator();

        connect(tileProperties, SIGNAL(triggered()),
                SLOT(editTileProperties()));
    }
#endif

#ifdef ZOMBOID
    QVector<QAction*> layerActions;
    QStringList layerNames;
    if (tile) {
        menu.addSeparator();

        // Get a list of layer names from the current map
        QSet<QString> set;
        foreach (TileLayer *tl, mMapDocument->map()->tileLayers()) {
            if (tl->group()) {
                QString name = MapComposite::layerNameWithoutPrefix(tl);
                set.insert(name);
            }
        }

        // Get a list of layer names for the current tileset
        for (int i = 0; i < m->tileset()->tileCount(); i++) {
            Tile *tile = m->tileset()->tileAt(i);
            QString layerName = TilesetManager::instance()->layerName(tile);
            if (!layerName.isEmpty())
                set.insert(layerName);
        }
        layerNames = QStringList::fromSet(set);
        layerNames.sort();

        QMenu *layersMenu = menu.addMenu(QLatin1String("Default Layer"));
        foreach (QString layerName, layerNames) {
            QAction *action = layersMenu->addAction(layerName);
            layerActions.append(action);
        }
    }
#endif

    menu.addSeparator();
    QAction *toggleGrid = menu.addAction(tr("Show &Grid"));
    toggleGrid->setCheckable(true);
    toggleGrid->setChecked(mDrawGrid);

    Preferences *prefs = Preferences::instance();
    connect(toggleGrid, SIGNAL(toggled(bool)),
            prefs, SLOT(setShowTilesetGrid(bool)));

#ifdef ZOMBOID
    QAction *action = menu.exec(event->globalPos());

    if (action && action == actionProperties) {
       // Select this tile to make sure it is clear that only the properties
        // of a single tile are being edited.
        selectionModel()->setCurrentIndex(index,
                                          QItemSelectionModel::SelectCurrent |
                                          QItemSelectionModel::Clear);
        editTileProperties();
    }

    else if (action && layerActions.contains(action)) {
        int index = layerActions.indexOf(action);
        QString layerName = layerNames[index];
        QModelIndexList indexes = selectionModel()->selectedIndexes();
        QUndoStack *undoStack = mMapDocument->undoStack();
        undoStack->beginMacro(tr("Change Tile Layer Name (x%n)", "", indexes.size()));
        foreach (QModelIndex index, indexes) {
            tile = m->tileAt(index);
            QString oldName = TilesetManager::instance()->layerName(tile);
            ChangeTileLayerName *undo = new ChangeTileLayerName(mMapDocument, tile, oldName, layerName);
            mMapDocument->undoStack()->push(undo);
        }
        undoStack->endMacro();
    }
#else
    menu.exec(event->globalPos());
#endif
}

void TilesetView::editTileProperties()
{
    const TilesetModel *m = tilesetModel();
    Tile *tile = m->tileAt(selectionModel()->currentIndex());
    if (!tile)
        return;

    PropertiesDialog propertiesDialog(tr("Tile"),
                                      tile,
                                      mMapDocument->undoStack(),
                                      this);
    propertiesDialog.exec();
}

void TilesetView::setDrawGrid(bool drawGrid)
{
    mDrawGrid = drawGrid;
    tilesetModel()->tilesetChanged();
}

void TilesetView::adjustScale()
{
    tilesetModel()->tilesetChanged();
}

#ifdef ZOMBOID
void TilesetView::autoSwitchLayerChanged(bool enabled)
{
    mShowLayerNames = enabled;
    tilesetModel()->tilesetChanged();
}
#endif
