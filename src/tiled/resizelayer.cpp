/*
 * resizelayer.cpp
 * Copyright 2009, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
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

#include "resizelayer.h"

#include "layer.h"
#include "layermodel.h"
#include "map.h"
#include "mapdocument.h"

#include <QCoreApplication>

using namespace Tiled;
using namespace Tiled::Internal;

ResizeLayer::ResizeLayer(MapDocument *mapDocument,
                         int levelIndex,
                         int layerIndex,
                         const QSize &size,
                         const QPoint &offset)
    : QUndoCommand(QCoreApplication::translate("Undo Commands",
                                               "Resize Layer"))
    , mMapDocument(mapDocument)
    , mLevelIndex(levelIndex)
    , mLayerIndex(layerIndex)
    , mOriginalLayer(0)
{
    // Create the resized layer (once)
    Layer *layer = mMapDocument->map()->layerAt(mLevelIndex, mLayerIndex);
    mResizedLayer = layer->clone();
    mResizedLayer->resize(size, offset);
}

ResizeLayer::~ResizeLayer()
{
    delete mOriginalLayer;
    delete mResizedLayer;
}

void ResizeLayer::undo()
{
    Q_ASSERT(!mResizedLayer);
    mResizedLayer = swapLayer(mOriginalLayer);
    mOriginalLayer = 0;
}

void ResizeLayer::redo()
{
    Q_ASSERT(!mOriginalLayer);
    mOriginalLayer = swapLayer(mResizedLayer);
    mResizedLayer = 0;
}

Layer *ResizeLayer::swapLayer(Layer *layer)
{
    const int currentLevelIndex = mMapDocument->currentLevelIndex();
    const int currentIndex = mMapDocument->currentLayerIndex();

    LayerModel *layerModel = mMapDocument->layerModel();
    Layer *replaced = layerModel->takeLayerAt(mLevelIndex, mLayerIndex);
    layerModel->insertLayer(mLayerIndex, layer);

    if (mLevelIndex == currentLevelIndex && mLayerIndex == currentIndex)
        mMapDocument->setCurrentLayerIndex(currentLevelIndex, currentIndex);

    return replaced;
}
