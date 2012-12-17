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

#include "pathgenerator.h"

#include "map.h"
#include "pathlayer.h"
#include "tilelayer.h"
#include "tileset.h"

using namespace Tiled;

PathGeneratorProperty::PathGeneratorProperty(const QString &name, const QString &type) :
    mName(name),
    mType(type)
{
}

PGP_Boolean::PGP_Boolean(const QString &name) :
    PathGeneratorProperty(name, QLatin1String("Boolean")),
    mValue(false)
{
}

void PGP_Boolean::clone(PathGeneratorProperty *other)
{
    mValue = other->asBoolean()->mValue;
}

QString PGP_Boolean::valueToString() const
{
    return QLatin1String(mValue ? "true" : "false");
}

bool PGP_Boolean::valueFromString(const QString &s)
{
    if (s == QLatin1String("true"))
        mValue = true;
    else if (s == QLatin1String("false"))
        mValue = false;
    else
        return false;
    return true;
}

PGP_Integer::PGP_Integer(const QString &name) :
    PathGeneratorProperty(name, QLatin1String("Integer")),
    mValue(1),
    mMin(1),
    mMax(100)
{
}

void PGP_Integer::clone(PathGeneratorProperty *other)
{
    mValue = other->asInteger()->mValue;
}

QString PGP_Integer::valueToString() const
{
    return QString::number(mValue);
}

bool PGP_Integer::valueFromString(const QString &s)
{
    bool ok;
    int value = s.toInt(&ok);
    if (ok && value >= mMin && value <= mMax) {
        mValue = value;
        return true;
    }
    return false;
}

PGP_String::PGP_String(const QString &name) :
    PathGeneratorProperty(name, QLatin1String("String"))
{
}

void PGP_String::clone(PathGeneratorProperty *other)
{
    mValue = other->asString()->mValue;
}

QString PGP_String::valueToString() const
{
    return mValue;
}

bool PGP_String::valueFromString(const QString &s)
{
    mValue = s;
    return true;
}

PGP_Layer::PGP_Layer(const QString &name) :
    PathGeneratorProperty(name, QLatin1String("Layer"))
{
}

void PGP_Layer::clone(PathGeneratorProperty *other)
{
    mValue = other->asLayer()->mValue;
}

QString PGP_Layer::valueToString() const
{
    return mValue;
}

bool PGP_Layer::valueFromString(const QString &s)
{
    mValue = s;
    return true;
}

PGP_Tile::PGP_Tile(const QString &name) :
    PathGeneratorProperty(name, QLatin1String("Tile"))
{
}

void PGP_Tile::clone(PathGeneratorProperty *other)
{
    mTilesetName = other->asTile()->mTilesetName;
    mTileID = other->asTile()->mTileID;
}

QString PGP_Tile::valueToString() const
{
    if (mTilesetName.isEmpty())
        return QString();
    return mTilesetName + QLatin1Char('_') + QString::number(mTileID);
}

static bool parseTileName(const QString &tileName, QString &tilesetName, int &index)
{
    tilesetName = tileName.mid(0, tileName.lastIndexOf(QLatin1Char('_')));
    QString indexString = tileName.mid(tileName.lastIndexOf(QLatin1Char('_')) + 1);

    // Strip leading zeroes from the tile index
    int i = 0;
    while (i < indexString.length() - 1 && indexString[i] == QLatin1Char('0'))
        i++;
    indexString.remove(0, i);

    bool ok;
    index = indexString.toInt(&ok);
    return ok;
}

bool PGP_Tile::valueFromString(const QString &s)
{
    if (s.isEmpty()) {
        mTilesetName.clear();
        mTileID = 0;
        return true;
    }
    QString tilesetName;
    int index;
    if (parseTileName(s, tilesetName, index)) {
        mTilesetName = tilesetName;
        mTileID = index;
        return true;
    }
    return false;
}

/////

PathGenerator::PathGenerator(const QString &label, const QString &type) :
    mLabel(label),
    mType(type),
    mRefCount(0),
    mPath(0)
{
}

void PathGenerator::generate(const Path *path, int level, QVector<TileLayer *> &layers)
{
    mPath = path;
    generate(level, layers);
}

static Tileset *findTileset(const QString &name, const QList<Tileset*> &tilesets)
{
    foreach (Tileset *ts, tilesets) {
        if (ts->name() == name)
            return ts;
    }
    return 0;
}

static QString layerNameWithoutPrefix(const QString &name)
{
    int pos = name.indexOf(QLatin1Char('_')) + 1; // Could be "-1 + 1 == 0"
    return name.mid(pos);
}


TileLayer *findTileLayer(const QString &name, const QVector<TileLayer*> &layers)
{
    foreach (TileLayer *tl, layers) {
        if (layerNameWithoutPrefix(tl->name()) == name)
            return tl;
    }
    return 0;
}

/**
 * Returns the lists of points on a line from (x0,y0) to (x1,y1).
 *
 * This is an implementation of bresenhams line algorithm, initially copied
 * from http://en.wikipedia.org/wiki/Bresenham's_line_algorithm#Optimization
 * changed to C++ syntax.
 */
// from stampBrush.cpp
static QVector<QPoint> calculateLine(int x0, int y0, int x1, int y1)
{
    QVector<QPoint> ret;

    bool steep = qAbs(y1 - y0) > qAbs(x1 - x0);
    if (steep) {
        qSwap(x0, y0);
        qSwap(x1, y1);
    }
    if (x0 > x1) {
        qSwap(x0, x1);
        qSwap(y0, y1);
    }
    const int deltax = x1 - x0;
    const int deltay = qAbs(y1 - y0);
    int error = deltax / 2;
    int ystep;
    int y = y0;

    if (y0 < y1)
        ystep = 1;
    else
        ystep = -1;

    for (int x = x0; x < x1 + 1 ; x++) {
        if (steep)
            ret += QPoint(y, x);
        else
            ret += QPoint(x, y);
        error = error - deltay;
        if (error < 0) {
             y = y + ystep;
             error = error + deltax;
        }
    }

    return ret;
}

void PathGenerator::outline(Tile *tile, TileLayer *tl)
{
    PathPoints points = mPath->points();
    if (mPath->isClosed())
        points += points.first();

    for (int i = 0; i < points.size() - 1; i++) {
        foreach (QPoint pt, calculateLine(points[i].x(), points[i].y(),
                                          points[i+1].x(), points[i+1].y())) {
            if (!tl->contains(pt))
                continue;
            Cell cell(tile);
            tl->setCell(pt.x(), pt.y(), cell);
        }
    }
}

void PathGenerator::outlineWidth(Tile *tile, TileLayer *tl, int width)
{
    PathPoints points = mPath->points();
    if (mPath->isClosed())
        points += points.first();

    for (int i = 0; i < points.size() - 1; i++) {
        bool vert = points[i].x() == points[i+1].x();
        bool horiz = points[i].y() == points[i+1].y();
        int dx = horiz ? width-width/2 : 0;
        int dy = vert ? width-width/2 : 0;
        bool firstSeg = i == 0;
        bool lastSeg = i < points.size() - 1;
        foreach (QPoint pt, calculateLine(points[i].x(), points[i].y(),
                                          points[i+1].x()+ dx, points[i+1].y() + dy)) {
            Cell cell(tile);
            if (vert) {
                for (int j = 0; j < width; j++) {
                    if (tl->contains(pt.x() - width / 2 + j, pt.y()))
                        tl->setCell(pt.x() - width / 2 + j, pt.y(), cell);
                }
            } else if (horiz) {
                for (int j = 0; j < width; j++) {
                    if (tl->contains(pt.x(), pt.y() - width / 2 + j))
                        tl->setCell(pt.x(), pt.y() - width / 2 + j, cell);
                }
            } else {
                if (!tl->contains(pt))
                    continue;
                Cell cell(tile);
                tl->setCell(pt.x(), pt.y(), cell);
            }
        }
    }
}

void PathGenerator::fill(Tile *tile, TileLayer *tl)
{
    if (!mPath->isClosed())
        return;

    QRect bounds = mPath->polygon().boundingRect();

    QPolygonF polygon = mPath->polygonf();
    for (int x = bounds.left(); x <= bounds.right(); x++) {
        for (int y = bounds.top(); y <= bounds.bottom(); y++) {
            QPointF pt(x + 0.5, y + 0.5);
            if (polygon.containsPoint(pt, Qt::WindingFill)) {
                if (!tl->contains(pt.toPoint()))
                    continue;
                Cell cell(tile);
                tl->setCell(pt.x(), pt.y(), cell);
            }
        }
    }
}

void PathGenerator::cloneProperties(const PathGenerator *other)
{
    for (int i = 0; i < mProperties.size(); i++)
        mProperties[i]->clone(other->mProperties[i]);
}

/////

PG_SingleTile::PG_SingleTile(const QString &label) :
    PathGenerator(label, QLatin1String("SingleTile")),
    mLayerName(QLatin1String("Floor")),
    mTilesetName(QLatin1String("floors_exterior_street_01")),
    mTileID(18)
{
}

PathGenerator *PG_SingleTile::clone() const
{
    PG_SingleTile *clone = new PG_SingleTile(mLabel);
    clone->cloneProperties(this);
    return clone;
}

void PG_SingleTile::generate(int level, QVector<TileLayer *> &layers)
{
    if (level != mPath->level())
        return;
    if (!mPath->points().size())
        return;

    TileLayer *tl = findTileLayer(mLayerName, layers);
    if (!tl) return;

    Tileset *ts = findTileset(mTilesetName, tl->map()->tilesets());
    if (!ts) return;

    if (mPath->isClosed())
        fill(ts->tileAt(mTileID), tl);

    outline(ts->tileAt(mTileID), tl);
}

/////

PG_Fence::PG_Fence(const QString &label) :
    PathGenerator(label, QLatin1String("Fence"))
{
    mProperties.resize(PropertyCount);

    PGP_Tile *prop = new PGP_Tile(QLatin1String("West1"));
    prop->mTilesetName = QLatin1String("fencing_01");
    prop->mTileID = 11;
    mProperties[West1] = prop;

    prop = new PGP_Tile(QLatin1String("West2"));
    prop->mTilesetName = QLatin1String("fencing_01");
    prop->mTileID = 10;
    mProperties[West2] = prop;

    prop = new PGP_Tile(QLatin1String("North1"));
    prop->mTilesetName = QLatin1String("fencing_01");
    prop->mTileID = 8;
    mProperties[North1] = prop;

    prop = new PGP_Tile(QLatin1String("North2"));
    prop->mTilesetName = QLatin1String("fencing_01");
    prop->mTileID = 9;
    mProperties[North2] = prop;

    prop = new PGP_Tile(QLatin1String("NorthWest"));
    prop->mTilesetName = QLatin1String("fencing_01");
    prop->mTileID = 12;
    mProperties[NorthWest] = prop;

    prop = new PGP_Tile(QLatin1String("SouthEast"));
    prop->mTilesetName = QLatin1String("fencing_01");
    prop->mTileID = 13;
    mProperties[SouthEast] = prop;

    PGP_Layer *prop2 = new PGP_Layer(QLatin1String("Layer"));
    prop2->mValue = QLatin1String("Furniture");
    mProperties[LayerName] = prop2;

#if 0
    // Tall wooden
    mTilesetName[West1] = QLatin1String("fencing_01");
    mTileID[West1] = 11;
    mTilesetName[West2] = QLatin1String("fencing_01");
    mTileID[West2] = 10;
    mTilesetName[North1] = QLatin1String("fencing_01");
    mTileID[North1] = 8;
    mTilesetName[North2] = QLatin1String("fencing_01");
    mTileID[North2] = 9;
    mTilesetName[NorthWest] = QLatin1String("fencing_01");
    mTileID[NorthWest] = 12;
    mTilesetName[SouthEast] = QLatin1String("fencing_01");
    mTileID[SouthEast] = 13;
#endif

#if 0
    for (int i = 0; i < TileCount; i++)
        mTileID[i] += 16; // Chainlink
#elif 0
    for (int i = 0; i < TileCount; i++)
        mTileID[i] += 16 + 8; // Short wooden
#elif 0
    // Black metal
    mTileID[West1] = mTileID[West2] = 2;
    mTileID[North1] = mTileID[North2] = 1;
    mTileID[NorthWest] = 3;
    mTileID[SouthEast] = 0;
#elif 0
    // White picket
    mTileID[West1] = mTileID[West2] = 4;
    mTileID[North1] = mTileID[North2] = 5;
    mTileID[NorthWest] = 6;
    mTileID[SouthEast] = 7;
#endif
}

PathGenerator *PG_Fence::clone() const
{
    PG_Fence *clone = new PG_Fence(mLabel);
    clone->cloneProperties(this);
    return clone;
}

void PG_Fence::generate(int level, QVector<TileLayer *> &layers)
{
    if (level != mPath->level())
        return;
    if (mPath->points().size() < 2)
        return;

    TileLayer *tl = findTileLayer(mProperties[LayerName]->asLayer()->mValue, layers);
    if (!tl) return;

    QVector<Tile*> tiles(TileCount);
    for (int i = 0; i < TileCount; i++) {
        PGP_Tile *prop = mProperties[i]->asTile();
        Tileset *ts = findTileset(prop->mTilesetName, tl->map()->tilesets());
        if (!ts) return;
        tiles[i] = ts->tileAt(prop->mTileID);
        if (!tiles[i]) return;
    }

    PathPoints points = mPath->points();
    if (mPath->isClosed())
        points += points.first();

    for (int i = 0; i < points.size() - 1; i++) {
        bool vert = points[i].x() == points[i+1].x();
        bool horiz = points[i].y() == points[i+1].y();
        int alternate = 0;
        if (horiz) {
            foreach (QPoint pt, calculateLine(points[i].x(), points[i].y(),
                                              points[i+1].x(), points[i+1].y())) {
                if (pt.x() == qMax(points[i].x(), points[i+1].x())) {
                    if (tl->contains(pt.x(), pt.y() - 1)) {
                        if (tl->cellAt(pt.x(), pt.y() - 1).tile == tiles[West2])
                            tl->setCell(pt.x(), pt.y(), Cell(tiles[SouthEast]));
                    }
                    break;
                }
                if (tl->contains(pt)) {
                    Tile *tile = tl->cellAt(pt).tile;
                    if (tile == tiles[West1])
                        tl->setCell(pt.x(), pt.y(), Cell(tiles[NorthWest]));
                    else
                        tl->setCell(pt.x(), pt.y(), Cell(tiles[North1 + alternate]));
                }
                alternate = !alternate;
            }
        } else if (vert) {
            foreach (QPoint pt, calculateLine(points[i].x(), points[i].y(),
                                              points[i+1].x(), points[i+1].y())) {
                if (pt.y() == qMax(points[i].y(), points[i+1].y())) {
                    if (tl->contains(pt.x() - 1, pt.y())) {
                        if (tl->cellAt(pt.x() - 1, pt.y()).tile == tiles[North2])
                            tl->setCell(pt.x(), pt.y(), Cell(tiles[SouthEast]));
                    }
                    break;
                }
                if (tl->contains(pt)) {
                    Tile *tile = tl->cellAt(pt).tile;
                    if (tile == tiles[North1])
                        tl->setCell(pt.x(), pt.y(), Cell(tiles[NorthWest]));
                    else
                        tl->setCell(pt.x(), pt.y(), Cell(tiles[West1 + alternate]));
                }
                alternate = !alternate;
            }
        }
    }
}

/////

PG_StreetLight::PG_StreetLight(const QString &label) :
    PathGenerator(label, QLatin1String("StreetLight"))
{
    mProperties.resize(PropertyCount);

    PGP_Tile *prop = new PGP_Tile(QLatin1String("West"));
    prop->mTilesetName = QLatin1String("lighting_outdoor_01");
    prop->mTileID = 9;
    mProperties[West] = prop;

    prop = new PGP_Tile(QLatin1String("North"));
    prop->mTilesetName = QLatin1String("lighting_outdoor_01");
    prop->mTileID = 10;
    mProperties[North] = prop;

    prop = new PGP_Tile(QLatin1String("East"));
    prop->mTilesetName = QLatin1String("lighting_outdoor_01");
    prop->mTileID = 11;
    mProperties[East] = prop;

    prop = new PGP_Tile(QLatin1String("South"));
    prop->mTilesetName = QLatin1String("lighting_outdoor_01");
    prop->mTileID = 8;
    mProperties[South] = prop;

    prop = new PGP_Tile(QLatin1String("Base"));
    prop->mTilesetName = QLatin1String("lighting_outdoor_01");
    prop->mTileID = 16;
    mProperties[Base] = prop;

    PGP_Layer *prop2 = new PGP_Layer(QLatin1String("Layer"));
    prop2->mValue = QLatin1String("Furniture");
    mProperties[LayerName] = prop2;

    PGP_Integer *prop3 = new PGP_Integer(QLatin1String("Spacing"));
    prop3->mMin = 1, prop3->mMax = 300, prop3->mValue = 10;
    mProperties[Spacing] = prop3;

    PGP_Boolean *prop4 = new PGP_Boolean(QLatin1String("Reverse"));
    prop4->mValue = false;
    mProperties[Reverse] = prop4;
}

PathGenerator *PG_StreetLight::clone() const
{
    PG_StreetLight *clone = new PG_StreetLight(mLabel);
    clone->cloneProperties(this);
    return clone;
}

void PG_StreetLight::generate(int level, QVector<TileLayer *> &layers)
{
    bool level0 = level == mPath->level();
    bool level1 = level == mPath->level() + 1;
    if (!level0 && !level1)
        return;
    if (mPath->points().size() < 2)
        return;

    PGP_Layer *prop = mProperties[LayerName]->asLayer();
    TileLayer *tl = findTileLayer(prop->mValue, layers);
    if (!tl) return;

    QVector<Tile*> tiles(TileCount);
    for (int i = 0; i < TileCount; i++) {
        PGP_Tile *prop = mProperties[i]->asTile();
        Tileset *ts = findTileset(prop->mTilesetName, tl->map()->tilesets());
        if (!ts) return;
        tiles[i] = ts->tileAt(prop->mTileID);
        if (!tiles[i]) return;
    }

    PathPoints points = mPath->points();
    if (mPath->isClosed())
        points += points.first();

    if (tl->map()->orientation() == Map::Isometric && level1) {
        for (int i = 0; i < points.size(); i++)
            points[i].translate(QPoint(-3, -3));
    }

    int spacing = mProperties[Spacing]->asInteger()->mValue;
    bool reverse = mProperties[Reverse]->asBoolean()->mValue;

    for (int i = 0; i < points.size() - 1; i++) {
        bool vert = points[i].x() == points[i+1].x();
        bool horiz = points[i].y() == points[i+1].y();
        int distance = 0;
        if (horiz) {
            foreach (QPoint pt, calculateLine(points[i].x(), points[i].y(),
                                              points[i+1].x(), points[i+1].y())) {
                if (tl->contains(pt) && !(distance % spacing)) {
                    tl->setCell(pt.x(), pt.y(), Cell(tiles[level1 ? (reverse ? South : North) : Base]));
                }
                ++distance;
            }
        } else if (vert) {
            foreach (QPoint pt, calculateLine(points[i].x(), points[i].y(),
                                              points[i+1].x(), points[i+1].y())) {
                if (tl->contains(pt) && !(distance % spacing)) {
                    tl->setCell(pt.x(), pt.y(), Cell(tiles[level1 ? (reverse ? East : West) : Base]));
                }
                ++distance;
            }
        }
    }
}
