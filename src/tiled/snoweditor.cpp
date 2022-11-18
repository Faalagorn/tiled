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

#include "tilemetainfodialog.h"
#include "tilemetainfomgr.h"
#include "tilesetmanager.h"
#include "zoomable.h"

#include "tile.h"
#include "tileset.h"

#include "BuildingEditor/buildingtiles.h"
#include "BuildingEditor/simplefile.h"

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

    mCurrentTilesetSource = TileMetaInfoMgr::instance()->tileset(QStringLiteral("e_roof_snow_1"));
    if (mCurrentTilesetSource) {
        ui->sourceView->setTileset(mCurrentTilesetSource);
    }

    setTilesetTargetList();
    setTilesetSourceList();
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
    Tile* tile = ui->targetView->model()->tileAt(index);
    if (tile == nullptr)
        return;
    QString tileName = BuildingTilesMgr::instance()->nameForTile(tile);
    QString snowName = BuildingTilesMgr::instance()->nameForTile(tilesetName, tileId);
    mAssignments[tileName] = snowName;
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
        if ((*tilesetPtr)->isMissing())
            tilesetView->clear();
        else {
            tilesetView->setTileset(*tilesetPtr);
            if (tilesetView == ui->targetView) {
                MixedTilesetModel *model = tilesetView->model();
                for (int tileId = 0; tileId < (*tilesetPtr)->tileCount(); tileId++) {
                    Tile *tile = (*tilesetPtr)->tileAt(tileId);
                    QString tileName = BuildingTilesMgr::nameForTile(tile);
                    if (mAssignments.contains(tileName)) {
                        QString snowTileName = mAssignments[tileName];
                        QString snowTilesetName;
                        int snowTileId;
                        if (BuildingTilesMgr::instance()->parseTileName(snowTileName, snowTilesetName, snowTileId)) {
                            if (Tileset *snowTileset = TileMetaInfoMgr::instance()->tileset(snowTilesetName)) {
                                if (Tile *snowTile = snowTileset->tileAt(snowTileId)) {
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
    SnowEditorFile file;
    if (!file.read(filePath)) {
        QMessageBox::warning(this, tr("Error"), file.errorString());
        return;
    }
    mAssignments.clear();
    mFileName = filePath;
    const QList<SnowEditorTile*> tiles = file.takeTiles();
    for (SnowEditorTile *tile : tiles) {
        mAssignments[tile->mNormal] = tile->mSnow;
    }
    qDeleteAll(tiles);
}

bool SnowEditor::fileSave(const QString &filePath)
{
    QList<SnowEditorTile*> tiles;
    const QStringList keys = mAssignments.keys();
    for (const QString &key : keys) {
        SnowEditorTile *tile = new SnowEditorTile;
        tile->mNormal = key;
        tile->mSnow = mAssignments.value(key);
        tiles += tile;
    }
    SnowEditorFile file;
    if (!file.write(filePath, tiles)) {
        QMessageBox::warning(this, tr("Error"), file.errorString());
        qDeleteAll(tiles);
        return false;
    }
    qDeleteAll(tiles);
    return true;
}

bool SnowEditor::confirmSave()
{
    if (mFileName.isEmpty())
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
    QString suggestedFileName = QLatin1String("ice-queen.txt");
    if (!mFileName.isEmpty()) {
        suggestedFileName = mFileName;
    }
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save As"),
                                                    suggestedFileName,
                                                    QLatin1String("Text files (*.txt)"));
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
    QString lastPath = settings.value(key, QStringLiteral("ice-queen.txt")).toString();

    QString fileName = QFileDialog::getOpenFileName(this, tr("Choose .txt file"),
                                                    lastPath,
                                                    QLatin1String("Text files (*.txt)"));
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

/////

#define VERSION1 1
#define VERSION_LATEST VERSION1

SnowEditorFile::SnowEditorFile()
{

}

bool SnowEditorFile::read(const QString &fileName)
{
    SimpleFile simpleFile;
    if (!simpleFile.read(fileName)) {
        mError = QStringLiteral("%1\n(while reading %2)")
                .arg(simpleFile.errorString())
                .arg(QDir::toNativeSeparators(fileName));
        return false;
    }

    if (simpleFile.version() != VERSION_LATEST) {
        mError = QStringLiteral("Expected %1 version %2, got %3")
                .arg(fileName).arg(VERSION_LATEST).arg(simpleFile.version());
        return false;
    }

    mTiles.clear();

    for (const SimpleFileKeyValue& keyValue : qAsConst(simpleFile.values)) {
        if (keyValue.name == QStringLiteral("version")) {
            continue;
        }
        SnowEditorTile *tile = new SnowEditorTile;
        tile->mNormal = BuildingTilesMgr::instance()->normalizeTileName(keyValue.name.trimmed());
        tile->mSnow = BuildingTilesMgr::instance()->normalizeTileName(keyValue.value.trimmed());
        mTiles += tile;
    }

    return true;
}

bool SnowEditorFile::write(const QString &fileName, const QList<SnowEditorTile *> tiles)
{
    SimpleFile simpleFile;
    for (SnowEditorTile *tile : tiles) {
        simpleFile.addValue(tile->mNormal, tile->mSnow);
    }
    qDebug() << "WRITE " << fileName;
    simpleFile.setVersion(VERSION_LATEST);
    if (!simpleFile.write(fileName)) {
        mError = simpleFile.errorString();
        return false;
    }
    return true;
}

QList<SnowEditorTile *> SnowEditorFile::takeTiles()
{
    QList<SnowEditorTile *> tiles = mTiles;
    mTiles.clear();
    return tiles;
}
