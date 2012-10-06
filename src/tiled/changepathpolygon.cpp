/*
 * changepolygon.cpp
 * Copyright 2011, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
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

#include "changepathpolygon.h"

#include "mapdocument.h"
#include "pathlayer.h"
#include "pathmodel.h"

#include <QCoreApplication>

using namespace Tiled;
using namespace Tiled::Internal;

ChangePathPolygon::ChangePathPolygon(MapDocument *mapDocument,
                             Path *path,
                             const QPolygon &oldPolygon)
    : mMapDocument(mapDocument)
    , mPath(path)
    , mOldPolygon(oldPolygon)
    , mNewPolygon(path->polygon())
{
    setText(QCoreApplication::translate("Undo Commands", "Change Path Polygon"));
}

void ChangePathPolygon::undo()
{
    mMapDocument->pathModel()->setPathPolygon(mPath, mOldPolygon);
}

void ChangePathPolygon::redo()
{
    mMapDocument->pathModel()->setPathPolygon(mPath, mNewPolygon);
}
