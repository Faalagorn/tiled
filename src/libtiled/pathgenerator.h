/*
 * pathgenerator.h
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PATHGENERATOR_H
#define PATHGENERATOR_H

#include <QString>

namespace Tiled {

class Map;
class Path;
class Tile;
class TileLayer;

class PathGeneratorType
{
public:
    PathGeneratorType(const QString &name);

private:
    QString mName;
};

class PathGenerator
{
public:
    PathGenerator(PathGeneratorType *type, Path *path);

    void generate(Map *map, QVector<TileLayer *> &layers);

    void outline(Tile *tile, TileLayer *tl);
    void outlineWidth(Tile *tile, TileLayer *tl, int width);
    void fill(Tile *tile, TileLayer *tl);

private:
    PathGeneratorType *mType;
    Path *mPath;

};

} // namespace Tiled

#endif // PATHGENERATOR_H
