/*
 * Copyright 2022, Tim Baker <treectrl@users.sf.net>
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

#include "snoweditor.h"
#include "ui_snoweditor.h"

#include "tiledeffile.h"
#include "tilemetainfodialog.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "zoomable.h"

#include "tile.h"
#include "tileset.h"

#include "BuildingEditor/buildingtiles.h"

#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>

using namespace Tiled;
using namespace Internal;
using namespace BuildingEditor;

SnowEditor::SnowEditor(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::SnowEditor),
    mZoomable(new Zoomable(this))
{
    ui->setupUi(this);

    connect(ui->actionOpen, &QAction::triggered, this, qOverload<>(&SnowEditor::fileOpen));
    connect(ui->actionSave, &QAction::triggered, this, qOverload<>(&SnowEditor::fileSave));
    connect(ui->actionClose, &QAction::triggered, this, &QWidget::close);

    ui->filterEditSource->setClearButtonEnabled(true);
    ui->filterEditSource->setEnabled(false);
    connect(ui->filterEditSource, &QLineEdit::textEdited, this,
            &SnowEditor::tilesetFilterSourceEdited);

    ui->filterEditTarget->setClearButtonEnabled(true);
    ui->filterEditTarget->setEnabled(false);
    connect(ui->filterEditTarget, &QLineEdit::textEdited, this,
            &SnowEditor::tilesetFilterTargetEdited);

    ui->targetView->model()->setShowHeaders(false);
    ui->targetView->setAcceptDrops(true);
    connect(ui->targetView->model(), &MixedTilesetModel::tileDroppedAt,
            this, &SnowEditor::tileDroppedAt);

    ui->targetView->setZoomable(mZoomable);
    ui->sourceView->setZoomable(mZoomable);

    ui->tilesetListSource->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    connect(ui->tilesetListSource, &QListWidget::itemSelectionChanged,
            this, &SnowEditor::tilesetSelectionChangedSource);

    ui->tilesetListTarget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    connect(ui->tilesetListTarget, &QListWidget::itemSelectionChanged,
            this, &SnowEditor::tilesetSelectionChangedTarget);

    connect(ui->tilesetMgrSource, &QAbstractButton::clicked,
            this, &SnowEditor::manageTilesets);

    connect(ui->tilesetMgrTarget, &QAbstractButton::clicked,
            this, &SnowEditor::manageTilesets);

    ui->sourceView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->sourceView->setDragEnabled(true);

    connect(TileMetaInfoMgr::instance(), &TileMetaInfoMgr::tilesetAdded,
            this, &SnowEditor::tilesetAdded);
    connect(TileMetaInfoMgr::instance(), &TileMetaInfoMgr::tilesetAboutToBeRemoved,
            this, &SnowEditor::tilesetAboutToBeRemoved);
    connect(TileMetaInfoMgr::instance(), &TileMetaInfoMgr::tilesetRemoved,
            this, &SnowEditor::tilesetRemoved);

    connect(TilesetManager::instance(), &TilesetManager::tilesetChanged,
            this, &SnowEditor::tilesetChanged);

//    mCurrentTilesetSource = TileMetaInfoMgr::instance()->tileset(QStringLiteral("e_roof_snow_1"));
//    if (mCurrentTilesetSource) {
//        ui->sourceView->setTileset(mCurrentTilesetSource);
//    }

//    setTilesetTargetList();
//    setTilesetSourceList();
}

SnowEditor::~SnowEditor()
{
    delete ui;
}

void SnowEditor::manageTilesets()
{
    TileMetaInfoDialog dialog(this);
    dialog.exec();

    TileMetaInfoMgr *mgr = TileMetaInfoMgr::instance();
    if (!mgr->writeTxt())
        QMessageBox::warning(this, tr("It's no good, Jim!"), mgr->errorString());
}

void SnowEditor::tileDroppedAt(const QString &tilesetName, int tileId, int row, int column, const QModelIndex &parent)
{
    QModelIndex index = ui->targetView->model()->index(row, column, parent);
    Tile* targetTile = ui->targetView->model()->tileAt(index);
    if (targetTile == nullptr)
        return;
    QString tileName = BuildingTilesMgr::instance()->nameForTile(targetTile);
    QString snowName = BuildingTilesMgr::instance()->nameForTile(tilesetName, tileId);

    if (TileDefTileset *tdts = mTileDefFile->tileset(targetTile->tileset()->name())) {
        if (TileDefTile *tdt = tdts->tileAt(targetTile->id())) {
            tdt->mPropertyUI.ChangePropertiesV(QStringLiteral("SnowTile"), snowName);
        }
    }

    Tile *snowTile = BuildingTilesMgr::instance()->tileFor(snowName);
    ui->targetView->model()->setOverlayTile(index, snowTile);
}

void SnowEditor::tilesetFilterSourceEdited(const QString &text)
{
    tilesetFilterEdited(ui->tilesetListSource, text);
}

void SnowEditor::tilesetFilterTargetEdited(const QString &text)
{
    tilesetFilterEdited(ui->tilesetListTarget, text);
}

void SnowEditor::tilesetSelectionChangedSource()
{
    tilesetSelectionChanged(ui->tilesetListSource, ui->sourceView, &mCurrentTilesetSource);
}

void SnowEditor::tilesetSelectionChangedTarget()
{
    tilesetSelectionChanged(ui->tilesetListTarget, ui->targetView, &mCurrentTilesetTarget);
}

void SnowEditor::tilesetFilterEdited(QListWidget *tilesetNamesList, const QString &text)
{
    for (int row = 0; row < tilesetNamesList->count(); row++) {
        QListWidgetItem* item = tilesetNamesList->item(row);
        item->setHidden(text.trimmed().isEmpty() ? false : !item->text().contains(text));
    }

    QListWidgetItem* current = tilesetNamesList->currentItem();
    if (current != nullptr && current->isHidden()) {
        // Select previous visible row.
        int row = tilesetNamesList->row(current) - 1;
        while (row >= 0 && tilesetNamesList->item(row)->isHidden())
            row--;
        if (row >= 0) {
            current = tilesetNamesList->item(row);
            tilesetNamesList->setCurrentItem(current);
            tilesetNamesList->scrollToItem(current);
            return;
        }

        // Select next visible row.
        row = tilesetNamesList->row(current) + 1;
        while (row < tilesetNamesList->count() && tilesetNamesList->item(row)->isHidden())
            row++;
        if (row < tilesetNamesList->count()) {
            current = tilesetNamesList->item(row);
            tilesetNamesList->setCurrentItem(current);
            tilesetNamesList->scrollToItem(current);
            return;
        }

        // All items hidden
        tilesetNamesList->setCurrentItem(nullptr);
    }

    current = tilesetNamesList->currentItem();
    if (current != nullptr)
        tilesetNamesList->scrollToItem(current);
}

void SnowEditor::setTilesetSourceList()
{
    setTilesetList(ui->filterEditSource, ui->tilesetListSource);
}

void SnowEditor::setTilesetTargetList()
{
    setTilesetList(ui->filterEditTarget, ui->tilesetListTarget);
}

void SnowEditor::tilesetSelectionChanged(QListWidget *tilesetNamesList, Tiled::Internal::MixedTilesetView *tilesetView, Tileset **tilesetPtr)
{
    QList<QListWidgetItem*> selection = tilesetNamesList->selectedItems();
    QListWidgetItem *item = selection.count() ? selection.first() : nullptr;
    *tilesetPtr = nullptr;
    if (item) {
        int row = tilesetNamesList->row(item);
        *tilesetPtr = TileMetaInfoMgr::instance()->tileset(row);
        if ((*tilesetPtr)->isMissing()) {
            tilesetView->clear();
        } else {
            tilesetView->setTileset(*tilesetPtr);
            if (tilesetView == ui->targetView) {
                MixedTilesetModel *model = tilesetView->model();
                TileDefTileset *tdts = mTileDefFile->tileset((*tilesetPtr)->name());
                if (tdts != nullptr) {
                    for (int tileId = 0; tileId < (*tilesetPtr)->tileCount(); tileId++) {
                        TileDefTile *tdt = tdts->tileAt(tileId);
                        if (tdt == nullptr)
                            continue;
                        UIProperties::UIProperty *property = tdt->property(QStringLiteral("SnowTile"));
                        if (property == nullptr)
                            continue;
                        if (property->getString().isEmpty())
                            continue;
                        QString snowTileName =property->getString();
                        QString snowTilesetName;
                        int snowTileId;
                        if (BuildingTilesMgr::instance()->parseTileName(snowTileName, snowTilesetName, snowTileId)) {
                            if (Tileset *snowTileset = TileMetaInfoMgr::instance()->tileset(snowTilesetName)) {
                                if (Tile *snowTile = snowTileset->tileAt(snowTileId)) {
                                    Tile *tile = (*tilesetPtr)->tileAt(tileId);
                                    model->setOverlayTile(model->index(tile), snowTile);
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        tilesetView->clear();
    }
    syncUI();
}

void SnowEditor::syncUI()
{

}

void SnowEditor::setTilesetList(QLineEdit *lineEdit, QListWidget *tilesetNamesList)
{
    tilesetNamesList->clear();
    // Add the list of tilesets, and resize it to fit
    int width = 64;
    QFontMetrics fm = tilesetNamesList->fontMetrics();
    const QList<Tileset*> tilesets = TileMetaInfoMgr::instance()->tilesets();
    for (Tileset *tileset : tilesets) {
        QListWidgetItem *item = new QListWidgetItem();
        item->setText(tileset->name());
        if (tileset->isMissing())
            item->setForeground(Qt::red);
        tilesetNamesList->addItem(item);
        width = qMax(width, fm.horizontalAdvance(tileset->name()));
    }
    int sbw = tilesetNamesList->verticalScrollBar()->sizeHint().width();
    tilesetNamesList->setFixedWidth(width + 16 + sbw);

    lineEdit->setFixedWidth(tilesetNamesList->width());
    lineEdit->setEnabled(tilesetNamesList->count() > 0);
    tilesetFilterSourceEdited(lineEdit->text());
}

void SnowEditor::fileOpen(const QString &filePath)
{
    mTileDefFile = new TileDefFile();
    if (!mTileDefFile->read(filePath)) {
        QMessageBox::warning(this, tr("Error"), mTileDefFile->errorString());
        delete mTileDefFile;
        mTileDefFile = nullptr;
        return;
    }

    setTilesetTargetList();
    setTilesetSourceList();
}

bool SnowEditor::fileSave(const QString &filePath)
{
    if (!mTileDefFile->write(filePath)) {
        QMessageBox::warning(this, tr("Error"), mTileDefFile->errorString());
        return false;
    }
    mTileDefFile->setFileName(filePath);
    return true;
}

bool SnowEditor::confirmSave()
{
    if (mTileDefFile == nullptr)
        return true;

    int ret = QMessageBox::warning(
            this, tr("Unsaved Changes"),
            tr("There are unsaved changes. Do you want to save now?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    switch (ret) {
    case QMessageBox::Save:    return fileSave();
    case QMessageBox::Discard: return true;
    case QMessageBox::Cancel:
    default:
        return false;
    }
}

QString SnowEditor::getSaveLocation()
{
    QSettings settings;
    QString key = QLatin1String("SnowEditor/LastOpenPath");
    QString suggestedFileName = QLatin1String("newtiledefintiions.tiles");
    if (mTileDefFile != nullptr) {
        suggestedFileName = mTileDefFile->fileName();
    }
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save As"),
                                                    suggestedFileName,
                                                    QLatin1String("Tile properties files (*.tiles)"));
    if (fileName.isEmpty())
        return QString();
    settings.setValue(key, QFileInfo(fileName).absolutePath());
    return fileName;
}

void SnowEditor::tilesetAdded(Tileset *tileset)
{
    setTilesetSourceList();
    setTilesetTargetList();
    int row = TileMetaInfoMgr::instance()->indexOf(tileset);
    ui->tilesetListSource->setCurrentRow(row);
    ui->tilesetListTarget->setCurrentRow(row);
}

void SnowEditor::tilesetAboutToBeRemoved(Tileset *tileset)
{
    int row = TileMetaInfoMgr::instance()->indexOf(tileset);
    delete ui->tilesetListSource->takeItem(row);
    delete ui->tilesetListTarget->takeItem(row);
}

void SnowEditor::tilesetRemoved(Tileset *tileset)
{
    Q_UNUSED(tileset)
}

// Called when a tileset image changes or a missing tileset was found.
void SnowEditor::tilesetChanged(Tileset *tileset)
{
    if (tileset == mCurrentTilesetTarget) {
        if (tileset->isMissing())
            ui->targetView->clear();
        else
            ui->targetView->setTileset(tileset);
    }

    if (tileset == mCurrentTilesetSource) {
        if (tileset->isMissing())
            ui->sourceView->clear();
        else
            ui->sourceView->setTileset(tileset);
    }

    int row = TileMetaInfoMgr::instance()->indexOf(tileset);
    if (QListWidgetItem *item = ui->tilesetListSource->item(row)) {
        item->setForeground(tileset->isMissing() ? Qt::red : Qt::black);
    }
    if (QListWidgetItem *item = ui->tilesetListTarget->item(row)) {
        item->setForeground(tileset->isMissing() ? Qt::red : Qt::black);
    }
}

void SnowEditor::fileOpen()
{
    if (!confirmSave())
        return;

    QSettings settings;
    QString key = QLatin1String("SnowEditor/LastOpenPath");
    QString lastPath = settings.value(key, QStringLiteral("newtiledefinitions.tiles")).toString();

    QString fileName = QFileDialog::getOpenFileName(this, tr("Choose .tiles file"),
                                                    lastPath,
                                                    QLatin1String("Tile properties files (*.tiles)"));
    if (fileName.isEmpty())
        return;

    settings.setValue(key, QFileInfo(fileName).absolutePath());

    fileOpen(fileName);

    syncUI();
}

bool SnowEditor::fileSave()
{
    QString fileName = getSaveLocation();
    if (fileName.isEmpty())
        return false;
    return fileSave(fileName);
}
