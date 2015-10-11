/**
 * Copyright (C) 2004 Scott Wheeler <wheeler@kde.org>
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

#ifndef PLAYLISTINTERFACE_H
#define PLAYLISTINTERFACE_H

#include <QList>

class FileHandle;
class PlaylistObserver;

/**
 * An interface implemented by PlaylistInterface to make it possible to watch
 * for changes in the PlaylistInterface.  This is a semi-standard observer
 * pattern from i.e. Design Patterns.
 */

class Watched
{
public:
    void addObserver(PlaylistObserver *observer);
    void removeObserver(PlaylistObserver *observer);

    /**
     * Call this to remove all objects observing this class unconditionally (for example, when
     * you're being destructed).
     */
    void clearObservers();

    /**
     * This is triggered when the currently playing item has been changed.
     */
    virtual void currentChanged();

    /**
     * This is triggered when the data in the playlist -- i.e. the tag content
     * changes.
     */
    virtual void dataChanged();

protected:
    virtual ~Watched();

private:
    QList<PlaylistObserver *> m_observers;
};

/**
 * This is a simple interface that should be used by things that implement a
 * playlist-like API.
 *
 * As implemented in this app, the model assumes a global playlist,
 * which may or may not correspond to any specific NormalPlaylist. Where the
 * comments below refer to "the playlist", it means this global playlist.
 */
class PlaylistInterface : public Watched
{
public:
    /** The text label for the playlist */
    virtual QString name() const = 0;

    /**
     * The FileHandle for the current track in the playlist, or FileHandle::null()
     * if there is no such track.
     */
    virtual FileHandle currentFile() const = 0;

    /**
     * The total run time of the playlist, in seconds.
     */
    virtual int time() const = 0;

    /**
     * @return the total number of tracks in the playlist, including both
     *         hidden and non-hidden tracks.
     */
    virtual int count() const = 0;

    /**
     * Command to move the track iterator to the next track in the playlist. It
     * does not start an actual Player.
     */
    virtual void playNext() = 0;

    /**
     * Command to move the track iterator to the previously played track.
     * This is 'best effort' since the amount of history to keep is not defined
     * by this interface.
     */
    virtual void playPrevious() = 0;

    /**
     * Command to clear the track iterator; the next attempt to retrieve
     * currentFile() will return FileHandle::null(). This method does not stop
     * the actual Player.
     */
    virtual void stop() = 0;

    /**
     * Determine if the playlist is active. The playlist is active from the
     * first playNext() call until stop(); a paused playlist is still reported
     * as "active".
     * Note: this is independent of the actual PlayerManager state.
     *
     * @return true if the playlist is active.
     */
    virtual bool playing() const = 0;
};

class PlaylistObserver
{
public:
    virtual ~PlaylistObserver();

    /**
     * This method must be implemented in concrete implementations; it should
     * define what action should be taken in the observer when the currently
     * playing item changes.
     */
    virtual void updateCurrent() = 0;

    /**
     * This method must be implemented in concrete implementations; it should
     * define what action should be taken when the data of the PlaylistItems in
     * the playlist changes.
     */
    virtual void updateData() = 0;

    void clearWatched() { m_playlist = 0; }

protected:
    PlaylistObserver(PlaylistInterface *playlist);
    const PlaylistInterface *playlist() const;

private:
    PlaylistInterface *m_playlist;
};

#endif

// vim: set et sw=4 tw=0 sta:
