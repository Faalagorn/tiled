/*
 * Copyright 2013, Tim Baker <treectrl@users.sf.net>
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

#ifndef VTSPREFSDIALOG_H
#define VTSPREFSDIALOG_H

#include <QDialog>

namespace Ui {
class VirtualTilesetPrefsDialog;
}

namespace Tiled {
namespace Internal {

class VirtualTilesetPrefsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VirtualTilesetPrefsDialog(QWidget *parent = 0);
    ~VirtualTilesetPrefsDialog();

    QString gameDir() const;

private slots:
    void browse();

private:
    Ui::VirtualTilesetPrefsDialog *ui;
};

} // namespace Internal
} // namespace Tiled

#endif // VTSPREFSDIALOG_H
