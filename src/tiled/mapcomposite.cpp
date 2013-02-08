/*
 * Copyright 2012, Tim Baker <treectrl@users.sf.net>
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

#include "mapcomposite.h"

#include "mapmanager.h"
#include "mapobject.h"
#include "maprenderer.h"
#include "objectgroup.h"
#include "tilelayer.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>

using namespace Tiled;

static void maxMargins(const QMargins &a,
                       const QMargins &b,
                       QMargins &out)
{
    out.setLeft(qMax(a.left(), b.left()));
    out.setTop(qMax(a.top(), b.top()));
    out.setRight(qMax(a.right(), b.right()));
    out.setBottom(qMax(a.bottom(), b.bottom()));
}

static void unionTileRects(const QRect &a,
                           const QRect &b,
                           QRect &out)
{
    if (a.isEmpty())
        out = b;
    else if (b.isEmpty())
        out = a;
    else
        out = a | b;
}

static void unionSceneRects(const QRectF &a,
                           const QRectF &b,
                           QRectF &out)
{
    if (a.isEmpty())
        out = b;
    else if (b.isEmpty())
        out = a;
    else
        out = a | b;
}

QString MapComposite::layerNameWithoutPrefix(const QString &name)
{
    int pos = name.indexOf(QLatin1Char('_')) + 1; // Could be "-1 + 1 == 0"
    return name.mid(pos);
}

QString MapComposite::layerNameWithoutPrefix(Layer *layer)
{
    return layerNameWithoutPrefix(layer->name());
}

///// ///// ///// ///// /////

CompositeLayerGroup::SubMapLayers::SubMapLayers(MapComposite *subMap,
                                                CompositeLayerGroup *layerGroup)
    : mSubMap(subMap)
    , mLayerGroup(layerGroup)
    , mBounds(layerGroup->bounds().translated(subMap->origin()))
{
}

///// ///// ///// ///// /////

CompositeLayerGroup::CompositeLayerGroup(MapComposite *owner, int level)
    : ZTileLayerGroup(owner->map(), level)
    , mOwner(owner)
    , mAnyVisibleLayers(false)
    , mNeedsSynch(true)
#ifdef BUILDINGED
    , mToolTileLayer(0)
#endif // BUILDINGED
{

}

void CompositeLayerGroup::addTileLayer(TileLayer *layer, int index)
{
    // Hack -- only a map being edited can set a TileLayer's group.
    ZTileLayerGroup *oldGroup = layer->group();
    ZTileLayerGroup::addTileLayer(layer, index);
    if (!mOwner->mapInfo()->isBeingEdited())
        layer->setGroup(oldGroup);

    // Remember the names of layers (without the N_ prefix)
    const QString name = MapComposite::layerNameWithoutPrefix(layer);
    mLayersByName[name].append(layer);

    index = mLayers.indexOf(layer);
    mVisibleLayers.insert(index, layer->isVisible());
    mLayerOpacity.insert(index, mOwner->mapInfo()->isBeingEdited()
                         ? layer->opacity() : 1.0f);

    // To optimize drawing of submaps, remember which layers are totally empty.
    // But don't do this for the top-level map (the one being edited).
    // TileLayer::isEmpty() is SLOW, it's why I'm caching it.
    bool empty = mOwner->mapInfo()->isBeingEdited()
            ? false
            : layer->isEmpty() || layer->name().contains(QLatin1String("NoRender"));
    mEmptyLayers.insert(index, empty);

#ifdef BUILDINGED
    mBlendLayers.insert(index, 0);
    mForceNonEmpty.insert(index, false);
#endif // BUILDINGED
}

void CompositeLayerGroup::removeTileLayer(TileLayer *layer)
{
    int index = mLayers.indexOf(layer);
    mVisibleLayers.remove(index);
    mLayerOpacity.remove(index);
    mEmptyLayers.remove(index);
#ifdef BUILDINGED
    mBlendLayers.remove(index);
    mForceNonEmpty.remove(index);
#endif // BUILDINGED

    // Hack -- only a map being edited can set a TileLayer's group.
    ZTileLayerGroup *oldGroup = layer->group();
    ZTileLayerGroup::removeTileLayer(layer);
    if (!mOwner->mapInfo()->isBeingEdited())
        layer->setGroup(oldGroup);

    const QString name = MapComposite::layerNameWithoutPrefix(layer);
    index = mLayersByName[name].indexOf(layer);
    mLayersByName[name].remove(index);
}

void CompositeLayerGroup::prepareDrawing(const MapRenderer *renderer, const QRect &rect)
{
    mPreparedSubMapLayers.resize(0);
    if (mAnyVisibleLayers == false)
        return;
    foreach (const SubMapLayers &subMapLayer, mVisibleSubMapLayers) {
        CompositeLayerGroup *layerGroup = subMapLayer.mLayerGroup;
        if (subMapLayer.mSubMap->isHiddenDuringDrag())
            continue;
        QRectF bounds = layerGroup->boundingRect(renderer);
        if ((bounds & rect).isValid()) {
            mPreparedSubMapLayers.append(subMapLayer);
            layerGroup->prepareDrawing(renderer, rect);
        }
    }
}

bool CompositeLayerGroup::orderedCellsAt(const QPoint &pos,
                                         QVector<const Cell *> &cells,
                                         QVector<qreal> &opacities) const
{
    static QLatin1String sFloor("0_Floor");

    MapComposite *root = mOwner->root();
    if (!mOwner->parent())
        root->mFirstCellIs0Floor = false;

    bool cleared = false;
    for (int index = 0; index < mLayers.size(); index++) {
        if (isLayerEmpty(index))
            continue;
        TileLayer *tl = mLayers[index];
#ifdef BUILDINGED
        TileLayer *tlBlend = mBlendLayers[index];
#endif // BUILDINGED
        QPoint subPos = pos - mOwner->orientAdjustTiles() * mLevel - tl->position();
        if (tl->contains(subPos)) {
            const Cell *cell = &tl->cellAt(subPos);
#ifdef BUILDINGED
            // Use an empty tool tile if given during erasing.
            if ((mToolTileLayer == tl) && !mToolTiles.isEmpty() &&
                    QRect(mToolTilesPos, QSize(mToolTiles.size(), mToolTiles[0].size())).contains(subPos))
                cell = &mToolTiles[subPos.x()-mToolTilesPos.x()][subPos.y()-mToolTilesPos.y()];
            else if (cell->isEmpty() && tlBlend && tlBlend->contains(subPos))
                cell = &tlBlend->cellAt(subPos);
#endif // BUILDINGED
            if (!cell->isEmpty()) {
                if (!cleared) {
                    bool isFloor = !mLevel && !index && (tl->name() == sFloor);
                    cells.resize((!isFloor && root->mFirstCellIs0Floor) ? 1 : 0);
                    opacities.resize((!isFloor && root->mFirstCellIs0Floor) ? 1 : 0);
                    cleared = true;
                    if (isFloor && !mOwner->parent())
                        mOwner->mFirstCellIs0Floor = true;
                }
                cells.append(cell);
#if 1
                opacities.append(mLayerOpacity[index]);
#else
                if (mHighlightLayer.isEmpty() || tl->name() == mHighlightLayer)
                    opacities.append(mLayerOpacity[index]);
                else
                    opacities.append(0.25);
#endif
            }
        }
    }

    // Overwrite map cells with sub-map cells at this location
    foreach (const SubMapLayers& subMapLayer, mPreparedSubMapLayers) {
        if (!subMapLayer.mBounds.contains(pos))
            continue;
        subMapLayer.mLayerGroup->orderedCellsAt(pos - subMapLayer.mSubMap->origin(),
                                                cells, opacities);
    }

    return !cells.isEmpty();
}

bool CompositeLayerGroup::isLayerEmpty(int index) const
{
    if (!mVisibleLayers[index])
        return true;
#ifdef BUILDINGED
    if (mForceNonEmpty[index])
        return false;
    if (mBlendLayers[index] && !mBlendLayers[index]->isEmpty())
        return false;
    if (mToolTileLayer && !mToolTiles.isEmpty())
        return false;
#endif // BUILDINGED
#if SPARSE_TILELAYER
    // Checking isEmpty() and mEmptyLayers to catch hidden NoRender layers in submaps.
    return mEmptyLayers[index] || mLayers[index]->isEmpty();
#else
    // TileLayer::isEmpty() is very slow.
    // Checking mEmptyLayers only catches hidden NoRender layers.
    // The actual tile layer might be empty.
    return mEmptyLayers[index]);
#endif
}

void CompositeLayerGroup::synch()
{
    if (!mVisible) {
        mAnyVisibleLayers = false;
        mTileBounds = QRect();
        mSubMapTileBounds = QRect();
        mDrawMargins = QMargins(0, mOwner->map()->tileHeight(), mOwner->map()->tileWidth(), 0);
        mVisibleSubMapLayers.clear();
#ifdef BUILDINGED
        mBlendLayers.fill(0);
#endif
        mNeedsSynch = false;
        return;
    }

    QRect r;
    // See TileLayer::drawMargins()
    QMargins m(0, mOwner->map()->tileHeight(), mOwner->map()->tileWidth(), 0);

    mAnyVisibleLayers = false;

#ifdef BUILDINGED
    // Do this before the isLayerEmpty() call below.
    mBlendLayers.fill(0);
    if (MapComposite *blendOverMap = mOwner->blendOverMap()) {
        if (CompositeLayerGroup *layerGroup = blendOverMap->tileLayersForLevel(mLevel)) {
            for (int i = 0; i < mLayers.size(); i++) {
                if (!mVisibleLayers[i])
                    continue;
                for (int j = 0; j < layerGroup->mLayers.size(); j++) {
                    TileLayer *blendLayer = layerGroup->mLayers[j];
                    if (blendLayer->name() == mLayers[i]->name()) {
                        mBlendLayers[i] = blendLayer;
                        if (!blendLayer->isEmpty()) {
                            unionTileRects(r, blendLayer->bounds().translated(mOwner->orientAdjustTiles() * mLevel), r);
                            maxMargins(m, blendLayer->drawMargins(), m);
                            mAnyVisibleLayers = true;
                        }
                        break;
                    }
                }
            }
        }
    }

    if (mToolTileLayer && !mToolTiles.isEmpty()) {
        unionTileRects(r, mToolTileLayer->bounds().translated(mOwner->orientAdjustTiles() * mLevel), r);
        maxMargins(m, QMargins(0, 128, 64, 0), m);
        mAnyVisibleLayers = true;
    }
#endif

    // Set the visibility and opacity of this group's layers to match the root
    // map's layer-group's layers. Layers without a matching name in the root map
    // are always shown at full opacity.
    // Note that changing the opacity of a layer is *not* a reason for calling
    // synch() again.
    if (mOwner->parent()) {
        for (int index = 0; index < mLayers.size(); index++) {
            mVisibleLayers[index] = true;
            mLayerOpacity[index] = 1.0;
        }
        CompositeLayerGroup *rootGroup = mOwner->root()->layerGroupForLevel(mOwner->levelRecursive() + mLevel);
        if (rootGroup) {
            // FIXME: this doesn't properly handle multiple layers with the same name.
            for (int rootIndex = 0; rootIndex < rootGroup->mLayers.size(); rootIndex++) {
                QString layerName = rootGroup->mLayers[rootIndex]->name();
                const QString name = MapComposite::layerNameWithoutPrefix(layerName);
                if (!mLayersByName.contains(name))
                    continue;
                foreach (Layer *layer, mLayersByName[name]) {
                    int index = mLayers.indexOf(layer->asTileLayer());
                    Q_ASSERT(index != -1);
                    mVisibleLayers[index] = rootGroup->mVisibleLayers[rootIndex];
                    mLayerOpacity[index] = rootGroup->mLayerOpacity[rootIndex];
                }
            }
        }
    }

    int index = 0;
    foreach (TileLayer *tl, mLayers) {
        if (!isLayerEmpty(index)) {
            unionTileRects(r, tl->bounds().translated(mOwner->orientAdjustTiles() * mLevel), r);
            maxMargins(m, tl->drawMargins(), m);
            mAnyVisibleLayers = true;
        }
        ++index;
    }

    mTileBounds = r;

    r = QRect();
    mVisibleSubMapLayers.resize(0);

    foreach (MapComposite *subMap, mOwner->subMaps()) {
        if (!subMap->isGroupVisible() || !subMap->isVisible())
            continue;
        int levelOffset = subMap->levelOffset();
        CompositeLayerGroup *layerGroup = subMap->tileLayersForLevel(mLevel - levelOffset);
        if (layerGroup) {
            layerGroup->synch();
            if (layerGroup->mAnyVisibleLayers) {
                mVisibleSubMapLayers.append(SubMapLayers(subMap, layerGroup));
                unionTileRects(r, layerGroup->bounds().translated(subMap->origin()), r);
                maxMargins(m, layerGroup->drawMargins(), m);
                mAnyVisibleLayers = true;
            }
        }
    }

#ifdef BUILDINGED
    if (mAnyVisibleLayers)
        maxMargins(m, QMargins(0, 128, 64, 0), m);
#endif

    mSubMapTileBounds = r;
    mDrawMargins = m;

    mNeedsSynch = false;
}

void CompositeLayerGroup::saveVisibility()
{
    mSavedVisibleLayers = mVisibleLayers;
}

void CompositeLayerGroup::restoreVisibility()
{
    mVisibleLayers = mSavedVisibleLayers;
}

void CompositeLayerGroup::saveOpacity()
{
    mSavedOpacity = mLayerOpacity;
}

void CompositeLayerGroup::restoreOpacity()
{
    mLayerOpacity = mSavedOpacity;
}

#ifdef BUILDINGED
bool CompositeLayerGroup::setLayerNonEmpty(const QString &layerName, bool force)
{
    const QString name = MapComposite::layerNameWithoutPrefix(layerName);
    if (!mLayersByName.contains(name))
        return false;
    foreach (Layer *layer, mLayersByName[name])
        setLayerNonEmpty(layer->asTileLayer(), force);
    return mNeedsSynch;
}

bool CompositeLayerGroup::setLayerNonEmpty(TileLayer *tl, bool force)
{
    int index = mLayers.indexOf(tl);
    Q_ASSERT(index != -1);
    if (force != mForceNonEmpty[index]) {
        mForceNonEmpty[index] = force;
        mNeedsSynch = true;
    }
    return mNeedsSynch;
}
#endif

QRect CompositeLayerGroup::bounds() const
{
    QRect bounds;
    unionTileRects(mTileBounds, mSubMapTileBounds, bounds);
    return bounds;
}

QMargins CompositeLayerGroup::drawMargins() const
{
    return mDrawMargins;
}

bool CompositeLayerGroup::setLayerVisibility(const QString &layerName, bool visible)
{
    const QString name = MapComposite::layerNameWithoutPrefix(layerName);
    if (!mLayersByName.contains(name))
        return false;
    foreach (Layer *layer, mLayersByName[name])
        setLayerVisibility(layer->asTileLayer(), visible);
    return mNeedsSynch;
}

bool CompositeLayerGroup::setLayerVisibility(TileLayer *tl, bool visible)
{
    int index = mLayers.indexOf(tl);
    Q_ASSERT(index != -1);
    if (visible != mVisibleLayers[index]) {
        mVisibleLayers[index] = visible;
        mNeedsSynch = true;
    }
    return mNeedsSynch;
}

bool CompositeLayerGroup::isLayerVisible(TileLayer *tl)
{
    int index = mLayers.indexOf(tl);
    Q_ASSERT(index != -1);
    return mVisibleLayers[index];
}

void CompositeLayerGroup::layerRenamed(TileLayer *layer)
{
    QMapIterator<QString,QVector<Layer*> > it(mLayersByName);
    while (it.hasNext()) {
        it.next();
        int index = it.value().indexOf(layer);
        if (index >= 0) {
            mLayersByName[it.key()].remove(index);
//            it.value().remove(index);
            break;
        }
    }

    const QString name = MapComposite::layerNameWithoutPrefix(layer);
    mLayersByName[name].append(layer);
}

bool CompositeLayerGroup::setLayerOpacity(const QString &layerName, qreal opacity)
{
    const QString name = MapComposite::layerNameWithoutPrefix(layerName);
    if (!mLayersByName.contains(name))
        return false;
    bool changed = false;
    foreach (Layer *layer, mLayersByName[name]) {
        if (setLayerOpacity(layer->asTileLayer(), opacity))
            changed = true;
    }
    return changed;
}

bool CompositeLayerGroup::setLayerOpacity(TileLayer *tl, qreal opacity)
{
    int index = mLayers.indexOf(tl);
    Q_ASSERT(index != -1);
    if (mLayerOpacity[index] != opacity) {
        mLayerOpacity[index] = opacity;
        return true;
    }
    return false;
}

void CompositeLayerGroup::synchSubMapLayerOpacity(const QString &layerName, qreal opacity)
{
    foreach (MapComposite *subMap, mOwner->subMaps()) {
        CompositeLayerGroup *layerGroup =
                subMap->tileLayersForLevel(mLevel - subMap->levelOffset());
        if (layerGroup) {
            layerGroup->setLayerOpacity(layerName, opacity);
            layerGroup->synchSubMapLayerOpacity(layerName, opacity);
        }
    }
}

bool CompositeLayerGroup::regionAltered(Tiled::TileLayer *tl)
{
    QMargins m;
    maxMargins(mDrawMargins, tl->drawMargins(), m);
    if (m != mDrawMargins) {
        setNeedsSynch(true);
        return true;
    }
#ifdef BUILDINGED
    int index = mLayers.indexOf(tl);
    if (mTileBounds.isEmpty() && mBlendLayers[index] && !mBlendLayers[index]->isEmpty()) {
        setNeedsSynch(true);
        return true;
    }
#endif
#if SPARSE_TILELAYER
    if (mTileBounds.isEmpty() && !tl->isEmpty()) {
        int index = mLayers.indexOf(tl);
        mEmptyLayers[index] = false;
        setNeedsSynch(true);
        return true;
    }
#endif
    return false;
}

QRectF CompositeLayerGroup::boundingRect(const MapRenderer *renderer)
{
    if (mNeedsSynch)
        synch();

    QRectF boundingRect = renderer->boundingRect(mTileBounds.translated(mOwner->originRecursive()),
                                                 mLevel + mOwner->levelRecursive());

    // The TileLayer includes the maximum tile size in its draw margins. So
    // we need to subtract the tile size of the map, since that part does not
    // contribute to additional margin.

    boundingRect.adjust(-mDrawMargins.left(),
                -qMax(0, mDrawMargins.top() - owner()->map()->tileHeight()),
                qMax(0, mDrawMargins.right() - owner()->map()->tileWidth()),
                mDrawMargins.bottom());

    foreach (const SubMapLayers &subMapLayer, mVisibleSubMapLayers) {
        QRectF bounds = subMapLayer.mLayerGroup->boundingRect(renderer);
        unionSceneRects(boundingRect, bounds, boundingRect);
    }

    return boundingRect;
}

///// ///// ///// ///// /////

// FIXME: If the MapDocument is saved to a new name, this MapInfo should be replaced with a new one
MapComposite::MapComposite(MapInfo *mapInfo, Map::Orientation orientRender,
                           MapComposite *parent, const QPoint &positionInParent,
                           int levelOffset)
    : mMapInfo(mapInfo)
    , mMap(mapInfo->map())
    , mParent(parent)
    , mPos(positionInParent)
    , mLevelOffset(levelOffset)
    , mOrientRender(orientRender)
    , mMinLevel(0)
    , mMaxLevel(0)
    , mVisible(true)
    , mGroupVisible(true)
    , mHiddenDuringDrag(false)
#ifdef BUILDINGED
    , mBlendOverMap(0)
#endif
{
#if 0
    MapManager::instance()->addReferenceToMap(mMapInfo);
#endif
    if (mOrientRender == Map::Unknown)
        mOrientRender = mMap->orientation();
    if (mMap->orientation() != mOrientRender) {
        Map::Orientation orientSelf = mMap->orientation();
        if (orientSelf == Map::Isometric && mOrientRender == Map::LevelIsometric)
            mOrientAdjustPos = mOrientAdjustTiles = QPoint(3, 3);
        if (orientSelf == Map::LevelIsometric && mOrientRender == Map::Isometric)
            mOrientAdjustPos = mOrientAdjustTiles = QPoint(-3, -3);
    }

    int index = 0;
    foreach (Layer *layer, mMap->layers()) {
        int level;
        if (levelForLayer(layer, &level)) {
            // FIXME: no changing of mMap should happen after it is loaded!
            layer->setLevel(level); // for ObjectGroup,ImageLayer as well

            if (TileLayer *tl = layer->asTileLayer()) {
                if (!mLayerGroups.contains(level))
                    mLayerGroups[level] = new CompositeLayerGroup(this, level);
                mLayerGroups[level]->addTileLayer(tl, index);
                if (!mapInfo->isBeingEdited())
                    mLayerGroups[level]->setLayerVisibility(tl, !layer->name().contains(QLatin1String("NoRender")));
            }
        }
        ++index;
    }

    // Load lots, but only if this is not the map being edited (that is handled
    // by the LotManager).
    if (!mapInfo->isBeingEdited()) {
        foreach (ObjectGroup *objectGroup, mMap->objectGroups()) {
            foreach (MapObject *object, objectGroup->objects()) {
                if (object->name() == QLatin1String("lot") && !object->type().isEmpty()) {
                    // FIXME: if this sub-map is converted from LevelIsometric to Isometric,
                    // then any sub-maps of its own will lose their level offsets.
                    MapInfo *subMapInfo = MapManager::instance()->loadMap(object->type(),
                                                                          QFileInfo(mMapInfo->path()).absolutePath());
                    if (!subMapInfo) {
                        qDebug() << "failed to find sub-map" << object->type() << "inside map" << mMapInfo->path();
#if 0 // FIXME: attempt to load this if mapsDirectory changes
                        subMapInfo = MapManager::instance()->getPlaceholderMap(object->type(), mMap->orientation(),
                                                                               32, 32); // FIXME: calculate map size
#endif
                    }
                    if (subMapInfo) {
                        int levelOffset;
                        (void) levelForLayer(objectGroup, &levelOffset);
                        addMap(subMapInfo, object->position().toPoint()
                               + mOrientAdjustPos * levelOffset,
                               levelOffset);
                    }
                }
            }
        }
    }

    mMinLevel = 10000;
    mMaxLevel = 0;

    foreach (CompositeLayerGroup *layerGroup, mLayerGroups) {
        if (!mMapInfo->isBeingEdited())
            layerGroup->synch();
        if (layerGroup->level() < mMinLevel)
            mMinLevel = layerGroup->level();
        if (layerGroup->level() > mMaxLevel)
            mMaxLevel = layerGroup->level();
    }

    if (mMinLevel == 10000)
        mMinLevel = 0;

    for (int level = mMinLevel; level <= mMaxLevel; ++level) {
        if (!mLayerGroups.contains(level))
            mLayerGroups[level] = new CompositeLayerGroup(this, level);
        mSortedLayerGroups.append(mLayerGroups[level]);
    }
}

MapComposite::~MapComposite()
{
    qDeleteAll(mSubMaps);
    qDeleteAll(mLayerGroups);
#if 0
    if (mMapInfo)
        MapManager::instance()->removeReferenceToMap(mMapInfo);
#endif
}

bool MapComposite::levelForLayer(const QString &layerName, int *levelPtr)
{
    if (levelPtr) (*levelPtr) = 0;

    // See if the layer name matches "0_foo" or "1_bar" etc.
    QStringList sl = layerName.trimmed().split(QLatin1Char('_'));
    if (sl.count() > 1 && !sl[1].isEmpty()) {
        bool conversionOK;
        uint level = sl[0].toUInt(&conversionOK);
        if (levelPtr) (*levelPtr) = level;
        return conversionOK;
    }
    return false;
}

bool MapComposite::levelForLayer(Layer *layer, int *levelPtr)
{
    return levelForLayer(layer->name(), levelPtr);
}

MapComposite *MapComposite::addMap(MapInfo *mapInfo, const QPoint &pos, int levelOffset)
{
    MapComposite *subMap = new MapComposite(mapInfo, mOrientRender, this, pos, levelOffset);
    mSubMaps.append(subMap);

    ensureMaxLevels(levelOffset + subMap->maxLevel());

    foreach (CompositeLayerGroup *layerGroup, mLayerGroups) {
        layerGroup->setNeedsSynch(true);
    }

    return subMap;
}

void MapComposite::removeMap(MapComposite *subMap)
{
    Q_ASSERT(mSubMaps.contains(subMap));
    mSubMaps.remove(mSubMaps.indexOf(subMap));
    delete subMap;

    foreach (CompositeLayerGroup *layerGroup, mLayerGroups)
        layerGroup->setNeedsSynch(true);
}

void MapComposite::moveSubMap(MapComposite *subMap, const QPoint &pos)
{
    Q_ASSERT(mSubMaps.contains(subMap));
    subMap->setOrigin(pos);

    foreach (CompositeLayerGroup *layerGroup, mLayerGroups)
        layerGroup->setNeedsSynch(true);
}

void MapComposite::layerAdded(int index)
{
    layerRenamed(index);
}

void MapComposite::layerAboutToBeRemoved(int index)
{
    Layer *layer = mMap->layerAt(index);
    if (TileLayer *tl = layer->asTileLayer()) {
        if (tl->group()) {
            CompositeLayerGroup *oldGroup = (CompositeLayerGroup*)tl->group();
            emit layerAboutToBeRemovedFromGroup(index);
            removeLayerFromGroup(index);
            emit layerRemovedFromGroup(index, oldGroup);
        }
    }
}

void MapComposite::layerRenamed(int index)
{
    Layer *layer = mMap->layerAt(index);

    int oldLevel = layer->level();
    int newLevel;
    bool hadGroup = false;
    bool hasGroup = levelForLayer(layer, &newLevel);
    CompositeLayerGroup *oldGroup = 0;

    if (TileLayer *tl = layer->asTileLayer()) {
        oldGroup = (CompositeLayerGroup*)tl->group();
        hadGroup = oldGroup != 0;
        if (oldGroup)
            oldGroup->layerRenamed(tl);
    }

    if ((oldLevel != newLevel) || (hadGroup != hasGroup)) {
        if (hadGroup) {
            emit layerAboutToBeRemovedFromGroup(index);
            removeLayerFromGroup(index);
            emit layerRemovedFromGroup(index, oldGroup);
        }
        if (oldLevel != newLevel) {
            layer->setLevel(newLevel);
            emit layerLevelChanged(index, oldLevel);
        }
        if (hasGroup && layer->isTileLayer()) {
            addLayerToGroup(index);
            emit layerAddedToGroup(index);
        }
    }
}

void MapComposite::addLayerToGroup(int index)
{
    Layer *layer = mMap->layerAt(index);
    Q_ASSERT(layer->isTileLayer());
    Q_ASSERT(levelForLayer(layer));
    if (TileLayer *tl = layer->asTileLayer()) {
        int level = tl->level();
        if (!mLayerGroups.contains(level)) {
            mLayerGroups[level] = new CompositeLayerGroup(this, level);

            if (level < mMinLevel)
                mMinLevel = level;
            if (level > mMaxLevel)
                mMaxLevel = level;

            mSortedLayerGroups.clear();
            for (int n = mMinLevel; n <= mMaxLevel; ++n) {
                if (mLayerGroups.contains(n))
                    mSortedLayerGroups.append(mLayerGroups[n]);
            }

            emit layerGroupAdded(level);
        }
        mLayerGroups[level]->addTileLayer(tl, index);
//        tl->setGroup(mLayerGroups[level]);
    }
}

void MapComposite::removeLayerFromGroup(int index)
{
    Layer *layer = mMap->layerAt(index);
    Q_ASSERT(layer->isTileLayer());
    if (TileLayer *tl = layer->asTileLayer()) {
#if 1
        // Unused hack for MiniMapScene
        if (!mMapInfo->isBeingEdited()) {
            if (CompositeLayerGroup *layerGroup = tileLayersForLevel(tl->level()))
                layerGroup->removeTileLayer(tl);
            return;
        }
#endif
        Q_ASSERT(tl->group());
        if (CompositeLayerGroup *layerGroup = (CompositeLayerGroup*)tl->group()) {
            layerGroup->removeTileLayer(tl);
//            tl->setGroup(0);
        }
    }
}

CompositeLayerGroup *MapComposite::tileLayersForLevel(int level) const
{
    if (mLayerGroups.contains(level))
        return mLayerGroups[level];
    return 0;
}

CompositeLayerGroup *MapComposite::layerGroupForLevel(int level) const
{
    if (mLayerGroups.contains(level))
        return mLayerGroups[level];
    return 0;
}

CompositeLayerGroup *MapComposite::layerGroupForLayer(TileLayer *tl) const
{
    if (tl->group())
        return tileLayersForLevel(tl->level());
    return 0;
}

const QList<MapComposite *> MapComposite::maps()
{
    QList<MapComposite*> ret;
    ret += this;
    foreach (MapComposite *subMap, mSubMaps)
        ret += subMap->maps();
    return ret;
}

void MapComposite::setOrigin(const QPoint &origin)
{
    mPos = origin;
}

QPoint MapComposite::originRecursive() const
{
    return mPos + (mParent ? mParent->originRecursive() : QPoint());
}

int MapComposite::levelRecursive() const
{
    return mLevelOffset + (mParent ? mParent->levelRecursive() : 0);
}

QRectF MapComposite::boundingRect(MapRenderer *renderer, bool forceMapBounds) const
{
    // The reason I'm checking renderer->maxLevel() here is because when drawing
    // map images, I don't want empty levels at the top.

    QRectF bounds;
    foreach (CompositeLayerGroup *layerGroup, mLayerGroups) {
        if (levelRecursive() + layerGroup->level() > renderer->maxLevel())
            continue;
        unionSceneRects(bounds,
                        layerGroup->boundingRect(renderer),
                        bounds);
    }
    if (forceMapBounds) {
        QRect mapTileBounds(mPos, mMap->size());

        // Always include level 0, even if there are no layers or only empty/hidden
        // layers on level 0, otherwise a SubMapItem's bounds won't include the
        // fancy rectangle.
        int minLevel = levelRecursive();
        if (minLevel > renderer->maxLevel())
            minLevel = renderer->maxLevel();
        unionSceneRects(bounds,
                        renderer->boundingRect(mapTileBounds, minLevel),
                        bounds);
        // When setting the bounds of the scene, make sure the highest level is included
        // in the sceneRect() so the grid won't be cut off.
        int maxLevel = levelRecursive() + mMaxLevel;
        if (!mMapInfo->isBeingEdited()) {
            maxLevel = levelRecursive();
            foreach (CompositeLayerGroup *layerGroup, mSortedLayerGroups) {
                if (!layerGroup->bounds().isEmpty())
                    maxLevel = levelRecursive() + layerGroup->level();
            }
        }
        if (maxLevel > renderer->maxLevel())
            maxLevel = renderer->maxLevel();
        unionSceneRects(bounds,
                        renderer->boundingRect(mapTileBounds, maxLevel),
                        bounds);
    }
    return bounds;
}

void MapComposite::saveVisibility()
{
    mSavedGroupVisible = mGroupVisible;
    mGroupVisible = true; // hack

    mSavedVisible = mVisible;
    mVisible = true; // hack

    foreach (CompositeLayerGroup *layerGroup, mLayerGroups)
        layerGroup->saveVisibility();

    // FIXME: there can easily be multiple instances of the same map,
    // in which case this does unnecessary work.
    foreach (MapComposite *subMap, mSubMaps)
        subMap->saveVisibility();
}

void MapComposite::restoreVisibility()
{
    mGroupVisible = mSavedGroupVisible;
    mVisible = mSavedVisible;

    foreach (CompositeLayerGroup *layerGroup, mLayerGroups)
        layerGroup->restoreVisibility();

    foreach (MapComposite *subMap, mSubMaps)
        subMap->restoreVisibility();
}

void MapComposite::saveOpacity()
{
    foreach (CompositeLayerGroup *layerGroup, mLayerGroups)
        layerGroup->saveOpacity();

    foreach (MapComposite *subMap, mSubMaps)
        subMap->saveOpacity();
}

void MapComposite::restoreOpacity()
{
    foreach (CompositeLayerGroup *layerGroup, mLayerGroups)
        layerGroup->restoreOpacity();

    foreach (MapComposite *subMap, mSubMaps)
        subMap->restoreOpacity();
}

void MapComposite::ensureMaxLevels(int maxLevel)
{
    maxLevel = qMax(maxLevel, mMaxLevel);
    if (mMinLevel == 0 && maxLevel < mLayerGroups.size())
        return;

    for (int level = 0; level <= maxLevel; level++) {
        if (!mLayerGroups.contains(level)) {
            mLayerGroups[level] = new CompositeLayerGroup(this, level);

            if (mMinLevel > level)
                mMinLevel = level;
            if (level > mMaxLevel)
                mMaxLevel = level;

            mSortedLayerGroups.clear();
            for (int i = mMinLevel; i <= mMaxLevel; ++i)
                mSortedLayerGroups.append(mLayerGroups[i]);

            emit layerGroupAdded(level);
        }
    }
}

MapComposite::ZOrderList MapComposite::zOrder()
{
    ZOrderList result;

    QVector<int> seenLevels;
    typedef QPair<int,Layer*> LayerPair;
    QMap<CompositeLayerGroup*,QVector<LayerPair> > layersAboveLevel;
    CompositeLayerGroup *previousGroup = 0;
    int layerIndex = -1;
    foreach (Layer *layer, mMap->layers()) {
        ++layerIndex;
        int level;
        bool hasGroup = levelForLayer(layer, &level);
        if (layer->isTileLayer()) {
            // The layer may not be in a group yet during renaming.
            if (hasGroup && mLayerGroups.contains(level)) {
                if (!seenLevels.contains(level)) {
                    seenLevels += level;
                    previousGroup = mLayerGroups[level];
                }
                continue;
            }
        }
        // Handle any layers not in a TileLayerGroup.
        // Layers between the first and last in a TileLayerGroup will be displayed above that TileLayerGroup.
        // Layers before the first TileLayerGroup will be displayed below the first TileLayerGroup.
        if (previousGroup)
            layersAboveLevel[previousGroup].append(qMakePair(layerIndex, layer));
        else
            result += ZOrderItem(layer, layerIndex);
    }

    foreach (CompositeLayerGroup *layerGroup, mSortedLayerGroups) {
        result += ZOrderItem(layerGroup);
        QVector<LayerPair> layers = layersAboveLevel[layerGroup];
        foreach (LayerPair pair, layers)
            result += ZOrderItem(pair.second, pair.first);
    }

    return result;
}

// When 2 TileZeds are running, TZA has main map, TZB has lot map, and lot map
// is saved, TZA MapImageManager puts up PROGRESS dialog, causing the scene to
// be redrawn before the MapComposites have been updated.
bool MapComposite::mapAboutToChange(MapInfo *mapInfo)
{
    bool affected = false;
    if (mapInfo == mMapInfo) {
        affected = true;
    }
    foreach (MapComposite *subMap, mSubMaps) {
        if (subMap->mapAboutToChange(mapInfo)) {
            affected = true;
        }
    }

    if (affected) {
        // See CompositeLayerGroupItem::paint() for why this stops drawing.
        // FIXME: Not safe enough!
        foreach (CompositeLayerGroup *layerGroup, mLayerGroups)
            layerGroup->setNeedsSynch(true);
    }

    return affected;
}

// Called by MapDocument when MapManager tells it a map changed, either due to
// its file changing on disk or because a building's map was affected by
// changing tilesets.
// Returns true if this map or any sub-map is affected.
bool MapComposite::mapChanged(MapInfo *mapInfo)
{
    if (mapInfo == mMapInfo) {
        recreate();
        return true;
    }

    bool changed = false;
    foreach (MapComposite *subMap, mSubMaps) {
        if (subMap->mapChanged(mapInfo)) {
            if (!changed) {
                foreach (CompositeLayerGroup *layerGroup, mLayerGroups)
                    layerGroup->setNeedsSynch(true);
                changed = true;
            }
        }
    }

    return changed;
}

bool MapComposite::isTilesetUsed(Tileset *tileset)
{
    foreach (MapComposite *mc, maps()) {
        if (mc->map()->isTilesetUsed(tileset))
            return true;
    }
    return false;
}

void MapComposite::synch()
{
    foreach (CompositeLayerGroup *layerGroup, mLayerGroups) {
        if (layerGroup->needsSynch())
            layerGroup->synch();
    }
}

void MapComposite::recreate()
{
    qDeleteAll(mSubMaps);
    qDeleteAll(mLayerGroups);
    mSubMaps.clear();
    mLayerGroups.clear();
    mSortedLayerGroups.clear();

    mMap = mMapInfo->map();

    ///// FIXME: everything below here is copied from our constructor

    if (mMap->orientation() != mOrientRender) {
        Map::Orientation orientSelf = mMap->orientation();
        if (orientSelf == Map::Isometric && mOrientRender == Map::LevelIsometric)
            mOrientAdjustPos = mOrientAdjustTiles = QPoint(3, 3);
        if (orientSelf == Map::LevelIsometric && mOrientRender == Map::Isometric)
            mOrientAdjustPos = mOrientAdjustTiles = QPoint(-3, -3);
    }

    int index = 0;
    foreach (Layer *layer, mMap->layers()) {
        int level;
        if (levelForLayer(layer, &level)) {
            // FIXME: no changing of mMap should happen after it is loaded!
            layer->setLevel(level); // for ObjectGroup,ImageLayer as well

            if (TileLayer *tl = layer->asTileLayer()) {
                if (!mLayerGroups.contains(level))
                    mLayerGroups[level] = new CompositeLayerGroup(this, level);
                mLayerGroups[level]->addTileLayer(tl, index);
                if (!mMapInfo->isBeingEdited())
                    mLayerGroups[level]->setLayerVisibility(tl, !layer->name().contains(QLatin1String("NoRender")));
            }
        }
        ++index;
    }

    // Load lots, but only if this is not the map being edited (that is handled
    // by the LotManager).
    if (!mMapInfo->isBeingEdited()) {
        foreach (ObjectGroup *objectGroup, mMap->objectGroups()) {
            foreach (MapObject *object, objectGroup->objects()) {
                if (object->name() == QLatin1String("lot") && !object->type().isEmpty()) {
                    // FIXME: if this sub-map is converted from LevelIsometric to Isometric,
                    // then any sub-maps of its own will lose their level offsets.
                    MapInfo *subMapInfo = MapManager::instance()->loadMap(object->type(),
                                                                          QFileInfo(mMapInfo->path()).absolutePath());
                    if (!subMapInfo) {
                        qDebug() << "failed to find sub-map" << object->type() << "inside map" << mMapInfo->path();
#if 0 // FIXME: attempt to load this if mapsDirectory changes
                        subMapInfo = MapManager::instance()->getPlaceholderMap(object->type(), mMap->orientation(),
                                                                               32, 32); // FIXME: calculate map size
#endif
                    }
                    if (subMapInfo) {
                        int levelOffset;
                        (void) levelForLayer(objectGroup, &levelOffset);
                        addMap(subMapInfo, object->position().toPoint()
                               + mOrientAdjustPos * levelOffset,
                               levelOffset);
                    }
                }
            }
        }
    }

    mMinLevel = 10000;
    mMaxLevel = 0;

    foreach (CompositeLayerGroup *layerGroup, mLayerGroups) {
        if (!mMapInfo->isBeingEdited())
            layerGroup->synch();
        if (layerGroup->level() < mMinLevel)
            mMinLevel = layerGroup->level();
        if (layerGroup->level() > mMaxLevel)
            mMaxLevel = layerGroup->level();
    }

    if (mMinLevel == 10000)
        mMinLevel = 0;

    for (int level = mMinLevel; level <= mMaxLevel; ++level) {
        if (!mLayerGroups.contains(level))
            mLayerGroups[level] = new CompositeLayerGroup(this, level);
        mSortedLayerGroups.append(mLayerGroups[level]);
    }

    /////

    if (mParent)
        mParent->moveSubMap(this, origin());
}

MapComposite *MapComposite::root()
{
    MapComposite *root = this;
    while (root->parent())
        root = root->parent();
    return root;
}

QStringList MapComposite::getMapFileNames() const
{
    QStringList result;

    result += mMapInfo->path();
    foreach (MapComposite *subMap, mSubMaps)
        foreach (QString path, subMap->getMapFileNames())
            if (!result.contains(path))
                result += path;

    return result;
}
