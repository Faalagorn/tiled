/*
 * Copyright 2012, Tim Baker <treectrl@users.sf.net>
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

#include "buildingtiles.h"

#include "buildingobjects.h"
#include "buildingpreferences.h"
#include "simplefile.h"

#include "tilesetmanager.h"

#include "tile.h"
#include "tileset.h"

#include <QCoreApplication>
#include <QMessageBox>

using namespace BuildingEditor;
using namespace Tiled;
using namespace Tiled::Internal;

static const char *TXT_FILE = "BuildingTiles.txt";

/////

BuildingTilesMgr *BuildingTilesMgr::mInstance = 0;

BuildingTilesMgr *BuildingTilesMgr::instance()
{
    if (!mInstance)
        mInstance = new BuildingTilesMgr;
    return mInstance;
}

void BuildingTilesMgr::deleteInstance()
{
    delete mInstance;
    mInstance = 0;
}

BuildingTilesMgr::BuildingTilesMgr() :
    mMissingTile(0),
    mNoneTiledTile(0),
    mNoneBuildingTile(0),
    mNoneCategory(0),
    mNoneTileEntry(0)
{
    mCatCurtains = new BTC_Curtains(QLatin1String("Curtains"));
    mCatDoors = new BTC_Doors(QLatin1String("Doors"));
    mCatDoorFrames = new BTC_DoorFrames(QLatin1String("Door Frames"));
    mCatFloors = new BTC_Floors(QLatin1String("Floors"));
    mCatEWalls = new BTC_EWalls(QLatin1String("Exterior Walls"));
    mCatIWalls = new BTC_IWalls(QLatin1String("Interior Walls"));
    mCatStairs = new BTC_Stairs(QLatin1String("Stairs"));
    mCatWindows = new BTC_Windows(QLatin1String("Windows"));
    mCatRoofCaps = new BTC_RoofCaps(QLatin1String("Roof Caps"));
    mCatRoofSlopes = new BTC_RoofSlopes(QLatin1String("Roof Slopes"));
    mCatRoofTops = new BTC_RoofTops(QLatin1String("Roof Tops"));

    mCategories << mCatEWalls << mCatIWalls << mCatFloors << mCatDoors <<
                   mCatDoorFrames << mCatWindows << mCatCurtains << mCatStairs
                   << mCatRoofCaps << mCatRoofSlopes << mCatRoofTops;

    foreach (BuildingTileCategory *category, mCategories)
        mCategoryByName[category->name()] = category;

    mCatRoofCaps->setShadowImage(QImage(QLatin1String(":/BuildingEditor/icons/shadow_roof_caps.png")));

    Tileset *tileset = new Tileset(QLatin1String("missing"), 64, 128);
    tileset->setTransparentColor(Qt::white);
    QString fileName = QLatin1String(":/BuildingEditor/icons/missing-tile.png");
    if (tileset->loadFromImage(QImage(fileName), fileName))
        mMissingTile = tileset->tileAt(0);
    else {
        QImage image(64, 128, QImage::Format_ARGB32);
        image.fill(Qt::red);
        tileset->loadFromImage(image, fileName);
        mMissingTile = tileset->tileAt(0);
    }

    tileset = new Tileset(QLatin1String("none"), 64, 128);
    tileset->setTransparentColor(Qt::white);
    fileName = QLatin1String(":/BuildingEditor/icons/none-tile.png");
    if (tileset->loadFromImage(QImage(fileName), fileName))
        mNoneTiledTile = tileset->tileAt(0);
    else {
        QImage image(64, 128, QImage::Format_ARGB32);
        image.fill(Qt::red);
        tileset->loadFromImage(image, fileName);
        mNoneTiledTile = tileset->tileAt(0);
    }

    mNoneBuildingTile = new NoneBuildingTile();

    mNoneCategory = new NoneBuildingTileCategory();
    mNoneTileEntry = new NoneBuildingTileEntry(mNoneCategory);
}

BuildingTilesMgr::~BuildingTilesMgr()
{
    TilesetManager::instance()->removeReferences(tilesets());
    TilesetManager::instance()->removeReferences(mRemovedTilesets);
    qDeleteAll(mCategories);
    if (mMissingTile)
        delete mMissingTile->tileset();
    if (mNoneTiledTile)
        delete mNoneTiledTile->tileset();
    delete mNoneBuildingTile;
}

BuildingTile *BuildingTilesMgr::add(const QString &tileName)
{
    QString tilesetName;
    int tileIndex;
    parseTileName(tileName, tilesetName, tileIndex);
    BuildingTile *btile = new BuildingTile(tilesetName, tileIndex);
    Q_ASSERT(!mTileByName.contains(btile->name()));
    mTileByName[btile->name()] = btile;
    mTiles = mTileByName.values(); // sorted by increasing tileset name and tile index!
    return btile;
}

BuildingTile *BuildingTilesMgr::get(const QString &tileName, int offset)
{
    if (tileName.isEmpty())
        return noneTile();

    QString adjustedName = adjustTileNameIndex(tileName, offset); // also normalized

    if (!mTileByName.contains(adjustedName))
        add(adjustedName);
    return mTileByName[adjustedName];
}

QString BuildingTilesMgr::nameForTile(const QString &tilesetName, int index)
{
    // The only reason I'm padding the tile index is so that the tiles are sorted
    // by increasing tileset name and index.
    return tilesetName + QLatin1Char('_') +
            QString(QLatin1String("%1")).arg(index, 3, 10, QLatin1Char('0'));
}

QString BuildingTilesMgr::nameForTile(Tile *tile)
{
    return nameForTile(tile->tileset()->name(), tile->id());
}

bool BuildingTilesMgr::parseTileName(const QString &tileName, QString &tilesetName, int &index)
{
    tilesetName = tileName.mid(0, tileName.lastIndexOf(QLatin1Char('_')));
    QString indexString = tileName.mid(tileName.lastIndexOf(QLatin1Char('_')) + 1);
    // Strip leading zeroes from the tile index
#if 1
    int i = 0;
    while (i < indexString.length() - 1 && indexString[i] == QLatin1Char('0'))
        i++;
    indexString.remove(0, i);
#else
    indexString.remove( QRegExp(QLatin1String("^[0]*")) );
#endif
    index = indexString.toInt();
    return true;
}

QString BuildingTilesMgr::adjustTileNameIndex(const QString &tileName, int offset)
{
    QString tilesetName;
    int index;
    parseTileName(tileName, tilesetName, index);
    index += offset;
    return nameForTile(tilesetName, index);
}

QString BuildingTilesMgr::normalizeTileName(const QString &tileName)
{
    if (tileName.isEmpty())
        return tileName;
    QString tilesetName;
    int index;
    parseTileName(tileName, tilesetName, index);
    return nameForTile(tilesetName, index);
}

void BuildingTilesMgr::addTileset(Tileset *tileset)
{
    Q_ASSERT(mTilesetByName.contains(tileset->name()) == false);
    mTilesetByName[tileset->name()] = tileset;
    if (!mRemovedTilesets.contains(tileset))
        TilesetManager::instance()->addReference(tileset);
    mRemovedTilesets.removeAll(tileset);
    emit tilesetAdded(tileset);
}

void BuildingTilesMgr::removeTileset(Tileset *tileset)
{
    Q_ASSERT(mTilesetByName.contains(tileset->name()));
    Q_ASSERT(mRemovedTilesets.contains(tileset) == false);
    emit tilesetAboutToBeRemoved(tileset);
    mTilesetByName.remove(tileset->name());
    emit tilesetRemoved(tileset);

    // Don't remove references now, that will delete the tileset, and the
    // user might undo the removal.
    mRemovedTilesets += tileset;
    //    TilesetManager::instance()->removeReference(tileset);
}

void BuildingTilesMgr::entryTileChanged(BuildingTileEntry *entry, int e)
{
    Q_UNUSED(e)
    emit entryTileChanged(entry);
}

QString BuildingTilesMgr::txtName()
{
    return QLatin1String(TXT_FILE);
}

QString BuildingTilesMgr::txtPath()
{
    return BuildingPreferences::instance()->configPath(txtName());
}

static void writeTileEntry(SimpleFileBlock &parentBlock, BuildingTileEntry *entry)
{
    BuildingTileCategory *category = entry->category();
    SimpleFileBlock block;
    block.name = QLatin1String("entry");
//    block.addValue("category", category->name());
    for (int i = 0; i < category->enumCount(); i++) {
        block.addValue(category->enumToString(i), entry->tile(i)->name());
    }
    for (int i = 0; i < category->enumCount(); i++) {
        QPoint p = entry->offset(i);
        if (p.isNull())
            continue;
        block.addValue("offset", QString(QLatin1String("%1 %2 %3"))
                       .arg(category->enumToString(i)).arg(p.x()).arg(p.y()));
    }
    parentBlock.blocks += block;
}

static BuildingTileEntry *readTileEntry(BuildingTileCategory *category,
                                        SimpleFileBlock &block,
                                        QString &error)
{
    BuildingTileEntry *entry = new BuildingTileEntry(category);

    foreach (SimpleFileKeyValue kv, block.values) {
        if (kv.name == QLatin1String("offset")) {
            QStringList split = kv.value.split(QLatin1Char(' '), QString::SkipEmptyParts);
            if (split.size() != 3) {
                error = BuildingTilesMgr::instance()->tr("Expected 'offset = name x y', got '%1'").arg(kv.value);
                delete entry;
                return false;
            }
            int e = category->enumFromString(split[0]);
            if (e == BuildingTileCategory::Invalid) {
                error = BuildingTilesMgr::instance()->tr("Unknown %1 enum name '%2'")
                        .arg(category->name()).arg(split[0]);
                delete entry;
                return 0;
            }
            entry->mOffsets[e] = QPoint(split[1].toInt(), split[2].toInt());
            continue;
        }
        int e = category->enumFromString(kv.name);
        if (e == BuildingTileCategory::Invalid) {
            error = BuildingTilesMgr::instance()->tr("Unknown %1 enum name '%2'")
                    .arg(category->name()).arg(kv.name);
            delete entry;
            return 0;
        }
        entry->mTiles[e] = BuildingTilesMgr::instance()->get(kv.value);
    }

    return entry;
}

// VERSION0: original format without 'version' keyvalue
#define VERSION0 0

// VERSION1
// added 'version' keyvalue
// added 'curtains' category
#define VERSION1 1

// VERSION2
// massive rewrite!
#define VERSION2 2
#define VERSION_LATEST VERSION2

bool BuildingTilesMgr::readTxt()
{
    QString fileName = BuildingPreferences::instance()
            ->configPath(QLatin1String(TXT_FILE));
    QFileInfo info(fileName);
    if (!info.exists()) {
        mError = tr("The %1 file doesn't exist.").arg(txtName());
        return false;
    }

    if (!upgradeTxt())
        return false;

    QString path = info.canonicalFilePath();
    SimpleFile simple;
    if (!simple.read(path)) {
        mError = tr("Error reading %1.").arg(path);
        return false;
    }

    if (simple.version() != VERSION_LATEST) {
        mError = tr("Expected %1 version %2, got %3")
                .arg(txtName()).arg(VERSION_LATEST).arg(simple.version());
        return false;
    }

    static const char *validCategoryNamesC[] = {
        "exterior_walls", "interior_walls", "floors", "doors", "door_frames",
        "windows", "curtains", "stairs", "roof_caps", "roof_slopes", "roof_tops",
        0
    };
    QStringList validCategoryNames;
    for (int i = 0; validCategoryNamesC[i]; i++) {
        QString categoryName = QLatin1String(validCategoryNamesC[i]);
        validCategoryNames << categoryName;
    }

    foreach (SimpleFileBlock block, simple.blocks) {
        if (block.name == QLatin1String("category")) {
            QString categoryName = block.value("name");
            if (!validCategoryNames.contains(categoryName)) {
                mError = tr("Unknown category '%1' in BuildingTiles.txt.").arg(categoryName);
                return false;
            }
            BuildingTileCategory *category = this->category(categoryName);
            foreach (SimpleFileBlock block2, block.blocks) {
                if (block2.name == QLatin1String("entry")) {
                    if (BuildingTileEntry *entry = readTileEntry(category, block2, mError)) {
                        // read offset = a b c here too
                        category->insertEntry(category->entryCount(), entry);
                    } else
                        return false;
                } else {
                    mError = tr("Unknown block name '%1'.\n%2")
                            .arg(block2.name)
                            .arg(path);
                    return false;
                }
            }
        } else {
            mError = tr("Unknown block name '%1'.\n%2")
                    .arg(block.name)
                    .arg(path);
            return false;
        }
    }

    // Check that all the tiles exist
    foreach (BuildingTileCategory *category, categories()) {
        foreach (BuildingTileEntry *entry, category->entries()) {
            for (int i = 0; i < category->enumCount(); i++) {
                if (tileFor(entry->tile(i)) == mMissingTile) {
                    mError = tr("Tile %1 #%2 doesn't exist.")
                            .arg(entry->tile(i)->mTilesetName)
                            .arg(entry->tile(i)->mIndex);
                    return false;
                }
            }
        }
    }

    foreach (BuildingTileCategory *category, mCategories)
        category->setDefaultEntry(category->entry(0));
    mCatCurtains->setDefaultEntry(noneTileEntry());

    return true;
}

void BuildingTilesMgr::writeTxt(QWidget *parent)
{
    SimpleFile simpleFile;
    foreach (BuildingTileCategory *category, categories()) {
        SimpleFileBlock categoryBlock;
        categoryBlock.name = QLatin1String("category");
        categoryBlock.values += SimpleFileKeyValue(QLatin1String("label"),
                                                   category->label());
        categoryBlock.values += SimpleFileKeyValue(QLatin1String("name"),
                                                   category->name());
        foreach (BuildingTileEntry *entry, category->entries()) {
            writeTileEntry(categoryBlock, entry);
        }

        simpleFile.blocks += categoryBlock;
    }
    QString fileName = BuildingPreferences::instance()
            ->configPath(QLatin1String(TXT_FILE));
    simpleFile.setVersion(VERSION_LATEST);
    if (!simpleFile.write(fileName)) {
        QMessageBox::warning(parent, tr("It's no good, Jim!"),
                             simpleFile.errorString());
    }
}

BuildingTileEntry *BuildingTilesMgr::defaultCategoryTile(int e) const
{
    return mCategories[e]->defaultEntry();
}

static SimpleFileBlock findCategoryBlock(const SimpleFileBlock &parent,
                                         const QString &categoryName)
{
    foreach (SimpleFileBlock block, parent.blocks) {
        if (block.name == QLatin1String("category")) {
            if (block.value("name") == categoryName)
                return block;
        }
    }
    return SimpleFileBlock();
}

bool BuildingTilesMgr::upgradeTxt()
{
    QString userPath = BuildingPreferences::instance()
            ->configPath(QLatin1String(TXT_FILE));

    SimpleFile userFile;
    if (!userFile.read(userPath)) {
        mError = userFile.errorString();
        return false;
    }

    int userVersion = userFile.version(); // may be zero for unversioned file
    if (userVersion == VERSION_LATEST)
        return true;

    // Not the latest version -> upgrade it.

    QString sourcePath = QCoreApplication::applicationDirPath() + QLatin1Char('/')
            + QLatin1String(TXT_FILE);

    SimpleFile sourceFile;
    if (!sourceFile.read(sourcePath)) {
        mError = sourceFile.errorString();
        return false;
    }
    Q_ASSERT(sourceFile.version() == VERSION_LATEST);

    if (userVersion == VERSION0) {
        userFile.blocks += findCategoryBlock(sourceFile, QLatin1String("curtains"));
    }

    if (VERSION_LATEST == VERSION2) {
        SimpleFileBlock newFile;
        // Massive rewrite -> BuildingTileEntry stuff
        foreach (SimpleFileBlock block, userFile.blocks) {
            if (block.name == QLatin1String("category")) {
                QString categoryName = block.value(QLatin1String("name"));
                SimpleFileBlock newCatBlock;
                newCatBlock.name = block.name;
                newCatBlock.values += SimpleFileKeyValue(QLatin1String("name"),
                                                         categoryName);
                BuildingTileCategory *category = this->category(categoryName);
                foreach (SimpleFileKeyValue kv, block.block("tiles").values) {
                    QString tileName = kv.value;
                    BuildingTileEntry *entry = category->createEntryFromSingleTile(tileName);
                    SimpleFileBlock newEntryBlock;
                    newEntryBlock.name = QLatin1String("entry");
                    for (int i = 0; i < category->enumCount(); i++) {
                        newEntryBlock.values += SimpleFileKeyValue(category->enumToString(i),
                                                                   entry->tile(i)->name());
                        if (!entry->offset(i).isNull())
                            newEntryBlock.values += SimpleFileKeyValue(QLatin1String("offset"),
                                                                       QLatin1String("FIXME"));
                    }
                    newCatBlock.blocks += newEntryBlock;
                }
                newFile.blocks += newCatBlock;
            }
        }
        userFile.blocks = newFile.blocks;
        userFile.values = newFile.values;
    }

    userFile.setVersion(VERSION_LATEST);
    if (!userFile.write(userPath)) {
        mError = userFile.errorString();
        return false;
    }
    return true;
}

Tiled::Tile *BuildingTilesMgr::tileFor(const QString &tileName)
{
    QString tilesetName;
    int index;
    parseTileName(tileName, tilesetName, index);
    if (!mTilesetByName.contains(tilesetName))
        return mMissingTile;
    if (index >= mTilesetByName[tilesetName]->tileCount())
        return mMissingTile;
    return mTilesetByName[tilesetName]->tileAt(index);
}

Tile *BuildingTilesMgr::tileFor(BuildingTile *tile, int offset)
{
    if (tile->isNone())
        return mNoneTiledTile;
    if (!mTilesetByName.contains(tile->mTilesetName))
        return mMissingTile;
    if (tile->mIndex + offset >= mTilesetByName[tile->mTilesetName]->tileCount())
        return mMissingTile;
    return mTilesetByName[tile->mTilesetName]->tileAt(tile->mIndex + offset);
}

BuildingTile *BuildingTilesMgr::fromTiledTile(Tile *tile)
{
    if (tile == mNoneTiledTile)
        return mNoneBuildingTile;
    return get(nameForTile(tile));
}

BuildingTileEntry *BuildingTilesMgr::defaultExteriorWall() const
{
    return mCatEWalls->defaultEntry();
}

BuildingTileEntry *BuildingTilesMgr::defaultInteriorWall() const
{
    return mCatIWalls->defaultEntry();
}

BuildingTileEntry *BuildingTilesMgr::defaultFloorTile() const
{
    return mCatFloors->defaultEntry();
}

BuildingTileEntry *BuildingTilesMgr::defaultDoorTile() const
{
    return mCatDoors->defaultEntry();
}

BuildingTileEntry *BuildingTilesMgr::defaultDoorFrameTile() const
{
    return mCatDoorFrames->defaultEntry();
}

BuildingTileEntry *BuildingTilesMgr::defaultWindowTile() const
{
    return mCatWindows->defaultEntry();
}

BuildingTileEntry *BuildingTilesMgr::defaultCurtainsTile() const
{
    return mCatCurtains->defaultEntry();
}

BuildingTileEntry *BuildingTilesMgr::defaultStairsTile() const
{
    return mCatStairs->defaultEntry();
}

BuildingTileEntry *BuildingTilesMgr::defaultRoofCapTiles() const
{
    return mCatRoofCaps->defaultEntry();
}

BuildingTileEntry *BuildingTilesMgr::defaultRoofSlopeTiles() const
{
    return mCatRoofSlopes->defaultEntry();
}

BuildingTileEntry *BuildingTilesMgr::defaultRoofTopTiles() const
{
    return mCatRoofTops->defaultEntry();
}

/////

QString BuildingTile::name() const
{
    return BuildingTilesMgr::nameForTile(mTilesetName, mIndex);
}

/////

BuildingTileEntry::BuildingTileEntry(BuildingTileCategory *category) :
    mCategory(category)
{
    if (category) {
        mTiles.resize(category->enumCount());
        mOffsets.resize(category->enumCount());
        for (int i = 0; i < mTiles.size(); i++)
            mTiles[i] = BuildingTilesMgr::instance()->noneTile();
    }
}

BuildingTile *BuildingTileEntry::displayTile() const
{
    return tile(mCategory->displayIndex());
}

void BuildingTileEntry::setTile(int e, BuildingTile *btile)
{
    Q_ASSERT(btile);
    mTiles[e] = btile;
}

BuildingTile *BuildingTileEntry::tile(int n) const
{
    if (n < 0 || n >= mTiles.size())
        return BuildingTilesMgr::instance()->noneTile();
    return mTiles[n];
}

QPoint BuildingTileEntry::offset(int n) const
{
    if (n < 0 || n >= mOffsets.size())
        return QPoint();
    return mOffsets[n];
}

bool BuildingTileEntry::usesTile(BuildingTile *btile) const
{
    return mTiles.contains(btile);
}

bool BuildingTileEntry::equals(BuildingTileEntry *other) const
{
    return (mCategory == other->mCategory) &&
            (mTiles == other->mTiles) &&
            (mOffsets == other->mOffsets);
}

BuildingTileEntry *BuildingTileEntry::asCategory(int n)
{
    return (mCategory == BuildingTilesMgr::instance()->category(n))
            ? this : 0;
}

BuildingTileEntry *BuildingTileEntry::asExteriorWall()
{
    return mCategory->asExteriorWalls() ? this : 0;
}

BuildingTileEntry *BuildingTileEntry::asInteriorWall()
{
    return mCategory->asInteriorWalls() ? this : 0;
}

BuildingTileEntry *BuildingTileEntry::asFloor()
{
    return mCategory->asFloors() ? this : 0;
}

BuildingTileEntry *BuildingTileEntry::asDoor()
{
    return mCategory->asDoors() ? this : 0;
}

BuildingTileEntry *BuildingTileEntry::asDoorFrame()
{
    return mCategory->asDoorFrames() ? this : 0;
}

BuildingTileEntry *BuildingTileEntry::asWindow()
{
    return mCategory->asWindows() ? this : 0;
}

BuildingTileEntry *BuildingTileEntry::asCurtains()
{
    return mCategory->asCurtains() ? this : 0;
}

BuildingTileEntry *BuildingTileEntry::asStairs()
{
    return mCategory->asStairs() ? this : 0;
}

BuildingTileEntry *BuildingTileEntry::asRoofCap()
{
    return mCategory->asRoofCaps() ? this : 0;
}

BuildingTileEntry *BuildingTileEntry::asRoofSlope()
{
    return mCategory->asRoofSlopes() ? this : 0;
}

BuildingTileEntry *BuildingTileEntry::asRoofTop()
{
    return mCategory->asRoofTops() ? this : 0;
}

/////

BTC_Doors::BTC_Doors(const QString &label) :
    BuildingTileCategory(QLatin1String("doors"), label, West)
{
    mEnumNames += QLatin1String("West");
    mEnumNames += QLatin1String("North");
    mEnumNames += QLatin1String("WestOpen");
    mEnumNames += QLatin1String("NorthOpen");
    Q_ASSERT(mEnumNames.size() == EnumCount);
}

BuildingTileEntry *BTC_Doors::createEntryFromSingleTile(const QString &tileName)
{
    BuildingTileEntry *entry = new BuildingTileEntry(this);
    entry->mTiles[West] = BuildingTilesMgr::instance()->get(tileName);
    entry->mTiles[North] = BuildingTilesMgr::instance()->get(tileName, 1);
    entry->mTiles[WestOpen] = BuildingTilesMgr::instance()->get(tileName, 2);
    entry->mTiles[NorthOpen] = BuildingTilesMgr::instance()->get(tileName, 3);
    return entry;
}

/////

BTC_Curtains::BTC_Curtains(const QString &label) :
    BuildingTileCategory(QLatin1String("curtains"), label, West)
{
    mEnumNames += QLatin1String("West");
    mEnumNames += QLatin1String("East");
    mEnumNames += QLatin1String("North");
    mEnumNames += QLatin1String("South");
    Q_ASSERT(mEnumNames.size() == EnumCount);
}

BuildingTileEntry *BTC_Curtains::createEntryFromSingleTile(const QString &tileName)
{
    BuildingTileEntry *entry = new BuildingTileEntry(this);
    entry->mTiles[West] = BuildingTilesMgr::instance()->get(tileName);
    entry->mTiles[East] = BuildingTilesMgr::instance()->get(tileName, 1);
    entry->mTiles[North] = BuildingTilesMgr::instance()->get(tileName, 2);
    entry->mTiles[South] = BuildingTilesMgr::instance()->get(tileName, 3);
    return entry;
}

/////

BTC_DoorFrames::BTC_DoorFrames(const QString &label) :
    BuildingTileCategory(QLatin1String("door_frames"), label, West)
{
    mEnumNames += QLatin1String("West");
    mEnumNames += QLatin1String("North");
    Q_ASSERT(mEnumNames.size() == EnumCount);
}

BuildingTileEntry *BTC_DoorFrames::createEntryFromSingleTile(const QString &tileName)
{
    BuildingTileEntry *entry = new BuildingTileEntry(this);
    entry->mTiles[West] = BuildingTilesMgr::instance()->get(tileName);
    entry->mTiles[North] = BuildingTilesMgr::instance()->get(tileName, 1);
    return entry;
}

/////

BTC_Floors::BTC_Floors(const QString &label) :
    BuildingTileCategory(QLatin1String("floors"), label, Floor)
{
    mEnumNames += QLatin1String("Floor");
    Q_ASSERT(mEnumNames.size() == EnumCount);
}

BuildingTileEntry *BTC_Floors::createEntryFromSingleTile(const QString &tileName)
{
    BuildingTileEntry *entry = new BuildingTileEntry(this);
    entry->mTiles[Floor] = BuildingTilesMgr::instance()->get(tileName);
    return entry;
}

/////

BTC_Stairs::BTC_Stairs(const QString &label) :
    BuildingTileCategory(QLatin1String("stairs"), label, West1)
{
    mEnumNames += QLatin1String("West1");
    mEnumNames += QLatin1String("West2");
    mEnumNames += QLatin1String("West3");
    mEnumNames += QLatin1String("North1");
    mEnumNames += QLatin1String("North2");
    mEnumNames += QLatin1String("North3");
    Q_ASSERT(mEnumNames.size() == EnumCount);
}

BuildingTileEntry *BTC_Stairs::createEntryFromSingleTile(const QString &tileName)
{
    BuildingTileEntry *entry = new BuildingTileEntry(this);
    entry->mTiles[West1] = BuildingTilesMgr::instance()->get(tileName);
    entry->mTiles[West2] = BuildingTilesMgr::instance()->get(tileName, 1);
    entry->mTiles[West3] = BuildingTilesMgr::instance()->get(tileName, 2);
    entry->mTiles[North1] = BuildingTilesMgr::instance()->get(tileName, 8);
    entry->mTiles[North2] = BuildingTilesMgr::instance()->get(tileName, 9);
    entry->mTiles[North3] = BuildingTilesMgr::instance()->get(tileName, 10);
    return entry;
}

/////

BTC_Walls::BTC_Walls(const QString &name, const QString &label) :
    BuildingTileCategory(name, label, West)
{
    mEnumNames += QLatin1String("West");
    mEnumNames += QLatin1String("North");
    mEnumNames += QLatin1String("NorthWest");
    mEnumNames += QLatin1String("SouthEast");
    mEnumNames += QLatin1String("WestWindow");
    mEnumNames += QLatin1String("NorthWindow");
    mEnumNames += QLatin1String("WestDoor");
    mEnumNames += QLatin1String("NorthDoor");
    Q_ASSERT(mEnumNames.size() == EnumCount);
}

BuildingTileEntry *BTC_Walls::createEntryFromSingleTile(const QString &tileName)
{
    BuildingTileEntry *entry = new BuildingTileEntry(this);
    entry->mTiles[West] = BuildingTilesMgr::instance()->get(tileName);
    entry->mTiles[North] = BuildingTilesMgr::instance()->get(tileName, 1);
    entry->mTiles[NorthWest] = BuildingTilesMgr::instance()->get(tileName, 2);
    entry->mTiles[SouthEast] = BuildingTilesMgr::instance()->get(tileName, 3);
    entry->mTiles[WestWindow] = BuildingTilesMgr::instance()->get(tileName, 8);
    entry->mTiles[NorthWindow] = BuildingTilesMgr::instance()->get(tileName, 9);
    entry->mTiles[WestDoor] = BuildingTilesMgr::instance()->get(tileName, 10);
    entry->mTiles[NorthDoor] = BuildingTilesMgr::instance()->get(tileName, 11);
    return entry;
}

/////

BTC_Windows::BTC_Windows(const QString &label) :
    BuildingTileCategory(QLatin1String("windows"), label, West)
{
    mEnumNames += QLatin1String("West");
    mEnumNames += QLatin1String("North");
    Q_ASSERT(mEnumNames.size() == EnumCount);
}

BuildingTileEntry *BTC_Windows::createEntryFromSingleTile(const QString &tileName)
{
    BuildingTileEntry *entry = new BuildingTileEntry(this);
    entry->mTiles[West] = BuildingTilesMgr::instance()->get(tileName);
    entry->mTiles[North] = BuildingTilesMgr::instance()->get(tileName, 1);
    return entry;
}

/////

BTC_RoofCaps::BTC_RoofCaps(const QString &label) :
    BuildingTileCategory(QLatin1String("roof_caps"), label, CapRiseE3)
{
    mEnumNames += QLatin1String("CapRiseE1");
    mEnumNames += QLatin1String("CapRiseE2");
    mEnumNames += QLatin1String("CapRiseE3");
    mEnumNames += QLatin1String("CapFallE1");
    mEnumNames += QLatin1String("CapFallE2");
    mEnumNames += QLatin1String("CapFallE3");

    mEnumNames += QLatin1String("CapRiseS1");
    mEnumNames += QLatin1String("CapRiseS2");
    mEnumNames += QLatin1String("CapRiseS3");
    mEnumNames += QLatin1String("CapFallS1");
    mEnumNames += QLatin1String("CapFallS2");
    mEnumNames += QLatin1String("CapFallS3");

    mEnumNames += QLatin1String("PeakPt5S");
    mEnumNames += QLatin1String("PeakPt5E");
    mEnumNames += QLatin1String("PeakOnePt5S");
    mEnumNames += QLatin1String("PeakOnePt5E");
    mEnumNames += QLatin1String("PeakTwoPt5S");
    mEnumNames += QLatin1String("PeakTwoPt5E");

    mEnumNames += QLatin1String("CapGapS1");
    mEnumNames += QLatin1String("CapGapS2");
    mEnumNames += QLatin1String("CapGapS3");
    mEnumNames += QLatin1String("CapGapE1");
    mEnumNames += QLatin1String("CapGapE2");
    mEnumNames += QLatin1String("CapGapE3");

    Q_ASSERT(mEnumNames.size() == EnumCount);
}

BuildingTileEntry *BTC_RoofCaps::createEntryFromSingleTile(const QString &tileName)
{
    BuildingTileEntry *entry = new BuildingTileEntry(this);
    entry->mTiles[CapRiseE1] = BuildingTilesMgr::instance()->get(tileName, 0);
    entry->mTiles[CapRiseE2] = BuildingTilesMgr::instance()->get(tileName, 1);
    entry->mTiles[CapRiseE3] = BuildingTilesMgr::instance()->get(tileName, 2);
    entry->mTiles[CapFallE1] = BuildingTilesMgr::instance()->get(tileName, 8);
    entry->mTiles[CapFallE2] = BuildingTilesMgr::instance()->get(tileName, 9);
    entry->mTiles[CapFallE3] = BuildingTilesMgr::instance()->get(tileName, 10);
    entry->mTiles[CapRiseS1] = BuildingTilesMgr::instance()->get(tileName, 13);
    entry->mTiles[CapRiseS2] = BuildingTilesMgr::instance()->get(tileName, 12);
    entry->mTiles[CapRiseS3] = BuildingTilesMgr::instance()->get(tileName, 11);
    entry->mTiles[CapFallS1] = BuildingTilesMgr::instance()->get(tileName, 5);
    entry->mTiles[CapFallS2] = BuildingTilesMgr::instance()->get(tileName, 4);
    entry->mTiles[CapFallS3] = BuildingTilesMgr::instance()->get(tileName, 3);
    entry->mTiles[PeakPt5S] = BuildingTilesMgr::instance()->get(tileName, 7);
    entry->mTiles[PeakPt5E] = BuildingTilesMgr::instance()->get(tileName, 15);
    entry->mTiles[PeakOnePt5S] = BuildingTilesMgr::instance()->get(tileName, 6);
    entry->mTiles[PeakOnePt5E] = BuildingTilesMgr::instance()->get(tileName, 14);
    entry->mTiles[PeakTwoPt5S] = BuildingTilesMgr::instance()->get(tileName, 17);
    entry->mTiles[PeakTwoPt5E] = BuildingTilesMgr::instance()->get(tileName, 16);
#if 0 // impossible to guess these
    entry->mTiles[CapGapS1] = BuildingTilesMgr::instance()->get(tileName, );
    entry->mTiles[CapGapS2] = BuildingTilesMgr::instance()->get(tileName, );
    entry->mTiles[CapGapS3] = BuildingTilesMgr::instance()->get(tileName, );
    entry->mTiles[CapGapE1] = BuildingTilesMgr::instance()->get(tileName, );
    entry->mTiles[CapGapE2] = BuildingTilesMgr::instance()->get(tileName, );
    entry->mTiles[CapGapE3] = BuildingTilesMgr::instance()->get(tileName, );
#endif
    return entry;
}

int BTC_RoofCaps::shadowToEnum(int shadowIndex)
{
    const int map[EnumCount] = {
        CapRiseE1, CapRiseE2, CapRiseE3, CapFallS3, CapFallS2, CapFallS1,
        CapFallE1, CapFallE2, CapFallE3, CapRiseS3, CapRiseS2, CapRiseS1,
        PeakPt5E, PeakOnePt5E, PeakTwoPt5E, PeakTwoPt5S, PeakOnePt5S, PeakPt5S,
        CapGapE1, CapGapE2, CapGapE3, CapGapS3, CapGapS2, CapGapS1
    };
    return map[shadowIndex];
}

int BTC_RoofCaps::enumToShadow(int e)
{
    int map[EnumCount];
    for (int i = 0; i < EnumCount; i++)
        map[shadowToEnum(i)] = i;
    return map[e];
}

/////

BTC_RoofSlopes::BTC_RoofSlopes(const QString &label) :
    BuildingTileCategory(QLatin1String("roof_slopes"), label, SlopeS2)
{
    mEnumNames += QLatin1String("SlopeS1");
    mEnumNames += QLatin1String("SlopeS2");
    mEnumNames += QLatin1String("SlopeS3");
    mEnumNames += QLatin1String("SlopeE1");
    mEnumNames += QLatin1String("SlopeE2");
    mEnumNames += QLatin1String("SlopeE3");

    mEnumNames += QLatin1String("SlopePt5S");
    mEnumNames += QLatin1String("SlopePt5E");
    mEnumNames += QLatin1String("SlopeOnePt5S");
    mEnumNames += QLatin1String("SlopeOnePt5E");
    mEnumNames += QLatin1String("SlopeTwoPt5S");
    mEnumNames += QLatin1String("SlopeTwoPt5E");
#if 0
    mEnumNames += QLatin1String("FlatTopW1");
    mEnumNames += QLatin1String("FlatTopW2");
    mEnumNames += QLatin1String("FlatTopW3");
    mEnumNames += QLatin1String("FlatTopN1");
    mEnumNames += QLatin1String("FlatTopN2");
    mEnumNames += QLatin1String("FlatTopN3");
#endif
    mEnumNames += QLatin1String("Inner1");
    mEnumNames += QLatin1String("Inner2");
    mEnumNames += QLatin1String("Inner3");
    mEnumNames += QLatin1String("Outer1");
    mEnumNames += QLatin1String("Outer2");
    mEnumNames += QLatin1String("Outer3");

    Q_ASSERT(mEnumNames.size() == EnumCount);
}

BuildingTileEntry *BTC_RoofSlopes::createEntryFromSingleTile(const QString &tileName)
{
    BuildingTileEntry *entry = new BuildingTileEntry(this);
    entry->mTiles[SlopeS1] = BuildingTilesMgr::instance()->get(tileName, 0);
    entry->mTiles[SlopeS2] = BuildingTilesMgr::instance()->get(tileName, 1);
    entry->mTiles[SlopeS3] = BuildingTilesMgr::instance()->get(tileName, 2);
    entry->mTiles[SlopeE1] = BuildingTilesMgr::instance()->get(tileName, 5);
    entry->mTiles[SlopeE2] = BuildingTilesMgr::instance()->get(tileName, 4);
    entry->mTiles[SlopeE3] = BuildingTilesMgr::instance()->get(tileName, 3);
    entry->mTiles[SlopePt5S] = BuildingTilesMgr::instance()->get(tileName, 15);
    entry->mTiles[SlopePt5E] = BuildingTilesMgr::instance()->get(tileName, 14);
    entry->mTiles[SlopeOnePt5S] = BuildingTilesMgr::instance()->get(tileName, 15);
    entry->mTiles[SlopeOnePt5E] = BuildingTilesMgr::instance()->get(tileName, 14);
    entry->mTiles[SlopeTwoPt5S] = BuildingTilesMgr::instance()->get(tileName, 15);
    entry->mTiles[SlopeTwoPt5E] = BuildingTilesMgr::instance()->get(tileName, 14);
    entry->mTiles[Inner1] = BuildingTilesMgr::instance()->get(tileName, 11);
    entry->mTiles[Inner2] = BuildingTilesMgr::instance()->get(tileName, 12);
    entry->mTiles[Inner3] = BuildingTilesMgr::instance()->get(tileName, 13);
    entry->mTiles[Outer1] = BuildingTilesMgr::instance()->get(tileName, 8);
    entry->mTiles[Outer2] = BuildingTilesMgr::instance()->get(tileName, 9);
    entry->mTiles[Outer3] = BuildingTilesMgr::instance()->get(tileName, 10);

    entry->mOffsets[SlopePt5S] = QPoint(1, 1);
    entry->mOffsets[SlopePt5E] = QPoint(1, 1);
    entry->mOffsets[SlopeTwoPt5S] = QPoint(-1, -1);
    entry->mOffsets[SlopeTwoPt5E] = QPoint(-1, -1);
    return entry;
}

/////

BTC_RoofTops::BTC_RoofTops(const QString &label) :
    BuildingTileCategory(QLatin1String("roof_tops"), label, West2)
{
    mEnumNames += QLatin1String("West1");
    mEnumNames += QLatin1String("West2");
    mEnumNames += QLatin1String("West3");
    mEnumNames += QLatin1String("North1");
    mEnumNames += QLatin1String("North2");
    mEnumNames += QLatin1String("North3");
    Q_ASSERT(mEnumNames.size() == EnumCount);
}

BuildingTileEntry *BTC_RoofTops::createEntryFromSingleTile(const QString &tileName)
{
    BuildingTileEntry *entry = new BuildingTileEntry(this);
    entry->mTiles[West1] = BuildingTilesMgr::instance()->get(tileName);
    entry->mTiles[West2] = BuildingTilesMgr::instance()->get(tileName);
    entry->mTiles[West3] = BuildingTilesMgr::instance()->get(tileName);
    entry->mTiles[North1] = BuildingTilesMgr::instance()->get(tileName);
    entry->mTiles[North2] = BuildingTilesMgr::instance()->get(tileName);
    entry->mTiles[North3] = BuildingTilesMgr::instance()->get(tileName);
    entry->mOffsets[West1] = QPoint(-1, -1);
    entry->mOffsets[West2] = QPoint(-2, -2);
    entry->mOffsets[North1] = QPoint(-1, -1);
    entry->mOffsets[North2] = QPoint(-2, -2);
    return entry;
}

/////

BuildingTileCategory::BuildingTileCategory(const QString &name,
                                           const QString &label,
                                           int displayIndex) :
    mName(name),
    mLabel(label),
    mDisplayIndex(displayIndex),
    mDefaultEntry(0)
{
}

BuildingTileEntry *BuildingTileCategory::entry(int index) const
{
    if (index < 0 || index >= mEntries.size())
        return BuildingTilesMgr::instance()->noneTileEntry();
    return mEntries[index];
}

void BuildingTileCategory::insertEntry(int index, BuildingTileEntry *entry)
{
    Q_ASSERT(entry && !entry->isNone());
    Q_ASSERT(!mEntries.contains(entry));
    Q_ASSERT(entry->category() == this);
    mEntries.insert(index, entry);
}

BuildingTileEntry *BuildingTileCategory::removeEntry(int index)
{
    return mEntries.takeAt(index);
}

QString BuildingTileCategory::enumToString(int index) const
{
    if (index < 0 || index >= mEnumNames.size())
        return QLatin1String("Invalid");
    return mEnumNames[index];
}

int BuildingTileCategory::enumFromString(const QString &s) const
{
    if (mEnumNames.contains(s))
        return mEnumNames.indexOf(s);
    return Invalid;
}

BuildingTileEntry *BuildingTileCategory::findMatch(BuildingTileEntry *entry) const
{
    foreach (BuildingTileEntry *candidate, mEntries) {
        if (candidate->equals(entry))
            return candidate;
    }
    return 0;
}

bool BuildingTileCategory::usesTile(Tile *tile) const
{
    BuildingTile *btile = BuildingTilesMgr::instance()->fromTiledTile(tile);
    foreach (BuildingTileEntry *entry, mEntries) {
        if (entry->usesTile(btile))
            return true;
    }
    return false;
}

BuildingTileEntry *BuildingTileCategory::createEntryFromSingleTile(const QString &tileName)
{
    Q_UNUSED(tileName)
    return 0;
}

/////
