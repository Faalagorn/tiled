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

#ifndef BUILDINGDOCUMENT_H
#define BUILDINGDOCUMENT_H

#include <QObject>
#include <QPoint>
#include <QSet>
#include <QSize>

class QUndoStack;

namespace BuildingEditor {

class BuildingObject;
class Building;
class BuildingFloor;
class BuildingTile;
class Door;
class FurnitureObject;
class FurnitureTile;
class Room;
class Window;

class BuildingDocument : public QObject
{
    Q_OBJECT
public:
    explicit BuildingDocument(Building *building, const QString &fileName);

    Building *building() const
    { return mBuilding; }

    QString fileName() const
    { return mFileName; }

    static BuildingDocument *read(const QString &fileName, QString &error);
    bool write(const QString &fileName, QString &error);

    void setCurrentFloor(BuildingFloor *floor);

    BuildingFloor *currentFloor() const
    { return mCurrentFloor; }

    QUndoStack *undoStack() const
    { return mUndoStack; }

    bool isModified() const;

    void setSelectedObjects(const QSet<BuildingObject*> &selection);

    const QSet<BuildingObject*> &selectedObjects() const
    { return mSelectedObjects; }

    void emitBuildingResized()
    { emit buildingResized(); }

    // +UNDO/REDO
    Room *changeRoomAtPosition(BuildingFloor *floor, const QPoint &pos, Room *room);
    BuildingTile *changeEWall(BuildingTile *tile);
    BuildingTile *changeWallForRoom(Room *room, BuildingTile *tile);
    BuildingTile *changeFloorForRoom(Room *room, BuildingTile *tile);
    void insertFloor(int index, BuildingFloor *floor);
    void insertObject(BuildingFloor *floor, int index, BuildingObject *object);
    BuildingObject *removeObject(BuildingFloor *floor, int index);
    QPoint moveObject(BuildingObject *object, const QPoint &pos);
    BuildingTile *changeDoorTile(Door *door, BuildingTile *tile, bool isFrame);
    BuildingTile *changeObjectTile(BuildingObject *object, BuildingTile *tile);

    void insertRoom(int index, Room *room);
    Room *removeRoom(int index);
    int reorderRoom(int index, Room *room);
    Room *changeRoom(Room *room, const Room *data);

    QVector<QVector<Room *> > swapFloorGrid(BuildingFloor *floor,
                                            const QVector<QVector<Room *> > &grid);

    QSize resizeBuilding(const QSize &newSize);
    QVector<QVector<Room *> > resizeFloor(BuildingFloor *floor,
                                          const QVector<QVector<Room *> > &grid);
    void rotateBuilding(bool right);
    void flipBuilding(bool horizontal);

    FurnitureTile *changeFurnitureTile(FurnitureObject *object, FurnitureTile *ftile);
    // -UNDO/REDO
    
signals:
    void currentFloorChanged();

    void roomAtPositionChanged(BuildingFloor *floor, const QPoint &pos);
    void roomDefinitionChanged();

    void floorAdded(BuildingFloor *floor);
    void floorEdited(BuildingFloor *floor);

    void objectAdded(BuildingObject *object);
    void objectAboutToBeRemoved(BuildingObject *object);
    void objectRemoved(BuildingFloor *floor, int index);
    void objectMoved(BuildingObject *object);
    void objectTileChanged(BuildingObject *object);

    void roomAdded(Room *room);
    void roomAboutToBeRemoved(Room *room);
    void roomRemoved(Room *room);
    void roomsReordered();
    void roomChanged(Room *room);

    void buildingResized();
    void buildingRotated();

    void selectedObjectsChanged();

public slots:
    
private:
    Building *mBuilding;
    QString mFileName;
    QUndoStack *mUndoStack;
    BuildingFloor *mCurrentFloor;
    QSet<BuildingObject*> mSelectedObjects;
};

} // namespace BuildingEditor

#endif // BUILDINGDOCUMENT_H