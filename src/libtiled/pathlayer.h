// FIXME: license

#ifndef PATHLAYER_H
#define PATHLAYER_H

#include "layer.h"

#include <QMetaType>

namespace Tiled {

class TILEDSHARED_EXPORT PathPoint
{
public:
    PathPoint()
        : mX(0)
        , mY(0)
    {

    }

    PathPoint(int x, int y)
        : mX(x)
        , mY(y)
    {

    }

    int x() const
    { return mX; }

    int y() const
    { return mY; }

    bool operator == (const PathPoint &other) const
    { return mX == other.mX && mY == other.mY; }

private:
    int mX, mY;
};

typedef QVector<PathPoint> PathPoints;

class TILEDSHARED_EXPORT Path
{
public:
    Path();

    PathLayer *pathLayer() const
    { return mLayer; }

    void setPathLayer(PathLayer *pathLayer)
    { mLayer = pathLayer; }

    void setPoints(const PathPoints &points);

    const PathPoints points() const
    { return mPoints; }

    int numPoints() const
    { return mPoints.count(); }

    void setClosed(bool closed)
    { mIsClosed = closed; }

    bool isClosed() const
    { return mIsClosed; }

    bool isVisible() const
    { return mVisible; }

    void setVisible(bool visible)
    { mVisible = visible; }

    QPolygonF polygon() const;

    void generate(Map *map, QVector<TileLayer *> &layers) const;

    Path *clone() const;

private:
    PathLayer *mLayer;
    PathPoints mPoints;
    bool mIsClosed;
    bool mVisible;
};

class TILEDSHARED_EXPORT PathLayer : public Layer
{
public:
    PathLayer();
    PathLayer(const QString &name, int x, int y, int width, int height);
    ~PathLayer();

    QSet<Tileset*> usedTilesets() const { return QSet<Tileset*>(); }
    bool referencesTileset(const Tileset *) const { return false; }
    void replaceReferencesToTileset(Tileset *, Tileset *) {}

    void offset(const QPoint &/*offset*/, const QRect &/*bounds*/,
                bool /*wrapX*/, bool /*wrapY*/)
    {}

    bool canMergeWith(Layer *) const { return false; }
    Layer *mergedWith(Layer *) const { return 0; }

    bool isEmpty() const;

    Layer *clone() const;

    const QList<Path*> &paths() const
    { return mPaths; }

    int pathCount() const
    { return mPaths.count(); }

    void addPath(Path *path);
    void insertPath(int index, Path *path);
    int removePath(Path *path);

    const QColor &color() const { return mColor; }
    void setColor(const QColor &color) {  mColor = color; }

    void generate(QVector<TileLayer*> &layers) const;

protected:
    PathLayer *initializeClone(PathLayer *clone) const;

private:
    QList<Path*> mPaths;
    QColor mColor;
};

} // namespace Tiled

Q_DECLARE_METATYPE(Tiled::PathLayer*)

#endif // PATHLAYER_H
