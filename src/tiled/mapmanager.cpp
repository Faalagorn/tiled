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

#include "mapmanager.h"

#include "mapcomposite.h"
#include "preferences.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "zprogress.h"

#include "map.h"
#include "mapreader.h"
#include "mapobject.h"
#include "objectgroup.h"
#include "tile.h"
#include "tilelayer.h"
#include "tileset.h"

#include "qtlockedfile.h"
using namespace SharedTools;

#include "BuildingEditor/buildingreader.h"
#include "BuildingEditor/buildingmap.h"
#include "BuildingEditor/buildingobjects.h"
#include "BuildingEditor/buildingtiles.h"
#include "BuildingEditor/furnituregroups.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef QT_NO_DEBUG
inline QNoDebug noise() { return QNoDebug(); }
#else
inline QDebug noise() { return QDebug(QtDebugMsg); }
#endif

using namespace Tiled;
using namespace Tiled::Internal;
using namespace BuildingEditor;

MapManager *MapManager::mInstance = NULL;

MapManager *MapManager::instance()
{
    if (!mInstance)
        mInstance = new MapManager;
    return mInstance;
}

void MapManager::deleteInstance()
{
    delete mInstance;
    mInstance = 0;
}

MapManager::MapManager() :
    mFileSystemWatcher(new FileSystemWatcher(this)),
    mNextThreadForJob(0)
#if 0
    mReferenceEpoch(0)
#endif
{
    connect(mFileSystemWatcher, SIGNAL(fileChanged(QString)),
            SLOT(fileChanged(QString)));

    mChangedFilesTimer.setInterval(500);
    mChangedFilesTimer.setSingleShot(true);
    connect(&mChangedFilesTimer, SIGNAL(timeout()),
            SLOT(fileChangedTimeout()));

    qRegisterMetaType<MapInfo*>("MapInfo*");

    mMapReaderThread.resize(4);
    mMapReaderWorker.resize(mMapReaderThread.size());
    for (int i = 0; i < mMapReaderThread.size(); i++) {
        mMapReaderThread[i] = new InterruptibleThread;
        mMapReaderWorker[i] = new MapReaderWorker(mMapReaderThread[i]->var());
        mMapReaderWorker[i]->moveToThread(mMapReaderThread[i]);
        connect(mMapReaderWorker[i], SIGNAL(loaded(Map*,MapInfo*)),
                SLOT(mapLoadedByThread(Map*,MapInfo*)));
        connect(mMapReaderWorker[i], SIGNAL(loaded(Building*,MapInfo*)),
                SLOT(buildingLoadedByThread(Building*,MapInfo*)));
        connect(mMapReaderWorker[i], SIGNAL(failedToLoad(QString,MapInfo*)),
                SLOT(failedToLoadByThread(QString,MapInfo*)));
        mMapReaderThread[i]->start();
    }

    connect(TileMetaInfoMgr::instance(), SIGNAL(tilesetAdded(Tiled::Tileset*)),
            SLOT(metaTilesetAdded(Tiled::Tileset*)));
    connect(TileMetaInfoMgr::instance(), SIGNAL(tilesetRemoved(Tiled::Tileset*)),
            SLOT(metaTilesetRemoved(Tiled::Tileset*)));
}

MapManager::~MapManager()
{
    for (int i = 0; i < mMapReaderThread.size(); i++) {
        mMapReaderThread[i]->interrupt(); // stop the long-running task
        mMapReaderThread[i]->quit(); // exit the event loop
        mMapReaderThread[i]->wait(); // wait for thread to terminate
        delete mMapReaderThread[i];
        delete mMapReaderWorker[i];
    }

    TilesetManager *tilesetManager = TilesetManager::instance();

    const QMap<QString,MapInfo*>::const_iterator end = mMapInfo.constEnd();
    QMap<QString,MapInfo*>::const_iterator it = mMapInfo.constBegin();
    while (it != end) {
        MapInfo *mapInfo = it.value();
        if (Map *map = mapInfo->map()) {
            tilesetManager->removeReferences(map->tilesets());
            delete map;
        }
        ++it;
    }
}

// mapName could be "Lot_Foo", "../Lot_Foo", "C:/maptools/Lot_Foo" with/without ".tmx"
QString MapManager::pathForMap(const QString &mapName, const QString &relativeTo)
{
    QString mapFilePath = mapName;

    if (QDir::isRelativePath(mapName)) {
        Q_ASSERT(!relativeTo.isEmpty());
        Q_ASSERT(!QDir::isRelativePath(relativeTo));
        mapFilePath = relativeTo + QLatin1Char('/') + mapName;
    }

    if (!mapFilePath.endsWith(QLatin1String(".tmx")) &&
            !mapFilePath.endsWith(QLatin1String(".tbx")))
        mapFilePath += QLatin1String(".tmx");

    QFileInfo fileInfo(mapFilePath);
    if (fileInfo.exists())
        return fileInfo.canonicalFilePath();

    return QString();
}

class EditorMapReader : public MapReader
{
protected:
    /**
     * Overridden to make sure the resolved reference is canonical.
     */
    QString resolveReference(const QString &reference, const QString &mapPath)
    {
        QString resolved = MapReader::resolveReference(reference, mapPath);
        QString canonical = QFileInfo(resolved).canonicalFilePath();

        // Make sure that we're not returning an empty string when the file is
        // not found.
        return canonical.isEmpty() ? resolved : canonical;
    }
};

MapInfo *MapManager::loadMap(const QString &mapName, const QString &relativeTo, bool asynch)
{
    QString mapFilePath = pathForMap(mapName, relativeTo);
    if (mapFilePath.isEmpty()) {
        mError = tr("A map file couldn't be found!\n%1").arg(mapName);
        return 0;
    }

    if (mMapInfo.contains(mapFilePath) && mMapInfo[mapFilePath]->map()) {
        return mMapInfo[mapFilePath];
    }

    QFileInfo fileInfoMap(mapFilePath);

    MapInfo *mapInfo = this->mapInfo(mapFilePath);
    if (!mapInfo)
        return 0;
    if (mapInfo->mLoading)
        return mapInfo;
    mapInfo->mLoading = true;
    QMetaObject::invokeMethod(mMapReaderWorker[mNextThreadForJob], "addJob",
                              Qt::QueuedConnection, Q_ARG(MapInfo*,mapInfo));
    mNextThreadForJob = (mNextThreadForJob + 1) % mMapReaderThread.size();

    if (asynch)
        return mapInfo;

    PROGRESS progress(tr("Reading %1").arg(fileInfoMap.completeBaseName()));
    while (mapInfo->mLoading) {
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    if (mapInfo->map())
        return mapInfo;
    return 0;

}

MapInfo *MapManager::newFromMap(Map *map, const QString &mapFilePath)
{
    MapInfo *info = new MapInfo(map->orientation(),
                                map->width(), map->height(),
                                map->tileWidth(), map->tileHeight());
    info->mMap = map;
    info->mBeingEdited = true;

    if (!mapFilePath.isEmpty()) {
        Q_ASSERT(!QFileInfo(mapFilePath).isRelative());
        info->setFilePath(mapFilePath);
//        mMapInfo[mapFilePath] = info;
    }

    // Not adding to the mMapInfo table because this function is for maps being edited, not lots

    return info;
}

#include <QCoreApplication>
#include <QXmlStreamReader>

class MapInfoReader
{
    Q_DECLARE_TR_FUNCTIONS(MapInfoReader)

public:
    bool openFile(QtLockedFile *file)
    {
        if (!file->exists()) {
            mError = tr("File not found: %1").arg(file->fileName());
            return false;
        }
        if (!file->open(QFile::ReadOnly | QFile::Text)) {
            mError = tr("Unable to read file: %1").arg(file->fileName());
            return false;
        }
        if (!file->lock(QtLockedFile::ReadLock)) {
            mError = tr("Unable to lock file for reading: %1").arg(file->fileName());
            return false;
        }

        return true;
    }

    MapInfo *readMap(const QString &mapFilePath)
    {
        QtLockedFile file(mapFilePath);
        if (!openFile(&file))
            return NULL;

        if (mapFilePath.endsWith(QLatin1String(".tbx")))
            return readBuilding(&file, QFileInfo(mapFilePath).absolutePath());

        return readMap(&file, QFileInfo(mapFilePath).absolutePath());
    }

    MapInfo *readMap(QIODevice *device, const QString &path)
    {
        Q_UNUSED(path)

        mError.clear();
        mMapInfo = NULL;

#if 1
        QByteArray data = device->read(1024);
        xml.addData(data);
#else
        xml.setDevice(device);
#endif

        if (xml.readNextStartElement() && xml.name() == "map") {
            mMapInfo = readMap();
        } else {
            xml.raiseError(tr("Not a map file."));
        }

        return mMapInfo;
    }

    MapInfo *readMap()
    {
        Q_ASSERT(xml.isStartElement() && xml.name() == "map");

        const QXmlStreamAttributes atts = xml.attributes();
        const int mapWidth =
                atts.value(QLatin1String("width")).toString().toInt();
        const int mapHeight =
                atts.value(QLatin1String("height")).toString().toInt();
        const int tileWidth =
                atts.value(QLatin1String("tilewidth")).toString().toInt();
        const int tileHeight =
                atts.value(QLatin1String("tileheight")).toString().toInt();

        const QString orientationString =
                atts.value(QLatin1String("orientation")).toString();
        const Map::Orientation orientation =
                orientationFromString(orientationString);
        if (orientation == Map::Unknown) {
            xml.raiseError(tr("Unsupported map orientation: \"%1\"")
                           .arg(orientationString));
        }

        mMapInfo = new MapInfo(orientation, mapWidth, mapHeight, tileWidth, tileHeight);

        return mMapInfo;
    }

    MapInfo *readBuilding(QIODevice *device, const QString &path)
    {
        Q_UNUSED(path)

        mError.clear();
        mMapInfo = NULL;

#if 1
        QByteArray data = device->read(1024);
        xml.addData(data);
#else
        xml.setDevice(device);
#endif

        if (xml.readNextStartElement() && xml.name() == "building") {
            mMapInfo = readBuilding();
        } else {
            xml.raiseError(tr("Not a building file."));
        }

        return mMapInfo;
    }

    MapInfo *readBuilding()
    {
        Q_ASSERT(xml.isStartElement() && xml.name() == "building");

        const QXmlStreamAttributes atts = xml.attributes();
        const int mapWidth =
                atts.value(QLatin1String("width")).toString().toInt();
        const int mapHeight =
                atts.value(QLatin1String("height")).toString().toInt();
        const int tileWidth = 64;
        const int tileHeight = 32;

        const Map::Orientation orient = static_cast<Map::Orientation>(BuildingEditor::BuildingMap::defaultOrientation());

#if 1
        // FIXME: If Map::Isometric orientation is used then we must know the number
        // of floors to determine the correct map size.  That would require parsing
        // the whole .tbx file.
        Q_ASSERT(orient == Map::LevelIsometric);
        int extra = 1;
#else
        int maxLevel = building->floorCount() - 1;
        int extraForWalls = 1;
        int extra = (orient == Map::LevelIsometric)
                ? extraForWalls : maxLevel * 3 + extraForWalls;
#endif

        mMapInfo = new MapInfo(orient, mapWidth + extra, mapHeight + extra,
                               tileWidth, tileHeight);

        return mMapInfo;
    }

    QXmlStreamReader xml;
    MapInfo *mMapInfo;
    QString mError;
};

MapInfo *MapManager::mapInfo(const QString &mapFilePath)
{
    if (mMapInfo.contains(mapFilePath))
       return mMapInfo[mapFilePath];

    QFileInfo fileInfo(mapFilePath);
    if (!fileInfo.exists())
        return NULL;

    MapInfoReader reader;
    MapInfo *mapInfo = reader.readMap(mapFilePath);
    if (!mapInfo)
        return NULL;

    noise() << "read map info for" << mapFilePath;
    mapInfo->setFilePath(mapFilePath);

    mMapInfo[mapFilePath] = mapInfo;
    mFileSystemWatcher->addPath(mapFilePath);

    return mapInfo;
}

// FIXME: this map is shared by any CellDocument whose cell has no map specified.
// Adding sub-maps to a cell may add new layers to this shared map.
// If that happens, all CellScenes using this map will need to be updated.
MapInfo *MapManager::getEmptyMap()
{
    QString mapFilePath(QLatin1String("<empty>"));
    if (mMapInfo.contains(mapFilePath))
        return mMapInfo[mapFilePath];

    MapInfo *mapInfo = new MapInfo(Map::LevelIsometric, 300, 300, 64, 32);
    Map *map = new Map(mapInfo->orientation(),
                       mapInfo->width(), mapInfo->height(),
                       mapInfo->tileWidth(), mapInfo->tileHeight());

    for (int level = 0; level < 1; level++) {
        TileLayer *tl = new TileLayer(tr("%1_Layer").arg(level), 0, 0, 300, 300);
        tl->setLevel(level);
        map->addLayer(tl);
    }

    mapInfo->mMap = map;
    mapInfo->setFilePath(mapFilePath);
    mMapInfo[mapFilePath] = mapInfo;
#if 0
    addReferenceToMap(mapInfo);
#endif
    return mapInfo;
}

MapInfo *MapManager::getPlaceholderMap(const QString &mapName, int width, int height)
{
    QString mapFilePath(mapName);
    if (mMapInfo.contains(mapFilePath))
        return mMapInfo[mapFilePath];

    if (width <= 0) width = 32;
    if (height <= 0) height = 32;
    MapInfo *mapInfo = new MapInfo(Map::LevelIsometric, width, height, 64, 32);
    Map *map = new Map(mapInfo->orientation(), mapInfo->width(), mapInfo->height(),
                       mapInfo->tileWidth(), mapInfo->tileHeight());

    for (int level = 0; level < 1; level++) {
        TileLayer *tl = new TileLayer(tr("%1_Layer").arg(level), 0, 0, 300, 300);
        tl->setLevel(level);
        map->addLayer(tl);
    }

    mapInfo->mMap = map;
    mapInfo->setFilePath(mapFilePath);
    mapInfo->mPlaceholder = true;
    mMapInfo[mapFilePath] = mapInfo;

    return mapInfo;
}

void MapManager::mapParametersChanged(MapInfo *mapInfo)
{
    Map *map = mapInfo->map();
    Q_ASSERT(map);
    mapInfo->mHeight = map->height();
    mapInfo->mWidth = map->width();
    mapInfo->mTileWidth = map->tileWidth();
    mapInfo->mTileHeight = map->tileHeight();
}

#if 0
void MapManager::addReferenceToMap(MapInfo *mapInfo)
{
    Q_ASSERT(mapInfo->mMap != 0);
    if (mapInfo->mMap) {
        mapInfo->mMapRefCount++;
        mapInfo->mReferenceEpoch = ++mReferenceEpoch;
        noise() << "MapManager refCount++ =" << mapInfo->mMapRefCount << mapInfo->mFilePath;
    }
}

void MapManager::removeReferenceToMap(MapInfo *mapInfo)
{
    Q_ASSERT(mapInfo->mMap != 0);
    if (mapInfo->mMap) {
        Q_ASSERT(mapInfo->mMapRefCount > 0);
        mapInfo->mMapRefCount--;
        noise() << "MapManager refCount-- =" << mapInfo->mMapRefCount << mapInfo->mFilePath;
        purgeUnreferencedMaps();
    }
}

void MapManager::purgeUnreferencedMaps()
{
    int unpurged = 0;
    foreach (MapInfo *mapInfo, mMapInfo) {
        if (mapInfo->mMap && mapInfo->mMapRefCount <= 0 &&
                (mapInfo->mReferenceEpoch <= mReferenceEpoch - 50)) {
            qDebug() << "MapManager purging" << mapInfo->mFilePath;
            TilesetManager *tilesetMgr = TilesetManager::instance();
            tilesetMgr->removeReferences(mapInfo->mMap->tilesets());
            delete mapInfo->mMap;
            mapInfo->mMap = 0;
        }
        else if (mapInfo->mMap && mapInfo->mMapRefCount <= 0)
            unpurged++;
    }
    if (unpurged) noise() << "MapManager unpurged =" << unpurged;
}

void MapManager::newMapFileCreated(const QString &path)
{
    // If a cell view is open with a placeholder map and that map now exists,
    // read the new map and allow the cell-scene to update itself.
    // This code is 90% the same as fileChangedTimeout().
    foreach (MapInfo *mapInfo, mMapInfo) {
        if (!mapInfo->mPlaceholder || !mapInfo->mMap)
            continue;
        if (QFileInfo(mapInfo->path()) != QFileInfo(path))
            continue;
        mFileSystemWatcher->addPath(mapInfo->path()); // FIXME: make canonical?
        fileChanged(mapInfo->path());
    }

    emit mapFileCreated(path);
}
#endif

Map *MapManager::convertOrientation(Map *map, Tiled::Map::Orientation orient)
{
    Map::Orientation orient0 = map->orientation();
    Map::Orientation orient1 = orient;

    if (orient0 != orient1) {
        Map *newMap = map->clone();
        newMap->setOrientation(orient);
        QPoint offset(3, 3);
        if (orient0 == Map::Isometric && orient1 == Map::LevelIsometric) {
            foreach (Layer *layer, newMap->layers()) {
                int level;
                if (MapComposite::levelForLayer(layer, &level) && level > 0)
                    layer->offset(offset * level, layer->bounds(), false, false);
            }
        }
        if (orient0 == Map::LevelIsometric && orient1 == Map::Isometric) {
            int level, maxLevel = 0;
            foreach (Layer *layer, map->layers())
                if (MapComposite::levelForLayer(layer, &level))
                    maxLevel = qMax(maxLevel, level);
            newMap->setWidth(map->width() + maxLevel * 3);
            newMap->setHeight(map->height() + maxLevel * 3);
            foreach (Layer *layer, newMap->layers()) {
                MapComposite::levelForLayer(layer, &level);
                layer->resize(newMap->size(), offset * (maxLevel - level));
            }
        }
        TilesetManager *tilesetManager = TilesetManager::instance();
        tilesetManager->addReferences(newMap->tilesets());
        map = newMap;
    }

    return map;
}

void MapManager::fileChanged(const QString &path)
{
    mChangedFiles.insert(path);
    mChangedFilesTimer.start();
}

void MapManager::fileChangedTimeout()
{
#if 0
    PROGRESS progress(tr("Examining changed maps..."));
#endif

    foreach (const QString &path, mChangedFiles) {
        if (mMapInfo.contains(path)) {
            noise() << "MapManager::fileChanged" << path;
            mFileSystemWatcher->removePath(path);
            QFileInfo info(path);
            if (info.exists()) {
                mFileSystemWatcher->addPath(path);
                MapInfo *mapInfo = mMapInfo[path];
                if (mapInfo->map()) {
                    Q_ASSERT(!mapInfo->isBeingEdited());
                    if (!mapInfo->isLoading()) {
                        mapInfo->mLoading = true; // FIXME: seems weird to change this for a loaded map
                        QMetaObject::invokeMethod(mMapReaderWorker[mNextThreadForJob], "addJob",
                                                  Qt::QueuedConnection, Q_ARG(MapInfo*,mapInfo));
                        mNextThreadForJob = (mNextThreadForJob + 1) % mMapReaderThread.size();
                    }
                }
                emit mapFileChanged(mapInfo);
            }
        }
    }

    mChangedFiles.clear();
}

void MapManager::metaTilesetAdded(Tileset *tileset)
{
    Q_UNUSED(tileset)
    foreach (MapInfo *mapInfo, mMapInfo) {
        if (mapInfo->map() && mapInfo->path().endsWith(QLatin1String(".tbx")))
            fileChanged(mapInfo->path());
    }
}

void MapManager::metaTilesetRemoved(Tileset *tileset)
{
    Q_UNUSED(tileset)
    foreach (MapInfo *mapInfo, mMapInfo) {
        if (mapInfo->map() && mapInfo->path().endsWith(QLatin1String(".tbx")))
            fileChanged(mapInfo->path());
    }
}

void MapManager::mapLoadedByThread(MapManager::Map *map, MapInfo *mapInfo)
{
    Tile *missingTile = TilesetManager::instance()->missingTile();
    foreach (Tileset *tileset, map->missingTilesets()) {
        if (tileset->tileHeight() == 128 && tileset->tileWidth() == 64) {
            // Replace the all-red image with something nicer.
            for (int i = 0; i < tileset->tileCount(); i++)
                tileset->tileAt(i)->setImage(missingTile->image());
        }
    }
    TilesetManager::instance()->addReferences(map->tilesets());

    bool replace = mapInfo->mMap != 0;
    if (replace) {
        Q_ASSERT(!mapInfo->isBeingEdited());
        emit mapAboutToChange(mapInfo);
        TilesetManager *tilesetMgr = TilesetManager::instance();
        tilesetMgr->removeReferences(mapInfo->mMap->tilesets());
        delete mapInfo->mMap;
    }

    mapInfo->mMap = map;
    mapInfo->mPlaceholder = false;
    mapInfo->mLoading = false;

    if (replace)
        emit mapChanged(mapInfo);

#if 0
    // The reference count is zero, but prevent it being immediately purged.
    // FIXME: add a reference and let the caller deal with it.
    mapInfo->mReferenceEpoch = mReferenceEpoch;
#endif
    emit mapLoaded(mapInfo);
}

void MapManager::buildingLoadedByThread(Building *building, MapInfo *mapInfo)
{
    BuildingReader reader;
    reader.fix(building);

    BuildingMap bmap(building);
    Map *map = bmap.mergedMap();

    QSet<Tileset*> usedTilesets;
    foreach (TileLayer *tl, map->tileLayers())
        usedTilesets += tl->usedTilesets();
    usedTilesets.remove(TilesetManager::instance()->missingTileset());

    TileMetaInfoMgr::instance()->loadTilesets(usedTilesets.toList());

    // The map references TileMetaInfoMgr's tilesets, but we add a reference
    // to them ourself below.
    TilesetManager::instance()->removeReferences(map->tilesets());

    mapLoadedByThread(map, mapInfo);
}

void MapManager::failedToLoadByThread(const QString error, MapInfo *mapInfo)
{
    mapInfo->mLoading = false;
    mError = error;
    emit mapFailedToLoad(mapInfo);
}

/////

MapReaderWorker::MapReaderWorker(bool *abortPtr) :
    BaseWorker(abortPtr),
    mWorkPending(false)
{
}

MapReaderWorker::~MapReaderWorker()
{
}

void MapReaderWorker::work()
{
    IN_WORKER_THREAD

    mWorkPending = false;

    while (mJobs.size()) {
        if (aborted()) {
            mJobs.clear();
            return;
        }

        Job job = mJobs.takeAt(0);

        if (job.mapInfo->path().endsWith(QLatin1String(".tbx"))) {
            Building *building = loadBuilding(job.mapInfo);
            if (building)
                emit loaded(building, job.mapInfo);
            else
                emit failedToLoad(mError, job.mapInfo);
        } else {
//            noise() << "READING STARTED" << job.mapInfo->path();
            Map *map = loadMap(job.mapInfo);
//            noise() << "READING FINISHED" << job.mapInfo->path();
            if (map)
                emit loaded(map, job.mapInfo);
            else
                emit failedToLoad(mError, job.mapInfo);
        }
    }
}

void MapReaderWorker::addJob(MapInfo *mapInfo)
{
    IN_WORKER_THREAD

    mJobs += Job(mapInfo);
    if (!mWorkPending) {
        mWorkPending = true;
        QMetaObject::invokeMethod(this, "work", Qt::QueuedConnection);
    }
}

class MapReaderWorker_MapReader : public MapReader
{
protected:
    /**
     * Overridden to make sure the resolved reference is canonical.
     */
    QString resolveReference(const QString &reference, const QString &mapPath)
    {
        QString resolved = MapReader::resolveReference(reference, mapPath);
        QString canonical = QFileInfo(resolved).canonicalFilePath();

        // Make sure that we're not returning an empty string when the file is
        // not found.
        return canonical.isEmpty() ? resolved : canonical;
    }
};

Map *MapReaderWorker::loadMap(MapInfo *mapInfo)
{
    MapReaderWorker_MapReader reader;
//    reader.setTilesetImageCache(TilesetManager::instance()->imageCache()); // not thread-safe class
    Map *map = reader.readMap(mapInfo->path());
    if (!map)
        mError = reader.errorString();
    return map;
}

MapReaderWorker::Building *MapReaderWorker::loadBuilding(MapInfo *mapInfo)
{
    BuildingReader reader;
    Building *building = reader.read(mapInfo->path());
    if (!building)
        mError = reader.errorString();
    return building;
}
