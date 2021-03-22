/*
 * buildingfloorV1.cpp
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

#if 0

#include "buildingfloorV1.h"

#include "building.h"
#include "buildingfloor.h"
#include "buildingobjects.h"
#include "buildingtemplates.h"
#include "buildingtiles.h"
#include "furnituregroups.h"
#include "roofhiding.h"

#if defined(Q_OS_WIN) && (_MSC_VER >= 1600)
// Hmmmm.  libtiled.dll defines the MapRands class as so:
// class TILEDSHARED_EXPORT MapRands : public QVector<QVector<int> >
// Suddenly I'm getting a 'multiply-defined symbol' error.
// I found the solution here:
// http://www.archivum.info/qt-interest@trolltech.com/2005-12/00242/RE-Linker-Problem-while-using-QMap.html
template class __declspec(dllimport) QMap<QString, QString>;
template class __declspec(dllimport) QVector<QVector<int> >;
#endif

using namespace BuildingEditor;

BuildingFloorV1::BuildingFloorV1(Building *building, int level) :
    mBuilding(building),
    mLevel(level)
{
    int w = building->width();
    int h = building->height();

    mRoomAtPos.resize(w);
    mIndexAtPos.resize(w);
    for (int x = 0; x < w; x++) {
        mRoomAtPos[x].resize(h);
        mIndexAtPos[x].resize(h);
        for (int y = 0; y < h; y++) {
            mRoomAtPos[x][y] = 0;
        }
    }

    // Copy stuff from the real floor
    BuildingFloor *floor = building->floor(level);
    mObjects = floor->objects();
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            SetRoomAt(x, y, floor->GetRoomAt(x, y));
        }
    }
}

BuildingFloorV1::~BuildingFloorV1()
{
//    qDeleteAll(mObjects);
}

BuildingFloor *BuildingFloorV1::floorAbove() const
{
    return isTopFloor() ? 0 : mBuilding->floor(mLevel + 1);
}

BuildingFloor *BuildingFloorV1::floorBelow() const
{
    return isBottomFloor() ? 0 : mBuilding->floor(mLevel - 1);
}

bool BuildingFloorV1::isTopFloor() const
{
    return mLevel == mBuilding->floorCount() - 1;
}

bool BuildingFloorV1::isBottomFloor() const
{
    return mLevel == 0;
}

void BuildingFloorV1::insertObject(int index, BuildingObject *object)
{
    mObjects.insert(index, object);
}

BuildingObject *BuildingFloorV1::removeObject(int index)
{
    BuildingObject *object = mObjects.takeAt(index);
    if (RoofObject *ro = object->asRoof())
        mFlatRoofsWithDepthThree.removeAll(ro);
    if (Stairs *stairs = object->asStairs())
        mStairs.removeAll(stairs);
    return object;
}

BuildingObject *BuildingFloorV1::objectAt(int x, int y)
{
    foreach (BuildingObject *object, mObjects)
        if (object->bounds().contains(x, y))
            return object;
    return 0;
}

void BuildingFloorV1::setGrid(const QVector<QVector<Room *> > &grid)
{
    mRoomAtPos = grid;

    mIndexAtPos.resize(mRoomAtPos.size());
    for (int x = 0; x < mIndexAtPos.size(); x++)
        mIndexAtPos[x].resize(mRoomAtPos[x].size());
}

static void ReplaceRoofSlope(RoofObject *ro, const QRect &r,
                             QVector<QVector<BuildingFloorV1::Square> > &squares,
                             RoofObject::RoofTile tile)
{
    if (r.isEmpty()) return;
    int offset = ro->getOffset(tile);
    QPoint tileOffset = ro->slopeTiles()->offset(offset);
    QRect bounds(0, 0, squares.size(), squares[0].size());
    QRect rOffset = r.translated(tileOffset) & bounds;
    for (int x = rOffset.left(); x <= rOffset.right(); x++)
        for (int y = rOffset.top(); y <= rOffset.bottom(); y++)
            squares[x][y].ReplaceRoof(ro->slopeTiles(), offset);
}

static void ReplaceRoofSlope(RoofObject *ro, const QRect &r,
                           const QVector<RoofObject::RoofTile> &tiles,
                           QVector<QVector<BuildingFloorV1::Square> > &squares)
{
    if (tiles.isEmpty()) return;
    for (int y = r.top(); y <= r.bottom(); y++)
        for (int x = r.left(); x <= r.right(); x++)
            ReplaceRoofSlope(ro, QRect(x, y, 1, 1), squares, tiles.at(x - r.left() + (y - r.top()) * r.width()));
}

static void ReplaceRoofGap(RoofObject *ro, const QRect &r,
                           QVector<QVector<BuildingFloorV1::Square> > &squares,
                           RoofObject::RoofTile tile)
{
    if (r.isEmpty()) return;
    int offset = ro->getOffset(tile);
    QPoint tileOffset = ro->capTiles()->offset(offset);
    QRect bounds(0, 0, squares.size(), squares[0].size());
    QRect rOffset = r.translated(tileOffset) & bounds;
    for (int x = rOffset.left(); x <= rOffset.right(); x++)
        for (int y = rOffset.top(); y <= rOffset.bottom(); y++)
            squares[x][y].ReplaceRoofCap(ro->capTiles(), offset);
}

static void ReplaceRoofCap(RoofObject *ro, int x, int y,
                           QVector<QVector<BuildingFloorV1::Square> > &squares,
                           RoofObject::RoofTile tile)
{
    int offset = ro->getOffset(tile);
    QPoint tileOffset = ro->capTiles()->offset(offset);
    QRect bounds(0, 0, squares.size(), squares[0].size());
    QPoint p = QPoint(x, y) + tileOffset;
    if (bounds.contains(p))
        squares[p.x()][p.y()].ReplaceRoofCap(ro->capTiles(), offset);
}

static void ReplaceRoofCap(RoofObject *ro, const QRect &r,
                           const QVector<RoofObject::RoofTile> &tiles,
                           QVector<QVector<BuildingFloorV1::Square> > &squares)
{
    if (tiles.isEmpty()) return;
    for (int y = r.top(); y <= r.bottom(); y++)
        for (int x = r.left(); x <= r.right(); x++)
            ReplaceRoofCap(ro, x, y, squares, tiles.at(x - r.left() + (y - r.top()) * r.width()));
}

static void ReplaceRoofTop(RoofObject *ro, const QRect &r,
                           QVector<QVector<BuildingFloorV1::Square> > &squares)
{
    if (r.isEmpty()) return;
    int offset = 0;
    if (ro->depth() == RoofObject::Zero)
        offset = ro->isN() ? BTC_RoofTops::North3 : BTC_RoofTops::West3;
    else if (ro->depth() == RoofObject::One)
        offset = ro->isN() ? BTC_RoofTops::North1 : BTC_RoofTops::West1;
    else if (ro->depth() == RoofObject::Two)
        offset = ro->isN() ? BTC_RoofTops::North2 : BTC_RoofTops::West2;
    else if (ro->depth() == RoofObject::Three)
        offset = ro->isN() ? BTC_RoofTops::North3 : BTC_RoofTops::West3;
    QPoint tileOffset = ro->topTiles()->offset(offset);
    QRect bounds(0, 0, squares.size(), squares[0].size());
    QRect rOffset = r.translated(tileOffset) & bounds;
    for (int x = rOffset.left(); x <= rOffset.right(); x++)
        for (int y = rOffset.top(); y <= rOffset.bottom(); y++)
            (ro->depth() == RoofObject::Zero || ro->depth() == RoofObject::Three)
                ? squares[x][y].ReplaceFloor(ro->topTiles(), offset)
                : squares[x][y].ReplaceRoofTop(ro->topTiles(), offset);
}

static void ReplaceRoofCorner(RoofObject *ro, int x, int y,
                              QVector<QVector<BuildingFloorV1::Square> > &squares,
                              RoofObject::RoofTile tile)
{
    int offset = ro->getOffset(tile);
    QPoint tileOffset = ro->slopeTiles()->offset(offset);
    QRect bounds(0, 0, squares.size(), squares[0].size());
    QPoint p = QPoint(x, y) + tileOffset;
    if (bounds.contains(p))
        squares[p.x()][p.y()].ReplaceRoof(ro->slopeTiles(), offset);
}

static void ReplaceRoofCorner(RoofObject *ro, const QRect &r,
                              const QVector<RoofObject::RoofTile> &tiles,
                              QVector<QVector<BuildingFloorV1::Square> > &squares)
{
    if (tiles.isEmpty()) return;
    for (int y = r.top(); y <= r.bottom(); y++)
        for (int x = r.left(); x <= r.right(); x++) {
            RoofObject::RoofTile tile = tiles.at(x - r.left() + (y - r.top()) * r.width());
            if (tile != RoofObject::TileCount)
                ReplaceRoofCorner(ro, x, y, squares, tile);
        }
}

static void ReplaceFurniture(int x, int y,
                             QVector<QVector<BuildingFloorV1::Square> > &squares,
                             BuildingTile *btile,
                             BuildingFloorV1::Square::SquareSection sectionMin,
                             BuildingFloorV1::Square::SquareSection sectionMax,
                             int dw = 0, int dh = 0)
{
    if (!btile)
        return;
    Q_ASSERT(dw <= 1 && dh <= 1);
    QRect bounds(0, 0, squares.size() - 1 + dw, squares[0].size() - 1 + dh);
    if (bounds.contains(x, y))
        squares[x][y].ReplaceFurniture(btile, sectionMin, sectionMax);
}

static void ReplaceDoor(Door *door, QVector<QVector<BuildingFloorV1::Square> > &squares)
{
    int x = door->x(), y = door->y();
    QRect bounds(0, 0, squares.size(), squares[0].size());
    if (bounds.contains(x, y)) {
        squares[x][y].ReplaceDoor(door->tile(),
                                  door->isW() ? BTC_Doors::West
                                              : BTC_Doors::North);
        squares[x][y].ReplaceFrame(door->frameTile(),
                                   door->isW() ? BTC_DoorFrames::West
                                               : BTC_DoorFrames::North);
    }
}

static void ReplaceWindow(Window *window, QVector<QVector<BuildingFloorV1::Square> > &squares)
{
    int x = window->x(), y = window->y();
    QRect bounds(0, 0, squares.size(), squares[0].size());
    if (bounds.contains(x, y)) {
        squares[x][y].ReplaceWindow(window->tile(),
                                    window->isW() ? BTC_Windows::West
                                                  : BTC_Windows::North);

        // Window curtains on exterior walls must be *inside* the
        // room.
        if (squares[x][y].mExterior) {
            int dx = window->isW() ? 1 : 0;
            int dy = window->isN() ? 1 : 0;
            if ((x - dx >= 0) && (y - dy >= 0))
                squares[x - dx][y - dy].ReplaceCurtains(window, true);
        } else
            squares[x][y].ReplaceCurtains(window, false);

        if (squares[x][y].mExterior) {
            if (window->isN()) {
                if (x > 0)
                    squares[x-1][y].ReplaceShutters(window, true);
                squares[x][y].ReplaceShutters(window, true);
                squares[x][y].ReplaceShutters(window, false);
                if (x < bounds.right())
                    squares[x + 1][y].ReplaceShutters(window, false);
            } else {
                if (y > 0)
                    squares[x][y - 1].ReplaceShutters(window, true);
                squares[x][y].ReplaceShutters(window, true);
                squares[x][y].ReplaceShutters(window, false);
                if (y < bounds.bottom())
                    squares[x][y + 1].ReplaceShutters(window, false);
            }
        } else {

        }
    }
}

void BuildingFloorV1::LayoutToSquares()
{
    int w = width() + 1;
    int h = height() + 1;
    // +1 for the outside walls;
    static const Square empty;
    squares.resize(w);
    for (int x = 0; x < w; x++)
        squares[x].fill(empty, h);

    BuildingTileEntry *wtype = 0;

    BuildingTileEntry *exteriorWall = mBuilding->exteriorWall();
    BuildingTileEntry *exteriorWallTrim = level() ? 0 : mBuilding->exteriorWallTrim();
    QVector<BuildingTileEntry*> interiorWalls;
    QVector<BuildingTileEntry*> interiorWallTrim;
    QVector<BuildingTileEntry*> floors;

    foreach (Room *room, mBuilding->rooms()) {
        interiorWalls += room->tile(Room::InteriorWall);
        interiorWallTrim += room->tile(Room::InteriorWallTrim);
        floors += room->tile(Room::Floor);
    }

    for (int x = 0; x < width(); x++) {
        for (int y = 0; y < height(); y++) {
            Room *room = mRoomAtPos[x][y];
            if (room != nullptr && RoofHiding::isEmptyOutside(room->Name))
                room = nullptr;
            mIndexAtPos[x][y] = room ? mBuilding->indexOf(room) : -1;
            squares[x][y].mExterior = room == 0;
        }
    }

    for (int x = 0; x < width() + 1; x++) {
        for (int y = 0; y < height() + 1; y++) {
            // Place N walls...
            if (x < width()) {
                if (y == height() && mIndexAtPos[x][y - 1] >= 0) {
                    squares[x][y].SetWallN(exteriorWall);
                    squares[x][y].SetWallTrimN(exteriorWallTrim);
                } else if (y < height() && mIndexAtPos[x][y] < 0 && y > 0 &&
                           mIndexAtPos[x][y-1] != mIndexAtPos[x][y]) {
                    squares[x][y].SetWallN(exteriorWall);
                    squares[x][y].SetWallTrimN(exteriorWallTrim);
                } else if (y < height() && (y == 0 || mIndexAtPos[x][y-1] != mIndexAtPos[x][y])
                           && mIndexAtPos[x][y] >= 0) {
                    squares[x][y].SetWallN(interiorWalls[mIndexAtPos[x][y]]);
                    squares[x][y].SetWallTrimN(interiorWallTrim[mIndexAtPos[x][y]]);
                }
            }
            // Place W walls...
            if (y < height()) {
                if (x == width() && mIndexAtPos[x - 1][y] >= 0) {
                    squares[x][y].SetWallW(exteriorWall);
                    squares[x][y].SetWallTrimW(exteriorWallTrim);
                } else if (x < width() && mIndexAtPos[x][y] < 0 && x > 0
                           && mIndexAtPos[x - 1][y] != mIndexAtPos[x][y]) {
                    squares[x][y].SetWallW(exteriorWall);
                    squares[x][y].SetWallTrimW(exteriorWallTrim);
                } else if (x < width() && mIndexAtPos[x][y] >= 0 &&
                           (x == 0 || mIndexAtPos[x - 1][y] != mIndexAtPos[x][y])) {
                    squares[x][y].SetWallW(interiorWalls[mIndexAtPos[x][y]]);
                    squares[x][y].SetWallTrimW(interiorWallTrim[mIndexAtPos[x][y]]);
                }
            }
        }
    }

    // Handle WallObjects.
    foreach (BuildingObject *object, mObjects) {
        if (WallObject *wall = object->asWall()) {
            int x = wall->x(), y = wall->y();
            if (wall->isN()) {
                QRect r = wall->bounds() & bounds(1, 0);
                for (y = r.top(); y <= r.bottom(); y++) {
                    squares[x][y].SetWallW(wall->tile(squares[x][y].mExterior
                                                      ? WallObject::TileExterior
                                                      : WallObject::TileInterior));
                    squares[x][y].SetWallTrimW(wall->tile(squares[x][y].mExterior
                                                          ? WallObject::TileExteriorTrim
                                                          : WallObject::TileInteriorTrim));
                }
            } else {
                QRect r = wall->bounds() & bounds(0, 1);
                for (x = r.left(); x <= r.right(); x++) {
                    squares[x][y].SetWallN(wall->tile(squares[x][y].mExterior
                                                      ? WallObject::TileExterior
                                                      : WallObject::TileInterior));
                    squares[x][y].SetWallTrimN(wall->tile(squares[x][y].mExterior
                                                          ? WallObject::TileExteriorTrim
                                                          : WallObject::TileInteriorTrim));
                }
            }
        }
    }

    // Furniture in the Walls layer replaces wall entries with tiles.
    QList<FurnitureObject*> wallReplacement;
    foreach (BuildingObject *object, mObjects) {
        if (FurnitureObject *fo = object->asFurniture()) {
            FurnitureTile *ftile = fo->furnitureTile()->resolved();
            if (ftile->owner()->layer() == FurnitureTiles::LayerWalls) {
                wallReplacement += fo;

                int x = fo->x(), y = fo->y();
                int dx = 0, dy = 0;
                bool killW = false, killN = false;
                switch (fo->furnitureTile()->orient()) {
                case FurnitureTile::FurnitureW:
                    killW = true;
                    break;
                case FurnitureTile::FurnitureE:
                    killW = true;
                    dx = 1;
                    break;
                case FurnitureTile::FurnitureN:
                    killN = true;
                    break;
                case FurnitureTile::FurnitureS:
                    killN = true;
                    dy = 1;
                    break;
                default:
                    break;
                }
                for (int i = 0; i < ftile->size().height(); i++) {
                    for (int j = 0; j < ftile->size().width(); j++) {
                        int sx = x + j + dx, sy = y + i + dy;
                        if (bounds(1, 1).contains(sx, sy)) {
                            Square &sq = squares[sx][sy];
                            if (killW)
                                sq.SetWallW(fo->furnitureTile(), ftile->tile(j, i));
                            if (killN)
                                sq.SetWallN(fo->furnitureTile(), ftile->tile(j, i));
                        }
                    }
                }
            }
        }
    }

    for (int x = 0; x < width() + 1; x++) {
        for (int y = 0; y < height() + 1; y++) {
            Square &s = squares[x][y];
            BuildingTileEntry *wallN = s.mWallN.entry;
            BuildingTileEntry *wallW = s.mWallW.entry;
            if (wallN || wallW) {
                if (!wallN) wallN = BuildingTilesMgr::instance()->noneTileEntry();
                if (!wallW) wallW = BuildingTilesMgr::instance()->noneTileEntry();
                if (wallN == wallW) { // may be "none"
                    s.ReplaceWall(wallN, Square::WallOrientNW);
                } else if (wallW->isNone()) {
                    s.ReplaceWall(wallN, Square::WallOrientN);
                } else if (wallN->isNone()) {
                    s.ReplaceWall(wallW, Square::WallOrientW);
                } else {
                    // Different non-none tiles.
                    s.mEntries[Square::SectionWall] = wallN;
                    s.mWallOrientation = Square::WallOrientN; // must be set before getWallOffset
                    s.mEntryEnum[Square::SectionWall] = s.getWallOffset();

                    s.mEntries[Square::SectionWall2] = wallW;
                    s.mWallOrientation = Square::WallOrientW; // must be set before getWallOffset
                    s.mEntryEnum[Square::SectionWall2] = s.getWallOffset();

                    s.mWallOrientation = Square::WallOrientNW;
                }
            }
        }
    }

    for (int x = 0; x < width() + 1; x++) {
        for (int y = 0; y < height() + 1; y++) {
            Square &sq = squares[x][y];
            if ((sq.mEntries[Square::SectionWall] &&
                    !sq.mEntries[Square::SectionWall]->isNone()) ||
                    (sq.mWallW.furniture) || (sq.mWallN.furniture))
                continue;
            // Put in the SE piece...
            if ((x > 0) && (y > 0) &&
                    (squares[x][y - 1].IsWallOrient(Square::WallOrientW) ||
                     squares[x][y - 1].IsWallOrient(Square::WallOrientNW)) &&
                    (squares[x - 1][y].IsWallOrient(Square::WallOrientN) ||
                     squares[x - 1][y].IsWallOrient(Square::WallOrientNW))) {

                // With WallObjects, there could be 2 different tiles meeting
                // at this SE corner.
                wtype = squares[x][y - 1].mWallW.entry;

                sq.ReplaceWall(wtype, Square::WallOrientSE);
            }
            // South end of a north-south wall.
            else if ((y > 0) &&
                     (squares[x][y - 1].IsWallOrient(Square::WallOrientW) ||
                      squares[x][y - 1].IsWallOrient(Square::WallOrientNW))) {
                wtype = squares[x][y - 1].mWallW.entry;
                sq.mEntries[Square::SectionWall] = wtype;
                sq.mEntryEnum[Square::SectionWall] = BTC_Walls::SouthEast;
                sq.mWallOrientation = Square::WallOrientInvalid;
            }
            // East end of a west-east wall.
            else if ((x > 0) &&
                     (squares[x - 1][y].IsWallOrient(Square::WallOrientN) ||
                      squares[x - 1][y].IsWallOrient(Square::WallOrientNW))) {
                wtype = squares[x - 1][y].mWallN.entry;
                sq.mEntries[Square::SectionWall] = wtype;
                sq.mEntryEnum[Square::SectionWall] = BTC_Walls::SouthEast;
                sq.mWallOrientation = Square::WallOrientInvalid;
            }
        }
    }

    mFlatRoofsWithDepthThree.clear();
    mStairs.clear();

    foreach (BuildingObject *object, mObjects) {
        int x = object->x();
        int y = object->y();
        if (Door *door = object->asDoor()) {
            ReplaceDoor(door, squares);
        }
        if (Window *window = object->asWindow()) {
            ReplaceWindow(window, squares);
        }
        if (Stairs *stairs = object->asStairs()) {
            // Stair objects are 5 tiles long but only have 3 tiles.
            if (stairs->isN()) {
                for (int i = 1; i <= 3; i++)
                    ReplaceFurniture(x, y + i, squares,
                                     stairs->tile()->tile(stairs->getOffset(x, y + i)),
                                     Square::SectionFurniture,
                                     Square::SectionFurniture4);
            } else {
                for (int i = 1; i <= 3; i++)
                    ReplaceFurniture(x + i, y, squares,
                                     stairs->tile()->tile(stairs->getOffset(x + i, y)),
                                     Square::SectionFurniture,
                                     Square::SectionFurniture4);
            }
            mStairs += stairs;
        }
        if (FurnitureObject *fo = object->asFurniture()) {
            FurnitureTile *ftile = fo->furnitureTile()->resolved();
            if (ftile->owner()->layer() == FurnitureTiles::LayerWalls) {
                continue;
            }
            for (int i = 0; i < ftile->size().height(); i++) {
                for (int j = 0; j < ftile->size().width(); j++) {
                    switch (ftile->owner()->layer()) {
                    case FurnitureTiles::LayerWalls:
                        // Handled after all the door/window objects
                        break;
                    case FurnitureTiles::LayerRoofCap: {
                        int dx = 0, dy = 0;
                        if (fo->furnitureTile()->isE()) ++dx;
                        if (fo->furnitureTile()->isS()) ++dy;
                        ReplaceFurniture(x + j + dx, y + i + dy,
                                         squares, ftile->tile(j, i),
                                         Square::SectionRoofCap,
                                         Square::SectionRoofCap2,
                                         dx, dy);
                        break;
                    }
                    case FurnitureTiles::LayerWallOverlay:
                        ReplaceFurniture(x + j, y + i, squares, ftile->tile(j, i),
                                         (ftile->isW() || ftile->isN()) ? Square::SectionWallOverlay : Square::SectionWallOverlay3,
                                         (ftile->isW() || ftile->isN()) ? Square::SectionWallOverlay2 : Square::SectionWallOverlay4);
                        break;
                    case FurnitureTiles::LayerWallFurniture:
                        ReplaceFurniture(x + j, y + i, squares, ftile->tile(j, i),
                                         (ftile->isW() || ftile->isN()) ? Square::SectionWallFurniture : Square::SectionWallFurniture3,
                                         (ftile->isW() || ftile->isN()) ? Square::SectionWallFurniture2 : Square::SectionWallFurniture4);
                        break;
                    case FurnitureTiles::LayerFrames: {
                        int dx = 0, dy = 0;
                        if (fo->furnitureTile()->isE()) ++dx;
                        if (fo->furnitureTile()->isS()) ++dy;
                        ReplaceFurniture(x + j + dx, y + i + dy,
                                         squares, ftile->tile(j, i),
                                         Square::SectionFrame,
                                         Square::SectionFrame,
                                         dx, dy);
                        break;
                    }
                    case FurnitureTiles::LayerDoors: {
                        int dx = 0, dy = 0;
                        if (fo->furnitureTile()->isE()) ++dx;
                        if (fo->furnitureTile()->isS()) ++dy;
                        ReplaceFurniture(x + j + dx, y + i + dy,
                                         squares, ftile->tile(j, i),
                                         Square::SectionDoor,
                                         Square::SectionDoor,
                                         dx, dy);
                        break;
                    }
                    case FurnitureTiles::LayerFurniture:
                        ReplaceFurniture(x + j, y + i, squares, ftile->tile(j, i),
                                         Square::SectionFurniture,
                                         Square::SectionFurniture4);
                        break;
                    case FurnitureTiles::LayerRoof:
                        ReplaceFurniture(x + j, y + i, squares, ftile->tile(j, i),
                                         Square::SectionRoof,
                                         Square::SectionRoof2);
                        break;
                    default:
                        Q_ASSERT(false);
                        break;
                    }
                }
            }
        }
        if (RoofObject *ro = object->asRoof()) {
            QRect r = ro->bounds();

            QRect tileRect;
            QVector<RoofObject::RoofTile> tiles;

            tiles = ro->slopeTiles(tileRect);
            ReplaceRoofSlope(ro, tileRect, tiles, squares);

            tiles = ro->westCapTiles(tileRect);
            ReplaceRoofCap(ro, tileRect, tiles, squares);

            tiles = ro->eastCapTiles(tileRect);
            ReplaceRoofCap(ro, tileRect, tiles, squares);

            tiles = ro->northCapTiles(tileRect);
            ReplaceRoofCap(ro, tileRect, tiles, squares);

            tiles = ro->southCapTiles(tileRect);
            ReplaceRoofCap(ro, tileRect, tiles, squares);

            tiles = ro->cornerTiles(tileRect);
            ReplaceRoofCorner(ro, tileRect, tiles, squares);

            // Roof tops with depth of 3 are placed in the floor layer of the
            // floor above.
            if (ro->depth() != RoofObject::Three)
                ReplaceRoofTop(ro, ro->flatTop(), squares);
            else if (!ro->flatTop().isEmpty())
                mFlatRoofsWithDepthThree += ro;
        }
    }

    foreach (FurnitureObject *fo, wallReplacement) {
        FurnitureTile *ftile = fo->furnitureTile()->resolved();
        int x = fo->x(), y = fo->y();
        int dx = 0, dy = 0;
        switch (fo->furnitureTile()->orient()) {
        case FurnitureTile::FurnitureW:
            break;
        case FurnitureTile::FurnitureE:
            dx = 1;
            break;
        case FurnitureTile::FurnitureN:
            break;
        case FurnitureTile::FurnitureS:
            dy = 1;
            break;
#if 0
        case FurnitureTile::FurnitureNW:
            s.mWallOrientation = Square::WallOrientNW;
            break;
        case FurnitureTile::FurnitureSE:
            s.mWallOrientation = Square::WallOrientSE;
            break;
#endif
        default:
            break;
        }
        for (int i = 0; i < ftile->size().height(); i++) {
            for (int j = 0; j < ftile->size().width(); j++) {
                if (bounds(1, 1).contains(x + j + dx, y + i + dy)) {
                    Square &s = squares[x + j + dx][y + i + dy];
                    Square::SquareSection section = Square::SectionWall;
                    if (s.mEntries[section] && !s.mEntries[section]->isNone()) {
                        // FIXME: if SectionWall2 is occupied, push it down to SectionWall
                        section = Square::SectionWall2;
                    }
                    if (s.mTiles[section] && !s.mTiles[section]->isNone()) {
                        // FIXME: if SectionWall2 is occupied, push it down to SectionWall
                        section = Square::SectionWall2;
                    }
                    s.mTiles[section] = ftile->tile(j, i);
                    s.mEntries[section] = 0;
                    s.mEntryEnum[section] = 0;

                    switch (fo->furnitureTile()->orient()) {
                    case FurnitureTile::FurnitureW:
                    case FurnitureTile::FurnitureE:
                        if (s.mWallOrientation == Square::WallOrientN)
                            s.mWallOrientation = Square::WallOrientNW;
                        else
                            s.mWallOrientation = Square::WallOrientW;
                        break;
                    case FurnitureTile::FurnitureN:
                    case FurnitureTile::FurnitureS:
                        if (s.mWallOrientation == Square::WallOrientW)
                            s.mWallOrientation = Square::WallOrientNW;
                        else
                            s.mWallOrientation = Square::WallOrientN;
                        break;
                    default:
                        break;
                    }
                }
            }
        }
    }

    // Place floors
    for (int x = 0; x < width(); x++) {
        for (int y = 0; y < height(); y++) {
            if (mIndexAtPos[x][y] >= 0)
                squares[x][y].ReplaceFloor(floors[mIndexAtPos[x][y]], 0);
        }
    }

#if 0 // BuildingFloorV1: this requires private member access, but we don't care about these as far as user-tiles are concerned.
    if (BuildingFloor *floorBelow = this->floorBelow()) {
        // Place flat roof tops above roofs on the floor below
        foreach (RoofObject *ro, floorBelow->mFlatRoofsWithDepthThree) {
            ReplaceRoofTop(ro, ro->flatTop(), squares);
        }

        // Nuke floors that have stairs on the floor below.
        foreach (Stairs *stairs, floorBelow->mStairs) {
            int x = stairs->x(), y = stairs->y();
            if (stairs->isW()) {
                if (x + 1 < 0 || x + 3 >= width() || y < 0 || y >= height())
                    continue;
                squares[x+1][y].ReplaceFloor(0, 0);
                squares[x+2][y].ReplaceFloor(0, 0);
                squares[x+3][y].ReplaceFloor(0, 0);
            }
            if (stairs->isN()) {
                if (x < 0 || x >= width() || y + 1 < 0 || y + 3 >= height())
                    continue;
                squares[x][y+1].ReplaceFloor(0, 0);
                squares[x][y+2].ReplaceFloor(0, 0);
                squares[x][y+3].ReplaceFloor(0, 0);
            }
        }
    }
#endif

    FloorTileGrid *userTilesWalls = mGrimeGrid.contains(QLatin1Literal("Walls")) ? mGrimeGrid[QLatin1Literal("Walls")] : 0;
    FloorTileGrid *userTilesWalls2 = mGrimeGrid.contains(QLatin1Literal("Walls2")) ? mGrimeGrid[QLatin1Literal("Walls2")] : 0;

    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            Square &sq = squares[x][y];

            sq.ReplaceWallTrim();
            if (sq.mEntryEnum[Square::SectionWall] == BTC_Walls::SouthEast) {
                if (y > 0 && squares[x][y-1].mWallW.trim) {
                    // SouthEast corner or south end of north-south wall
                    sq.mEntries[Square::SectionWallTrim] = squares[x][y-1].mWallW.trim;
                    sq.mEntryEnum[Square::SectionWallTrim] = BTC_Walls::SouthEast;
                } else if (x > 0 && squares[x-1][y].mWallN.trim) {
                    // East end of west-east wall
                    sq.mEntries[Square::SectionWallTrim] = squares[x-1][y].mWallN.trim;
                    sq.mEntryEnum[Square::SectionWallTrim] = BTC_Walls::SouthEast;
                }
            }

            if (sq.mExterior) {
                // Place exterior wall grime on level 0 only.
                if (level() > 0)
                    continue;
                QString userTileWalls = userTilesWalls ? userTilesWalls->at(x, y) : QString();
                QString userTileWalls2 = userTilesWalls2 ? userTilesWalls2->at(x, y) : QString();
                BuildingTileEntry *grimeTile = building()->tile(Building::GrimeWall);
                sq.ReplaceWallGrime(grimeTile, userTileWalls, userTileWalls2);

            } else {
                Room *room = GetRoomAt(x, y);
                BuildingTileEntry *grimeTile = room ? room->tile(Room::GrimeFloor) : 0;
                sq.ReplaceFloorGrime(grimeTile);

                QString userTileWalls = userTilesWalls ? userTilesWalls->at(x, y) : QString();
                QString userTileWalls2 = userTilesWalls2 ? userTilesWalls2->at(x, y) : QString();
                grimeTile = room ? room->tile(Room::GrimeWall) : 0;
                sq.ReplaceWallGrime(grimeTile, userTileWalls, userTileWalls2);
            }
        }
    }
}

Door *BuildingFloorV1::GetDoorAt(int x, int y)
{
    foreach (BuildingObject *o, mObjects) {
        if (!o->bounds().contains(x, y))
            continue;
        if (Door *door = o->asDoor())
            return door;
    }
    return 0;
}

Window *BuildingFloorV1::GetWindowAt(int x, int y)
{
    foreach (BuildingObject *o, mObjects) {
        if (!o->bounds().contains(x, y))
            continue;
        if (Window *window = o->asWindow())
            return window;
    }
    return 0;
}

Stairs *BuildingFloorV1::GetStairsAt(int x, int y)
{
    foreach (BuildingObject *o, mObjects) {
        if (!o->bounds().contains(x, y))
            continue;
        if (Stairs *stairs = o->asStairs())
            return stairs;
    }
    return 0;
}

FurnitureObject *BuildingFloorV1::GetFurnitureAt(int x, int y)
{
    foreach (BuildingObject *o, mObjects) {
        if (!o->bounds().contains(x, y))
            continue;
        if (FurnitureObject *fo = o->asFurniture())
            return fo;
    }
    return 0;
}

void BuildingFloorV1::SetRoomAt(int x, int y, Room *room)
{
    mRoomAtPos[x][y] = room;
}

Room *BuildingFloorV1::GetRoomAt(const QPoint &pos)
{
    if (!contains(pos))
        return 0;
    return mRoomAtPos[pos.x()][pos.y()];
}

int BuildingFloorV1::width() const
{
    return mBuilding->width();
}

int BuildingFloorV1::height() const
{
    return mBuilding->height();
}

QRect BuildingFloorV1::bounds(int dw, int dh) const
{
    return mBuilding->bounds().adjusted(0, 0, dw, dh);
}

bool BuildingFloorV1::contains(int x, int y, int dw, int dh)
{
    return bounds(dw, dh).contains(x, y);
}

bool BuildingFloorV1::contains(const QPoint &p, int dw, int dh)
{
    return contains(p.x(), p.y(), dw, dh);
}

QVector<QRect> BuildingFloorV1::roomRegion(Room *room)
{
    QRegion region;
    for (int y = 0; y < height(); y++) {
        for (int x = 0; x < width(); x++) {
            if (mRoomAtPos[x][y] == room)
                region += QRect(x, y, 1, 1);
        }
    }

    // Clean up the region by merging vertically-adjacent rectangles of the
    // same width.
    QVector<QRect> rects = region.rects();
    for (int i = 0; i < rects.size(); i++) {
        QRect r = rects[i];
        if (!r.isValid()) continue;
        for (int j = 0; j < rects.size(); j++) {
            if (i == j) continue;
            QRect r2 = rects.at(j);
            if (!r2.isValid()) continue;
            if (r2.left() == r.left() && r2.right() == r.right()) {
                if (r.bottom() + 1 == r2.top()) {
                    r.setBottom(r2.bottom());
                    rects[j] = QRect();
                } else if (r.top() == r2.bottom() + 1) {
                    r.setTop(r2.top());
                    rects[j] = QRect();
                }
            }
        }
        rects[i] = r;
    }

    QVector<QRect> result;
    foreach (QRect r, rects) {
        if (r.isValid())
            result += r;
    }
    return result;
}

QVector<QVector<Room *> > BuildingFloorV1::resizeGrid(const QSize &newSize) const
{
    QVector<QVector<Room *> > grid = mRoomAtPos;
    grid.resize(newSize.width());
    for (int x = 0; x < newSize.width(); x++)
        grid[x].resize(newSize.height());
    return grid;
}

QMap<QString,FloorTileGrid*> BuildingFloorV1::resizeGrime(const QSize &newSize) const
{
    QMap<QString,FloorTileGrid*> grid;
    foreach (QString key, mGrimeGrid.keys()) {
        grid[key] = new FloorTileGrid(newSize.width(), newSize.height());
        for (int x = 0; x < qMin(mGrimeGrid[key]->width(), newSize.width()); x++)
            for (int y = 0; y < qMin(mGrimeGrid[key]->height(), newSize.height()); y++)
                grid[key]->replace(x, y, mGrimeGrid[key]->at(x, y));

    }

    return grid;
}

void BuildingFloorV1::rotate(bool right)
{
    int oldWidth = mRoomAtPos.size();
    int oldHeight = mRoomAtPos[0].size();

    int newWidth = oldWidth, newHeight = oldHeight;
    qSwap(newWidth, newHeight);

    QVector<QVector<Room*> > roomAtPos;
    roomAtPos.resize(newWidth);
    for (int x = 0; x < newWidth; x++) {
        roomAtPos[x].resize(newHeight);
        for (int y = 0; y < newHeight; y++) {
            roomAtPos[x][y] = 0;
        }
    }

    for (int x = 0; x < oldWidth; x++) {
        for (int y = 0; y < oldHeight; y++) {
            Room *room = mRoomAtPos[x][y];
            if (right)
                roomAtPos[oldHeight - y - 1][x] = room;
            else
                roomAtPos[y][oldWidth - x - 1] = room;
        }
    }

    setGrid(roomAtPos);

    foreach (BuildingObject *object, mObjects)
        object->rotate(right);
}

void BuildingFloorV1::flip(bool horizontal)
{
    if (horizontal) {
        for (int x = 0; x < width() / 2; x++)
            mRoomAtPos[x].swap(mRoomAtPos[width() - x - 1]);
    } else {
        for (int x = 0; x < width(); x++)
            for (int y = 0; y < height() / 2; y++)
                qSwap(mRoomAtPos[x][y], mRoomAtPos[x][height() - y - 1]);
    }

    foreach (BuildingObject *object, mObjects)
        object->flip(horizontal);
}

#if 0
BuildingFloorV1 *BuildingFloorV1::clone()
{
    BuildingFloorV1 *klone = new BuildingFloorV1(mBuilding, mLevel);
    klone->mIndexAtPos = mIndexAtPos;
    klone->mRoomAtPos = mRoomAtPos;
    foreach (BuildingObject *object, mObjects) {
        BuildingObject *kloneObject = object->clone();
        kloneObject->setFloor(klone);
        klone->mObjects += kloneObject;
    }
    klone->mGrimeGrid = mGrimeGrid;
    foreach (QString key, klone->mGrimeGrid.keys())
        klone->mGrimeGrid[key] = new FloorTileGrid(*klone->mGrimeGrid[key]);
    return klone;
}
#endif

QString BuildingFloorV1::grimeAt(const QString &layerName, int x, int y) const
{
    if (mGrimeGrid.contains(layerName))
        return mGrimeGrid[layerName]->at(x, y);
    return QString();
}

FloorTileGrid *BuildingFloorV1::grimeAt(const QString &layerName, const QRect &r)
{
    if (mGrimeGrid.contains(layerName))
        return mGrimeGrid[layerName]->clone(r);
    return new FloorTileGrid(r.width(), r.height());
}

FloorTileGrid *BuildingFloorV1::grimeAt(const QString &layerName, const QRect &r,
                                      const QRegion &rgn)
{
    if (mGrimeGrid.contains(layerName))
        return mGrimeGrid[layerName]->clone(r, rgn);
    return new FloorTileGrid(r.width(), r.height());
}

QMap<QString,FloorTileGrid*> BuildingFloorV1::grimeClone() const
{
    QMap<QString,FloorTileGrid*> clone;
    foreach (QString key, mGrimeGrid.keys()) {
        clone[key] = new FloorTileGrid(*mGrimeGrid[key]);
    }
    return clone;
}

QMap<QString, FloorTileGrid *> BuildingFloorV1::setGrime(const QMap<QString,
                                                       FloorTileGrid *> &grime)
{
    QMap<QString,FloorTileGrid*> old = mGrimeGrid;
    mGrimeGrid = grime;
    return old;
}

void BuildingFloorV1::setGrime(const QString &layerName, int x, int y,
                             const QString &tileName)
{
    if (!mGrimeGrid.contains(layerName))
        mGrimeGrid[layerName] = new FloorTileGrid(width() + 1, height() + 1);
    mGrimeGrid[layerName]->replace(x, y, tileName);
}


void BuildingFloorV1::setGrime(const QString &layerName, const QPoint &p,
                             const FloorTileGrid *other)
{
    if (!mGrimeGrid.contains(layerName))
        mGrimeGrid[layerName] = new FloorTileGrid(width() + 1, height() + 1);
    mGrimeGrid[layerName]->replace(p, other);
}

void BuildingFloorV1::setGrime(const QString &layerName, const QRegion &rgn,
                             const QString &tileName)
{
    if (!mGrimeGrid.contains(layerName)) {
        if (tileName.isEmpty())
            return;
        mGrimeGrid[layerName] = new FloorTileGrid(width() + 1, height() + 1);
    }
    mGrimeGrid[layerName]->replace(rgn, tileName);
}

void BuildingFloorV1::setGrime(const QString &layerName, const QRegion &rgn,
                             const QPoint &pos, const FloorTileGrid *other)
{
    if (!mGrimeGrid.contains(layerName))
        mGrimeGrid[layerName] = new FloorTileGrid(width() + 1, height() + 1);
    mGrimeGrid[layerName]->replace(rgn, pos, other);
}

bool BuildingFloorV1::hasUserTiles() const
{
    foreach (FloorTileGrid *g, mGrimeGrid)
        if (!g->isEmpty()) return true;
    return false;
}

bool BuildingFloorV1::hasUserTiles(const QString &layerName)
{
    if (mGrimeGrid.contains(layerName))
        return !mGrimeGrid[layerName]->isEmpty();
    return false;
}

/////

BuildingFloorV1::Square::Square() :
    mEntries(MaxSection, 0),
    mEntryEnum(MaxSection, 0),
    mWallOrientation(WallOrientInvalid),
    mExterior(true),
    mTiles(MaxSection, 0)
{
}


BuildingFloorV1::Square::~Square()
{
    // mTiles are owned by BuildingTiles
//    for (int i = 0; i < MaxSection; i++)
    //        delete mTiles[i];
}

void BuildingFloorV1::Square::SetWallN(BuildingTileEntry *tile)
{
    mWallN.entry = tile;
    mWallN.furniture = 0;
    mWallN.furnitureBldgTile = 0;
}

void BuildingFloorV1::Square::SetWallW(BuildingTileEntry *tile)
{
    mWallW.entry = tile;
    mWallW.furniture = 0;
    mWallW.furnitureBldgTile = 0;
}

void BuildingFloorV1::Square::SetWallN(FurnitureTile *ftile, BuildingTile *btile)
{
    mWallN.entry = 0;
    mWallN.furniture = ftile;
    mWallN.furnitureBldgTile = btile;
}

void BuildingFloorV1::Square::SetWallW(FurnitureTile *ftile, BuildingTile *btile)
{
    mWallW.entry = 0;
    mWallW.furniture = ftile;
    mWallW.furnitureBldgTile = btile;
}

void BuildingFloorV1::Square::SetWallTrimN(BuildingTileEntry *tile)
{
    mWallN.trim = tile;
}

void BuildingFloorV1::Square::SetWallTrimW(BuildingTileEntry *tile)
{
    mWallW.trim = tile;
}

bool BuildingFloorV1::Square::IsWallOrient(BuildingFloorV1::Square::WallOrientation orient)
{
    return mEntries[SectionWall] &&
            !mEntries[SectionWall]->isNone() &&
            (mWallOrientation == orient);
}

void BuildingFloorV1::Square::ReplaceFloor(BuildingTileEntry *tile, int offset)
{
    mEntries[SectionFloor] = tile;
    mEntryEnum[SectionFloor] = offset;
}

void BuildingFloorV1::Square::ReplaceWall(BuildingTileEntry *tile,
                                        WallOrientation orient)
{
    mEntries[SectionWall] = tile;
    mWallOrientation = orient; // Must set this before getWallOffset() is called
    mEntryEnum[SectionWall] = getWallOffset();
}

void BuildingFloorV1::Square::ReplaceDoor(BuildingTileEntry *tile, int offset)
{
    // Must put a non-zero tile here.  See getWallOffset().
    mEntries[SectionDoor] = tile ? tile : BuildingTilesMgr::instance()->noneTileEntry();
    mEntryEnum[SectionDoor] = offset;

    // Nuke any existing window
    if (mEntries[SectionWindow]) {
        if ((mEntryEnum[SectionWindow] == BTC_Windows::West) == (offset == BTC_Doors::West)) {
            mEntries[SectionWindow] = 0;
            mEntryEnum[SectionWindow] = 0;
        }
    }

    if (mWallOrientation == WallOrientNW) {
        BuildingTileEntry *entry1 = mEntries[SectionWall];
        BuildingTileEntry *entry2 = mEntries[SectionWall2];
        if (!entry1) entry1 = BuildingTilesMgr::instance()->noneTileEntry();
        if (!entry2) entry2 = BuildingTilesMgr::instance()->noneTileEntry();
        if (!entry1->isNone() && !entry2->isNone()) {
            // 2 different walls
            if (offset == BTC_Doors::West) {
                if (mEntryEnum[SectionWall] == BTC_Walls::West || mEntryEnum[SectionWall] == BTC_Walls::WestWindow)
                    mEntryEnum[SectionWall] = BTC_Walls::WestDoor;
                else if (mEntryEnum[SectionWall2] == BTC_Walls::West || mEntryEnum[SectionWall2] == BTC_Walls::WestWindow)
                    mEntryEnum[SectionWall2] = BTC_Walls::WestDoor;
            } else if (offset == BTC_Doors::North) {
                if (mEntryEnum[SectionWall] == BTC_Walls::North || mEntryEnum[SectionWall] == BTC_Walls::NorthWindow)
                    mEntryEnum[SectionWall] = BTC_Walls::NorthDoor;
                else if (mEntryEnum[SectionWall2] == BTC_Walls::North || mEntryEnum[SectionWall2] == BTC_Walls::NorthWindow)
                    mEntryEnum[SectionWall2] = BTC_Walls::NorthDoor;
            }
        } else {
            // Single NW tile -> split into 2.
            // Stack the wall containing the door on top of the other wall.
            mEntries[SectionWall2] = entry1;
            if (offset == BTC_Doors::West) {
                mEntryEnum[SectionWall] = BTC_Walls::North;
                mEntryEnum[SectionWall2] = BTC_Walls::WestDoor;
            } else {
                mEntryEnum[SectionWall] = BTC_Walls::West;
                mEntryEnum[SectionWall2] = BTC_Walls::NorthDoor;
            }
        }
        return;
    }

    if (mWallOrientation != WallOrientInvalid)
        mEntryEnum[SectionWall] = getWallOffset();
}

void BuildingFloorV1::Square::ReplaceFrame(BuildingTileEntry *tile, int offset)
{
    // Must put a non-zero tile here.  See getWallOffset().
    mEntries[SectionFrame] = tile ? tile : BuildingTilesMgr::instance()->noneTileEntry();
    mEntryEnum[SectionFrame] = offset;
}

void BuildingFloorV1::Square::ReplaceWindow(BuildingTileEntry *tile, int offset)
{
    // Must put a non-zero tile here.  See getWallOffset().
    mEntries[SectionWindow] = tile ? tile : BuildingTilesMgr::instance()->noneTileEntry();
    mEntryEnum[SectionWindow] = offset;

    // Nuke any existing door
    if (mEntries[SectionDoor]) {
        if ((mEntryEnum[SectionDoor] == BTC_Doors::West) == (offset == BTC_Windows::West)) {
            mEntries[SectionDoor] = 0;
            mEntryEnum[SectionDoor] = 0;
        }
    }

    if (mWallOrientation == WallOrientNW) {
        BuildingTileEntry *entry1 = mEntries[SectionWall];
        BuildingTileEntry *entry2 = mEntries[SectionWall2];
        if (!entry1) entry1 = BuildingTilesMgr::instance()->noneTileEntry();
        if (!entry2) entry2 = BuildingTilesMgr::instance()->noneTileEntry();
        if (!entry1->isNone() && !entry2->isNone()) {
            // 2 different walls
            if (offset == BTC_Windows::West) {
                if (mEntryEnum[SectionWall] == BTC_Walls::West || mEntryEnum[SectionWall] == BTC_Walls::WestDoor)
                    mEntryEnum[SectionWall] = BTC_Walls::WestWindow;
                else if (mEntryEnum[SectionWall2] == BTC_Walls::West || mEntryEnum[SectionWall2] == BTC_Walls::WestDoor)
                    mEntryEnum[SectionWall2] = BTC_Walls::WestWindow;
            } else if (offset == BTC_Windows::North) {
                if (mEntryEnum[SectionWall] == BTC_Walls::North || mEntryEnum[SectionWall] == BTC_Walls::NorthDoor)
                    mEntryEnum[SectionWall] = BTC_Walls::NorthWindow;
                else if (mEntryEnum[SectionWall2] == BTC_Walls::North || mEntryEnum[SectionWall2] == BTC_Walls::NorthDoor)
                    mEntryEnum[SectionWall2] = BTC_Walls::NorthWindow;
            }
        } else {
            // Single NW tile -> split into 2.
            // Stack the wall containing the window on top of the other wall.
            mEntries[SectionWall2] = entry1;
            if (offset == BTC_Windows::West) {
                mEntryEnum[SectionWall] = BTC_Walls::North;
                mEntryEnum[SectionWall2] = BTC_Walls::WestWindow;
            } else {
                mEntryEnum[SectionWall] = BTC_Walls::West;
                mEntryEnum[SectionWall2] = BTC_Walls::NorthWindow;
            }
        }
        return;
    }

    if (mWallOrientation != WallOrientInvalid)
        mEntryEnum[SectionWall] = getWallOffset();
}

void BuildingFloorV1::Square::ReplaceCurtains(Window *window, bool exterior)
{
    mEntries[exterior ? SectionCurtains2 : SectionCurtains] = window->curtainsTile();
    mEntryEnum[exterior ? SectionCurtains2 : SectionCurtains] = window->isW()
            ? (exterior ? BTC_Curtains::East : BTC_Curtains::West)
            : (exterior ? BTC_Curtains::South : BTC_Curtains::North);
}

void BuildingFloorV1::Square::ReplaceShutters(Window *window, bool first)
{
    if (!window->shuttersTile() || window->shuttersTile()->isNone())
        return;

    if (mExterior) {
        if (window->isN()) {
            ReplaceFurniture(window->shuttersTile()->tile(first ? BTC_Shutters::NorthLeft : BTC_Shutters::NorthRight),
                             SectionWallFurniture, SectionWallFurniture2);
        } else {
            ReplaceFurniture(window->shuttersTile()->tile(first ? BTC_Shutters::WestAbove : BTC_Shutters::WestBelow),
                             SectionWallFurniture, SectionWallFurniture2);
        }
    } else {

    }
}

void BuildingFloorV1::Square::ReplaceFurniture(BuildingTileEntry *tile, int offset)
{
    if (offset < 0) { // see getStairsOffset
        mEntries[SectionFurniture] = 0;
        mEntryEnum[SectionFurniture] = 0;
        return;
    }
    if (mEntries[SectionFurniture] && !mEntries[SectionFurniture]->isNone()) {
        mEntries[SectionFurniture2] = tile;
        mEntryEnum[SectionFurniture2] = offset;
        return;
    }
    mEntries[SectionFurniture] = tile;
    mEntryEnum[SectionFurniture] = offset;
}

void BuildingFloorV1::Square::ReplaceFurniture(BuildingTile *btile,
                                             SquareSection sectionMin,
                                             SquareSection sectionMax)
{
    for (int s = sectionMin; s <= sectionMax; s++) {
        if (!mTiles[s] || mTiles[s]->isNone()) {
            mTiles[s] = btile;
            mEntryEnum[s] = 0;
            return;
        }
    }
    for (int s = sectionMin + 1; s <= sectionMax; s++) {
        mTiles[s-1] = mTiles[s];
        mEntryEnum[s-1] = mEntryEnum[s];
    }
    mTiles[sectionMax] = btile;
    mEntryEnum[sectionMax] = 0;
}

void BuildingFloorV1::Square::ReplaceRoof(BuildingTileEntry *tile, int offset)
{
    if (mEntries[SectionRoof] && !mEntries[SectionRoof]->isNone()) {
        mEntries[SectionRoof2] = tile;
        mEntryEnum[SectionRoof2] = offset;
        return;
    }
    mEntries[SectionRoof] = tile;
    mEntryEnum[SectionRoof] = offset;
}

void BuildingFloorV1::Square::ReplaceRoofCap(BuildingTileEntry *tile, int offset)
{
    if (mEntries[SectionRoofCap] && !mEntries[SectionRoofCap]->isNone()) {
        mEntries[SectionRoofCap2] = tile;
        mEntryEnum[SectionRoofCap2] = offset;
        return;
    }
    mEntries[SectionRoofCap] = tile;
    mEntryEnum[SectionRoofCap] = offset;
}

void BuildingFloorV1::Square::ReplaceRoofTop(BuildingTileEntry *tile, int offset)
{
    mEntries[SectionRoofTop] = tile;
    mEntryEnum[SectionRoofTop] = offset;
}

void BuildingFloorV1::Square::ReplaceFloorGrime(BuildingTileEntry *grimeTile)
{
    if (!grimeTile || grimeTile->isNone())
        return;

    BuildingTileEntry *wallTile1 = mEntries[SectionWall];
    if (wallTile1 && wallTile1->isNone()) wallTile1 = 0;

    BuildingTileEntry *wallTile2 = mEntries[SectionWall2];
    if (wallTile2 && wallTile2->isNone()) wallTile2 = 0;

    Q_ASSERT(!(wallTile1 == 0 && wallTile2 != 0));

    int grimeEnumW = -1, grimeEnumN = -1;

    if (wallTile1) {
        switch (mEntryEnum[SectionWall]) {
        case BTC_Walls::West:
        case BTC_Walls::WestWindow: grimeEnumW = BTC_GrimeFloor::West; break;
        case BTC_Walls::WestDoor: break; // no grime by doors
        case BTC_Walls::North:
        case BTC_Walls::NorthWindow: grimeEnumN = BTC_GrimeFloor::North; break;
        case BTC_Walls::NorthDoor: break; // no grime by doors
        case BTC_Walls::NorthWest:
            grimeEnumW = BTC_GrimeFloor::West;
            grimeEnumN = BTC_GrimeFloor::North;
            break;
        case BTC_Walls::SouthEast:
            // Hack - ignore "end cap" of a wall object.
            if (mWallOrientation != Square::WallOrientSE)
                break;
            grimeEnumW = BTC_GrimeFloor::NorthWest;
            break;
        }
    }
    if (wallTile2) {
        switch (mEntryEnum[SectionWall2]) {
        case BTC_Walls::West:
        case BTC_Walls::WestWindow: grimeEnumW = BTC_GrimeFloor::West; break;
        case BTC_Walls::WestDoor: break;
        case BTC_Walls::North:
        case BTC_Walls::NorthWindow: grimeEnumN = BTC_GrimeFloor::North; break;
        case BTC_Walls::NorthDoor: break;
        case BTC_Walls::NorthWest: Q_ASSERT(false); break;
        case BTC_Walls::SouthEast: Q_ASSERT(false); break;
        }
    }

    // Handle furniture in Walls layer
    if (mWallW.furniture && mWallW.furniture->resolved()->allowGrime())
        grimeEnumW = BTC_GrimeFloor::West;
    if (mWallN.furniture && mWallN.furniture->resolved()->allowGrime())
        grimeEnumN = BTC_GrimeFloor::North;

    if (grimeEnumW >= 0) {
        mEntries[SectionFloorGrime] = grimeTile;
        mEntryEnum[SectionFloorGrime] = grimeEnumW;
    }
    if (grimeEnumN >= 0) {
        mEntries[SectionFloorGrime2] = grimeTile;
        mEntryEnum[SectionFloorGrime2] = grimeEnumN;
    }
}

#include "tiledeffile.h"
#include "tilemetainfomgr.h"

namespace
{
struct GrimeProperties
{
    bool West;
    bool North;
    bool SouthEast;
    bool FullWindow;
    bool Trim;
    bool DoubleLeft;
    bool DoubleRight;
};
}

static bool tileHasGrimeProperties(BuildingTile *btile, GrimeProperties *props)
{
    if (btile == nullptr)
        return false;

    Tiled::Internal::TileDefWatcher *tileDefWatcher = getTileDefWatcher();
    tileDefWatcher->check();

    if (props) {
        props->West = props->North = props->SouthEast = false;
        props->FullWindow = props->Trim = false;
        props->DoubleLeft = props->DoubleRight = false;
    }

    if (Tiled::Internal::TileDefTileset *tdts = tileDefWatcher->mTileDefFile->tileset(btile->mTilesetName)) {
        if (Tiled::Internal::TileDefTile *tdt = tdts->tileAt(btile->mIndex)) {
            if (tdt->mProperties.contains(QString::fromLatin1("GrimeType"))) {
                if (props) {
                    if (tdt->mProperties.contains(QString::fromLatin1("DoorWallW")) ||
                            tdt->mProperties.contains(QString::fromLatin1("WallW")) ||
                            tdt->mProperties.contains(QString::fromLatin1("WallWTrans")) ||
                            tdt->mProperties.contains(QString::fromLatin1("windowW")))
                        props->West = true;
                    else if (tdt->mProperties.contains(QString::fromLatin1("DoorWallN")) ||
                             tdt->mProperties.contains(QString::fromLatin1("WallN")) ||
                             tdt->mProperties.contains(QString::fromLatin1("WallNTrans")) ||
                             tdt->mProperties.contains(QString::fromLatin1("windowN")))
                        props->North = true;
                    else if (tdt->mProperties.contains(QString::fromLatin1("WallNW")))
                        props->West = props->North = true;
                    else if (tdt->mProperties.contains(QString::fromLatin1("WallSE")))
                        props->SouthEast = true;
                    if (tdt->mProperties.contains(QLatin1Literal("GrimeType"))) {
                        props->FullWindow = tdt->mProperties[QLatin1Literal("GrimeType")] == QLatin1Literal("FullWindow");
                        props->Trim = tdt->mProperties[QLatin1Literal("GrimeType")] == QLatin1Literal("Trim");
                        props->DoubleLeft = tdt->mProperties[QLatin1Literal("GrimeType")] == QLatin1Literal("DoubleLeft");
                        props->DoubleRight = tdt->mProperties[QLatin1Literal("GrimeType")] == QLatin1Literal("DoubleRight");
                    }
                }
                return true;
            }
            if (tdt->mProperties.contains(QString::fromLatin1("WallW"))) {
                if (props) {
                    props->West = true; // regular west wall
                }
                return true;
            }
            if (tdt->mProperties.contains(QString::fromLatin1("WallN"))) {
                if (props) {
                    props->North = true; // regular north wall
                }
                return true;
            }
        }
    }

    return false;
}

void BuildingFloorV1::Square::ReplaceWallGrime(BuildingTileEntry *grimeTile, const QString &userTileWalls, const QString &userTileWalls2)
{
    if (!grimeTile || grimeTile->isNone())
        return;

    BuildingTileEntry *tileEntry1 = mEntries[SectionWall];
    if (tileEntry1 && tileEntry1->isNone())
        tileEntry1 = 0;

    BuildingTileEntry *tileEntry2 = mEntries[SectionWall2];
    if (tileEntry2 && tileEntry2->isNone())
        tileEntry2 = 0;

    Q_ASSERT(!(tileEntry1 == 0 && tileEntry2 != 0));

    BuildingTile *tileW = 0, *tileN = 0, *tileNW = 0, *tileSE = 0;
    int grimeEnumW = -1, grimeEnumN = -1;

    if (tileEntry1) {
        BuildingTile *btile = tileEntry1->tile(mEntryEnum[SectionWall]);
        switch (mEntryEnum[SectionWall]) {
        case BTC_Walls::West:
            tileW = btile;
            grimeEnumW = BTC_GrimeWall::West;
            break;
        case BTC_Walls::WestDoor:
            tileW = btile;
            grimeEnumW = BTC_GrimeWall::WestDoor;
            break;
        case BTC_Walls::WestWindow:
            tileW = btile;
            grimeEnumW = BTC_GrimeWall::WestWindow;
            break;
        case BTC_Walls::North:
            grimeEnumN = BTC_GrimeWall::North;
            tileN = btile;
            break;
        case BTC_Walls::NorthDoor:
            tileN = btile;
            grimeEnumN = BTC_GrimeWall::NorthDoor;
            break;
        case BTC_Walls::NorthWindow:
            tileN = btile;
            grimeEnumN = BTC_GrimeWall::NorthWindow;
            break;
        case BTC_Walls::NorthWest:
            tileNW = btile;
            grimeEnumW = BTC_GrimeWall::NorthWest;
            grimeEnumN = -1;
            break;
        case BTC_Walls::SouthEast:
            tileSE = btile;
            grimeEnumW = BTC_GrimeWall::SouthEast;
            break;
        }
    }
    if (tileEntry2) {
        BuildingTile *btile = tileEntry2->tile(mEntryEnum[SectionWall2]);
        switch (mEntryEnum[SectionWall2]) {
        case BTC_Walls::West:
            tileW = btile;
            grimeEnumW = BTC_GrimeWall::West;
            break;
        case BTC_Walls::WestDoor:
            tileW = btile;
            grimeEnumW = BTC_GrimeWall::WestDoor;
            break;
        case BTC_Walls::WestWindow:
            tileW = btile;
            grimeEnumW = BTC_GrimeWall::WestWindow;
            break;
        case BTC_Walls::North:
            grimeEnumN = BTC_GrimeWall::North;
            tileN = btile;
            break;
        case BTC_Walls::NorthDoor:
            tileN = btile;
            grimeEnumN = BTC_GrimeWall::NorthDoor;
            break;
        case BTC_Walls::NorthWindow:
            tileN = btile;
            grimeEnumN = BTC_GrimeWall::NorthWindow;
            break;
        case BTC_Walls::NorthWest:
            tileNW = btile;
            grimeEnumW = BTC_GrimeWall::NorthWest;
            grimeEnumN = -1;
            break;
        case BTC_Walls::SouthEast:
            tileSE = btile;
            grimeEnumW = BTC_GrimeWall::SouthEast;
            break;
        }
    }

    if (mWallW.furnitureBldgTile && !mWallW.furnitureBldgTile->isNone() && mWallW.furniture->resolved()->allowGrime()) {
        tileW = mWallW.furnitureBldgTile;
        grimeEnumW = BTC_GrimeWall::West;
    }
    if (mWallN.furnitureBldgTile && !mWallN.furnitureBldgTile->isNone() && mWallN.furniture->resolved()->allowGrime()) {
        tileN = mWallN.furnitureBldgTile;
        grimeEnumN = BTC_GrimeWall::North;
    }

    GrimeProperties props;

    if (tileNW && tileHasGrimeProperties(tileNW, &props)) {
        if (props.FullWindow)
            grimeEnumW = grimeEnumN = -1;
        else if (props.Trim) {
            grimeEnumW = BTC_GrimeWall::NorthWestTrim;
            grimeEnumN = -1;
        }
    }
    if (tileW && tileHasGrimeProperties(tileW, &props)) {
        if (props.FullWindow)
            grimeEnumW = -1;
        else if (props.Trim)
            grimeEnumW = BTC_GrimeWall::WestTrim;
        else if (props.DoubleLeft)
            grimeEnumW = BTC_GrimeWall::WestDoubleLeft;
        else if (props.DoubleRight)
            grimeEnumW = BTC_GrimeWall::WestDoubleRight;
    }
    if (tileN && tileHasGrimeProperties(tileN, &props)) {
        if (props.FullWindow)
            grimeEnumN = -1;
        else if (props.Trim)
            grimeEnumN = BTC_GrimeWall::NorthTrim;
        else if (props.DoubleLeft)
            grimeEnumN = BTC_GrimeWall::NorthDoubleLeft;
        else if (props.DoubleRight)
            grimeEnumN = BTC_GrimeWall::NorthDoubleRight;
    }

    if (!userTileWalls.isEmpty()) {
        if (BuildingTile *btile = BuildingTilesMgr::instance()->get(userTileWalls)) {
            if (tileHasGrimeProperties(btile, &props)) {
                if (props.FullWindow) {
                    if (props.West) grimeEnumW = -1;
                    if (props.North) grimeEnumN = -1;
                } else if (props.Trim) {
                    if (props.West && props.North) {
                        grimeEnumW = BTC_GrimeWall::NorthWestTrim;
                        grimeEnumN = -1;
                    } else if (props.West) {
                        grimeEnumW = BTC_GrimeWall::WestTrim;
                    } else if (props.North) {
                        grimeEnumN = BTC_GrimeWall::NorthTrim;
                    } else if (props.SouthEast) {
                        grimeEnumW = BTC_GrimeWall::SouthEastTrim;
                    }
                } else if (props.DoubleLeft) {
                    if (props.West) grimeEnumW = BTC_GrimeWall::WestDoubleLeft;
                    else if (props.North) grimeEnumN = BTC_GrimeWall::NorthDoubleLeft;
                } else if (props.DoubleRight) {
                    if (props.West) grimeEnumW = BTC_GrimeWall::WestDoubleRight;
                    else if (props.North) grimeEnumN = BTC_GrimeWall::NorthDoubleRight;
                } else if (props.West) {
                    grimeEnumW = BTC_GrimeWall::West;
                } else if (props.North) {
                    grimeEnumN = BTC_GrimeWall::North;
                }
            }
        }
    }
    if (!userTileWalls2.isEmpty()) {
        if (BuildingTile *btile = BuildingTilesMgr::instance()->get(userTileWalls2)) {
            if (tileHasGrimeProperties(btile, &props)) {
                if (props.FullWindow) {
                    if (props.West) grimeEnumW = -1;
                    if (props.North) grimeEnumN = -1;
                } else if (props.Trim) {
                    if (props.West && props.North) {
                        grimeEnumW = BTC_GrimeWall::NorthWestTrim;
                        grimeEnumN = -1;
                    } else if (props.West) {
                        grimeEnumW = BTC_GrimeWall::WestTrim;
                    } else if (props.North) {
                        grimeEnumN = BTC_GrimeWall::NorthTrim;
                    } else if (props.SouthEast) {
                        grimeEnumW = BTC_GrimeWall::SouthEastTrim;
                    }
                } else if (props.DoubleLeft) {
                    if (props.West) grimeEnumW = BTC_GrimeWall::WestDoubleLeft;
                    else if (props.North) grimeEnumN = BTC_GrimeWall::NorthDoubleLeft;
                } else if (props.DoubleRight) {
                    if (props.West) grimeEnumW = BTC_GrimeWall::WestDoubleRight;
                    else if (props.North) grimeEnumN = BTC_GrimeWall::NorthDoubleRight;
                } else if (props.West) {
                    grimeEnumW = BTC_GrimeWall::West;
                } else if (props.North) {
                    grimeEnumN = BTC_GrimeWall::North;
                }
            }
        }
    }

    // 2 different wall tiles in a NW corner can't use a single NW grime tile.
    if (grimeEnumW == BTC_GrimeWall::NorthWest && grimeEnumN >= 0) {
        grimeEnumW = BTC_GrimeWall::West;
    }
    if (grimeEnumW == BTC_GrimeWall::NorthWestTrim && grimeEnumN >= 0) {
        grimeEnumW = BTC_GrimeWall::WestTrim;
    }

    if (grimeEnumW >= 0) {
        mEntries[SectionWallGrime] = grimeTile;
        mEntryEnum[SectionWallGrime] = grimeEnumW;
    }
    if (grimeEnumN >= 0) {
        mEntries[SectionWallGrime2] = grimeTile;
        mEntryEnum[SectionWallGrime2] = grimeEnumN;
    }
}

void BuildingFloorV1::Square::ReplaceWallTrim()
{
    BuildingTileEntry *wallTile1 = mEntries[SectionWall];
    if (wallTile1 && wallTile1->isNone()) wallTile1 = 0;

    BuildingTileEntry *wallTile2 = mEntries[SectionWall2];
    if (wallTile2 && wallTile2->isNone()) wallTile2 = 0;

    if (!wallTile1 && !wallTile2) return;

    BuildingTileEntry *trimW = mWallW.trim;
    BuildingTileEntry *trimN = mWallN.trim;

    // No trim over furniture in the Walls layer
    if (trimW && mWallW.furniture) trimW = 0;
    if (trimN && mWallN.furniture) trimN = 0;

    if (!trimW && !trimN) return;

    int enumW = -1;
    int enumN = -1;

    // SectionWall could be a west, north, or north-west wall
    if (wallTile1) {
        if (mEntryEnum[SectionWall] == BTC_Walls::West ||
                mEntryEnum[SectionWall] == BTC_Walls::WestDoor ||
                mEntryEnum[SectionWall] == BTC_Walls::WestWindow ||
                mEntryEnum[SectionWall] == BTC_Walls::NorthWest) {
            enumW = mEntryEnum[SectionWall];
            if (enumW == BTC_Walls::NorthWest) {
                enumW = BTC_Walls::West;
                enumN = BTC_Walls::North;
            }
        } else {
            enumN = mEntryEnum[SectionWall];
        }
    }

    // Use the same stacking order as the walls
    int sectionW = SectionWallTrim2;
    int sectionN = SectionWallTrim;

    // 2 different wall tiles forming north-west corner
    if (wallTile2) {
        if (mEntryEnum[SectionWall2] == BTC_Walls::West ||
                mEntryEnum[SectionWall2] == BTC_Walls::WestDoor ||
                mEntryEnum[SectionWall2] == BTC_Walls::WestWindow) {
            enumW = mEntryEnum[SectionWall2];
        } else {
            enumN = mEntryEnum[SectionWall2];
            qSwap(sectionW, sectionN);
        }
    }

    if ((enumW == BTC_Walls::West && enumN == BTC_Walls::North) && (trimW == trimN)) {
        if (sectionN == SectionWallTrim2) {
            enumN = BTC_Walls::NorthWest;
            enumW = -1;
        } else {
            enumW = BTC_Walls::NorthWest;
            enumN = -1;
        }
    }

    if (enumW >= 0) {
        mEntries[sectionW] = trimW;
        mEntryEnum[sectionW] = enumW;
    }
    if (enumN >= 0) {
        mEntries[sectionN] = trimN;
        mEntryEnum[sectionN] = enumN;
    }
}

int BuildingFloorV1::Square::getWallOffset()
{
    BuildingTileEntry *tile = mEntries[SectionWall];
    if (!tile)
        return -1;

    int offset = BTC_Walls::West;

    switch (mWallOrientation) {
    case WallOrientN:
        if (mEntries[SectionDoor] != 0 &&
                mEntryEnum[SectionDoor] == BTC_Doors::North)
            offset = BTC_Walls::NorthDoor;
        else if (mEntries[SectionWindow] != 0 &&
                 mEntryEnum[SectionWindow] == BTC_Windows::North)
            offset = BTC_Walls::NorthWindow;
        else
            offset = BTC_Walls::North;
        break;
    case WallOrientNW:
        offset = BTC_Walls::NorthWest;
        break;
    case WallOrientW:
        if (mEntries[SectionDoor] != 0 &&
                mEntryEnum[SectionDoor] == BTC_Doors::West)
            offset = BTC_Walls::WestDoor;
        else if (mEntries[SectionWindow] != 0 &&
                 mEntryEnum[SectionWindow] == BTC_Windows::West)
            offset = BTC_Walls::WestWindow;
        break;
    case WallOrientSE:
        offset = BTC_Walls::SouthEast;
        break;
    default:
        Q_ASSERT(false);
        break;
    }

    return offset;
}

#endif
