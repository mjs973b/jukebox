/**
 * Copyright (C) 2005 Michael Pyne <mpyne@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COVERICONVIEW_H
#define COVERICONVIEW_H

#include <k3iconview.h>

#include "covermanager.h"

// The WebImageFetcher dialog also has a class named CoverIconViewItem and I
// don't like the idea of naming it "CoverIVI" or something, so just namespace
// it out.  I would merge them except for webimagefetcher's dependence on KIO
// and such.

namespace CoverUtility
{
    class CoverIconViewItem : public K3IconViewItem
    {
    public:
        CoverIconViewItem(coverKey id, Q3IconView *parent);

        coverKey id() const { return m_id; }

    private:
        coverKey m_id;
    };
}

using CoverUtility::CoverIconViewItem;

/**
 * This class subclasses K3IconView in order to provide cover drag-and-drop
 * support.
 *
 * @author Michael Pyne <mpyne@kde.org>
 */
class CoverIconView : public K3IconView
{
public:
    explicit CoverIconView(QWidget *parent, const char *name = 0);

    CoverIconViewItem *currentItem() const;

protected:
    virtual Q3DragObject *dragObject();
};

#endif /* COVERICONVIEW_H */

// vim: set et sw=4 tw=0 sta:
