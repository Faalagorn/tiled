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

#ifndef PATHGENERATORMGR_H
#define PATHGENERATORMGR_H

#include <QList>
#include <QMap>
#include <QObject>

namespace Tiled {

class PathGenerator;
class Tileset;

namespace Internal {

class PathGeneratorMgr : public QObject
{
    Q_OBJECT
public:
    static PathGeneratorMgr *instance();
    static void deleteInstance();

    void insertGenerator(int index, PathGenerator *pgen);
    PathGenerator *removeGenerator(int index);

    const QList<PathGenerator*> &generators() const
    { return mGenerators; }

    const QList<PathGenerator*> &generatorTypes() const
    { return mGeneratorTypes; }

    Tiled::Tileset *tilesetFor(const QString &tilesetName)
    {
        if (mTilesetByName.contains(tilesetName))
            return mTilesetByName[tilesetName];
        return 0;
    }

    const QMap<QString,Tiled::Tileset*> &tilesetsMap() const
    { return mTilesetByName; }

    QList<Tileset*> tilesets() const
    { return mTilesetByName.values(); }

    int indexOf(Tileset *ts)
    { return tilesets().indexOf(ts); }

    QString txtName();
    QString txtPath();

    bool readTxt();
    bool writeTxt();
    bool upgradeTxt();
    bool mergeTxt();

    QString errorString() const
    { return mError; }

    bool Startup();

    Tileset *loadTileset(const QString &source);
    void addTileset(Tileset *ts);
    void removeTileset(Tileset *ts);

private:
    PathGenerator *findGeneratorType(const QString &type);
    
private:
    explicit PathGeneratorMgr(QObject *parent = 0);
    ~PathGeneratorMgr();
    static PathGeneratorMgr *mInstance;

    QList<PathGenerator*> mGenerators;
    QList<PathGenerator*> mGeneratorTypes;

    QMap<QString,Tiled::Tileset*> mTilesetByName;
    QList<Tiled::Tileset*> mRemovedTilesets;

    int mRevision;
    int mSourceRevision;
    QString mError;
};

} // namespace Internal;
} // namespace Tiled

#endif // PATHGENERATORMGR_H
