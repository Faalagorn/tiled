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

#ifndef SIMPLEFILE_H
#define SIMPLEFILE_H

#include <QString>
#include <QTextStream>

class SimpleFileKeyValue
{
public:
    QString name;
    QString value;
};

class SimpleFileBlock
{
public:
    QString name;
    QList<SimpleFileKeyValue> values;
    QList<SimpleFileBlock> blocks;

    QString value(const char *key)
    { return value(QLatin1String(key)); }

    QString value(const QString &key);

    SimpleFileBlock block(const char *name)
    { return block(QLatin1String(name)); }

    SimpleFileBlock block(const QString &name);

    void print();
};


class SimpleFile : public SimpleFileBlock
{
public:
    SimpleFile();

    bool read(const QString &filePath);

private:
    SimpleFileBlock readBlock(QTextStream &ts);
};

#endif // SIMPLEFILE_H
