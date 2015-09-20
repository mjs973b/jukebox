/**
 * Copyright (C) 2015 Mike Scheutzow <mjs973@gmail.com>
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

#ifndef NORMALPLAYLIST_H
#define NORMALPLAYLIST_H

#include "playlist.h"

class NormalPlaylist : public Playlist
{
    Q_OBJECT
public:
    NormalPlaylist(PlaylistCollection *collection, const QString& name = QString()) :
        Playlist(collection, name)
    {
    }

    NormalPlaylist(PlaylistCollection *collection, const PlaylistItemList &items,
             const QString &name /*= QString()*/) :
        Playlist(collection, items, name)
    {
    }

    NormalPlaylist(PlaylistCollection *collection, const QFileInfo& playlistFile) :
        Playlist(collection, playlistFile)
    {
    }

    NormalPlaylist(PlaylistCollection *collection,
                   bool delaySetup) :
        Playlist(collection, delaySetup)
    {
    }

    virtual ~NormalPlaylist() { };

    virtual int getType() { return Playlist::Type::Normal; }

    void read(QDataStream &s);
};

QDataStream &operator<<(QDataStream &s, const NormalPlaylist &p);

QDataStream &operator>>(QDataStream &s, NormalPlaylist &p);

#if 0
// template method implementations

template <class ItemType>
ItemType *NormalPlaylist::createItem(const FileHandle &file, Q3ListViewItem *after,
                               bool emitChanged)
{
    CollectionListItem *item = collectionListItem(file);
    if(item && (!m_members.insert(file.absFilePath()) || m_allowDuplicates)) {

        ItemType *i = after ? new ItemType(item, this, after) : new ItemType(item, this);
        setupItem(i);

        if(emitChanged)
            dataChanged();

        return i;
    }
    else
        return 0;
}

template <class ItemType, class SiblingType>
ItemType *NormalPlaylist::createItem(SiblingType *sibling, ItemType *after)
{
    m_disableColumnWidthUpdates = true;

    if(!m_members.insert(sibling->file().absFilePath()) || m_allowDuplicates) {
        after = new ItemType(sibling->collectionItem(), this, after);
        setupItem(after);
    }

    m_disableColumnWidthUpdates = false;

    return after;
}

template <class ItemType, class SiblingType>
void NormalPlaylist::createItems(const QList<SiblingType *> &siblings, ItemType *after)
{
    if(siblings.isEmpty())
        return;

    foreach(SiblingType *sibling, siblings)
        after = createItem(sibling, after);

    dataChanged();
    slotWeightDirty();
}
#endif
#endif

// vim: set et sw=4 tw=0 sta:
