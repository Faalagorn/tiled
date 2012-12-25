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

#include "mapimagemanager.h"

#include "imagelayer.h"
#include "isometricrenderer.h"
#include "map.h"
#include "mapcomposite.h"
#include "mapmanager.h"
#include "objectgroup.h"
#include "orthogonalrenderer.h"
#include "staggeredrenderer.h"
#include "tilelayer.h"
#include "zprogress.h"
#include "zlevelrenderer.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

using namespace Tiled;

const int IMAGE_WIDTH = 512;

MapImageManager *MapImageManager::mInstance = NULL;

MapImageManager::MapImageManager()
    : QObject()
{
    connect(MapManager::instance(), SIGNAL(mapFileChanged(MapInfo*)),
            SLOT(mapFileChanged(MapInfo*)));
}

MapImageManager *MapImageManager::instance()
{
    if (mInstance == NULL)
        mInstance = new MapImageManager;
    return mInstance;
}

void MapImageManager::deleteInstance()
{
    delete mInstance;
}

MapImage *MapImageManager::getMapImage(const QString &mapName, const QString &relativeTo)
{
    QString mapFilePath = MapManager::instance()->pathForMap(mapName, relativeTo);
    if (mapFilePath.isEmpty())
        return 0;

    if (mMapImages.contains(mapFilePath))
        return mMapImages[mapFilePath];

    ImageData data = generateMapImage(mapFilePath);
    if (!data.valid)
        return 0;

    MapInfo *mapInfo = MapManager::instance()->mapInfo(mapFilePath);
    MapImage *mapImage = new MapImage(data.image, data.scale, data.levelZeroBounds, mapInfo);
#if 1
    // Set up file modification tracking on each TMX that makes
    // up this image.
    QList<MapInfo*> sources;
    foreach (QString source, data.sources)
        if (MapInfo *sourceInfo = MapManager::instance()->mapInfo(source))
            sources += sourceInfo;
    mapImage->setSources(sources);
#endif
    mMapImages.insert(mapFilePath, mapImage);
    return mapImage;
}

MapImage *MapImageManager::newFromMap(MapComposite *mapComposite)
{
    ImageData data = generateMapImage(mapComposite);
    Q_ASSERT(data.valid);
    MapImage *mapImage = new MapImage(data.image, data.scale, data.levelZeroBounds, mapComposite->mapInfo());
    return mapImage;
}

MapImageManager::ImageData MapImageManager::generateMapImage(const QString &mapFilePath)
{
#if 0
    if (mapFilePath == QLatin1String("<fail>")) {
        QImage image(IMAGE_WIDTH, 256, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setFont(QFont(QLatin1String("Helvetica"), 48, 1, true));
        painter.drawText(0, 0, image.width(), image.height(), Qt::AlignCenter, QLatin1String("FAIL"));
        return image;
    }
#endif

    QFileInfo fileInfo(mapFilePath);
    QFileInfo imageInfo = imageFileInfo(mapFilePath);
    QFileInfo imageDataInfo = imageDataFileInfo(imageInfo);
    if (imageInfo.exists() && imageDataInfo.exists() && (fileInfo.lastModified() < imageInfo.lastModified())) {
        QImage image(imageInfo.absoluteFilePath());
        if (image.isNull())
            QMessageBox::warning(0, tr("Error Loading Image"),
                                 tr("An error occurred trying to read a map thumbnail image.\n") + imageInfo.absoluteFilePath());
        if (image.width() == IMAGE_WIDTH) {
            ImageData data = readImageData(imageDataInfo);
            // If the image was originally created with some tilesets missing,
            // try to recreate the image in case those tileset issues were
            // resolved.
            if (data.missingTilesets)
                data.valid = false;
#if 1
            if (data.valid) {
                foreach (QString source, data.sources) {
                    QFileInfo sourceInfo(source);
                    if (sourceInfo.exists() && (sourceInfo.lastModified() > imageInfo.lastModified())) {
                        data.valid = false;
                        break;
                    }
                }
            }
#endif
            if (data.valid) {
                data.image = image;
                return data;
            }
        }
    }

    PROGRESS progress(tr("Generating thumbnail for %1").arg(fileInfo.completeBaseName()));

    MapInfo *mapInfo = MapManager::instance()->loadMap(mapFilePath);
    if (!mapInfo) {
        mError = MapManager::instance()->errorString();
    if (!mapInfo)
        return ImageData(); // TODO: Add error handling
    }

    progress.update(tr("Generating thumbnail for %1").arg(fileInfo.completeBaseName()));

    MapComposite mapComposite(mapInfo);
    ImageData data = generateMapImage(&mapComposite);

    foreach (MapComposite *mc, mapComposite.maps()) {
        if (mc->map()->hasMissingTilesets()) {
            data.missingTilesets = true;
            break;
        }
    }

    data.image.save(imageInfo.absoluteFilePath());
    writeImageData(imageDataInfo, data);

    return data;
}

MapImageManager::ImageData MapImageManager::generateMapImage(MapComposite *mapComposite)
{
    Map *map = mapComposite->map();

    MapRenderer *renderer = NULL;

    switch (map->orientation()) {
    case Map::Isometric:
        renderer = new IsometricRenderer(map);
        break;
    case Map::LevelIsometric:
        renderer = new ZLevelRenderer(map);
        break;
    case Map::Orthogonal:
        renderer = new OrthogonalRenderer(map);
        break;
    case Map::Staggered:
        renderer = new StaggeredRenderer(map);
        break;
    default:
        return ImageData();
    }

    mapComposite->saveVisibility();
    mapComposite->saveOpacity();
    foreach (CompositeLayerGroup *layerGroup, mapComposite->sortedLayerGroups()) {
        foreach (TileLayer *tl, layerGroup->layers()) {
            bool isVisible = true;
            if (tl->name().contains(QLatin1String("NoRender")))
                isVisible = false;
            layerGroup->setLayerVisibility(tl, isVisible);
            layerGroup->setLayerOpacity(tl, 1.0f);
        }
        layerGroup->synch();
    }

    // Don't draw empty levels
    int maxLevel = 0;
    foreach (CompositeLayerGroup *layerGroup, mapComposite->sortedLayerGroups()) {
        if (!layerGroup->bounds().isEmpty())
            maxLevel = layerGroup->level();
    }
    renderer->setMaxLevel(maxLevel);

    QRectF sceneRect = mapComposite->boundingRect(renderer);
    QSize mapSize = sceneRect.size().toSize();
    if (mapSize.isEmpty())
        return ImageData();

    qreal scale = IMAGE_WIDTH / qreal(mapSize.width());
    mapSize *= scale;

    QImage image(mapSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);

    painter.setRenderHints(QPainter::SmoothPixmapTransform |
                           QPainter::HighQualityAntialiasing);
    painter.setTransform(QTransform::fromScale(scale, scale).translate(-sceneRect.left(), -sceneRect.top()));

    foreach (MapComposite::ZOrderItem zo, mapComposite->zOrder()) {
        if (zo.group) {
            renderer->drawTileLayerGroup(&painter, zo.group);
        } else if (TileLayer *tl = zo.layer->asTileLayer()) {
            if (tl->name().contains(QLatin1String("NoRender")))
                continue;
            renderer->drawTileLayer(&painter, tl);
        }
    }

    mapComposite->restoreVisibility();
    mapComposite->restoreOpacity();
    foreach (CompositeLayerGroup *layerGroup, mapComposite->sortedLayerGroups())
        layerGroup->synch();

    ImageData data;
    data.image = image;
    data.scale = scale;
    data.levelZeroBounds = renderer->boundingRect(QRect(0, 0, map->width(), map->height()));
    data.levelZeroBounds.translate(-sceneRect.topLeft());
    data.sources = mapComposite->getMapFileNames();
    data.valid = true;

    delete renderer;

    return data;
}

#define IMAGE_DATA_MAGIC 0xB15B00B5
#define IMAGE_DATA_VERSION 3

MapImageManager::ImageData MapImageManager::readImageData(const QFileInfo &imageDataFileInfo)
{
    ImageData data;
    QFile file(imageDataFileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return data;

    QDataStream in(&file);

    quint32 magic;
    in >> magic;
    if (magic != IMAGE_DATA_MAGIC)
        return data;

    quint32 version;
    in >> version;
    if (version != IMAGE_DATA_VERSION)
        return data;

    in >> data.scale;

    qreal x, y, w, h;
    in >> x >> y >> w >> h;
    data.levelZeroBounds.setCoords(x, y, x + w, y + h);

    qint32 count;
    in >> count;
    for (int i = 0; i < count; i++) {
        QString source;
        in >> source;
        data.sources += source;
    }

    in >> data.missingTilesets;

    // TODO: sanity-check the values
    data.valid = true;

    return data;
}

void MapImageManager::writeImageData(const QFileInfo &imageDataFileInfo, const MapImageManager::ImageData &data)
{
    QFile file(imageDataFileInfo.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly))
        return;

    QDataStream out(&file);
    out << quint32(IMAGE_DATA_MAGIC);
    out << quint32(IMAGE_DATA_VERSION);
    out.setVersion(QDataStream::Qt_4_0);
    out << data.scale;
    QRectF r = data.levelZeroBounds;
    out << r.x() << r.y() << r.width() << r.height();
    out << qint32(data.sources.length());
    foreach (QString source, data.sources)
        out << source;
    out << data.missingTilesets;
}

void MapImageManager::mapFileChanged(MapInfo *mapInfo)
{
    QMap<QString,MapImage*>::iterator it_begin = mMapImages.begin();
    QMap<QString,MapImage*>::iterator it_end = mMapImages.end();
    QMap<QString,MapImage*>::iterator it;

    for (it = it_begin; it != it_end; it++) {
        MapImage *mapImage = it.value();
#if 1
        if (mapImage->sources().contains(mapInfo)) {
            ImageData data = generateMapImage(mapImage->mapInfo()->path());
#else
        if (mapImage->mapInfo() == mapInfo) {
            ImageData data = generateMapImage(mapInfo->path());
#endif
            if (!data.valid)
                return;
            mapImage->mapFileChanged(data.image, data.scale,
                                     data.levelZeroBounds);

            // Set up file modification tracking on each TMX that makes
            // up this image.
            QList<MapInfo*> sources;
            foreach (QString source, data.sources)
                if (MapInfo *sourceInfo = MapManager::instance()->mapInfo(source))
                    sources += sourceInfo;
            mapImage->setSources(sources);

            emit mapImageChanged(mapImage);
        }
    }
}

QFileInfo MapImageManager::imageFileInfo(const QString &mapFilePath)
{
    QFileInfo mapFileInfo(mapFilePath);
    QDir mapDir = mapFileInfo.absoluteDir();
    if (!mapDir.exists())
        return QFileInfo();
    QFileInfo imagesDirInfo(mapDir, QLatin1String(".pzeditor"));
    if (!imagesDirInfo.exists()) {
        if (!mapDir.mkdir(QLatin1String(".pzeditor")))
            return QFileInfo();
    }
    return QFileInfo(imagesDirInfo.absoluteFilePath() + QLatin1Char('/') +
                     mapFileInfo.completeBaseName() + QLatin1String(".png"));
}

QFileInfo MapImageManager::imageDataFileInfo(const QFileInfo &imageFileInfo)
{
    return QFileInfo(imageFileInfo.absolutePath() + QLatin1Char('/') +
                     imageFileInfo.completeBaseName() + QLatin1String(".dat"));
}

///// ///// ///// ///// /////

MapImage::MapImage(QImage image, qreal scale, const QRectF &levelZeroBounds, MapInfo *mapInfo)
    : mImage(image)
    , mInfo(mapInfo)
    , mLevelZeroBounds(levelZeroBounds)
    , mScale(scale)
{
}

QPointF MapImage::tileToPixelCoords(qreal x, qreal y)
{
    const int tileWidth = mInfo->tileWidth();
    const int tileHeight = mInfo->tileHeight();
    const int originX = mInfo->height() * tileWidth / 2;

    return QPointF((x - y) * tileWidth / 2 + originX,
                   (x + y) * tileHeight / 2);
}

QRectF MapImage::tileBoundingRect(const QRect &rect)
{
    const int tileWidth = mInfo->tileWidth();
    const int tileHeight = mInfo->tileHeight();

    const int originX = mInfo->height() * tileWidth / 2;
    const QPoint pos((rect.x() - (rect.y() + rect.height()))
                     * tileWidth / 2 + originX,
                     (rect.x() + rect.y()) * tileHeight / 2);

    const int side = rect.height() + rect.width();
    const QSize size(side * tileWidth / 2,
                     side * tileHeight / 2);

    return QRect(pos, size);
}

QRectF MapImage::bounds()
{
    int mapWidth = mInfo->width(), mapHeight = mInfo->height();
    return tileBoundingRect(QRect(0,0,mapWidth,mapHeight));
}

qreal MapImage::scale()
{
    return mScale;
}

QPointF MapImage::tileToImageCoords(qreal x, qreal y)
{
    QPointF pos = tileToPixelCoords(x, y);
    pos += mLevelZeroBounds.topLeft();
    return pos * scale();
}

void MapImage::mapFileChanged(QImage image, qreal scale, const QRectF &levelZeroBounds)
{
    mImage = image;
    mScale = scale;
    mLevelZeroBounds = levelZeroBounds;
}
