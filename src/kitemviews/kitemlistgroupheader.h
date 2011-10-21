/***************************************************************************
 *   Copyright (C) 2011 by Peter Penz <peter.penz19@gmail.com>             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA            *
 ***************************************************************************/

#ifndef KITEMLISTGROUPHEADER_H
#define KITEMLISTGROUPHEADER_H

#include <libdolphin_export.h>

#include <QByteArray>
#include <QGraphicsWidget>
#include <QVariant>

class KItemListView;

class LIBDOLPHINPRIVATE_EXPORT KItemListGroupHeader : public QGraphicsWidget
{
    Q_OBJECT

public:
    KItemListGroupHeader(QGraphicsWidget* parent = 0);
    virtual ~KItemListGroupHeader();

    void setRole(const QByteArray& role);
    QByteArray role() const;

    void setData(const QVariant& data);
    QVariant data() const;

    virtual QSizeF sizeHint(Qt::SizeHint which = Qt::PreferredSize, const QSizeF& constraint = QSizeF()) const;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = 0);

protected:
    virtual void roleChanged(const QByteArray& current, const QByteArray& previous);
    virtual void dataChanged(const QVariant& current, const QVariant& previous);

private:
    QByteArray m_role;
    QVariant m_data;

};
#endif


