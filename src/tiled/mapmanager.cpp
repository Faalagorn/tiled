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
#include "zprogress.h"

#include "map.h"
#include "mapreader.h"
#include "mapobject.h"
#include "objectgroup.h"
#include "tilelayer.h"
#include "tileset.h"
#include "tilesetmanager.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

using namespace Tiled;
using namespace Internal;

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

MapManager::MapManager()
    : mFileSystemWatcher(new FileSystemWatcher(this))
{
    connect(mFileSystemWatcher, SIGNAL(fileChanged(QString)),
            SLOT(fileChanged(QString)));

    mChangedFilesTimer.setInterval(500);
    mChangedFilesTimer.setSingleShot(true);
    connect(&mChangedFilesTimer, SIGNAL(timeout()),
            SLOT(fileChangedTimeout()));
}

MapManager::~MapManager()
{
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
QString MapManager::pathForMap(const QString &mapName, const QString &_relativeTo)
{
    QString mapFilePath = mapName;
    QString relativeTo = _relativeTo;

    QDir mapsDirectory(Preferences::instance()->mapsDirectory());
    if (relativeTo.isEmpty() && mapsDirectory.exists())
        relativeTo = mapsDirectory.canonicalPath();

    if (QDir::isRelativePath(mapName)) {
        Q_ASSERT(!relativeTo.isEmpty());
        Q_ASSERT(!QDir::isRelativePath(relativeTo));
        mapFilePath = relativeTo + QLatin1Char('/') + mapName;
    }

    if (!mapFilePath.endsWith(QLatin1String(".tmx")))
        mapFilePath += QLatin1String(".tmx");

    QFileInfo fileInfo(mapFilePath);
    if (fileInfo.exists())
        return fileInfo.canonicalFilePath();

    if (mapsDirectory.exists() && (relativeTo != mapsDirectory.canonicalPath()))
        return pathForMap(mapName, mapsDirectory.canonicalPath());

    return QString();
}

#include <QApplication>
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

        // Hack to keep the app responsive.
        // TODO: Move map reading to a worker thread. Only issue is tileset images
        // cannot be accessed outside the GUI thread.
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

        // Make sure that we're not returning an empty string when the file is
        // not found.
        return canonical.isEmpty() ? resolved : canonical;
    }
};

#ifdef THREADIT
#include <QThread>

class MapReaderThread : public QThread
{
public:
    MapReaderThread(const QString &mapFilePath)
        : mMapFilePath(mapFilePath)
        , mMap(0)
    {
    }

    void run()
    {
        EditorMapReader reader;
        reader.setTilesetImageCache(gTilesetImageCache);
        mMap = reader.readMap(mMapFilePath);
    }

    QString mMapFilePath;
    Map *mMap;
};
#endif

MapInfo *MapManager::loadMap(const QString &mapName, const QString &relativeTo)
{
    QString mapFilePath = pathForMap(mapName, relativeTo);
    if (mapFilePath.isEmpty())
        return 0;

    if (mMapInfo.contains(mapFilePath) && mMapInfo[mapFilePath]->map()) {
        return mMapInfo[mapFilePath];
    }

    QFileInfo fileInfoMap(mapFilePath);

    PROGRESS progress(tr("Reading %1").arg(fileInfoMap.completeBaseName()));

#ifdef THREADIT
    MapReaderThread thread(mapFilePath);
    thread.start();
    while (thread.isRunning()) {
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    Map *map = thread.mMap;
#else
    EditorMapReader reader;
    reader.setTilesetImageCache(TilesetManager::instance()->imageCache());
    Map *map = reader.readMap(mapFilePath);
#endif
    if (!map) {
        mError = reader.errorString();
        return 0; // TODO: Add error handling
    }

    TilesetManager::instance()->addReferences(map->tilesets());

    if (!mMapInfo.contains(mapFilePath)) {
        MapInfo *info = new MapInfo(map->orientation(),
                                    map->width(), map->height(),
                                    map->tileWidth(), map->tileHeight());
        info->setFilePath(mapFilePath);
        mMapInfo[mapFilePath] = info;
        mFileSystemWatcher->addPath(mapFilePath);
    }

    mMapInfo[mapFilePath]->mMap = map;
    mMapInfo[mapFilePath]->mLastModified = QFileInfo(mapFilePath).lastModified();

    return mMapInfo[mapFilePath];
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
    bool openFile(QFile *file)
    {
        if (!file->exists()) {
            mError = tr("File not found: %1").arg(file->fileName());
            return false;
        } else if (!file->open(QFile::ReadOnly | QFile::Text)) {
            mError = tr("Unable to read file: %1").arg(file->fileName());
            return false;
        }

        return true;
    }

    MapInfo *readMap(const QString &mapFilePath)
    {
        QFile file(mapFilePath);
        if (!openFile(&file))
            return NULL;

        return readMap(&file, QFileInfo(mapFilePath).absolutePath());
    }

    MapInfo *readMap(QIODevice *device, const QString &path)
    {
        Q_UNUSED(path)

        mError.clear();
        mMapInfo = NULL;

        xml.setDevice(device);

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

    qDebug() << "read map info for" << mapFilePath;
    mapInfo->setFilePath(mapFilePath);

    mapInfo->mLastModified = QFileInfo(mapFilePath).lastModified();
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

void MapManager::mapChanged(MapInfo *mapInfo)
{
    Map *map = mapInfo->map();
    Q_ASSERT(map);
    mapInfo->mHeight = map->height();
    mapInfo->mWidth = map->width();
    mapInfo->mTileWidth = map->tileWidth();
    mapInfo->mTileHeight = map->tileHeight();
}

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
            foreach (Layer *layer, newMap->layers()) {
                int level;
                if (MapComposite::levelForLayer(layer, &level) && level > 0)
                    layer->setPosition(-offset * level);
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
    foreach (const QString &path, mChangedFiles) {
        if (mMapInfo.contains(path)) {
            qDebug() << "MapManager::fileChanged" << path;
            QFileInfo info(path);
            if (info.exists()) {
                MapInfo *mapInfo = mMapInfo[path];
                if (Map *oldMap = mapInfo->map()) {
                    Q_ASSERT(!mapInfo->isBeingEdited());

                    mapInfo->mMap = 0;
                    MapInfo *sameInfo = loadMap(path);
                    if (sameInfo && sameInfo->map()) {
                        TilesetManager *tilesetMgr = TilesetManager::instance();
                        tilesetMgr->removeReferences(oldMap->tilesets());
                        delete oldMap;
                    } else {
                        qDebug() << "MapManager::fileChangedTimeout: FAILED to load the changed map";
                        // Error loading the new map, keep the old one.
                        mapInfo->mMap = oldMap;
                        continue;
                    }

                }
                emit mapFileChanged(mapInfo);
            }
        }
    }

    mChangedFiles.clear();
}
