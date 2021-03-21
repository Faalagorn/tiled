/*
 * zgriditem.cpp
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

#include "zgriditem.h"

#include "map.h"
#include "mapdocument.h"
#include "maprenderer.h"
#include "preferences.h"

#include <QStyleOptionGraphicsItem>

using namespace Tiled;
using namespace Tiled::Internal;

ZGridItem::ZGridItem()
    : mMapDocument(0)
{
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption);
}

void ZGridItem::setMapDocument(MapDocument *mapDocument)
{
    if (mMapDocument == mapDocument)
        return;

    mMapDocument = mapDocument;

    updateBoundingRect();
}

void ZGridItem::currentLayerIndexChanged()
{
    updateBoundingRect();
}

QRectF ZGridItem::boundingRect() const
{
    return mBoundingRect;
}

void ZGridItem::paint(QPainter *painter,
                      const QStyleOptionGraphicsItem *option,
                      QWidget *)
{
    if (!mMapDocument || !mMapDocument->renderer())
        return;
    const MapRenderer *renderer = mMapDocument->renderer();
#if 1
    const QRect bounds = QRect(0, 0, mMapDocument->map()->width(), mMapDocument->map()->height());
    QRectF boundsF = mMapDocument->renderer()->boundingRect(bounds, mMapDocument->currentLevelIndex());
//  Q_ASSERT(mBoundingRect == boundsF);
    if (mBoundingRect != boundsF)
        return;
#endif
	QColor gridColor = Preferences::instance()->gridColor();
    renderer->drawGrid(painter, option->exposedRect, gridColor, mMapDocument->currentLevelIndex());
}

void ZGridItem::updateBoundingRect()
{
    QRectF boundsF;

    if (!mMapDocument || !mMapDocument->map() || !mMapDocument->renderer()) {
    } else {
        const QRect bounds = QRect(0, 0, mMapDocument->map()->width(), mMapDocument->map()->height());
        boundsF = mMapDocument->renderer()->boundingRect(bounds, mMapDocument->currentLevelIndex());
    }
    if (boundsF != mBoundingRect) {
        prepareGeometryChange();
        mBoundingRect = boundsF;
    }
}
