/*
 * Copyright 2013, Tim Baker <treectrl@users.sf.net>
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

#include "buildingmap.h"

#include "building.h"
#include "buildingfloor.h"
#include "buildingobjects.h"
#include "buildingtemplates.h"
#include "buildingtiles.h"

#include "mapcomposite.h"
#include "mapmanager.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"

#include "isometricrenderer.h"
#include "map.h"
#include "maprenderer.h"
#include "tilelayer.h"
#include "tileset.h"
#include "zlevelrenderer.h"

#include <QDebug>

using namespace BuildingEditor;
using namespace Tiled;
using namespace Tiled::Internal;

BuildingMap::BuildingMap(Building *building) :
    mBuilding(building),
    mMapComposite(0),
    mMap(0),
    mBlendMapComposite(0),
    mBlendMap(0),
    mMapRenderer(0),
    pending(false),
    pendingRecreateAll(false),
    pendingBuildingResized(false),
    mCursorObjectFloor(0),
    mShadowBuilding(0)
{
    BuildingToMap();
}

BuildingMap::~BuildingMap()
{
    if (mMapComposite) {
        delete mMapComposite->mapInfo();
        delete mMapComposite;
        TilesetManager::instance()->removeReferences(mMap->tilesets());
        delete mMap;

        delete mBlendMapComposite->mapInfo();
        delete mBlendMapComposite;
        TilesetManager::instance()->removeReferences(mBlendMap->tilesets());
        delete mBlendMap;

        delete mMapRenderer;
    }

    if (mShadowBuilding)
        delete mShadowBuilding;
}

QString BuildingMap::buildingTileAt(int x, int y, int level, const QString &layerName)
{
    CompositeLayerGroup *layerGroup = mBlendMapComposite->layerGroupForLevel(level);

    QString tileName;

    foreach (TileLayer *tl, layerGroup->layers()) {
        if (layerName == MapComposite::layerNameWithoutPrefix(tl)) {
            if (tl->contains(x, y)) {
                Tile *tile = tl->cellAt(x, y).tile;
                if (tile)
                    tileName = BuildingTilesMgr::nameForTile(tile);
            }
            break;
        }
    }

    return tileName;
}

// The order must match the LayerIndexXXX constants.
// FIXME: add user-defined layers as well from TMXConfig.txt.
static const char *gLayerNames[] = {
    "Floor",
    "FloorGrime",
    "FloorGrime2",
    "Walls",
    "Walls2",
    "RoofCap",
    "RoofCap2",
    "WallOverlay",
    "WallOverlay2",
    "WallGrime",
    "WallFurniture",
    "Frames",
    "Doors",
    "Curtains",
    "Furniture",
    "Furniture2",
    "Curtains2",
    "Roof",
    "Roof2",
    "RoofTop",
    0
};

QStringList BuildingMap::layerNames(int level)
{
    Q_UNUSED(level)
//    if (!mDocument)
//        return QStringList();

    QStringList ret;
    for (int i = 0; gLayerNames[i]; i++)
        ret += QLatin1String(gLayerNames[i]);
    return ret;
}

#if 1
void BuildingMap::setCursorObject(BuildingFloor *floor, BuildingObject *object)
{
    if (mCursorObjectFloor && (mCursorObjectFloor != floor)) {
        pendingLayoutToSquares.insert(mCursorObjectFloor);
        if (!pending) {
            QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
            pending = true;
        }
        mCursorObjectFloor = 0;
    }

    if (mShadowBuilding->setCursorObject(floor, object)) {
        pendingLayoutToSquares.insert(floor);
        if (!pending) {
            QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
            pending = true;
        }
        mCursorObjectFloor = object ? floor : 0;
    }
}

void BuildingMap::dragObject(BuildingFloor *floor, BuildingObject *object, const QPoint &offset)
{
    mShadowBuilding->dragObject(floor, object, offset);
    pendingLayoutToSquares.insert(floor);
    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }
}

void BuildingMap::resetDrag(BuildingFloor *floor, BuildingObject *object)
{
    mShadowBuilding->resetDrag(object);
    pendingLayoutToSquares.insert(floor);
    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }
}

void BuildingMap::changeFloorGrid(BuildingFloor *floor, const QVector<QVector<Room*> > &grid)
{
    mShadowBuilding->changeFloorGrid(floor, grid);
    pendingLayoutToSquares.insert(floor);
    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }
}

void BuildingMap::resetFloorGrid(BuildingFloor *floor)
{
    mShadowBuilding->resetFloorGrid(floor);
    pendingLayoutToSquares.insert(floor);
    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }
}

void BuildingMap::suppressTiles(BuildingFloor *floor, const QRegion &rgn)
{
    QRegion update;
    if (mSuppressTiles.contains(floor) && mSuppressTiles[floor] == rgn)
        return;
    if (rgn.isEmpty()) {
        update = mSuppressTiles[floor];
        mSuppressTiles.remove(floor);
    } else {
        update = rgn | mSuppressTiles[floor];
        mSuppressTiles[floor] = rgn;
    }
    if (!update.isEmpty()) {
        foreach (QRect r, update.rects()) {
            r &= floor->bounds(1, 1);
            pendingSquaresToTileLayers[floor] |= r;
            foreach (QString layerName, floor->grimeLayers())
                pendingUserTilesToLayer[floor][layerName] |= r;
        }
        if (!pending) {
            QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
            pending = true;
        }
    }
}

#else
/**
  This method requires a bit of explanation.  The purpose is to show the result of
  adding or resizing a building object in real time.  BuildingFloor::LayoutToSquares
  is rather slow and doesn't support updating a sub-area of a floor.  This method
  creates a new tiny building that is just a bit larger than the object being
  created or resized.  The tiny building is given a copy of only those objects
  that overlap its bounds.  LayoutToSquares is then run just on the floor in the
  tiny building, and those squares are later used by BuildingSquaresToTileLayers.
  There are still issues with objects that should affect the floors above/below
  like stairs.
  */
void BuildingMap::setCursorObject(BuildingFloor *floor, BuildingObject *object,
                                  const QRect &bounds)
{
    if (mCursorObjectFloor) {
        BuildingFloor *floor2 = mBuilding->floor(mCursorObjectFloor->level());
        pendingSquaresToTileLayers[floor2] |= mCursorObjectFloor->bounds().translated(mCursorObjectPos);
        if (!pending) {
            QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
            pending = true;
        }
        delete mCursorObjectFloor;
        mCursorObjectFloor = 0;
        delete mCursorObjectBuilding;
        mCursorObjectBuilding = 0;
    }
    if (object/* && floor->bounds().intersects(object->bounds())*/) {
        // When resizing a wall object, we must call SquaresToTileLayers with
        // the combined area of the wall object's original bounds and the
        // current bounds during resizing.
        mCursorObjectBounds = bounds.isNull() ? object->bounds() : bounds;
        QRect r = mCursorObjectBounds.adjusted(-2, -2, 2, 2) & floor->bounds();
        mCursorObjectBuilding = new Building(r.width(), r.height());
        mCursorObjectBuilding->setExteriorWall(floor->building()->exteriorWall());
        foreach (Room *room, floor->building()->rooms())
            mCursorObjectBuilding->insertRoom(mCursorObjectBuilding->roomCount(),
                                              room);
        foreach (BuildingFloor *floor2, floor->building()->floors()) {
            BuildingFloor *clone = new BuildingFloor(mCursorObjectBuilding, floor2->level());
            mCursorObjectBuilding->insertFloor(clone->level(), clone);
            // TODO: clone stairs/roofs on floor below
            if (floor2 == floor)
                mCursorObjectFloor = clone;
        }

        for (int x = r.x(); x <= r.right(); x++) {
            for (int y = r.y(); y <= r.bottom(); y++) {
                mCursorObjectFloor->SetRoomAt(x - r.x(), y - r.y(), floor->GetRoomAt(x, y));
            }
        }

        // Copy overlapping objects.
        foreach (BuildingObject *object, floor->objects()) {
            if (r.adjusted(0,0,1,1) // some objects can be on the edge of the building
                    .intersects(object->bounds())) {
                BuildingObject *clone = object->clone();
                clone->setPos(clone->pos() - r.topLeft());
                clone->setFloor(mCursorObjectFloor);
                mCursorObjectFloor->insertObject(mCursorObjectFloor->objectCount(),
                                                 clone);
            }
        }

        // Clone the given object if it is a cursor object.
        if (!floor->objects().contains(object)) {
            BuildingObject *clone = object->clone();
            clone->setPos(clone->pos() - r.topLeft());
            clone->setFloor(mCursorObjectFloor);
            mCursorObjectFloor->insertObject(mCursorObjectFloor->objectCount(), clone);
        }

        mCursorObjectFloor->LayoutToSquares();
        mCursorObjectPos = r.topLeft();

        pendingSquaresToTileLayers[floor] |= r;
        if (!pending) {
            QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
            pending = true;
        }
    }
}
#endif

Map *BuildingMap::mergedMap() const
{
    Map *map = mBlendMap->clone();
    TilesetManager::instance()->addReferences(map->tilesets());
    for (int i = 0; i < map->layerCount(); i++) {
        if (TileLayer *tl = map->layerAt(i)->asTileLayer())
            tl->merge(tl->position(), mMap->layerAt(i)->asTileLayer());

    }
    return map;
}

void BuildingMap::buildingRotated()
{
    pendingBuildingResized = true;

    // When rotating or flipping, all the user tiles are cleared.
    // However, no signal is emitted until the buildingRotated signal.
    pendingEraseUserTiles = mBuilding->floors().toSet();

    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }
}

void BuildingMap::buildingResized()
{
    pendingBuildingResized = true;
    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }
}

void BuildingMap::BuildingToMap()
{
    if (mMapComposite) {
        delete mMapComposite->mapInfo();
        delete mMapComposite;
        TilesetManager::instance()->removeReferences(mMap->tilesets());
        delete mMap;

        delete mBlendMapComposite->mapInfo();
        delete mBlendMapComposite;
        TilesetManager::instance()->removeReferences(mBlendMap->tilesets());
        delete mBlendMap;

        delete mMapRenderer;
    }

    if (mShadowBuilding)
        delete mShadowBuilding;
    mShadowBuilding = new ShadowBuilding(mBuilding);
    mCursorObjectFloor = 0;

    Map::Orientation orient = Map::LevelIsometric;

    int maxLevel =  mBuilding->floorCount() - 1;
    int extraForWalls = 1;
    int extra = (orient == Map::LevelIsometric)
            ? extraForWalls : maxLevel * 3 + extraForWalls;
    QSize mapSize(mBuilding->width() + extra,
                  mBuilding->height() + extra);

    mMap = new Map(orient,
                   mapSize.width(), mapSize.height(),
                   64, 32);

    // Add tilesets from Tilesets.txt
    foreach (Tileset *ts, TileMetaInfoMgr::instance()->tilesets())
        mMap->addTileset(ts);
    TilesetManager::instance()->addReferences(mMap->tilesets());

    switch (mMap->orientation()) {
    case Map::Isometric:
        mMapRenderer = new IsometricRenderer(mMap);
        break;
    case Map::LevelIsometric:
        mMapRenderer = new ZLevelRenderer(mMap);
        break;
    default:
        return;
    }

    Q_ASSERT(sizeof(gLayerNames)/sizeof(gLayerNames[0]) == BuildingFloor::Square::MaxSection + 1);

    foreach (BuildingFloor *floor, mBuilding->floors()) {
        foreach (QString name, layerNames(floor->level())) {
            QString layerName = tr("%1_%2").arg(floor->level()).arg(name);
            TileLayer *tl = new TileLayer(layerName,
                                          0, 0, mapSize.width(), mapSize.height());
            mMap->addLayer(tl);
        }
    }

    MapInfo *mapInfo = MapManager::instance()->newFromMap(mMap);
    mMapComposite = new MapComposite(mapInfo);

    // Synch layer opacity with the floor.
    foreach (CompositeLayerGroup *layerGroup, mMapComposite->layerGroups()) {
        BuildingFloor *floor = mBuilding->floor(layerGroup->level());
        foreach (TileLayer *tl, layerGroup->layers()) {
            QString layerName = MapComposite::layerNameWithoutPrefix(tl);
            layerGroup->setLayerOpacity(tl, floor->layerOpacity(layerName));
        }
    }

    // This map displays the automatically-generated tiles from the building.
    mBlendMap = mMap->clone();
    TilesetManager::instance()->addReferences(mBlendMap->tilesets());
    mapInfo = MapManager::instance()->newFromMap(mBlendMap);
    mBlendMapComposite = new MapComposite(mapInfo);
    mMapComposite->setBlendOverMap(mBlendMapComposite);

    // Set the automatically-generated tiles.
    foreach (CompositeLayerGroup *layerGroup, mBlendMapComposite->layerGroups()) {
        BuildingFloor *floor = mBuilding->floor(layerGroup->level());
        floor->LayoutToSquares();
        BuildingSquaresToTileLayers(floor, floor->bounds(1, 1), layerGroup);
    }

    // Set the user-drawn tiles.
    foreach (BuildingFloor *floor, mBuilding->floors()) {
        foreach (QString layerName, floor->grimeLayers())
            userTilesToLayer(floor, layerName, floor->bounds(1, 1));
    }

    // Do this before calculating the bounds of CompositeLayerGroupItem
    mMapRenderer->setMaxLevel(mMapComposite->maxLevel());
}

void BuildingMap::BuildingSquaresToTileLayers(BuildingFloor *floor,
                                              const QRect &area,
                                              CompositeLayerGroup *layerGroup)
{
    int maxLevel = floor->building()->floorCount() - 1;
    int offset = (mMap->orientation() == Map::LevelIsometric)
            ? 0 : (maxLevel - floor->level()) * 3;

    QRegion suppress;
    if (mSuppressTiles.contains(floor))
        suppress = mSuppressTiles[floor];

    int section = 0;
    foreach (TileLayer *tl, layerGroup->layers()) {
        if (area == floor->bounds(1, 1))
            tl->erase();
        else
            tl->erase(area/*.adjusted(0,0,1,1)*/);
        for (int x = area.x(); x <= area.right(); x++) {
            for (int y = area.y(); y <= area.bottom(); y++) {
                if (section != BuildingFloor::Square::SectionFloor
                        && suppress.contains(QPoint(x, y)))
                    continue;
                const BuildingFloor::Square &square =
                        mShadowBuilding->floor(floor->level())->squares[x][y];
                if (BuildingTile *btile = square.mTiles[section]) {
                    if (!btile->isNone()) {
                        if (Tiled::Tile *tile = BuildingTilesMgr::instance()->tileFor(btile))
                            tl->setCell(x + offset, y + offset, Cell(tile));
                    }
                    continue;
                }
                if (BuildingTileEntry *entry = square.mEntries[section]) {
                    int tileOffset = square.mEntryEnum[section];
                    if (entry->isNone() || entry->tile(tileOffset)->isNone())
                        continue;
                    if (Tiled::Tile *tile = BuildingTilesMgr::instance()->tileFor(entry->tile(tileOffset)))
                        tl->setCell(x + offset, y + offset, Cell(tile));
                }

            }
        }
        layerGroup->regionAltered(tl); // possibly set mNeedsSynch
        section++;
    }
}

void BuildingMap::userTilesToLayer(BuildingFloor *floor,
                                   const QString &layerName,
                                   const QRect &bounds)
{
    CompositeLayerGroup *layerGroup = mMapComposite->layerGroupForLevel(floor->level());
    TileLayer *layer = 0;
    foreach (TileLayer *tl, layerGroup->layers()) {
        if (layerName == MapComposite::layerNameWithoutPrefix(tl)) {
            layer = tl;
            break;
        }
    }

    QMap<QString,Tileset*> tilesetByName;
    foreach (Tileset *ts, mMap->tilesets())
        tilesetByName[ts->name()] = ts;

    QRegion suppress;
    if (mSuppressTiles.contains(floor))
        suppress = mSuppressTiles[floor];

    for (int x = bounds.left(); x <= bounds.right(); x++) {
        for (int y = bounds.top(); y <= bounds.bottom(); y++) {
            if (suppress.contains(QPoint(x, y))) {
                layer->setCell(x, y, Cell());
                continue;
            }
            QString tileName = floor->grimeAt(layerName, x, y);
            Tile *tile = 0;
            if (!tileName.isEmpty()) {
                tile = TilesetManager::instance()->missingTile();
                QString tilesetName;
                int index;
                if (BuildingTilesMgr::parseTileName(tileName, tilesetName, index)) {
                    if (tilesetByName.contains(tilesetName)) {
                        tile = tilesetByName[tilesetName]->tileAt(index);
                    }
                }
            }
            layer->setCell(x, y, Cell(tile));
        }
    }

    layerGroup->regionAltered(layer); // possibly set mNeedsSynch
}

void BuildingMap::floorAdded(BuildingFloor *floor)
{
    Q_UNUSED(floor)
    recreateAllLater();
}

void BuildingMap::floorRemoved(BuildingFloor *floor)
{
    Q_UNUSED(floor)
    recreateAllLater();
}

void BuildingMap::floorEdited(BuildingFloor *floor)
{
    mShadowBuilding->floorEdited(floor);

    pendingLayoutToSquares.insert(floor);
    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }
}

void BuildingMap::floorTilesChanged(BuildingFloor *floor)
{
    mShadowBuilding->floorTilesChanged(floor);

    pendingEraseUserTiles.insert(floor);
    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }
}

void BuildingMap::floorTilesChanged(BuildingFloor *floor, const QString &layerName,
                                    const QRect &bounds)
{
    mShadowBuilding->floorTilesChanged(floor, layerName, bounds);

    pendingUserTilesToLayer[floor][layerName] |= bounds;
    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }
}

void BuildingMap::objectAdded(BuildingObject *object)
{
    BuildingFloor *floor = object->floor();
    pendingLayoutToSquares.insert(floor);

    // Stairs affect the floor tiles on the floor above.
    // Roofs sometimes affect the floor tiles on the floor above.
    if (BuildingFloor *floorAbove = floor->floorAbove()) {
        if (object->affectsFloorAbove())
            pendingLayoutToSquares.insert(floorAbove);
    }

    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }

    mShadowBuilding->objectAdded(object);
}

void BuildingMap::objectAboutToBeRemoved(BuildingObject *object)
{
    BuildingFloor *floor = object->floor();
    pendingLayoutToSquares.insert(floor);

    // Stairs affect the floor tiles on the floor above.
    // Roofs sometimes affect the floor tiles on the floor above.
    if (BuildingFloor *floorAbove = floor->floorAbove()) {
        if (object->affectsFloorAbove())
            pendingLayoutToSquares.insert(floorAbove);
    }

    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }

    mShadowBuilding->objectAboutToBeRemoved(object);
}

void BuildingMap::objectRemoved(BuildingObject *object)
{
    Q_UNUSED(object)
}

void BuildingMap::objectMoved(BuildingObject *object)
{
    BuildingFloor *floor = object->floor();
    pendingLayoutToSquares.insert(floor);

    // Stairs affect the floor tiles on the floor above.
    // Roofs sometimes affect the floor tiles on the floor above.
    if (BuildingFloor *floorAbove = floor->floorAbove()) {
        if (object->affectsFloorAbove())
            pendingLayoutToSquares.insert(floorAbove);
    }

    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }

    mShadowBuilding->objectMoved(object);
}

void BuildingMap::objectTileChanged(BuildingObject *object)
{
    BuildingFloor *floor = object->floor();
    pendingLayoutToSquares.insert(floor);

    // Stairs affect the floor tiles on the floor above.
    // Roofs sometimes affect the floor tiles on the floor above.
    if (BuildingFloor *floorAbove = floor->floorAbove()) {
        if (object->affectsFloorAbove())
            pendingLayoutToSquares.insert(floorAbove);
    }

    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }

    mShadowBuilding->objectTileChanged(object);
}

void BuildingMap::roomAdded(Room *room)
{
    mShadowBuilding->roomAdded(room);
}

void BuildingMap::roomRemoved(Room *room)
{
    mShadowBuilding->roomRemoved(room);
}

// When tilesets are added/removed, BuildingTile -> Tiled::Tile needs to be
// redone.
void BuildingMap::tilesetAdded(Tileset *tileset)
{
    int index = mMap->tilesets().indexOf(tileset);
    if (index >= 0)
        return;

    mMap->addTileset(tileset);
    TilesetManager::instance()->addReference(tileset);

    mBlendMap->addTileset(tileset);
    TilesetManager::instance()->addReference(tileset);

    foreach (BuildingFloor *floor, mBuilding->floors()) {
        pendingSquaresToTileLayers[floor] = floor->bounds(1, 1);
        foreach (QString layerName, floor->grimeLayers())
            pendingUserTilesToLayer[floor][layerName] = floor->bounds(1, 1);
    }

    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }
}

void BuildingMap::tilesetAboutToBeRemoved(Tileset *tileset)
{
    int index = mMap->tilesets().indexOf(tileset);
    if (index < 0)
        return;

    mMap->removeTilesetAt(index);
    TilesetManager::instance()->removeReference(tileset);

    mBlendMap->removeTilesetAt(index);
    TilesetManager::instance()->removeReference(tileset);

    // Erase every layer to get rid of Tiles from the tileset.
    foreach (CompositeLayerGroup *lg, mMapComposite->layerGroups())
        foreach (TileLayer *tl, lg->layers())
            tl->erase();
    foreach (CompositeLayerGroup *lg, mBlendMapComposite->layerGroups())
        foreach (TileLayer *tl, lg->layers())
            tl->erase();

    foreach (BuildingFloor *floor, mBuilding->floors()) {
        pendingSquaresToTileLayers[floor] = floor->bounds(1, 1);
        foreach (QString layerName, floor->grimeLayers())
            pendingUserTilesToLayer[floor][layerName] = floor->bounds(1, 1);
    }

    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }
}

void BuildingMap::tilesetRemoved(Tileset *tileset)
{
    Q_UNUSED(tileset)
}

// FIXME: tilesetChanged?

void BuildingMap::handlePending()
{
    QMap<int,QRegion> updatedLevels;

    if (pendingRecreateAll) {
        emit aboutToRecreateLayers(); // LayerGroupItems need to get ready
        BuildingToMap();
        pendingBuildingResized = false;
        pendingEraseUserTiles.clear(); // no need to erase, we just recreated the layers
    }

    if (pendingRecreateAll || pendingBuildingResized) {
        pendingLayoutToSquares = mBuilding->floors().toSet();
        pendingUserTilesToLayer.clear();
        foreach (BuildingFloor *floor, mBuilding->floors()) {
            foreach (QString layerName, floor->grimeLayers()) {
                pendingUserTilesToLayer[floor][layerName] = floor->bounds(1, 1);
            }
        }
    }

    if (pendingBuildingResized) {
        int extra = (mMap->orientation() == Map::LevelIsometric) ?
                    1 : mMapComposite->maxLevel() * 3 + 1;
        int width = mBuilding->width() + extra;
        int height = mBuilding->height() + extra;

        foreach (Layer *layer, mMap->layers())
            layer->resize(QSize(width, height), QPoint());
        mMap->setWidth(width);
        mMap->setHeight(height);

        foreach (Layer *layer, mBlendMap->layers())
            layer->resize(QSize(width, height), QPoint());
        mBlendMap->setWidth(width);
        mBlendMap->setHeight(height);

        delete mShadowBuilding;
        mShadowBuilding = new ShadowBuilding(mBuilding);
    }

    if (!pendingLayoutToSquares.isEmpty()) {
        foreach (BuildingFloor *floor, pendingLayoutToSquares) {
            floor->LayoutToSquares(); // not sure this belongs in this class
            pendingSquaresToTileLayers[floor] = floor->bounds(1, 1);

            mShadowBuilding->floor(floor->level())->LayoutToSquares();
        }
    }

    if (!pendingSquaresToTileLayers.isEmpty()) {
        foreach (BuildingFloor *floor, pendingSquaresToTileLayers.keys()) {
            CompositeLayerGroup *layerGroup = mBlendMapComposite->layerGroupForLevel(floor->level());
            QRect area = pendingSquaresToTileLayers[floor].boundingRect(); // TODO: only affected region
            BuildingSquaresToTileLayers(floor, area, layerGroup);
            if (layerGroup->needsSynch()) {
                mMapComposite->layerGroupForLevel(floor->level())->setNeedsSynch(true);
                layerGroup->synch(); // Don't really need to synch the blend-over-map, but do need
                                     // to update its draw margins so MapComposite::regionAltered
                                     // doesn't set mNeedsSynch repeatedly.
            }
            updatedLevels[floor->level()] |= area;
        }
    }

    if (!pendingEraseUserTiles.isEmpty()) {
        foreach (BuildingFloor *floor, pendingEraseUserTiles) {
            CompositeLayerGroup *layerGroup = mMapComposite->layerGroupForLevel(floor->level());
            foreach (TileLayer *tl, layerGroup->layers())
                tl->erase();
            foreach (QString layerName, floor->grimeLayers())
                pendingUserTilesToLayer[floor][layerName] = floor->bounds(1, 1);
            updatedLevels[floor->level()] |= floor->bounds();
        }
    }

    if (!pendingUserTilesToLayer.isEmpty()) {
        foreach (BuildingFloor *floor, pendingUserTilesToLayer.keys()) {
            foreach (QString layerName, pendingUserTilesToLayer[floor].keys()) {
                QRegion rgn = pendingUserTilesToLayer[floor][layerName];
                foreach (QRect r, rgn.rects())
                    userTilesToLayer(floor, layerName, r);
                updatedLevels[floor->level()] |= rgn;
            }
        }
    }

    if (pendingRecreateAll)
        emit layersRecreated();
    else if (pendingBuildingResized)
        emit mapResized();

    foreach (int level, updatedLevels.keys())
        emit layersUpdated(level, updatedLevels[level]);

    pending = false;
    pendingRecreateAll = false;
    pendingBuildingResized = false;
    pendingLayoutToSquares.clear();
    pendingSquaresToTileLayers.clear();
    pendingEraseUserTiles.clear();
    pendingUserTilesToLayer.clear();
}

void BuildingMap::recreateAllLater()
{
    pendingRecreateAll = true;
    if (!pending) {
        QMetaObject::invokeMethod(this, "handlePending", Qt::QueuedConnection);
        pending = true;
    }
}

/////

BuildingModifier::BuildingModifier(ShadowBuilding *shadowBuilding) :
    mShadowBuilding(shadowBuilding)
{
    mShadowBuilding->addModifier(this);
}

BuildingModifier::~BuildingModifier()
{
    mShadowBuilding->removeModifier(this);
}

class AddObjectModifier : public BuildingModifier
{
public:
    AddObjectModifier(ShadowBuilding *sb, BuildingFloor *floor, BuildingObject *object) :
        BuildingModifier(sb)
    {
        BuildingFloor *shadowFloor = mShadowBuilding->floor(floor->level());
        mObject = object;
        mShadowObject = mShadowBuilding->cloneObject(shadowFloor, object);
        shadowFloor->insertObject(shadowFloor->objectCount(), mShadowObject);
    }

    ~AddObjectModifier()
    {
        // It's possible the object was added to the floor after this
        // modifier was created.  For example, RoofTool adds the actual
        // cursor object to the floor when creating a new roof object.
        if (mObject)
            mShadowBuilding->objectAboutToBeRemoved(mObject);
    }

    BuildingObject *mObject;
    BuildingObject *mShadowObject;
};

class ResizeObjectModifier : public BuildingModifier
{
public:
    ResizeObjectModifier(ShadowBuilding *sb, BuildingObject *object,
                         BuildingObject *shadowObject) :
        BuildingModifier(sb),
        mObject(object),
        mShadowObject(shadowObject)
    {
    }

    ~ResizeObjectModifier()
    {
        // When resizing is cancelled/finished, redisplay the object.
        mShadowBuilding->recreateObject(mObject->floor(), mObject);
    }

    BuildingObject *mObject;
    BuildingObject *mShadowObject;
};

class MoveObjectModifier : public BuildingModifier
{
public:
    MoveObjectModifier(ShadowBuilding *sb, BuildingObject *object) :
        BuildingModifier(sb),
        mObject(object)
    {
        // The shadow object should already exist, we're just moving an existing object.

    }

    ~MoveObjectModifier()
    {
        setOffset(QPoint(0, 0));
    }

    void setOffset(const QPoint &offset)
    {
        BuildingObject *shadowObject = mShadowBuilding->shadowObject(mObject);
        shadowObject->setPos(mObject->pos() + offset);
    }

    BuildingObject *mObject;
};


class ChangeFloorGridModifier : public BuildingModifier
{
public:
    ChangeFloorGridModifier(ShadowBuilding *sb, BuildingFloor *floor) :
        BuildingModifier(sb),
        mFloor(floor)
    {
        // The shadow object should already exist, we're just moving an existing object.
    }

    ~ChangeFloorGridModifier()
    {
        BuildingFloor *shadowFloor = mShadowBuilding->floor(mFloor->level());
        shadowFloor->setGrid(mFloor->grid());
    }

    void setGrid(const QVector<QVector<Room*> > &grid)
    {
        BuildingFloor *shadowFloor = mShadowBuilding->floor(mFloor->level());
        shadowFloor->setGrid(grid);
    }

    BuildingFloor *mFloor;
};

ShadowBuilding::ShadowBuilding(const Building *building) :
    mBuilding(building),
    mCursorObjectModifier(0)
{
    mShadowBuilding = new Building(mBuilding->width(), mBuilding->height());
    mShadowBuilding->setTiles(mBuilding->tiles());
    foreach (Room *room, mBuilding->rooms())
        mShadowBuilding->insertRoom(mShadowBuilding->roomCount(), room);
    foreach (BuildingFloor *floor, mBuilding->floors()) {
        BuildingFloor *f = cloneFloor(floor);
        f->LayoutToSquares();
    }

}

ShadowBuilding::~ShadowBuilding()
{
    delete mShadowBuilding;
    qDeleteAll(mModifiers);
}

BuildingFloor *ShadowBuilding::floor(int level) const
{
    return mShadowBuilding->floor(level);
}

void ShadowBuilding::buildingRotated()
{
}

void ShadowBuilding::buildingResized()
{
}

void ShadowBuilding::floorAdded(BuildingFloor *floor)
{
    Q_UNUSED(floor)
    // The whole ShadowBuilding gets recreated elsewhere.
}

void ShadowBuilding::floorRemoved(BuildingFloor *floor)
{
    Q_UNUSED(floor)
    // The whole ShadowBuilding gets recreated elsewhere.
}

void ShadowBuilding::floorEdited(BuildingFloor *floor)
{
    // BuildingDocument emits roomDefinitionChanged when the exterior wall changes.
    // BuildingTileModeScene::roomDefinitionChanged() calls this method.
    mShadowBuilding->setExteriorWall(mBuilding->exteriorWall());

    mShadowBuilding->floor(floor->level())->setGrid(floor->grid());
}

void ShadowBuilding::floorTilesChanged(BuildingFloor *floor)
{
    BuildingFloor *shadowFloor = mShadowBuilding->floor(floor->level());

    QMap<QString,FloorTileGrid*> grime = shadowFloor->setGrime(floor->grimeClone());
    foreach (FloorTileGrid *grid, grime.values())
        delete grid;
}

void ShadowBuilding::floorTilesChanged(BuildingFloor *floor,
                                       const QString &layerName,
                                       const QRect &bounds)
{
    BuildingFloor *shadowFloor = mShadowBuilding->floor(floor->level());

    FloorTileGrid *grid = floor->grimeAt(layerName, bounds);
    shadowFloor->setGrime(layerName, bounds.topLeft(), grid);
    delete grid;
}

void ShadowBuilding::objectAdded(BuildingObject *object)
{
    foreach (BuildingModifier *bmod, mModifiers) {
        if (AddObjectModifier *mod = dynamic_cast<AddObjectModifier*>(bmod)) {
            if (mod->mObject == object) {
                mod->mObject = 0;
            }
        }
    }

    BuildingFloor *shadowFloor = mShadowBuilding->floor(object->floor()->level());

    // Check if the object was already added.  For example, RoofTool creates
    // a cursor-object for a new roof, then adds that object to the floor
    // when new roof object is added to the building.
    if (mOriginalToShadowObject.contains(object)) {
        BuildingObject *shadowObject = mOriginalToShadowObject[object];
        shadowFloor->removeObject(shadowObject->index());
        shadowFloor->insertObject(object->index(), shadowObject);
        return;
    }

    shadowFloor->insertObject(object->index(), cloneObject(shadowFloor, object));
}

void ShadowBuilding::objectAboutToBeRemoved(BuildingObject *object)
{
    if (mOriginalToShadowObject.contains(object)) {
        BuildingObject *shadowObject = mOriginalToShadowObject[object];
        shadowObject->floor()->removeObject(shadowObject->index());
        delete shadowObject;
        mOriginalToShadowObject.remove(object);
    }
}

void ShadowBuilding::objectRemoved(BuildingObject *object)
{
    Q_UNUSED(object)
}

void ShadowBuilding::objectMoved(BuildingObject *object)
{
    // This also gets called when a roof object is resized.
    if (mOriginalToShadowObject.contains(object)) {
        recreateObject(object->floor(), object);
    }
}

void ShadowBuilding::objectTileChanged(BuildingObject *object)
{
    recreateObject(object->floor(), object);
}

void ShadowBuilding::roomAdded(Room *room)
{
    mShadowBuilding->insertRoom(mBuilding->indexOf(room), room);
}

void ShadowBuilding::roomRemoved(Room *room)
{
    mShadowBuilding->removeRoom(mShadowBuilding->indexOf(room));
}

BuildingFloor *ShadowBuilding::cloneFloor(BuildingFloor *floor)
{
    BuildingFloor *f = new BuildingFloor(mShadowBuilding, floor->level());
    f->setGrid(floor->grid());
    f->setGrime(floor->grimeClone());
    mShadowBuilding->insertFloor(f->level(), f);
    foreach (BuildingObject *object, floor->objects())
        f->insertObject(f->objectCount(), cloneObject(f, object));
    return f;
}

BuildingObject *ShadowBuilding::cloneObject(BuildingFloor *shadowFloor, BuildingObject *object)
{
    Q_ASSERT(!mOriginalToShadowObject.contains(object));
    BuildingObject *clone = object->clone();
    clone->setFloor(shadowFloor);
    mOriginalToShadowObject[object] = clone;
    return clone;
}

void ShadowBuilding::recreateObject(BuildingFloor *originalFloor, BuildingObject *object)
{
    if (mOriginalToShadowObject.contains(object)) {
        BuildingObject *shadowObject = mOriginalToShadowObject[object];
        int index = shadowObject->index();
        BuildingFloor *shadowFloor = shadowObject->floor();
        shadowFloor->removeObject(index);
        delete shadowObject;
        mOriginalToShadowObject.remove(object);

        shadowFloor = mShadowBuilding->floor(originalFloor->level());
        shadowObject = cloneObject(shadowFloor, object);
        shadowFloor->insertObject(index, shadowObject);
    }
}

void ShadowBuilding::addModifier(BuildingModifier *modifier)
{
    mModifiers += modifier;
}

void ShadowBuilding::removeModifier(BuildingModifier *modifier)
{
    mModifiers.removeAll(modifier);
}

bool ShadowBuilding::setCursorObject(BuildingFloor *floor, BuildingObject *object)
{
    if (!object) {
        if (mCursorObjectModifier) {
            delete mCursorObjectModifier;
            mCursorObjectModifier = 0;
            return true;
        }
        return false;
    }

    // Recreate the object, its tile or orientation may have changed.
    // Also, the floor the cursor object is on might have changed.
    if (mCursorObjectModifier) {
        // FIXME: any modifier using the recreated shadow object must get updated
        // or they will still point to the old shadow object.
        if (mOriginalToShadowObject.contains(object))
            recreateObject(floor, object);
    } else {
        bool cursorObject = floor->indexOf(object) == -1;
        if (cursorObject) {
            mCursorObjectModifier = new AddObjectModifier(this, floor, object);
        } else {
            mCursorObjectModifier = new ResizeObjectModifier(this, object,
                                                             mOriginalToShadowObject[object]);
        }
    }

    return true;
}

void ShadowBuilding::dragObject(BuildingFloor *floor, BuildingObject *object, const QPoint &offset)
{
    if (object->floor() == 0) {
        foreach (BuildingModifier *bmod, mModifiers) {
            if (AddObjectModifier *mod = dynamic_cast<AddObjectModifier*>(bmod)) {
                if (mod->mObject == object) {
                    mod->mShadowObject->setPos(object->pos() + offset);
                    return;
                }
            }
        }
        AddObjectModifier *mod = new AddObjectModifier(this, floor, object);
        mod->mShadowObject->setPos(object->pos() + offset);
        return;
    }

    foreach (BuildingModifier *bmod, mModifiers) {
        if (MoveObjectModifier *mod = dynamic_cast<MoveObjectModifier*>(bmod)) {
            if (mod->mObject == object) {
                mod->setOffset(offset);
                return;
            }
        }
    }

    MoveObjectModifier *mod = new MoveObjectModifier(this, object);
    mod->setOffset(offset);
}

void ShadowBuilding::resetDrag(BuildingObject *object)
{
    foreach (BuildingModifier *bmod, mModifiers) {
        if (MoveObjectModifier *mod = dynamic_cast<MoveObjectModifier*>(bmod)) {
            if (mod->mObject == object) {
                delete mod;
                return;
            }
        }
        if (AddObjectModifier *mod = dynamic_cast<AddObjectModifier*>(bmod)) {
            if (mod->mObject == object) {
                delete mod;
                return;
            }
        }
    }
}

void ShadowBuilding::changeFloorGrid(BuildingFloor *floor, const QVector<QVector<Room*> > &grid)
{
    foreach (BuildingModifier *bmod, mModifiers) {
        if (ChangeFloorGridModifier *mod = dynamic_cast<ChangeFloorGridModifier*>(bmod)) {
            if (mod->mFloor == floor) {
                mod->setGrid(grid);
                return;
            }
        }
    }

    ChangeFloorGridModifier *mod = new ChangeFloorGridModifier(this, floor);
    mod->setGrid(grid);
}

void ShadowBuilding::resetFloorGrid(BuildingFloor *floor)
{
    foreach (BuildingModifier *bmod, mModifiers) {
        if (ChangeFloorGridModifier *mod = dynamic_cast<ChangeFloorGridModifier*>(bmod)) {
            if (mod->mFloor == floor) {
                delete mod;
                return;
            }
        }
    }
}