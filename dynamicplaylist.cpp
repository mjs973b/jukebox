/**
 * Copyright (C) 2003-2004 Scott Wheeler <wheeler@kde.org>
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

#include "dynamicplaylist.h"
#include "collectionlist.h"
#include "playlistcollection.h"
#include "tracksequencemanager.h"

#include <QTimer>

////////////////////////////////////////////////////////////////////////////////
// public methods
////////////////////////////////////////////////////////////////////////////////

DynamicPlaylist::DynamicPlaylist(const PlaylistList &playlists,
                                 PlaylistCollection *collection,
                                 const QString &name,
                                 const QString &iconName,
                                 bool setupPlaylist,
                                 bool synchronizePlaying) :
    Playlist(collection, true),
    PlaylistObserver(PlaylistCollection::instance()),
    m_playlists(playlists),
    m_dirty(true),
    m_synchronizePlaying(synchronizePlaying)
{
    if(setupPlaylist)
        collection->setupPlaylist(this, iconName);
    setName(name);
    setColumnSortEnabled(true);
}

DynamicPlaylist::~DynamicPlaylist()
{
    lower();
}

bool DynamicPlaylist::getPolicy(Playlist::Policy p) const {
    switch(p) {
    case PolicyCanModifyContent: return false;
    case PolicyCanRename:        return false;
    case PolicyCanDelete:        return false;
    case PolicyCanReload:        return false;
    case PolicyPromptToSave:     return false;
    default:                     return false;
    }
}

void DynamicPlaylist::setPlaylists(const PlaylistList &playlists)
{
    m_playlists = playlists;
    updateItems();
}

////////////////////////////////////////////////////////////////////////////////
// public slots
////////////////////////////////////////////////////////////////////////////////

void DynamicPlaylist::slotReload()
{
    for(PlaylistList::Iterator it = m_playlists.begin(); it != m_playlists.end(); ++it)
        (*it)->slotReload();

    checkUpdateItems();
}

/**
 * This method is called when this playlist is the track table visible to the
 * user, but it is about to be replaced by \p top. This is invoked only for
 * the ephemeral DynamicPlaylist and for the ShowMore SearchPlaylist.
 *
 * @param top  the new playlist
 */
void DynamicPlaylist::lower(QWidget *top)
{
    Q_UNUSED(top);
#if 0
    /* FIXME: None of this code makes any sense. It's 'top' that needs to be
     * sync'ed, not 'this'. Also this method is called when user wants to just
     * navigate to a different playlist; that should have no effect on the
     * currently-playing playlist. What a mess. (mjs)
     *
     * There seems to be only one ephemeral DynamicPlaylist (not counting any
     * SearchPlaylists) which lives forever on the qstackedwidget, so maybe
     * that has something to do with this code?
     */
//    if(top == this)
//        return;
//
//    if(hasPlayingItem()) {
//        PlaylistList l;
//        l.append(this);
//        for(PlaylistList::Iterator it = m_playlists.begin();
//            it != m_playlists.end(); ++it)
//        {
//            (*it)->synchronizePlayingItems(l, true);
//        }
//    }
//
//    PlaylistItemList list = PlaylistItem::playingItems();
//    for(PlaylistItemList::Iterator it = list.begin(); it != list.end(); ++it) {
//        if((*it)->playlist() == this) {
//            list.erase(it);
//            break;
//        }
//    }
//
//    if(!list.isEmpty())
//        TrackSequenceManager::instance()->setDefaultPlaylist(list.front()->playlist());
#endif
}

/* @see PlaylistObserver */
void DynamicPlaylist::updateCurrent()
{
}

/* @see PlaylistObserver */
void DynamicPlaylist::updateData()
{
    // rebuild the list the next time updateItems() is called
    m_dirty = true;
}

////////////////////////////////////////////////////////////////////////////////
// protected methods
////////////////////////////////////////////////////////////////////////////////

PlaylistItemList DynamicPlaylist::items()
{
    checkUpdateItems();
    return Playlist::items();
}

void DynamicPlaylist::showEvent(QShowEvent *e)
{
    checkUpdateItems();
    Playlist::showEvent(e);
}

void DynamicPlaylist::paintEvent(QPaintEvent *e)
{
    checkUpdateItems();
    Playlist::paintEvent(e);
}

/* m_playlists contains a list of playlists which form this DynamicPlaylist */
void DynamicPlaylist::updateItems()
{
    QList<PlaylistItem*> siblings;

    foreach(Playlist *pl, m_playlists) {
        siblings += pl->items();
    }

    // compare the content of the two QList
    if(m_siblings != siblings) {
        m_siblings = siblings;
        QTimer::singleShot(0, this, SLOT(slotUpdateItems()));
    }
}

bool DynamicPlaylist::synchronizePlaying() const
{
    return m_synchronizePlaying;
}

////////////////////////////////////////////////////////////////////////////////
// private methods
////////////////////////////////////////////////////////////////////////////////

void DynamicPlaylist::checkUpdateItems()
{
    if(!m_dirty)
        return;

    updateItems();

    m_dirty = false;
}

////////////////////////////////////////////////////////////////////////////////
// private slots
////////////////////////////////////////////////////////////////////////////////

void DynamicPlaylist::slotUpdateItems()
{
    // This should be optimized to check to see which items are already in the
    // list and just adding those and removing the ones that aren't.

    this->clearItems( this->items() );
    //kDebug() << "playlist item cnt = " << m_siblings.count();
    createItems(m_siblings);
    if(m_synchronizePlaying)
        synchronizePlayingItems(m_playlists, true);
}

#include "dynamicplaylist.moc"

// vim: set et sw=4 tw=0 sta:
