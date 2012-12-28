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

#ifndef BUILDINGTMX_H
#define BUILDINGTMX_H

#include <QObject>
#include <QStringList>

class MapComposite;

namespace Tiled {
class Tileset;
}

namespace BuildingEditor {

class Building;

class BuildingTMX : public QObject
{
public:
    static BuildingTMX *instance();
    static void deleteInstance();

    BuildingTMX();

//    const QList<LayerInfo> &layers() const
//    { return mLayers; }

    bool exportTMX(Building *building, const MapComposite *mapComposite,
                   const QString &fileName);

    QString txtName();
    QString txtPath();

    bool readTxt();
    bool writeTxt();

    Tiled::Tileset *loadTileset(const QString &imageSource);

    QString errorString() const
    { return mError; }

private:
    bool upgradeTxt();
    bool mergeTxt();

    class LayerInfo
    {
    public:
        enum Type {
            Tile,
            Object
        };

        LayerInfo(const QString &name, Type type) :
            mName(name),
            mType(type)
        {}

        QString mName;
        Type mType;
    };

    static BuildingTMX *mInstance;
    QList<LayerInfo> mLayers;
    int mRevision;
    int mSourceRevision;
    QString mError;
};

} // namespace BuildingEditor

#endif // BUILDINGTMX_H
