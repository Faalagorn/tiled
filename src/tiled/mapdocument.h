/*
 * mapdocument.h
 * Copyright 2008-2010, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 * Copyright 2009, Jeff Bland <jeff@teamphobic.com>
 * Copyright 2011, Stefan Beller <stefanbeller@googlemail.com
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

#ifndef MAPDOCUMENT_H
#define MAPDOCUMENT_H

#include <QList>
#include <QObject>
#include <QRegion>
#include <QString>

#include "layer.h"
#ifdef ZOMBOID
#include "map.h" // for MapRands
#endif

class QPoint;
class QRect;
class QSize;
class QUndoStack;

#ifdef ZOMBOID
class CompositeLayerGroup;
class MapComposite;
class MapInfo;
class WorldCell;
class WorldCellLot;
#endif

namespace Tiled {

class Map;
class MapObject;
class MapRenderer;
class Tileset;
#ifdef ZOMBOID
class Tile;
#endif

namespace Internal {

class LayerModel;
class TileSelectionModel;
class MapObjectModel;

/**
 * Represents an editable map. The purpose of this class is to make sure that
 * any editing operations will cause the appropriate signals to be emitted, in
 * order to allow the GUI to update accordingly.
 *
 * At the moment the map document provides the layer model, keeps track of the
 * the currently selected layer and provides an API for adding and removing
 * map objects. It also owns the QUndoStack.
 */
class MapDocument : public QObject
{
    Q_OBJECT

public:
    /**
     * Constructs a map document around the given map. The map document takes
     * ownership of the map.
     */
    MapDocument(Map *map, const QString &fileName = QString());

    /**
     * Destructor.
     */
    ~MapDocument();

    /**
     * Saves the map to its current file name. Returns whether or not the file
     * was saved successfully. If not, <i>error</i> will be set to the error
     * message if it is not 0.
     */
    bool save(QString *error = 0);

    /**
     * Saves the map to the file at \a fileName. Returns whether or not the
     * file was saved successfully. If not, <i>error</i> will be set to the
     * error message if it is not 0.
     *
     * If the save was succesful, the file name of this document will be set
     * to \a fileName.
     */
    bool save(const QString &fileName, QString *error = 0);

    QString fileName() const { return mFileName; }
    void setFileName(const QString &fileName);

    QString displayName() const;

    bool isModified() const;

    /**
     * Returns the map instance. Be aware that directly modifying the map will
     * not allow the GUI to update itself appropriately.
     */
    Map *map() const { return mMap; }

#ifdef ZOMBOID
    MapComposite *mapComposite() const { return mMapComposite; }
#endif

    /**
     * Sets the current layer to the given index.
     */
    void setCurrentLayerIndex(int levelIndex, int layerIndex);

    /**
     * Returns the index of the currently selected layer. Returns -1 if no
     * layer is currently selected.
     */
    int currentLayerIndex() const { return mCurrentLayerIndex; }

    /**
     * Returns the currently selected layer, or 0 if no layer is currently
     * selected.
     */
    Layer *currentLayer() const;

#ifdef ZOMBOID
    void setCurrentLevelIndex(int index);

    int currentLevelIndex() const
    {
        return mCurrentLevelIndex;
    }

    MapLevel *currentMapLevel() const;

    void setCurrentLevelAndLayer(int levelIndex, int layerIndex);


    /* For the visibility slider in the LayerDock. */
    void setMaxVisibleLayer(int index) { mMaxVisibleLayer = index; }
    int maxVisibleLayer() const { return mMaxVisibleLayer; }
#endif

    /**
     * Resize this map to the given \a size, while at the same time shifting
     * the contents by \a offset.
     */
    void resizeMap(const QSize &size, const QPoint &offset);

    /**
     * Offsets the layers at \a layerIndexes by \a offset, within \a bounds,
     * and optionally wraps on the X or Y axis.
     */
    void offsetMap(const QList<int> &layerIndexes,
                   const QPoint &offset,
                   const QRect &bounds,
                   bool wrapX, bool wrapY);

    void addLayer(Layer::Type layerType);
    void duplicateLayer();
    void mergeLayerDown();
    void moveLayerUp(int levelIndex, int layerIndex);
    void moveLayerDown(int levelIndex, int layerIndex);
    void removeLayer(int levelIndex, int layerIndex);
    void toggleOtherLayers(int levelIndex, int layerIndex);
#ifdef ZOMBOID
    void setLayerVisible(int levelIndex, int layerIndex, bool visible);
#endif

    void insertTileset(int index, Tileset *tileset);
    void removeTilesetAt(int index);
    void moveTileset(int from, int to);
    void setTilesetFileName(Tileset *tileset, const QString &fileName);
    void setTilesetName(Tileset *tileset, const QString &name);

    /**
     * Returns the layer model. Can be used to modify the layer stack of the
     * map, and to display the layer stack in a view.
     */
    LayerModel *layerModel() const { return mLayerModel; }

    MapObjectModel *mapObjectModel() const { return mMapObjectModel; }
#ifdef ZOMBOID
    void setLayerGroupVisibility(CompositeLayerGroup *layerGroup, bool visible);
#endif

    /**
     * Returns the map renderer.
     */
    MapRenderer *renderer() const { return mRenderer; }

    /**
     * Returns the undo stack of this map document. Should be used to push any
     * commands on that modify the map.
     */
    QUndoStack *undoStack() const { return mUndoStack; }

    /**
     * Returns the selected area of tiles.
     */
    const QRegion &tileSelection() const { return mTileSelection; }

    /**
     * Sets the selected area of tiles.
     */
    void setTileSelection(const QRegion &selection);

#ifdef ZOMBOID
    const QRegion &bmpSelection() const;
    void setBmpSelection(const QRegion &selection);

    void paintBmp(int bmpIndex, int px, int py, const QImage &source,
                  const QRegion &paintRgn);
    QImage swapBmpImage(int bmpIndex, const QImage &image);
    void emitBmpPainted(int bmpIndex, const QRegion &rgn)
    { emit bmpPainted(bmpIndex, rgn); }
    MapRands swapBmpRands(int bmpIndex, const MapRands &rands);

    void setBmpAliases(const QList<BmpAlias*> &aliases);
    void setBmpRules(const QString &fileName, const QList<BmpRule*> &rules);
    void setBmpBlends(const QString &fileName, const QList<BmpBlend*> &blends);

    MapNoBlend paintNoBlend(MapNoBlend *noBlend, const MapNoBlend &other, const QRegion &rgn);
    void swapNoBlend(MapNoBlend *noBlend, MapNoBlend *other);
#endif // ZOMBOID

    /**
     * Returns the list of selected objects.
     */
    const QList<MapObject*> &selectedObjects() const
    { return mSelectedObjects; }

    /**
     * Sets the list of selected objects, emitting the selectedObjectsChanged
     * signal.
     */
    void setSelectedObjects(const QList<MapObject*> &selectedObjects);

    /**
     * Makes sure the all tilesets which are used at the given \a map will be
     * present in the map document.
     *
     * To reach the aim, all similar tilesets will be replaced by the version
     * in the current map document and all missing tilesets will be added to
     * the current map document.
     *
     * \warning This method assumes that the tilesets in \a map are managed by
     *          the TilesetManager!
     */
    void unifyTilesets(Map *map);

    void emitMapChanged();

    /**
     * Emits the region changed signal for the specified region. The region
     * should be in tile coordinates. This method is used by the TilePainter.
     */
#ifdef ZOMBOID
    void emitRegionChanged(const QRegion &region, Layer *layer);
#else
    void emitRegionChanged(const QRegion &region);
#endif

    /**
     * Emits the region edited signal for the specified region and tile layer.
     * The region should be in tile coordinates. This should be called from
     * all map document changing classes which are triggered by user input.
     */
    void emitRegionEdited(const QRegion &region, Layer *layer);

#ifdef ZOMBOID
    /**
      * emitRegionChanged -- redraw part of the map
      * emitRegionEdited -- changes due to user edits, not undo/redo
      * emitRegionAltered -- changes due to user edit plus undo/redo
      * This is to support the MiniMap.
      */
    void emitRegionAltered(const QRegion &region, Layer *layer);

    void setTileLayerName(Tile *tile, const QString &name);

    void setBlendEdgesEverywhere(bool enabled);
#endif

    /**
     * Emits the editLayerNameRequested signal, to get renamed.
     */
    inline void emitEditLayerNameRequested()
    { emit editLayerNameRequested(); }

signals:
    void fileNameChanged();
    void modifiedChanged();

    /**
     * Emitted when the selected tile region changes. Sends the currently
     * selected region and the previously selected region.
     */
    void tileSelectionChanged(const QRegion &newSelection,
                              const QRegion &oldSelection);

#ifdef ZOMBOID
    void bmpSelectionChanged(const QRegion &newSelection,
                             const QRegion &oldSelection);
#endif // ZOMBOID

    /**
     * Emitted when the list of selected objects changes.
     */
    void selectedObjectsChanged();

    /**
     * Emitted when the map size or its tile size changes.
     */
    void mapChanged();

    void layerAdded(int z, int index);
    void layerAboutToBeRemoved(int z, int index);
    void layerRenamed(int z, int index);
    void layerRemoved(int z, int index);
    void layerChanged(int z, int index);

#ifdef ZOMBOID
    void layerGroupAdded(int level);
    void layerGroupVisibilityChanged(CompositeLayerGroup *layerGroup);

    void layerAddedToGroup(int z, int index);
    void layerAboutToBeRemovedFromGroup(int z, int index);
    void layerRemovedFromGroup(int z, int index, CompositeLayerGroup *oldGroup);

    void layerLevelChanged(int z, int index, int oldLevel);
#endif

    /**
     * Emitted after a new layer was added and the name should be edited.
     * Applies to the current layer.
     */
    void editLayerNameRequested();

    /**
     * Emitted when the current layer index changes.
     */
    void currentLayerIndexChanged(int levelIndex, int layerIndex);

    /**
     * Emitted when a certain region of the map changes. The region is given in
     * tile coordinates.
     */
#ifdef ZOMBOID
    void regionChanged(const QRegion &region, Layer *layer);
#else
    void regionChanged(const QRegion &region);
#endif

    /**
     * Emitted when a certain region of the map was edited by user input.
     * The region is given in tile coordinates.
     * If multiple layers have been edited, multiple signals will be emitted.
     */
    void regionEdited(const QRegion &region, Layer *layer);

#ifdef ZOMBOID
    /**
      * regionChanged -- redraw part of the map
      * regionEdited -- changes due to user edits, not undo/redo
      * regionAltered -- changes due to user edit plus undo/redo
      * This is to support the MiniMap.
      */
    void regionAltered(const QRegion &region, Layer *layer);
#endif

    void tilesetAdded(int index, Tileset *tileset);
    void tilesetRemoved(Tileset *tileset);
    void tilesetMoved(int from, int to);
    void tilesetFileNameChanged(Tileset *tileset);
    void tilesetNameChanged(Tileset *tileset);
#ifdef ZOMBOID
    void tileLayerNameChanged(Tile *tile);
#endif

    void objectsAdded(const QList<MapObject*> &objects);
    void objectsAboutToBeRemoved(const QList<MapObject*> &objects);
    void objectsRemoved(ObjectGroup *objectGroup, const QList<MapObject*> &objects);
    void objectsChanged(const QList<MapObject*> &objects);

#ifdef ZOMBOID
    void mapCompositeChanged();

    void bmpPainted(int bmpIndex, const QRegion &region);
    void bmpAliasesChanged();
    void bmpRulesChanged();
    void bmpBlendsChanged();
    void bmpBlendEdgesEverywhereChanged();

    void noBlendPainted(MapNoBlend *noBlend, const QRegion &rgn);
#endif

private slots:
    void onObjectsRemoved(ObjectGroup *objectGroup, const QList<MapObject*> &objects);

    void onLayerAdded(int z, int index);
    void onLayerAboutToBeRemoved(int z, int index);
    void onLayerRemoved(int z, int index);
#ifdef ZOMBOID
    void onLayerRenamed(int z, int index);

    void onMapAboutToChange(MapInfo *mapInfo);
    void onMapChanged(MapInfo *mapInfo);

    void bmpBlenderRegionAltered(const QRegion &region);

    void mapLoaded(MapInfo *info);
    void mapFailedToLoad(MapInfo *info);

    void handleMapsLoadedNow();

    void beforeWorldChanged(const QString &fileName);
    void afterWorldChanged(const QString &fileName);

    void initAdjacentMaps();
#endif

private:
    void deselectObjects(const QList<MapObject*> &objects);

    QString mFileName;
    Map *mMap;
    LayerModel *mLayerModel;
    QRegion mTileSelection;
    QList<MapObject*> mSelectedObjects;
    MapRenderer *mRenderer;
    int mCurrentLevelIndex;
    int mCurrentLayerIndex;
    MapObjectModel *mMapObjectModel;
#ifdef ZOMBOID
    int mMaxVisibleLayer;
    MapComposite *mMapComposite;
#ifdef SEPARATE_BMP_SELECTION
    QRegion mBmpSelection;
#endif
    WorldCell *mWorldCell;

    struct AdjacentMap {
        AdjacentMap(int x, int y, MapInfo *info) :
            pos(x, y),
            info(info)
        {}
        QPoint pos;
        MapInfo *info;
    };
    QMultiMap<MapInfo*,AdjacentMap> mAdjacentMapsLoading;

    struct LoadingSubMap {
        LoadingSubMap(WorldCellLot *lot, MapInfo *mapInfo) :
            lot(lot),
            mapInfo(mapInfo)
        {}
        WorldCellLot *lot;
        MapInfo *mapInfo;
    };
    QMultiMap<MapInfo*,LoadingSubMap> mAdjacentSubMapsLoading;

    QList<MapInfo*> mMapsLoaded;
#endif // ZOMBOID
    QUndoStack *mUndoStack;
};

} // namespace Internal
} // namespace Tiled

#endif // MAPDOCUMENT_H
