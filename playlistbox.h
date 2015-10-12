/**
 * Copyright (C) 2002-2004 Scott Wheeler <wheeler@kde.org>
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

#ifndef PLAYLISTBOX_H
#define PLAYLISTBOX_H

#include "playlistcollection.h"
#include "playlistinterface.h"

#include <k3listview.h>

#include <QHash>

class Playlist;
class PlaylistItem;
class ViewMode;

class KMenu;

template<class T>
class QList;

typedef QList<Playlist *> PlaylistList;

/**
 * This is the play list selection box that is by default on the left side of
 * JuK's main widget (PlaylistSplitter).
 */

class PlaylistBox : public K3ListView
{
    Q_OBJECT

public:
    class Item;
    typedef QList<Item *> ItemList;

    friend class Item;

    PlaylistBox(PlayerManager *player, QWidget *parent, QStackedWidget *playlistStack);
    virtual ~PlaylistBox();

    virtual void raise2(Playlist *playlist);
    virtual void duplicate();
    virtual void remove();

    // try to select playlist from previous app run
    virtual void restorePrevSelection();

    /**
     * @return  a list with all the Playlist objects managed by this playlistbox.
     */
    QList<Playlist*> getAllPlaylists() const;

    /**
     * For view modes that have dynamic playlists, this freezes them from
     * removing playlists.
     */
    virtual void setDynamicListsFrozen(bool frozen);

    Item *dropItem() const { return m_dropItem; }

    void setupPlaylist3(Playlist *playlist, const QString &iconName, Item *parentItem = 0);

    /**
     * Write modified playlists to disk.
     *
     * @param bDialogOK set to true if the call is permitted to show a
     * dialog, set to false if save must not block the UI (e.g. logging out).
     */
    void savePlaylistsToDisk(bool bDialogOK);

public slots:
    virtual void paste();
    virtual void clear();
    virtual void selectAll();       /* see using...selectAll; below */

    // Called after files loaded to pickup any new files that might be present
    // in managed directories.
    void slotScanFolders();
    void slotFreezePlaylists();
    void slotUnfreezePlaylists();
    void slotPlaylistDataChanged();
    void slotSetHistoryPlaylistEnabled(bool enable);

private:
    using K3ListView::selectAll; // Avoid warning about hiding this function.

public:
    virtual void removePlaylist(Playlist *playlist);
protected:
    virtual bool eventFilter(QObject *watched, QEvent *e);

signals:
    void signalPlaylistDestroyed(Playlist *);
    void startupComplete(); ///< Emitted after playlists are loaded.
    void startFilePlayback(const FileHandle &file);

private:
    void readConfig();
    void saveConfig();

    virtual void decode(const QMimeData *s, Item *item);
    virtual void contentsDropEvent(QDropEvent *e);
    virtual void contentsDragMoveEvent(QDragMoveEvent *e);
    virtual void contentsDragLeaveEvent(QDragLeaveEvent *e);

    /**
     * @return the list of selected items, in top-to-bottom sequence.
     */
    QList<Item*> getQ3SelectedItems() const;

    /**
     * @return the list of selected items, in the order that user ctrl-clicked
     * on them. Note that name 'selectedItems' is already in use for something
     * different.
     */
    QList<Item*> selectedBoxItems() const;

    void setSingleItem(Q3ListViewItem *item);

    /**
     * Count this widget's Playlist items. The tricky case is the
     * CollectionList in tree mode, where the top 3 levels need to be
     * considered.
     * @return  the number of Playlists found
     */
    int countPlaylistInView();

    void setupItem(Item *item);
    void setupUpcomingPlaylist();
    int viewModeIndex() const { return m_viewModeIndex; }
    ViewMode *viewMode() const { return m_viewModes[m_viewModeIndex]; }
    void updateLocalSelectionList();

private slots:
    /**
     * Catches QListBox::currentChanged(QListBoxItem *), does a cast and then re-emits
     * the signal as currentChanged(Item *).
     */
    void slotSelectionChanged();
    void slotDoubleClicked(Q3ListViewItem *);
    void slotUpdateMenus();
    void slotShowContextMenu(Q3ListViewItem *, const QPoint &point, int);
    void slotSetViewMode(int index);
    void slotSavePlaylistsToCache();
    void slotShowDropTarget();

    void slotPlaylistItemsDropped(Playlist *p);

    void slotAddItem(const QString &tag, unsigned column);
    void slotRemoveItem(const QString &tag, unsigned column);

    // Used to load the playlists after GUI setup.
    void slotLoadCachedPlaylists();

private:
    KMenu *m_contextMenu;
    QHash<Playlist *, Item*> m_playlistDict;
    int m_viewModeIndex;
    QList<ViewMode *> m_viewModes;
    KAction *m_k3bAction;
    Item *m_dropItem;
    QTimer *m_showTimer;
    QTimer *m_savePlaylistTimer;
    /** a user-ordered list of selected PlaylistBox Items */
    QList<Item*> m_selectedList;
};

class PlaylistBox::Item : public QObject, public K3ListViewItem /*, public PlaylistObserver */
{
    friend class PlaylistBox;
    friend class ViewMode;
    friend class CompactViewMode;
    friend class TreeViewMode;

    Q_OBJECT

    // moc won't let me create private QObject subclasses and Qt won't let me
    // make the destructor protected, so here's the closest hack that will
    // compile.

public:
    virtual ~Item();

protected:
    Item(PlaylistBox *listBox, const QString &icon, const QString &text, Playlist *l = 0);
    Item(Item *parent, const QString &icon, const QString &text, Playlist *l = 0);

    Playlist *playlist() const { return m_playlist; }
    PlaylistBox *listView() const { return static_cast<PlaylistBox *>(K3ListViewItem::listView()); }
    QString iconName() const { return m_iconName; }
    QString text() const { return m_text; }
    void setSortedFirst(bool first = true) { m_sortedFirst = first; }

    virtual int compare(Q3ListViewItem *i, int col, bool) const;
    virtual void paintCell(QPainter *p, const QColorGroup &colorGroup, int column, int width, int align);
    virtual void paintFocus(QPainter *, const QColorGroup &, const QRect &) {}
    virtual void setText(int column, const QString &text);

    virtual QString text(int column) const { return K3ListViewItem::text(column); }

    virtual void setup();

    static Item *collectionItem() { return m_collectionItem; }
    static void setCollectionItem(Item *item) { m_collectionItem = item; }

protected slots:
    void slotSetName(const QString &name);

private:
    // setup() was already taken.
    void init();

    Playlist *m_playlist;
    QString m_text;
    QString m_iconName;
    bool m_sortedFirst;
    static Item *m_collectionItem;
};

#endif

// vim: set et sw=4 tw=0 sta:
