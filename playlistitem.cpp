/**
 * Copyright (C) 2002-2004 Scott Wheeler <wheeler@kde.org>
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

#include "playlistitem.h"

#include <config-juk.h>
#include <kdebug.h>
#include <kiconloader.h>

#include <QPixmap>
#include <QFileInfo>

#include "collectionlist.h"
#include "musicbrainzquery.h"
#include "tag.h"
#include "coverinfo.h"
#include "covermanager.h"
#include "tagtransactionmanager.h"

PlaylistItemList PlaylistItem::m_playingItems; // static

static void startMusicBrainzQuery(const FileHandle &file)
{
#if HAVE_TUNEPIMP
    // This deletes itself when finished.
    new MusicBrainzLookup(file);
#else
    Q_UNUSED(file)
#endif
}

////////////////////////////////////////////////////////////////////////////////
// PlaylistItem public methods
////////////////////////////////////////////////////////////////////////////////

PlaylistItem::~PlaylistItem()
{
    // Although this isn't the most efficient way to accomplish the task of
    // stopping playback when deleting the item being played, it has the
    // stark advantage of working reliably.  I'll tell anyone who tries to
    // optimize this, the timing issues can be *hard*. -- mpyne

    m_collectionItem->removeChildItem(this);

    if(m_playingItems.contains(this)) {
        m_playingItems.removeAll(this);
        if(m_playingItems.isEmpty())
            playlist()->setPlaying(0);
    }

    playlist()->updateDeletedItem(this);
    emit playlist()->signalAboutToRemove(this);

    if(m_watched)
        Pointer::clear(this);
}

void PlaylistItem::setFile(const FileHandle &file)
{
    m_collectionItem->updateCollectionDict(d->fileHandle.absFilePath(), file.absFilePath());
    d->fileHandle = file;
    refresh();
}

void PlaylistItem::setFile(const QString &file)
{
    QString oldPath = d->fileHandle.absFilePath();
    d->fileHandle.setFile(file);
    m_collectionItem->updateCollectionDict(oldPath, d->fileHandle.absFilePath());
    refresh();
}

FileHandle PlaylistItem::file() const
{
    return d->fileHandle;
}

/* This will create a static pointer to a QPixmap at app runtime. The icon 
 * path will be searched for ${name}.png e.g. "playing.png". The install
 * process should create these icons at install time. See class KIconLoader.
 */
K_GLOBAL_STATIC_WITH_ARGS(QPixmap, globalCheckboxOnImage, (SmallIcon("cb-on")))
K_GLOBAL_STATIC_WITH_ARGS(QPixmap, globalCheckboxOffImage, (SmallIcon("cb-off")))
K_GLOBAL_STATIC_WITH_ARGS(QPixmap, globalPlayingImage, (UserIcon("playing")))

// FIXME: this method is called way too often (mjs)
const QPixmap *PlaylistItem::pixmap(int column) const
{
    if(column == CoverColumn)
    {
        // coverInfo is already cached in RAM, so this call is not expensive
        bool b = d->fileHandle.coverInfo()->hasCover();
        return (b) ? globalCheckboxOnImage : globalCheckboxOffImage;
    }

    if(column == playlist()->leftColumn() &&
        m_playingItems.contains(const_cast<PlaylistItem *>(this)))
    {
        return globalPlayingImage;
    }

    return K3ListViewItem::pixmap(column);
}

QString PlaylistItem::text(int column) const
{
    const Tag *tag = d->fileHandle.tag();
    if(!tag)
        return QString();

    switch(column) {
    case TrackColumn:
        return tag->title();
    case ArtistColumn:
        return tag->artist();
    case AlbumColumn:
        return tag->album();
    case CoverColumn:
        return QString();
    case TrackNumberColumn:
        return tag->track() > 0
            ? QString::number(tag->track())
            : QString();
    case GenreColumn:
        return tag->genre();
    case YearColumn:
        return tag->year() > 0
            ? QString::number(tag->year())
            : QString();
    case LengthColumn:
        return tag->lengthString();
    case BitrateColumn:
        return QString::number(tag->bitrate());
    case CommentColumn:
        return tag->comment();
    case FileNameColumn:
        return d->fileHandle.fileInfo().fileName();
    case FullPathColumn:
        return d->fileHandle.fileInfo().absoluteFilePath();
    default:
        return K3ListViewItem::text(column);
    }
}

void PlaylistItem::setText(int column, const QString &text)
{
    if(column >= 0 && column <= lastColumn()) {
        K3ListViewItem::setText(column, QString());
        return;
    }

    K3ListViewItem::setText(column, text);
    playlist()->slotWeightDirty(column);
}

void PlaylistItem::setPlaying(bool playing, bool master)
{
    m_playingItems.removeAll(this);

    if(playing) {
        if(master)
            m_playingItems.prepend(this);
        else
            m_playingItems.append(this);
    }
    else {

        // This is a tricky little recursion, but it
        // in fact does clear the list.

        if(!m_playingItems.isEmpty())
            m_playingItems.front()->setPlaying(false);
    }

    listView()->triggerUpdate();
}

void PlaylistItem::setSelected(bool selected)
{
    playlist()->markItemSelected(this, selected);
    K3ListViewItem::setSelected(selected);
}

void PlaylistItem::guessTagInfo(TagGuesser::Type type)
{
    switch(type) {
    case TagGuesser::FileName:
    {
        TagGuesser guesser(d->fileHandle.absFilePath());
        Tag *tag = TagTransactionManager::duplicateTag(d->fileHandle.tag());

        if(!guesser.title().isNull())
            tag->setTitle(guesser.title());
        if(!guesser.artist().isNull())
            tag->setArtist(guesser.artist());
        if(!guesser.album().isNull())
            tag->setAlbum(guesser.album());
        if(!guesser.track().isNull())
            tag->setTrack(guesser.track().toInt());
        if(!guesser.comment().isNull())
            tag->setComment(guesser.comment());

        TagTransactionManager::instance()->changeTagOnItem(this, tag);
        break;
    }
    case TagGuesser::MusicBrainz:
        startMusicBrainzQuery(d->fileHandle);
        break;
    }
}

Playlist *PlaylistItem::playlist() const
{
    return static_cast<Playlist *>(listView());
}

QVector<int> PlaylistItem::cachedWidths() const
{
    return d->cachedWidths;
}

void PlaylistItem::refresh()
{
    m_collectionItem->refresh();
}

void PlaylistItem::refreshFromDisk()
{
    d->fileHandle.refresh();
    refresh();
}

void PlaylistItem::clear()
{
    playlist()->clearItem(this);
}

////////////////////////////////////////////////////////////////////////////////
// PlaylistItem protected methods
////////////////////////////////////////////////////////////////////////////////

PlaylistItem::PlaylistItem(CollectionListItem *item, Playlist *parent) :
    K3ListViewItem(parent),
    d(0),
    m_watched(0)
{
    setup(item);
}

PlaylistItem::PlaylistItem(CollectionListItem *item, Playlist *parent, Q3ListViewItem *after) :
    K3ListViewItem(parent, after),
    d(0),
    m_watched(0)
{
    setup(item);
}


// This constructor should only be used by the CollectionList subclass.

PlaylistItem::PlaylistItem(CollectionList *parent) :
    K3ListViewItem(parent),
    m_watched(0)
{
    d = new Data;
    m_collectionItem = static_cast<CollectionListItem *>(this);
    setDragEnabled(true);
}

void PlaylistItem::paintCell(QPainter *p, const QColorGroup &cg, int column, int width, int align)
{
    if(!m_playingItems.contains(this))
        return K3ListViewItem::paintCell(p, cg, column, width, align);

    QPalette colorGroup = cg;

    QColor base = colorGroup.color( QPalette::Base );
    QColor selection = colorGroup.color( QPalette::Highlight );

    int r = (base.red() + selection.red()) / 2;
    int b = (base.blue() + selection.blue()) / 2;
    int g = (base.green() + selection.green()) / 2;

    QColor c(r, g, b);

    colorGroup.setColor(QPalette::Base, c);
    Q3ListViewItem::paintCell(p, colorGroup, column, width, align);
}

int PlaylistItem::compare(Q3ListViewItem *item, int column, bool ascending) const
{
    // reimplemented from QListViewItem

    if(!item)
        return 0;

    PlaylistItem *playlistItem = static_cast<PlaylistItem *>(item);

    // The following statments first check to see if you can sort based on the
    // specified column.  If the values for the two PlaylistItems are the same
    // in that column it then tries to sort based on columns 1, 2, 3 and 0,
    // (artist, album, track number, track name) in that order.

    int c = compare(this, playlistItem, column, ascending);

    if(c != 0)
        return c;
    else {
        // Loop through the columns doing comparisons until something is differnt.
        // If all else is the same, compare the track name.

        int last = playlist()->isColumnVisible(AlbumColumn) ? TrackNumberColumn : ArtistColumn;

        for(int i = ArtistColumn; i <= last; i++) {
            if(playlist()->isColumnVisible(i)) {
                c = compare(this, playlistItem, i, ascending);
                if(c != 0)
                    return c;
            }
        }
        return compare(this, playlistItem, TrackColumn, ascending);
    }
}

/* compare two items at specific column position. Always use String compare
 * for extra columns.
 */
int PlaylistItem::compare(const PlaylistItem *firstItem, const PlaylistItem *secondItem, int column, bool) const
{
    if(column < 0 ||
       !firstItem->d ||
       !secondItem->d) {
        return 0;
    }

    // is compare on extra column?
    if(column > lastColumn()) {
        QString first = firstItem->text(column).toLower();
        QString second = secondItem->text(column).toLower();
        return first.localeAwareCompare(second);
    }

    const FileHandle& fileHandle1 = firstItem->d->fileHandle;
    const FileHandle& fileHandle2 = secondItem->d->fileHandle;
    const Tag *tag1 = fileHandle1.tag();
    const Tag *tag2 = fileHandle2.tag();

    switch(column) {
    case TrackNumberColumn:
        if(tag1->track() > tag2->track())
            return 1;
        else if(tag1->track() < tag2->track())
            return -1;
        else
            return 0;
        break;
    case LengthColumn:
        if(tag1->seconds() > tag2->seconds())
            return 1;
        else if(tag1->seconds() < tag2->seconds())
            return -1;
        else
            return 0;
        break;
    case BitrateColumn:
        if(tag1->bitrate() > tag2->bitrate())
            return 1;
        else if(tag1->bitrate() < tag2->bitrate())
            return -1;
        else
            return 0;
        break;
    case CoverColumn: {
        coverKey key1 = fileHandle1.coverInfo()->coverId();
        coverKey key2 = fileHandle2.coverInfo()->coverId();
        if(key1 == key2)
            return 0;
        else if (key1 != CoverManager::NoMatch)
            return -1;
        else
            return 1;
        }
        break;
    default:
        return QString::localeAwareCompare(firstItem->d->metadata[column],
                                           secondItem->d->metadata[column]);
    }
}

bool PlaylistItem::isValid() const
{
    return bool(d->fileHandle.tag());
}

void PlaylistItem::setTrackId(quint32 id)
{
    m_trackId = id;
}

////////////////////////////////////////////////////////////////////////////////
// PlaylistItem private methods
////////////////////////////////////////////////////////////////////////////////

void PlaylistItem::setup(CollectionListItem *item)
{
    if(!item || !item->d) {
        kError() << "internal error: item or item->d is null";
        return;
    }
    m_collectionItem = item;

    d = item->d;
    item->addChildItem(this);
    setDragEnabled(true);
}

////////////////////////////////////////////////////////////////////////////////
// PlaylistItem::Pointer implementation
////////////////////////////////////////////////////////////////////////////////

QMap<PlaylistItem *, QList<PlaylistItem::Pointer *> > PlaylistItem::Pointer::m_map; // static

PlaylistItem::Pointer::Pointer(PlaylistItem *item) :
    m_item(item)
{
    if(!m_item)
        return;

    m_item->m_watched = true;
    m_map[m_item].append(this);
}

PlaylistItem::Pointer::Pointer(const Pointer &p) :
    m_item(p.m_item)
{
    m_map[m_item].append(this);
}

PlaylistItem::Pointer::~Pointer()
{
    if(!m_item)
        return;

    m_map[m_item].removeAll(this);
    if(m_map[m_item].isEmpty()) {
        m_map.remove(m_item);
        m_item->m_watched = false;
    }
}

PlaylistItem::Pointer &PlaylistItem::Pointer::operator=(PlaylistItem *item)
{
    if(item == m_item)
        return *this;

    if(m_item) {
        m_map[m_item].removeAll(this);
        if(m_map[m_item].isEmpty()) {
            m_map.remove(m_item);
            m_item->m_watched = false;
        }
    }

    if(item) {
        m_map[item].append(this);
        item->m_watched = true;
    }

    m_item = item;

    return *this;
}

void PlaylistItem::Pointer::clear(PlaylistItem *item) // static
{
    if(!item)
        return;

    QList<Pointer *> l = m_map[item];
    foreach(Pointer *pointer, l)
        pointer->m_item = 0;
    m_map.remove(item);
    item->m_watched = false;
}

// vim: set et sw=4 tw=0 sta:
