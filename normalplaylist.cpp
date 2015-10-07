/**
 * Copyright (C) 2002-2004 Scott Wheeler <wheeler@kde.org>
 * Copyright (C) 2008 Michael Pyne <mpyne@kde.org>
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

#include <kdebug.h>

//#include "juk-exception.h"  // BICStreamException
#include "normalplaylist.h"

NormalPlaylist::NormalPlaylist(PlaylistCollection *collection, const QString& name) :
    Playlist(collection, name)
{
}

NormalPlaylist::NormalPlaylist(PlaylistCollection *collection, 
        const PlaylistItemList &items,
        const QString &name) :
    Playlist(collection, items, name)
{
}

NormalPlaylist::NormalPlaylist(PlaylistCollection *collection, 
        const QFileInfo& playlistFile) :
    Playlist(collection, playlistFile)
{
}

NormalPlaylist::NormalPlaylist(PlaylistCollection *collection,
        bool delaySetup) :
    Playlist(collection, delaySetup)
{
}

bool NormalPlaylist::getPolicy(Playlist::Policy p) {
    switch(p) {
    case PolicyCanModifyContent: return true;
    case PolicyCanRename:        return true;
    case PolicyCanDelete:        return true;
    case PolicyCanReload:        return true;
    case PolicyPromptToSave:     return true;
    default:                     return false;
    }
}

#if 1
void NormalPlaylist::read(QDataStream &s) {
    Playlist::read(s);
}
#else
void NormalPlaylist::read(QDataStream &s) {
    s >> m_playlistName
      >> m_fileName;

    kDebug() << m_fileName;

    // m_fileName is probably empty.
    if(m_playlistName.isEmpty())
        throw BICStreamException();

    // Do not sort. Add the files in the order they were saved.
    setSorting(columns() + 1);

    QStringList files;
    s >> files;

    Q3ListViewItem *after = 0;

    m_blockDataChanged = true;

    foreach(const QString &file, files) {
        if(file.isEmpty())
            throw BICStreamException();

        after = createItem(FileHandle(file), after, false);
    }

    m_blockDataChanged = false;

    dataChanged();
    m_collection->setupPlaylist(this, "audio-midi");
}
#endif

QDataStream& operator<<(QDataStream &s, const NormalPlaylist &p) {
    qDebug() << "Playlist::operator<<: Write " << p.name();
    s << p.name();
    s << p.fileName();
    s << p.files();

    return s;
}

QDataStream& operator>>(QDataStream &s, NormalPlaylist &p)
{
    p.read(s);
    return s;
}

