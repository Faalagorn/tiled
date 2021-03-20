/*
 * addremovelayer.cpp
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

#include "addremovelayer.h"

#include "layer.h"
#include "layermodel.h"
#include "mapdocument.h"

namespace Tiled {
namespace Internal {

AddRemoveLayer::AddRemoveLayer(MapDocument *mapDocument,
                               int levelIndex,
                               int layerIndex,
                               Layer *layer)
    : mMapDocument(mapDocument)
    , mLayer(layer)
    , mLevelIndex(levelIndex)
    , mLayerIndex(layerIndex)
{
}

AddRemoveLayer::~AddRemoveLayer()
{
    delete mLayer;
}

void AddRemoveLayer::addLayer()
{
    const int currentLayer = mMapDocument->currentLayerIndex();

    mMapDocument->layerModel()->insertLayer(mLayerIndex, mLayer);
    mLayer = nullptr;

    // Insertion below or at the current layer increases current layer index
    if (mLayerIndex <= currentLayer)
        mMapDocument->setCurrentLayerIndex(currentLayer + 1);
}

void AddRemoveLayer::removeLayer()
{
    const int currentLayer = mMapDocument->currentLayerIndex();

    mLayer = mMapDocument->layerModel()->takeLayerAt(mLevelIndex, mLayerIndex);

    // Removal below the current layer decreases the current layer index
    if (mLayerIndex < currentLayer)
        mMapDocument->setCurrentLayerIndex(currentLayer - 1);
}

} // namespace Internal
} // namespace Tiled
