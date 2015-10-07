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

/**
 * This class represents a run-of-the-mill m3u playlist.
 */
class NormalPlaylist : public Playlist
{
    Q_OBJECT
public:
    NormalPlaylist(PlaylistCollection *collection, const QString& name = QString());

    NormalPlaylist(PlaylistCollection *collection, const PlaylistItemList &items,
             const QString &name);

    NormalPlaylist(PlaylistCollection *collection, const QFileInfo& playlistFile);

    NormalPlaylist(PlaylistCollection *collection, bool delaySetup);

    virtual ~NormalPlaylist() { };

    virtual int getType() { return Playlist::Type::Normal; }

    void read(QDataStream &s);

    virtual bool getPolicy(Policy p);
};

QDataStream &operator<<(QDataStream &s, const NormalPlaylist &p);

QDataStream &operator>>(QDataStream &s, NormalPlaylist &p);

#endif

// vim: set et sw=4 tw=0 sta:
