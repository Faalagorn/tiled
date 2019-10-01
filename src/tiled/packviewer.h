/*
 * Copyright 2014, Tim Baker <treectrl@users.sf.net>
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

#ifndef PACKVIEWER_H
#define PACKVIEWER_H

#include "texturepackfile.h"

#include <QGraphicsItem>
#include <QMainWindow>

namespace Ui {
class PackViewer;
}

namespace Tiled {
namespace Internal {
class Zoomable;
}
}

class PackImageItem : public QGraphicsPixmapItem
{
public:
    PackImageItem(QGraphicsRectItem *rectItem);
    void setPackPage(PackPage pp) { mPackPage = pp; }
    void hoverMoveEvent(QGraphicsSceneHoverEvent *event);

    PackPage mPackPage;
    QGraphicsRectItem *mTileRectItem;
};

class PackViewer : public QMainWindow
{
    Q_OBJECT

public:
    explicit PackViewer(QWidget *parent = nullptr);
    ~PackViewer();

private slots:
    void openPack();
    void itemSelectionChanged();
    void scaleChanged(qreal scale);
    void chooseBackgroundColor();
    void setBackgroundColor(const QColor &color);
    void extractImages();

private:
    void readSettings();
    void writeSettings();

    Ui::PackViewer *ui;
    PackFile mPackFile;
    QString mPackDirectory;
    Tiled::Internal::Zoomable *mZoomable;
    QGraphicsRectItem *mRectItem;
    QGraphicsRectItem *mTileRectItem;
    PackImageItem *mPixmapItem;
};

#endif // PACKVIEWER_H
