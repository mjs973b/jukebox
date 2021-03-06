/**
 * Copyright (C) 2002-2004 Michael Pyne <mpyne@kde.org>
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

#include "tracksequenceiterator.h"

#include <kaction.h>
#include <kapplication.h>
#include <kdebug.h>
#include <krandom.h>
#include <ktoggleaction.h>

#include "playlist.h"
#include "actioncollection.h"
#include "tag.h"
#include "filehandle.h"

using namespace ActionCollection;

TrackSequenceIterator::TrackSequenceIterator() :
    m_current(0)
{
}

TrackSequenceIterator::TrackSequenceIterator(const TrackSequenceIterator &other) :
    m_current(other.m_current)
{
}

TrackSequenceIterator::~TrackSequenceIterator()
{
}

void TrackSequenceIterator::setCurrent(PlaylistItem *current)
{
    m_current = current;
}

void TrackSequenceIterator::playlistChanged()
{
}

void TrackSequenceIterator::itemAboutToDie(const PlaylistItem *)
{
}

DefaultSequenceIterator::DefaultSequenceIterator() :
    TrackSequenceIterator()
{
}

DefaultSequenceIterator::DefaultSequenceIterator(const DefaultSequenceIterator &other)
    : TrackSequenceIterator(other)
{
}

DefaultSequenceIterator::~DefaultSequenceIterator()
{
}

void DefaultSequenceIterator::advance()
{
    if(!current())
        return;

    bool isRandom = action("randomPlay") && action<KToggleAction>("randomPlay")->isChecked();
    bool loop = action<KAction>("loopPlaylist") && action<KAction>("loopPlaylist")->isChecked();
    bool albumRandom = action("albumRandomPlay") && action<KToggleAction>("albumRandomPlay")->isChecked();

    if(isRandom || albumRandom) {
        if(m_randomItems.isEmpty() && loop) {

            // Since refillRandomList will remove the currently playing item,
            // we should clear it out first since that's not good for e.g.
            // lists with 1-2 items.  We need to remember the Playlist though.

            Playlist *playlist = current()->playlist();
            setCurrent(0);

            refillRandomList(playlist);
        }

        if(m_randomItems.isEmpty()) {
            setCurrent(0);
            return;
        }

        PlaylistItem *item;

        if(albumRandom) {
            if(m_albumSearch.isNull() || m_albumSearch.matchedItems().isEmpty()) {
                item = m_randomItems[KRandom::random() % m_randomItems.count()];
                initAlbumSearch(item);
            }

            // This can be null if initAlbumSearch() left the m_albumSearch
            // empty because the album text was empty.  Since we initAlbumSearch()
            // with an item, the matchedItems() should never be empty.

            if(!m_albumSearch.isNull()) {
                PlaylistItemList albumMatches = m_albumSearch.matchedItems();
                if(albumMatches.isEmpty()) {
                    kError() << "Unable to initialize album random play.\n";
                    kError() << "List of potential results is empty.\n";

                    return; // item is still set to random song from a few lines earlier.
                }

                item = albumMatches[0];

                // Pick first song remaining in list.

                for(int i = 0; i < albumMatches.count(); ++i)
                    if(albumMatches[i]->file().tag()->track() < item->file().tag()->track())
                        item = albumMatches[i];
                m_albumSearch.clearItem(item);

                if(m_albumSearch.matchedItems().isEmpty()) {
                    m_albumSearch.clearComponents();
                    m_albumSearch.search();
                }
            }
            else
                kError() << "Unable to perform album random play on " << *item << endl;
        }
        else
            item = m_randomItems[KRandom::random() % m_randomItems.count()];

        setCurrent(item);
        m_randomItems.removeAll(item);
    }
    else {
        PlaylistItem *next = current()->itemBelow();
        if(!next && loop) {
            Playlist *p = current()->playlist();
            next = p->firstChild();
            while(next && !next->isVisible())
                next = static_cast<PlaylistItem *>(next->nextSibling());
        }

        setCurrent(next);
    }
}

void DefaultSequenceIterator::backup()
{
    if(!current())
        return;

    PlaylistItem *item = current()->itemAbove();

    if(item)
        setCurrent(item);
}

void DefaultSequenceIterator::prepareToPlay(Playlist *playlist)
{
    bool random = action("randomPlay") && action<KToggleAction>("randomPlay")->isChecked();
    bool albumRandom = action("albumRandomPlay") && action<KToggleAction>("albumRandomPlay")->isChecked();

    if(random || albumRandom) {
        PlaylistItemList items = playlist->selectedItems();
        if(items.isEmpty())
            items = playlist->visibleItems();

        PlaylistItem *newItem = 0;
        if(!items.isEmpty())
            newItem = items[KRandom::random() % items.count()];

        setCurrent(newItem);
        refillRandomList();
    }
    else {
        Q3ListViewItemIterator it(playlist, Q3ListViewItemIterator::Visible | Q3ListViewItemIterator::Selected);
        if(!it.current())
            it = Q3ListViewItemIterator(playlist, Q3ListViewItemIterator::Visible);

        setCurrent(static_cast<PlaylistItem *>(it.current()));
    }
}

void DefaultSequenceIterator::reset()
{
    m_randomItems.clear();
    m_albumSearch.clearComponents();
    m_albumSearch.search();
    setCurrent(0);
}

void DefaultSequenceIterator::playlistChanged()
{
    refillRandomList();
}

void DefaultSequenceIterator::itemAboutToDie(const PlaylistItem *item)
{
    PlaylistItem *stfu_gcc = const_cast<PlaylistItem *>(item);
    m_randomItems.removeAll(stfu_gcc);
}

void DefaultSequenceIterator::setCurrent(PlaylistItem *current)
{
    PlaylistItem *oldCurrent = DefaultSequenceIterator::current();

    TrackSequenceIterator::setCurrent(current);

    bool random = action("randomPlay") && action<KToggleAction>("randomPlay")->isChecked();
    bool albumRandom = action("albumRandomPlay") && action<KToggleAction>("albumRandomPlay")->isChecked();

    if((albumRandom || random) && current && m_randomItems.isEmpty()) {

        // We're setting a current item, refill the random list now, and remove
        // the current item.

        refillRandomList();
    }

    m_randomItems.removeAll(current);

    if(albumRandom && current && !oldCurrent) {

        // Same idea as above

        initAlbumSearch(current);
        m_albumSearch.clearItem(current);
    }
}

DefaultSequenceIterator *DefaultSequenceIterator::clone() const
{
    return new DefaultSequenceIterator(*this);
}

void DefaultSequenceIterator::refillRandomList(Playlist *p)
{
    if(!p) {
        if (!current())
            return;

        p = current()->playlist();

        if(!p) {
            kError() << "Item has no playlist!\n";
            return;
        }
    }

    m_randomItems = p->visibleItems();
    m_randomItems.removeAll(current());
    m_albumSearch.clearComponents();
    m_albumSearch.search();
}

void DefaultSequenceIterator::initAlbumSearch(PlaylistItem *searchItem)
{
    if(!searchItem)
        return;

    m_albumSearch.clearPlaylists();
    m_albumSearch.addPlaylist(searchItem->playlist());

    ColumnList columns;

    m_albumSearch.setSearchMode(PlaylistSearch::MatchAll);
    m_albumSearch.clearComponents();

    // If the album name is empty, it will mess up the search,
    // so ignore empty album names.

    if(searchItem->file().tag()->album().isEmpty())
        return;

    columns.append(PlaylistItem::AlbumColumn);

    m_albumSearch.addComponent(PlaylistSearch::Component(
        searchItem->file().tag()->album(),
        true,
        columns,
        PlaylistSearch::Component::Exact)
    );

    // If there is an Artist tag with the track, match against it as well
    // to avoid things like multiple "Greatest Hits" albums matching the
    // search.

    if(!searchItem->file().tag()->artist().isEmpty()) {
        kDebug() << "Searching both artist and album.";
        columns[0] = PlaylistItem::ArtistColumn;

        m_albumSearch.addComponent(PlaylistSearch::Component(
            searchItem->file().tag()->artist(),
            true,
            columns,
            PlaylistSearch::Component::Exact)
        );
    }

    m_albumSearch.search();
}

// vim: set et sw=4 tw=0 sta:
