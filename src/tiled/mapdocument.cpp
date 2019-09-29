/*
 * mapdocument.cpp
 * Copyright 2008-2010, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 * Copyright 2009, Jeff Bland <jeff@teamphobic.com>
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

#include "mapdocument.h"

#include "addremovelayer.h"
#include "addremovemapobject.h"
#include "addremovetileset.h"
#include "changeproperties.h"
#include "changetileselection.h"
#include "imagelayer.h"
#include "isometricrenderer.h"
#include "layermodel.h"
#include "mapobjectmodel.h"
#ifdef ZOMBOID
#include "bmpblender.h"
#include "bmptool.h"
#include "mapcomposite.h"
#include "mapmanager.h"
#include "preferences.h"
#include "zlevelrenderer.h"
#include "zlevelsmodel.h"
#include "worlded/world.h"
#include "worlded/worldcell.h"
#include "worlded/worldedmgr.h"
#endif
#include "map.h"
#include "mapobject.h"
#include "movelayer.h"
#include "objectgroup.h"
#include "offsetlayer.h"
#include "orthogonalrenderer.h"
#include "painttilelayer.h"
#include "resizelayer.h"
#include "resizemap.h"
#include "staggeredrenderer.h"
#include "tile.h"
#include "tilelayer.h"
#include "tilesetmanager.h"
#include "tileset.h"
#include "tmxmapwriter.h"

#include <QFileInfo>
#include <QRect>
#include <QUndoStack>
#ifdef ZOMBOID
#include <QDir>
#endif

using namespace Tiled;
using namespace Tiled::Internal;

MapDocument::MapDocument(Map *map, const QString &fileName):
    mFileName(fileName),
    mMap(map),
    mLayerModel(new LayerModel(this)),
    mMapObjectModel(new MapObjectModel(this)),
#ifdef ZOMBOID
    mLevelsModel(new ZLevelsModel(this)),
    mMapComposite(nullptr),
    mWorldCell(nullptr),
#endif
    mUndoStack(new QUndoStack(this))
{
#ifdef ZOMBOID
    mMapComposite = new MapComposite(MapManager::instance()->newFromMap(map, fileName));
    connect(mMapComposite->bmpBlender(), SIGNAL(regionAltered(QRegion)),
            SLOT(bmpBlenderRegionAltered(QRegion)));
    connect(this, SIGNAL(layerAdded(int)),
             mMapComposite->bmpBlender(), SLOT(updateWarnings()));
    connect(this, SIGNAL(layerRenamed(int)),
             mMapComposite->bmpBlender(), SLOT(updateWarnings()));
    connect(this, SIGNAL(layerRemoved(int)),
             mMapComposite->bmpBlender(), SLOT(updateWarnings()));
    connect(MapManager::instance(), SIGNAL(mapAboutToChange(MapInfo*)),
            SLOT(onMapAboutToChange(MapInfo*)));
    connect(MapManager::instance(), SIGNAL(mapChanged(MapInfo*)),
            SLOT(onMapChanged(MapInfo*)));

    if (!mFileName.isEmpty() && Preferences::instance()->showAdjacentMaps()) {
        connect(MapManager::instance(), SIGNAL(mapLoaded(MapInfo*)),
                SLOT(mapLoaded(MapInfo*)));
        connect(MapManager::instance(), SIGNAL(mapFailedToLoad(MapInfo*)),
                SLOT(mapFailedToLoad(MapInfo*)));
        connect(WorldEd::WorldEdMgr::instance(), SIGNAL(afterWorldChanged(QString)),
                SLOT(initAdjacentMaps()));
        initAdjacentMaps();
    }
#endif
    switch (map->orientation()) {
    case Map::Isometric:
        mRenderer = new IsometricRenderer(map);
        break;
    case Map::Staggered:
        mRenderer = new StaggeredRenderer(map);
        break;
#ifdef ZOMBOID
    case Map::LevelIsometric:
        mRenderer = new ZLevelRenderer(map);
        break;
#endif
    default:
        mRenderer = new OrthogonalRenderer(map);
        break;
    }

#ifdef ZOMBOID
    mRenderer->setMaxLevel(mMapComposite->maxLevel());
#endif

    mCurrentLayerIndex = (map->layerCount() == 0) ? -1 : 0;
    mLayerModel->setMapDocument(this);

    // Forward signals emitted from the layer model
    connect(mLayerModel, SIGNAL(layerAdded(int)), SLOT(onLayerAdded(int)));
    connect(mLayerModel, SIGNAL(layerAboutToBeRemoved(int)),
            SLOT(onLayerAboutToBeRemoved(int)));
    connect(mLayerModel, SIGNAL(layerRemoved(int)), SLOT(onLayerRemoved(int)));
    connect(mLayerModel, SIGNAL(layerChanged(int)), SIGNAL(layerChanged(int)));
#ifdef ZOMBOID
    connect(mLayerModel, SIGNAL(layerRenamed(int)), SLOT(onLayerRenamed(int)));
    mMaxVisibleLayer = map->layerCount();

    connect(mMapComposite, SIGNAL(layerGroupAdded(int)),
            SIGNAL(layerGroupAdded(int)));
    connect(mMapComposite, SIGNAL(layerAddedToGroup(int)),
            SIGNAL(layerAddedToGroup(int)));
    connect(mMapComposite, SIGNAL(layerAboutToBeRemovedFromGroup(int)),
            SIGNAL(layerAboutToBeRemovedFromGroup(int)));
    connect(mMapComposite, SIGNAL(layerRemovedFromGroup(int,CompositeLayerGroup*)),
            SIGNAL(layerRemovedFromGroup(int,CompositeLayerGroup*)));
    connect(mMapComposite, SIGNAL(layerLevelChanged(int,int)),
            SIGNAL(layerLevelChanged(int,int)));
#endif

#ifdef ZOMBOID
    mLevelsModel->setMapDocument(this);
#endif

    // Forward signals emitted from the map object model
    mMapObjectModel->setMapDocument(this);
    connect(mMapObjectModel, SIGNAL(objectsAdded(QList<MapObject*>)),
            SIGNAL(objectsAdded(QList<MapObject*>)));
    connect(mMapObjectModel, SIGNAL(objectsChanged(QList<MapObject*>)),
            SIGNAL(objectsChanged(QList<MapObject*>)));
    connect(mMapObjectModel, SIGNAL(objectsAboutToBeRemoved(QList<MapObject*>)),
            SIGNAL(objectsAboutToBeRemoved(QList<MapObject*>)));
    connect(mMapObjectModel, SIGNAL(objectsRemoved(QList<MapObject*>)),
            SLOT(onObjectsRemoved(QList<MapObject*>)));

    connect(mUndoStack, SIGNAL(cleanChanged(bool)), SIGNAL(modifiedChanged()));

    // Register tileset references
    TilesetManager *tilesetManager = TilesetManager::instance();
    tilesetManager->addReferences(mMap->tilesets());

#ifdef ZOMBOID
    connect(tilesetManager, SIGNAL(tileLayerNameChanged(Tile*)),
            SIGNAL(tileLayerNameChanged(Tile*)));

    mMapComposite->setShowLotFloorsOnly(Preferences::instance()->showLotFloorsOnly());
#endif
}

MapDocument::~MapDocument()
{
    // Unregister tileset references
    TilesetManager *tilesetManager = TilesetManager::instance();
    tilesetManager->removeReferences(mMap->tilesets());

#ifdef ZOMBOID
    // Paranoia
    mLevelsModel->setMapDocument(0);
    mMapObjectModel->setMapDocument(0);
    delete mMapComposite;
#endif

    delete mRenderer;
    delete mMap;
}

bool MapDocument::save(QString *error)
{
    return save(fileName(), error);
}

bool MapDocument::save(const QString &fileName, QString *error)
{
    TmxMapWriter mapWriter;

    if (!mapWriter.write(map(), fileName)) {
        if (error)
            *error = mapWriter.errorString();
        return false;
    }

    undoStack()->setClean();
    setFileName(fileName);

    return true;
}

void MapDocument::setFileName(const QString &fileName)
{
    if (mFileName == fileName)
        return;

    mFileName = fileName;
    emit fileNameChanged();
}

/**
 * Returns the name with which to display this map. It is the file name without
 * its path, or 'untitled.tmx' when the map has no file name.
 */
QString MapDocument::displayName() const
{
    QString displayName = QFileInfo(mFileName).fileName();
    if (displayName.isEmpty())
        displayName = tr("untitled.tmx");

    return displayName;
}

/**
 * Returns whether the map has unsaved changes.
 */
bool MapDocument::isModified() const
{
    return !mUndoStack->isClean();
}

void MapDocument::setCurrentLayerIndex(int index)
{
    Q_ASSERT(index >= -1 && index < mMap->layerCount());
    mCurrentLayerIndex = index;

    /* This function always sends the following signal, even if the index
     * didn't actually change. This is because the selected index in the layer
     * table view might be out of date anyway, and would otherwise not be
     * properly updated.
     *
     * This problem happens due to the selection model not sending signals
     * about changes to its current index when it is due to insertion/removal
     * of other items. The selected item doesn't change in that case, but our
     * layer index does.
     */
    emit currentLayerIndexChanged(mCurrentLayerIndex);
}

Layer *MapDocument::currentLayer() const
{
    if (mCurrentLayerIndex == -1)
        return 0;

    return mMap->layerAt(mCurrentLayerIndex);
}

void MapDocument::resizeMap(const QSize &size, const QPoint &offset)
{
    const QRegion movedSelection = mTileSelection.translated(offset);
    const QRectF newArea = QRectF(-offset, size);

    // Resize the map and each layer
    mUndoStack->beginMacro(tr("Resize Map"));
#ifdef ZOMBOID
    mUndoStack->push(new ResizeMap(this, size, true));
#endif
    for (int i = 0; i < mMap->layerCount(); ++i) {
        if (ObjectGroup *objectGroup = mMap->layerAt(i)->asObjectGroup()) {
            // Remove objects that will fall outside of the map
            foreach (MapObject *o, objectGroup->objects()) {
                if (!(newArea.contains(o->position())
                      || newArea.intersects(o->bounds()))) {
                    mUndoStack->push(new RemoveMapObject(this, o));
                }
            }
        }

        mUndoStack->push(new ResizeLayer(this, i, size, offset));
    }
#ifdef ZOMBOID
    mUndoStack->push(new ResizeBmpImage(this, 0, size, offset));
    mUndoStack->push(new ResizeBmpImage(this, 1, size, offset));
    mUndoStack->push(new ResizeBmpRands(this, 0, size));
    mUndoStack->push(new ResizeBmpRands(this, 1, size));
    foreach (MapNoBlend *noBlend, mMap->noBlends())
        mUndoStack->push(new ResizeNoBlend(this, noBlend, size, offset));
    mUndoStack->push(new ResizeMap(this, size, false));
#else
    mUndoStack->push(new ResizeMap(this, size));
#endif
    mUndoStack->push(new ChangeTileSelection(this, movedSelection));
#ifdef ZOMBOID
#ifdef SEPARATE_BMP_SELECTION
    QRegion bmpSelection = mBmpSelection.translated(offset);
    mUndoStack->push(new ChangeBmpSelection(this, bmpSelection));
#endif
#endif
    mUndoStack->endMacro();

    // TODO: Handle layers that don't match the map size correctly
}

void MapDocument::offsetMap(const QList<int> &layerIndexes,
                            const QPoint &offset,
                            const QRect &bounds,
                            bool wrapX, bool wrapY)
{
    if (layerIndexes.empty())
        return;

    if (layerIndexes.size() == 1) {
        mUndoStack->push(new OffsetLayer(this, layerIndexes.first(), offset,
                                         bounds, wrapX, wrapY));
    } else {
        mUndoStack->beginMacro(tr("Offset Map"));
        foreach (const int layerIndex, layerIndexes) {
            mUndoStack->push(new OffsetLayer(this, layerIndex, offset,
                                             bounds, wrapX, wrapY));
        }

#ifdef ZOMBOID
        // Offset the BMP images and MapNoBlends only if every rule+blend layer
        // is being offset.
        // Don't offset the MapRands.
        bool allBMPLayers = true;
        foreach (QString layerName, mapComposite()->bmpBlender()->tileLayerNames()) {
            int layerIndex = map()->indexOfLayer(layerName, Layer::TileLayerType);
            if (layerIndex >= 0 && !layerIndexes.contains(layerIndex)) {
                allBMPLayers = false;
                break;
            }
        }
        if (allBMPLayers) {
            mUndoStack->push(new OffsetBmpImage(this, 0, offset, bounds, wrapX, wrapY));
            mUndoStack->push(new OffsetBmpImage(this, 1, offset, bounds, wrapX, wrapY));
            foreach (MapNoBlend *noBlend, map()->noBlends())
                mUndoStack->push(new OffsetNoBlend(this, noBlend, offset, bounds, wrapX, wrapY));
        }
#endif

        mUndoStack->endMacro();
    }
}

/**
 * Adds a layer of the given type to the top of the layer stack. After adding
 * the new layer, emits editLayerNameRequested().
 */
void MapDocument::addLayer(Layer::Type layerType)
{
    Layer *layer = 0;
    QString name;

#if 1
    // Create the new layer in the same level as the current layer.
    // Stack it with other layers of the same type in level-order.
    int level = currentLevel();
    int index = mMap->layerCount();
    Layer *topLayerOfSameTypeInSameLevel = 0;
    Layer *bottomLayerOfSameTypeInGreaterLevel = 0;
    Layer *topLayerOfSameTypeInLesserLevel = 0;
    foreach (Layer *layer, mMap->layers(layerType)) {
        if ((layer->level() > level) && !bottomLayerOfSameTypeInGreaterLevel)
            bottomLayerOfSameTypeInGreaterLevel = layer;
        if (layer->level() < level)
            topLayerOfSameTypeInLesserLevel = layer;
        if (layer->level() == level)
            topLayerOfSameTypeInSameLevel = layer;
    }
    if (topLayerOfSameTypeInSameLevel)
        index = mMap->layers().indexOf(topLayerOfSameTypeInSameLevel) + 1;
    else if (bottomLayerOfSameTypeInGreaterLevel)
        index = mMap->layers().indexOf(bottomLayerOfSameTypeInGreaterLevel);
    else if (topLayerOfSameTypeInLesserLevel)
        index = mMap->layers().indexOf(topLayerOfSameTypeInLesserLevel) + 1;

    switch (layerType) {
    case Layer::TileLayerType:
        name = tr("%1_Tile Layer %2").arg(level).arg(mMap->tileLayerCount() + 1);
        layer = new TileLayer(name, 0, 0, mMap->width(), mMap->height());
        break;
    case Layer::ObjectGroupType:
        name = tr("%1_Object Layer %2").arg(level).arg(mMap->objectGroupCount() + 1);
        layer = new ObjectGroup(name, 0, 0, mMap->width(), mMap->height());
        break;
    case Layer::ImageLayerType:
        name = tr("%1_Image Layer %2").arg(level).arg(mMap->imageLayerCount() + 1);
        layer = new ImageLayer(name, 0, 0, mMap->width(), mMap->height());
        break;
    case Layer::AnyLayerType:
        break; // Q_ASSERT below will fail.
    }
    Q_ASSERT(layer);
#else
    switch (layerType) {
    case Layer::TileLayerType:
        name = tr("Tile Layer %1").arg(mMap->tileLayerCount() + 1);
        layer = new TileLayer(name, 0, 0, mMap->width(), mMap->height());
        break;
    case Layer::ObjectGroupType:
        name = tr("Object Layer %1").arg(mMap->objectGroupCount() + 1);
        layer = new ObjectGroup(name, 0, 0, mMap->width(), mMap->height());
        break;
    case Layer::ImageLayerType:
        name = tr("Image Layer %1").arg(mMap->imageLayerCount() + 1);
        layer = new ImageLayer(name, 0, 0, mMap->width(), mMap->height());
        break;
    case Layer::AnyLayerType:
        break; // Q_ASSERT below will fail.
    }
    Q_ASSERT(layer);

    const int index = mMap->layerCount();
#endif
    mUndoStack->push(new AddLayer(this, index, layer));
    setCurrentLayerIndex(index);

    emit editLayerNameRequested();
}

/**
 * Duplicates the currently selected layer.
 */
void MapDocument::duplicateLayer()
{
    if (mCurrentLayerIndex == -1)
        return;

    Layer *duplicate = mMap->layerAt(mCurrentLayerIndex)->clone();
#ifdef ZOMBOID
    // Duplicate the layer into the same level by preserving the N_ prefix.
    duplicate->setName(tr("%1 copy").arg(duplicate->name()));
#else
    duplicate->setName(tr("Copy of %1").arg(duplicate->name()));
#endif

    const int index = mCurrentLayerIndex + 1;
    QUndoCommand *cmd = new AddLayer(this, index, duplicate);
    cmd->setText(tr("Duplicate Layer"));
    mUndoStack->push(cmd);
    setCurrentLayerIndex(index);
}

/**
 * Merges the currently selected layer with the layer below. This only works
 * when the layers can be merged.
 *
 * \see Layer::canMergeWith
 */
void MapDocument::mergeLayerDown()
{
    if (mCurrentLayerIndex < 1)
        return;

    Layer *upperLayer = mMap->layerAt(mCurrentLayerIndex);
    Layer *lowerLayer = mMap->layerAt(mCurrentLayerIndex - 1);

    if (!lowerLayer->canMergeWith(upperLayer))
        return;

    Layer *merged = lowerLayer->mergedWith(upperLayer);

    mUndoStack->beginMacro(tr("Merge Layer Down"));
    mUndoStack->push(new AddLayer(this, mCurrentLayerIndex - 1, merged));
    mUndoStack->push(new RemoveLayer(this, mCurrentLayerIndex));
    mUndoStack->push(new RemoveLayer(this, mCurrentLayerIndex));
    mUndoStack->endMacro();
}

/**
 * Moves the given layer up. Does nothing when no valid layer index is
 * given.
 */
void MapDocument::moveLayerUp(int index)
{
    if (index < 0 || index >= mMap->layerCount() - 1)
        return;

    mUndoStack->push(new MoveLayer(this, index, MoveLayer::Up));
}

/**
 * Moves the given layer down. Does nothing when no valid layer index is
 * given.
 */
void MapDocument::moveLayerDown(int index)
{
    if (index < 1 || index >= mMap->layerCount())
        return;

    mUndoStack->push(new MoveLayer(this, index, MoveLayer::Down));
}

/**
 * Removes the given layer.
 */
void MapDocument::removeLayer(int index)
{
    if (index < 0 || index >= mMap->layerCount())
        return;

    mUndoStack->push(new RemoveLayer(this, index));
}

/**
  * Show or hide all other layers except the layer at the given index.
  * If any other layer is visible then all layers will be hidden, otherwise
  * the layers will be shown.
  */
void MapDocument::toggleOtherLayers(int index)
{
    mLayerModel->toggleOtherLayers(index);
}

#ifdef ZOMBOID
void MapDocument::setLayerVisible(int layerIndex, bool visible)
{
    int row = mMap->layerCount() - layerIndex - 1;
    mLayerModel->setData(mLayerModel->index(row),
                         visible ? Qt::Checked : Qt::Unchecked,
                         Qt::CheckStateRole);
}
#endif // ZOMBOID

/**
 * Adds a tileset to this map at the given \a index. Emits the appropriate
 * signal.
 */
void MapDocument::insertTileset(int index, Tileset *tileset)
{
    mMap->insertTileset(index, tileset);
    TilesetManager *tilesetManager = TilesetManager::instance();
    tilesetManager->addReference(tileset);
#ifdef ZOMBOID
    mMapComposite->bmpBlender()->tilesetAdded(tileset);
#endif
    emit tilesetAdded(index, tileset);
}

/**
 * Removes the tileset at the given \a index from this map. Emits the
 * appropriate signal.
 *
 * \warning Does not make sure that any references to tiles in the removed
 *          tileset are cleared.
 */
void MapDocument::removeTilesetAt(int index)
{
    Tileset *tileset = mMap->tilesets().at(index);
    mMap->removeTilesetAt(index);
#ifdef ZOMBOID
    mMapComposite->bmpBlender()->tilesetRemoved(tileset->name());
#endif
    emit tilesetRemoved(tileset);
    TilesetManager *tilesetManager = TilesetManager::instance();
    tilesetManager->removeReference(tileset);
}

void MapDocument::moveTileset(int from, int to)
{
    if (from == to)
        return;

    Tileset *tileset = mMap->tilesets().at(from);
    mMap->removeTilesetAt(from);
    mMap->insertTileset(to, tileset);
    emit tilesetMoved(from, to);
}

void MapDocument::setTileSelection(const QRegion &selection)
{
    if (mTileSelection != selection) {
        const QRegion oldTileSelection = mTileSelection;
        mTileSelection = selection;
        emit tileSelectionChanged(mTileSelection, oldTileSelection);
    }
}

#ifdef ZOMBOID
const QRegion &MapDocument::bmpSelection() const
{
#ifdef SEPARATE_BMP_SELECTION
    return mBmpSelection;
#else
    return tileSelection();
#endif
}

void MapDocument::setBmpSelection(const QRegion &selection)
{
#ifdef SEPARATE_BMP_SELECTION
    if (mBmpSelection != selection) {
        const QRegion oldSelection = mBmpSelection;
        mBmpSelection = selection;
        emit bmpSelectionChanged(mBmpSelection, oldSelection);
    }
#else
    setTileSelection(selection);
#endif
}

void MapDocument::paintBmp(int bmpIndex, int px, int py, const QImage &source,
                           const QRegion &paintRgn)
{
    MapBmp &bmp = mMap->rbmp(bmpIndex);
    QRegion region = paintRgn & QRect(0, 0, bmp.width(), bmp.height());

    foreach (QRect r, region.rects()) {
        for (int y = r.top(); y <= r.bottom(); y++) {
            for (int x = r.left(); x <= r.right(); x++) {
                bmp.setPixel(x, y, source.pixel(x - px, y - py));
            }
        }
    }

    mapComposite()->bmpBlender()->markDirty(region);

    emit bmpPainted(bmpIndex, region);
}

QImage MapDocument::swapBmpImage(int bmpIndex, const QImage &image)
{
    QImage old = mMap->bmp(bmpIndex).image();
    mMap->rbmp(bmpIndex).rimage() = image;
    return old;
}

MapRands MapDocument::swapBmpRands(int bmpIndex, const MapRands &rands)
{
    MapRands old = mMap->bmp(bmpIndex).rands();
    mMap->rbmp(bmpIndex).rrands() = rands;
    return old;
}

void MapDocument::setBmpAliases(const QList<BmpAlias *> &aliases)
{
    mMap->rbmpSettings()->setAliases(aliases);

    mapComposite()->bmpBlender()->fromMap();
    mapComposite()->bmpBlender()->recreate();

    emit bmpAliasesChanged();
}

void MapDocument::setBmpRules(const QString &fileName,
                                      const QList<BmpRule *> &rules)
{
    mMap->rbmpSettings()->setRulesFile(fileName);
    mMap->rbmpSettings()->setRules(rules);

    mapComposite()->bmpBlender()->fromMap();
    mapComposite()->bmpBlender()->recreate();

    emit bmpRulesChanged();
}

void MapDocument::setBmpBlends(const QString &fileName,
                               const QList<BmpBlend *> &blends)
{
    mMap->rbmpSettings()->setBlendsFile(fileName);
    mMap->rbmpSettings()->setBlends(blends);

    mapComposite()->bmpBlender()->fromMap();
    mapComposite()->bmpBlender()->recreate();

    emit bmpBlendsChanged();
}

MapNoBlend MapDocument::paintNoBlend(MapNoBlend *noBlend, const MapNoBlend &other, const QRegion &rgn)
{
    MapNoBlend old = noBlend->copy(rgn);
    noBlend->replace(&other, rgn);
    emit noBlendPainted(noBlend, rgn);
    return old;
}

void MapDocument::swapNoBlend(MapNoBlend *noBlend, MapNoBlend *other)
{
    MapNoBlend old(noBlend->layerName(), noBlend->width(), noBlend->height());
    old.replace(noBlend);
    noBlend->replace(other);
    other->replace(&old);
    // swapNoBlend() gets called when resizing a map
//    emit noBlendPainted(noBlend, QRect(0, 0, noBlend->width(), noBlend->height()));
}
#endif // ZOMBOID

void MapDocument::setSelectedObjects(const QList<MapObject *> &selectedObjects)
{
    mSelectedObjects = selectedObjects;
    emit selectedObjectsChanged();
}

/**
 * Makes sure the all tilesets which are used at the given \a map will be
 * present in the map document.
 *
 * To reach the aim, all similar tilesets will be replaced by the version
 * in the current map document and all missing tilesets will be added to
 * the current map document.
 */
void MapDocument::unifyTilesets(Map *map)
{
    QList<QUndoCommand*> undoCommands;
    QList<Tileset*> existingTilesets = mMap->tilesets();
    TilesetManager *tilesetManager = TilesetManager::instance();

    // Add tilesets that are not yet part of this map
    foreach (Tileset *tileset, map->tilesets()) {
        if (existingTilesets.contains(tileset))
            continue;

        Tileset *replacement = tileset->findSimilarTileset(existingTilesets);
        if (!replacement) {
            undoCommands.append(new AddTileset(this, tileset));
            continue;
        }

        // Merge the tile properties
        const int sharedTileCount = qMin(tileset->tileCount(),
                                         replacement->tileCount());
        for (int i = 0; i < sharedTileCount; ++i) {
            Tile *replacementTile = replacement->tileAt(i);
            Properties properties = replacementTile->properties();
            properties.merge(tileset->tileAt(i)->properties());
            undoCommands.append(new ChangeProperties(tr("Tile"),
                                                     replacementTile,
                                                     properties));
        }
        map->replaceTileset(tileset, replacement);

        tilesetManager->addReference(replacement);
        tilesetManager->removeReference(tileset);
    }
    if (!undoCommands.isEmpty()) {
        mUndoStack->beginMacro(tr("Tileset Changes"));
        foreach (QUndoCommand *command, undoCommands)
            mUndoStack->push(command);
        mUndoStack->endMacro();
    }
}

/**
 * Emits the map changed signal. This signal should be emitted after changing
 * the map size or its tile size.
 */
void MapDocument::emitMapChanged()
{
#ifdef ZOMBOID
    MapManager::instance()->mapParametersChanged(mMapComposite->mapInfo());
#endif
    emit mapChanged();
}

#ifdef ZOMBOID
void MapDocument::emitRegionChanged(const QRegion &region, Layer *layer)
{
    emit regionChanged(region, layer);
}
#else
void MapDocument::emitRegionChanged(const QRegion &region)
{
    emit regionChanged(region);
}
#endif

void MapDocument::emitRegionEdited(const QRegion &region, Layer *layer)
{
    emit regionEdited(region, layer);
}

#ifdef ZOMBOID
void MapDocument::emitRegionAltered(const QRegion &region, Layer *layer)
{
#if 1
    if (mMapComposite->bmpBlender()->tileLayerNames().contains(layer->name())) {
        mMapComposite->bmpBlender()->markDirty(region);
    }
#endif
    emit regionAltered(region, layer);
}

void MapDocument::setTileLayerName(Tile *tile, const QString &name)
{
    TilesetManager::instance()->setLayerName(tile, name);
}
#endif // ZOMBOID

/**
 * Before forwarding the signal, the objects are removed from the list of
 * selected objects, triggering a selectedObjectsChanged signal when * appropriate.
 */
void MapDocument::onObjectsRemoved(const QList<MapObject*> &objects)
{
    deselectObjects(objects);
    emit objectsRemoved(objects);
}

void MapDocument::onLayerAdded(int index)
{
    emit layerAdded(index);
#ifdef ZOMBOID
    mMapComposite->layerAdded(index);
#endif

    // Select the first layer that gets added to the map
    if (mMap->layerCount() == 1)
        setCurrentLayerIndex(0);
}

void MapDocument::onLayerAboutToBeRemoved(int index)
{
    // Deselect any objects on this layer when necessary
    if (ObjectGroup *og = dynamic_cast<ObjectGroup*>(mMap->layerAt(index)))
        deselectObjects(og->objects());
#ifdef ZOMBOID
    mMapComposite->layerAboutToBeRemoved(index);
#endif
    emit layerAboutToBeRemoved(index);
}
void MapDocument::onLayerRemoved(int index)
{
    // Bring the current layer index to safety
    bool currentLayerRemoved = mCurrentLayerIndex == mMap->layerCount();
    if (currentLayerRemoved)
        mCurrentLayerIndex = mCurrentLayerIndex - 1;


    emit layerRemoved(index);

    // Emitted after the layerRemoved signal so that the MapScene has a chance
    // of synchronizing before adapting to the newly selected index
    if (currentLayerRemoved)
        emit currentLayerIndexChanged(mCurrentLayerIndex);
}

#ifdef ZOMBOID
void MapDocument::setLayerGroupVisibility(CompositeLayerGroup *layerGroup, bool visible)
{
    layerGroup->setVisible(visible);
    emit layerGroupVisibilityChanged(layerGroup);
}

void MapDocument::onLayerRenamed(int index)
{
    mMapComposite->layerRenamed(index);

    emit layerRenamed(index);
}

void MapDocument::onMapAboutToChange(MapInfo *mapInfo)
{
    mMapComposite->mapAboutToChange(mapInfo);
}

void MapDocument::onMapChanged(MapInfo *mapInfo)
{
    bool changed = false;

    if (mMapComposite->mapChanged(mapInfo))
        changed = true;

    // If an adjacent map was just reloaded, all the WorldEd lots in it will
    // have been deleted.
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            if (MapComposite *adjacentMap = mMapComposite->adjacentMap(x, y)) {
                if (adjacentMap->mapInfo() == mapInfo) {
                    int cx = mWorldCell->x(), cy = mWorldCell->y();
                    if (WorldCell *cell = mWorldCell->world()->cellAt(cx + x, cy + y)) {
                        foreach (WorldCellLot *lot, cell->lots()) {
                            MapInfo *subMapInfo = MapManager::instance()->loadMap(
                                        lot->mapName(), QString(), true, MapManager::PriorityLow);
                            if (subMapInfo) {
                                if (subMapInfo->isLoading())
                                    mAdjacentSubMapsLoading.insert(subMapInfo, LoadingSubMap(lot, subMapInfo));
                                else
                                    adjacentMap->addMap(subMapInfo, lot->pos(), lot->level());
                            }
                        }
                    }
                }
            }
        }
    }

    if (changed)
        emit mapCompositeChanged();
}

void MapDocument::bmpBlenderRegionAltered(const QRegion &region)
{
    foreach (QString layerName, mapComposite()->bmpBlender()->tileLayerNames()) {
        int index = map()->indexOfLayer(layerName, Layer::TileLayerType);
        if (index == -1)
            continue;
        TileLayer *tl = map()->layerAt(index)->asTileLayer();
        mapComposite()->tileLayersForLevel(0)->regionAltered(tl);
        emit regionAltered(region, tl); // infinite loop with emitRegionAltered()
        break; // this should redraw the whole layergroup anyway
    }
}

void MapDocument::mapLoaded(MapInfo *info)
{
    if (!mAdjacentMapsLoading.contains(info) &&
            !mAdjacentSubMapsLoading.contains(info)) return;

    if (mMapsLoaded.isEmpty())
        QMetaObject::invokeMethod(this, "handleMapsLoadedNow", Qt::QueuedConnection);
    mMapsLoaded += info;
}

void MapDocument::mapFailedToLoad(MapInfo *info)
{
    mAdjacentMapsLoading.remove(info);
    mAdjacentSubMapsLoading.remove(info);
}

void MapDocument::handleMapsLoadedNow()
{
    bool changed = false;
    // It could happen that the WorldEd project file was changed while
    // adjacent maps were being loaded, causes mWorldCell to be set to null.
    WorldCell *cell = mWorldCell;
    if (!cell) {
        mMapsLoaded.clear();
        mAdjacentMapsLoading.clear();
        mAdjacentSubMapsLoading.clear();
        return;
    }

    while (!mMapsLoaded.isEmpty()) {
        MapInfo *info = mMapsLoaded.takeFirst();

        foreach (const AdjacentMap &am, mAdjacentMapsLoading.values(info)) {
            mMapComposite->setAdjacentMap(am.pos.x(), am.pos.y(), am.info);

            MapComposite *adjacentMap = mMapComposite->adjacentMap(am.pos.x(),
                                                                   am.pos.y());
            WorldCell *cell2 = cell->world()->cellAt(am.pos + cell->pos());
            foreach (WorldCellLot *lot, cell2->lots()) {
                MapInfo *subMapInfo = MapManager::instance()->loadMap(
                            lot->mapName(), QString(), true, MapManager::PriorityLow);
                if (subMapInfo && !subMapInfo->isLoading() && !mAdjacentSubMapsLoading.contains(subMapInfo))
                    adjacentMap->addMap(subMapInfo, lot->pos(), lot->level());
            }

            changed = true;
        }
        mAdjacentMapsLoading.remove(info);

        foreach (const LoadingSubMap &sm, mAdjacentSubMapsLoading.values(info)) {
            int x = sm.lot->cell()->x(), y = sm.lot->cell()->y();
            if (MapComposite *adjacentMap = mMapComposite->adjacentMap(x - cell->x(),
                                                                       y - cell->y()))
                adjacentMap->addMap(info, sm.lot->pos(), sm.lot->level());
            changed = true;
        }
        mAdjacentSubMapsLoading.remove(info);
    }

    // This lets ZomboidScene update itself (syncing and repainting).
    if (changed)
        emit mapCompositeChanged(); ///////

    if (!mMapsLoaded.isEmpty())
        QMetaObject::invokeMethod(this, "handleMapsLoadedNow", Qt::QueuedConnection);
}

void MapDocument::beforeWorldChanged(const QString &fileName)
{
    Q_UNUSED(fileName);
    mWorldCell = 0;
}

void MapDocument::afterWorldChanged(const QString &fileName)
{
    Q_UNUSED(fileName);
    mWorldCell = WorldEd::WorldEdMgr::instance()->cellForMap(mFileName);
}

#endif // ZOMBOID

void MapDocument::deselectObjects(const QList<MapObject *> &objects)
{
    int removedCount = 0;
    foreach (MapObject *object, objects)
        removedCount += mSelectedObjects.removeAll(object);

    if (removedCount > 0)
        emit selectedObjectsChanged();
}

#ifdef ZOMBOID
void MapDocument::initAdjacentMaps()
{
    QVector<MapInfo*> adjacentMaps(9);

    if (WorldCell *cell = WorldEd::WorldEdMgr::instance()->cellForMap(mFileName)) {
        mWorldCell = cell;
        int cx = cell->x(), cy = cell->y();
        for (int y = -1; y <= 1; y++) {
            if (cy + y < 0 || cy + y >= cell->world()->height()) continue;
            for (int x = -1; x <= 1; x++) {
                if (cx + x < 0 || cx + x >= cell->world()->width()) continue;
                if (x == 0 && y == 0) continue;
                if (WorldCell *cell2 = cell->world()->cellAt(cx + x, cy + y)) {
                    if (cell2->mapFilePath().isEmpty()) continue;
                    QFileInfo info(cell2->mapFilePath());
                    if (info.exists()) {
                        MapInfo *mapInfo = MapManager::instance()->loadMap(
                                    info.absoluteFilePath(), QString(), true,
                                    MapManager::PriorityMedium);
                        if (mapInfo) {
                            if (mapInfo->isLoading())
                                mAdjacentMapsLoading.insert(mapInfo, AdjacentMap(x, y, mapInfo));
                            else
                                mMapComposite->setAdjacentMap(x, y, mapInfo);

                            MapComposite *adjacentMap = mMapComposite->adjacentMap(x, y);
                            foreach (WorldCellLot *lot, cell2->lots()) {
                                MapInfo *subMapInfo = MapManager::instance()->loadMap(
                                            lot->mapName(), QString(), true, MapManager::PriorityLow);
                                if (subMapInfo) {
                                    if (subMapInfo->isLoading())
                                        mAdjacentSubMapsLoading.insert(subMapInfo, LoadingSubMap(lot, subMapInfo));
                                    else if (adjacentMap)
                                        adjacentMap->addMap(subMapInfo, lot->pos(), lot->level());
                                }
                            }
                            adjacentMaps[(x + 1) + (y + 1) * 3] = mapInfo;
                        }
                    }
                }
            }
        }
    }

    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            if (MapComposite *mc = mMapComposite->adjacentMap(x, y)) {
                int index = (x + 1) + (y + 1) * 3;
                if (mc->mapInfo() != adjacentMaps[index])
                    mMapComposite->setAdjacentMap(x, y, 0);
            }
        }
    }
}
#endif // ZOMBOID

void MapDocument::setTilesetFileName(Tileset *tileset,
                                     const QString &fileName)
{
    tileset->setFileName(fileName);
    emit tilesetFileNameChanged(tileset);
}

void MapDocument::setTilesetName(Tileset *tileset, const QString &name)
{
    tileset->setName(name);
    emit tilesetNameChanged(tileset);
}
