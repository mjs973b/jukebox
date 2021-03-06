/**
 * Copyright (C) 2002-2004 Scott Wheeler <wheeler@kde.org>
 * Copyright (C) 2007 Michael Pyne <mpyne@kde.org>
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

#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <k3listview.h>
#include <kglobalsettings.h>
#include <kdebug.h>

#include <QVector>
#include <QDateTime>
#include <QEvent>
#include <QList>

#include "covermanager.h"
#include "stringhash.h"
#include "playlistsearch.h"
#include "tagguesser.h"
#include "playlistinterface.h"
#include "filehandle.h"

class KMenu;
class KActionMenu;

class QFileInfo;
class QFocusEvent;
class QMimeData;
class QDrag;
class QAction;

class WebImageFetcher;
class PlaylistItem;
class PlaylistCollection;
class PlaylistToolTip;
class CollectionListItem;

typedef QList<PlaylistItem *> PlaylistItemList;

class Playlist : public K3ListView /*, public PlaylistInterface */
{
    Q_OBJECT

public:

    explicit Playlist(PlaylistCollection *collection, const QString &name = QString(),
             const QString &iconName = "audio-midi");
    Playlist(PlaylistCollection *collection, const PlaylistItemList &items,
             const QString &name = QString(), const QString &iconName = "audio-midi");
    Playlist(PlaylistCollection *collection, const QFileInfo &playlistFile,
             const QString &iconName = "audio-midi");

    /**
     * This constructor should generally only be used either by the cache
     * restoration methods or by subclasses that want to handle calls to
     * PlaylistCollection::setupPlaylist() differently.
     *
     * @param extraColumns is used to allocate columns for subclasses that
     * need them. Extra columns are assumed to start at lastColumn()+1.
     */
    Playlist(PlaylistCollection *collection, bool delaySetup, int extraColumns = 0);

    virtual ~Playlist();

    /* values 0-4 are written in cache file, so can't be changed without
     * breaking backward compatiblity.
     */
    class Type {
    public:
        const static int Normal   = 0;
        const static int Search   = 1;          // user-defined
        const static int History  = 2;
        const static int Upcoming = 3;
        const static int Folder   = 4;
        const static int Dynamic  = 5;
        const static int CollectionList = 6;
        const static int SystemSearch = 7;      // system-defined
    };

    /** @return whether current column-resize mode is manual or automatic */
    static bool manualResize();

    /**
     * An quick and efficent method to determine the most specific 
     * type of a Playlist object.
     *
     * @return a unique integer for the type of Playlist object.
     */
    virtual int getType() const = 0;

    /**
     * @return  the label for this playlist.
     * @see PlaylistInterface
     */
    virtual QString name() const;

    /**
     * Get the current file info for this playlist.
     * @return  a FileHandle for the current track
     * @see PlaylistInterface
     */
    virtual FileHandle currentFile() const;

    /**
     * @return  the number of tracks in this playlist, including both hidden
     *          and non-hidden items.
     * @see PlaylistInterface
     */
    virtual int count() const;

    /**
     * @return the sum of all track lengths, in seconds. 
     * @see PlaylistInterface
     */
    virtual int time() const;

    /**
     * Step iterator forward by one.
     * @see PlaylistInterface
     */
    virtual void playNext();

    /**
     * Step iterator back by one.
     * @see PlaylistInterface
     */
    virtual void playPrevious();

    /**
     * Clear the iterator. Does not directly stop the Player.
     * @see PlaylistInterface
     */
    virtual void stop();

    /**
     * Set iterator to the first item of this playlist.
     */
    void playFirst();

    /**
     * Move iterator to the next album in the playlist.  Only useful when in
     * album random play mode.
     */
    void playNextAlbum();

    /**
     * Saves the file to the currently set file name.  If there is no filename
     * currently set, the default behavior is to prompt the user for a file
     * name.
     */
    virtual void save();

    /**
     * Standard "save as".  Prompts the user for a location where to save the
     * playlist to, and updates fileName() for a normal playlist.
     */
    virtual void saveAs();

    /**
     * Write a .m3u to user-specified location, but do not update fileName().
     */
    virtual bool exportFile();

    /**
     * Removes \a item from the Playlist, but not from the disk.
     *
     * Since the GUI updates after an item is cleared, you should use clearItems() if you have
     * a list of items to remove, as that will remove the whole batch before updating
     * other components/GUI to the change.
     */
    virtual void clearItem(PlaylistItem *item);

    /**
     * Remove \a items from the playlist and emit a signal indicating
     * that the number of items in the list has changed.
     */
    virtual void clearItems(const PlaylistItemList &items);

    /**
     * Accessor function to return a pointer to the currently playing file.
     *
     * @return 0 if no file is playing, otherwise a pointer to the PlaylistItem
     *     of the track that is currently playing.
     */
    static PlaylistItem *playingItem();

    /**
     * All of the (media) files in the list.
     */
    QStringList files() const;

    /**
     * Returns a list of all of the items in the playlist.
     */
    virtual PlaylistItemList items();

    /**
     * Returns a list of all of the \e visible items in the playlist.
     */
    PlaylistItemList visibleItems();

    /**
     * Returns a list of the currently selected items.
     */
    PlaylistItemList selectedItems();

    /**
     * Returns properly casted first child item in list.
     */
    PlaylistItem *firstChild() const;

    /**
     * Allow duplicate files in the playlist.
     */
    void setAllowDuplicates(bool allow) { m_allowDuplicates = allow; }

    /**
     * This is being used as a mini-factory of sorts to make the construction
     * of PlaylistItems virtual.  In this case it allows for the creation of
     * both PlaylistItems and CollectionListItems.
     */
    virtual PlaylistItem *createItem(const FileHandle &file,
                                     Q3ListViewItem *after = 0,
                                     bool emitChanged = true);

    /**
     * This is implemented as a template method to allow subclasses to
     * instantiate their PlaylistItem subclasses using the same method.
     */
    template <class ItemType>
    ItemType *createItem(const FileHandle &file,
                         Q3ListViewItem *after = 0,
                         bool emitChanged = true);

    virtual void createItems(const PlaylistItemList &siblings, PlaylistItem *after = 0);

    /**
     * This handles adding files of various types -- music, playlist or directory
     * files.  Music files that are found will be added to this playlist.  New
     * playlist files that are found will result in new playlists being created.
     *
     * Note that this should not be used in the case of adding *only* playlist
     * items since it has the overhead of checking to see if the file is a playlist
     * or directory first.
     */
    virtual void addFiles(const QStringList &files, PlaylistItem *after = 0);

    /**
     * Returns the file name associated with this playlist (an m3u file) or
     * an empty QString if no such file exists.
     */
    QString fileName() const { return m_fileName; }

    /**
     * Sets the file name to be associated with this playlist; this file should
     * have the "m3u" extension.
     */
    void setFileName(const QString &n) { m_fileName = n; }

    /**
     * Hides column \a c.  If \a updateeMenu is true then menu check status
     * will be udated.
     */
    void hideColumn(int c, bool updateMenu = false);

    /**
     * Shows column \a c.  If \a updateMenu is true then menu check status
     * will be udated.
     */
    void showColumn(int c, bool updateMenu = false);
    bool isColumnVisible(int c) const;

    /**
     * This sets a name for the playlist that is \e different from the file name.
     */
    void setName(const QString &n);

    /**
     * Returns the KActionMenu that allows this to be embedded in menus outside
     * of the playlist.
     */
    //KActionMenu *columnVisibleAction() const;

    /**
     * Set item to be the playing item.  If \a item is null then this will clear
     * the playing indicator.
     */
    static void setPlaying(PlaylistItem *item, bool addToHistory = true);

    /**
     * @return true if playing track belongs to this playlist.
     */
    bool hasPlayingItem() const;

    /**
     * This forces an update of the left most visible column, but does not save
     * the settings for this.
     */
    void updateLeftColumn();

    /**
     * Returns the leftmost visible column of the listview.
     */
    int leftColumn() const { return m_leftColumn; }

    /**
     * Sets the items in the list to be either visible based on the value of
     * visible.  This is useful for search operations and such.
     */
    static void setItemsVisible(const PlaylistItemList &items, bool visible = true);

    /**
     * Returns the search associated with this list, or an empty search if one
     * has not yet been set.
     */
    PlaylistSearch search() const { return m_search; }

    /**
     * Set the search associtated with this playlist.
     */
    void setSearch(const PlaylistSearch &s);

    /**
     * If the search is disabled then all items will be shown, not just those that
     * match the current search.
     */
    void setSearchEnabled(bool searchEnabled);

    /**
     * Marks \a item as either selected or deselected based.
     */
    void markItemSelected(PlaylistItem *item, bool selected);

    /**
     * The Policy enum describes immutable characteristics associated with
     * a Playlist:
     *
     * PolicyCanModifyContent
     *
     * Report whether the content of this playlist is permitted to be 
     * modified i.e. whether .mp3 files can be added, deleted or renamed 
     * for this .m3u playlist BY USER ACTION.
     * This is class policy, not mutable state.
     *
     * PolicyCanRename
     *
     * Report whether the label on this playlist is permitted to be 
     * changed BY USER ACTION.
     * This is class policy, not mutable state.
     *
     * PolicyCanDelete
     *
     * Report whether this playlist is permitted to be deleted (both in 
     * RAM and on disk) BY USER ACTION.
     * This is class policy, not mutable state.
     *
     * PolicyCanReload
     *
     * Returns true if it's ok to reread/regenerate this playlist
     * BY USER ACTION. This is class policy, not mutable state.
     *
     * PolicyPromptToSave
     *
     * Returns true if this playlist can be assigned an m3u filename
     * by the user and that file can be created/written to disk. Some
     * Playlist types do not allow this e.g. CollectionList.
     */

    enum Policy { PolicyCanModifyContent, PolicyCanRename, PolicyCanDelete,
        PolicyCanReload, PolicyPromptToSave };

    virtual bool getPolicy(Policy p) const = 0;

    /**
     * Determine if tracks can be added, deleted or moved within this playlist by
     * direct user action, including sorting. This flag is mutable. For more
     * fine-grained reasons, call getPolicy(PolicyCanModifyContent) and
     * isContentMutable(). This does not imply that the track metadata is
     * also read-only.
     *
     * @return true if track position modification is allowed
     */
    virtual bool isListReadOnly() const;

    /**
     * Check if critical info differs between RAM and source disk file.
     * If there's no disk file, it obviously doesn't match. This is useful
     * when deciding whether or not to cache this playlist.
     *
     * @return true if label and track list match the m3u disk file.
     */
    bool isMatchToDiskFile() const;

    /**
     * Determine if this playlist content can be modified by the app.
     * This is _state_ that can be changed programatically at any time.
     * This flag is completely distinct from canModifyContent(),
     * which applies only to USER ACTION.
     *
     * This is useful for lists imported from read-only .m3u files, to
     * avoid modifications which we can't write back to disk.
     *
     * @return true if track addition, track deletion, track file rename, 
     * and playlist object delete is allowed for this playlist.
     */
    bool isContentMutable() const;

    /**
     * Set the isContentMutable() flag.
     */
    void setContentMutable(bool b);

    /**
     * Set the isContentMutuable() state of this playlist based on the read-write 
     * state of m3u playlist file. Does nothing if no fileName() specified.
     */
    void checkForReadOnlyM3uFile();

    /**
     * Synchronizes the playing item in this playlist with the playing item
     * in \a sources.  If \a setMaster is true, this list will become the source
     * for determining the next item.
     */
    void synchronizePlayingItems(const PlaylistList &sources, bool setMaster);

    /**
     * Playlists have a common set of shared settings such as visible columns
     * that should be applied just before the playlist is shown.  Calling this
     * method applies those.
     */
    void applySharedSettings();

    void read(QDataStream &s);

    static void setShuttingDown() { m_shuttingDown = true; }

public slots:
    /**
     * Remove the currently selected items from the playlist and disk.
     */
    void slotRemoveSelectedItems() { removeFromDisk(selectedItems()); }

    /*
     * The edit slots are required to use the canonical names so that they are
     * detected by the application wide framework.
     */
    virtual void cut() { copy(); clear(); }

    /**
     * Puts a list of URLs pointing to the files in the current selection on the
     * clipboard.
     */
    virtual void copy();

    /**
     * Checks the clipboard for local URLs to be inserted into this playlist.
     */
    virtual void paste();

    /**
     * Removes the selected items from the list, but not the disk.
     *
     * @see clearItem()
     * @see clearItems()
     */
    virtual void clear();
    virtual void selectAll() { K3ListView::selectAll(true); }

    /**
     * Refreshes the tags of the selection from disk, or all of the files in the
     * list if there is no selection.
     */
    virtual void slotRefresh();

    void slotGuessTagInfo(TagGuesser::Type type);

    /**
     * Renames the selected items' files based on their tags contents.
     *
     * @see PlaylistItem::renameFile()
     */
    void slotRenameFile();

    /**
     * Sets the cover of the selected items, pass in true if you want to load from the local system,
     * false if you want to load from the internet.
     */
    void slotAddCover(bool fromLocal);

    /**
     * Shows a large image of the cover
     */
    void slotViewCover();

    /**
     * Removes covers from the selected items
     */
    void slotRemoveCover();

    /**
     * Shows the cover manager GUI dialog
     */
    void slotShowCoverManager();

    /**
     * Reload the playlist contents from the m3u file.
     */
    virtual void slotReload();

    /**
     * Tells the listview that the next time that it paints that the weighted
     * column widths must be recalculated.  If this is called without a column
     * all visible columns are marked as dirty.
     */
    void slotWeightDirty(int column = -1);

    void slotShowPlaying();

    void slotColumnResizeModeChanged();

    virtual void dataChanged();

public:
    /**
     * Force column visibility and width to the value in SharedSettings.
     */
    void updateColumnFixedWidth();

    /**
     * Force the state of the FileListChanged flag.
     */
    void setFileListChanged(bool b);

    /**
     * Determine if any modifications were made to the file names 
     * after this playlist was created or loaded from disk.
     */
    bool hasFileListChanged() const;

    /**
     * The actual file-writing routine.
     * @param bDialogOk  true if showing dialog and blocking UI is Ok, false
     * if not.
     * @return true on successful write, false on error.
     */
    bool saveFile(const QString& fileName, bool bDialogOk);

    /**
     * Directly set the last modified timestamp. Needed by Cache class.
     */
    void setFileListLastModified(const QDateTime& t);

    /**
     * Specify whether the user is permitted to sort this playlist by column.
     * Typically called from a derived Playlist constructor after all columns
     * have been added to the table. The default value is false.
     */
    void setColumnSortEnabled(bool b);

protected:
    /**
     * Remove \a items from the playlist and disk.  This will ignore items that
     * are not actually in the list.
     */
    void removeFromDisk(const PlaylistItemList &items);

    // the following are all reimplemented from base classes

    virtual bool eventFilter(QObject *watched, QEvent *e);
    virtual void keyPressEvent(QKeyEvent *e);
    virtual Q3DragObject *dragObject(QWidget *parent);
    virtual Q3DragObject *dragObject() { return dragObject(this); }
    virtual void decode(const QMimeData *s, PlaylistItem *item = 0);
    virtual void contentsDropEvent(QDropEvent *e);
    virtual void contentsMouseDoubleClickEvent(QMouseEvent *e);
    virtual void contentsDragEnterEvent(QDragEnterEvent *e);
    virtual void showEvent(QShowEvent *e);
    virtual bool acceptDrag(QDropEvent *e) const;
    virtual void viewportPaintEvent(QPaintEvent *pe);
    virtual void viewportResizeEvent(QResizeEvent *re);

    virtual void insertItem(Q3ListViewItem *item);
    virtual void takeItem(Q3ListViewItem *item);

    virtual bool hasItem(const QString &file) const { return m_members.contains(file); }

    virtual int addColumn(const QString &label, int width = -1);
    using K3ListView::addColumn;

    /**
     * Do some finial initialization of created items.  Notably ensure that they
     * are shown or hidden based on the contents of the current PlaylistSearch.
     *
     * This is called by the PlaylistItem constructor.
     */
    void setupItem(PlaylistItem *item);

    /**
     * Forwards the call to the parent to enable or disable automatic deletion
     * of tree view playlists.  Used by CollectionListItem.
     */
    void setDynamicListsFrozen(bool frozen);

    template <class ItemType, class SiblingType>
    ItemType *createItem(SiblingType *sibling, ItemType *after = 0);

    /**
     * As a template this allows us to use the same code to initialize the items
     * in subclasses. ItemType should be a PlaylistItem subclass.
     */
    template <class ItemType, class SiblingType>
    void createItems(const QList<SiblingType *> &siblings, ItemType *after = 0);

protected slots:
    void slotPopulateBackMenu() const;
    void slotPlayFromBackMenu(QAction *) const;

signals:

    /**
     * This is connected to the PlaylistBox::Item to let it know when the
     * playlist's name has changed.
     */
    void signalNameChanged(const QString &name);

    /**
     * This signal is emitted just before a playlist item is removed from the
     * list allowing for any cleanup that needs to happen.  Typically this
     * is used to remove the item from the history and safeguard against
     * dangling pointers.
     */
    void signalAboutToRemove(PlaylistItem *item);

    void signalEnableDirWatch(bool enable);

    void signalPlaylistItemsDropped(Playlist *p);

private:
    void setup();

    /**
     * This function is called to let the user know that JuK has automatically enabled
     * manual column width adjust mode.
     */
    void notifyUserColumnWidthModeChanged();

    /**
     * Read an m3u file into list, converting to canonical file name.
     */
    bool readFile(const QFileInfo& src, QList<QString>& rows) const;

    /**
     * Load the playlist from a list of file names.
     */
    void loadTableFromList(const QList<QString> &list);

    /**
     * Load the playlist from a file.
     * @param src path to m3u file.
     */
    void loadTableFromFile(const QFileInfo &src);

    /**
     * Writes \a text to \a item in \a column.  This is used by the inline tag
     * editor.  Returns false if the tag update failed.
     */
    bool editTag(PlaylistItem *item, const QString &text, int column);

    /**
     * Returns the index of the left most visible column in the playlist.
     *
     * \see isColumnVisible()
     */
    int leftMostVisibleColumn() const;

    /**
     * This method is used internally to provide the backend to the other item
     * lists.
     *
     * \see items()
     * \see visibleItems()
     * \see selectedItems()
     */
    PlaylistItemList items(Q3ListViewItemIterator::IteratorFlag flags);

    /**
     * Build the column "weights" for the weighted width mode.
     */
    void calculateColumnWeights();

    void addFile(const QString &file, FileHandleList &files, bool importPlaylists,
                 PlaylistItem **after);
    void addFileHelper(FileHandleList &files, PlaylistItem **after,
                       bool ignoreTimer = false);

    void importRecentPlaylistFile(const QFileInfo& fileInfo);

    void redisplaySearch() { setSearch(m_search); }

    /**
     * Sets the cover for items to the cover identified by id.
     */
    void refreshAlbums(const PlaylistItemList &items, coverKey id = CoverManager::NoMatch);

    void refreshAlbum(const QString &artist, const QString &album);

    void updatePlaying() const;

    /**
     * This function should be called when item is deleted to ensure that any
     * internal bookkeeping is performed.  It is automatically called by
     * PlaylistItem::~PlaylistItem and by clearItem() and clearItems().
     */
    void updateDeletedItem(PlaylistItem *item);

    /**
     * Used as a helper to implement template<> createItem().  This grabs the
     * CollectionListItem for file if it exists, otherwise it creates a new one and
     * returns that.  If 0 is returned then some kind of error occurred, such as file not
     * found and probably nothing should be done with the FileHandle you have.
     */
    CollectionListItem *collectionListItem(const FileHandle &file);

    /**
     * This class is used internally to store settings that are shared by all
     * of the playlists, such as column order.  It is implemented as a singleton.
     */
    class SharedSettings;

    using K3ListView::selectAll; // Avoid warning about hiding this function.

private slots:

    /**
     * Handle the necessary tasks needed to create and setup the playlist that
     * don't need to happen in the ctor, such as setting up the columns,
     * initializing the RMB menu, and setting up signal/slot connections.
     *
     * Used to be a subclass of K3ListView::polish() but the timing of the
     * call is not consistent and therefore lead to crashes.
     */
    void slotInitialize();

    void slotUpdateColumnWidths();

    void slotAddToUpcoming();

    /**
     * Update menu items that are affected by focus or selection changes.
     */
    void slotUpdateMenus();

    /**
     * Show the RMB menu.  Matches the signature for the signal
     * QListView::contextMenuRequested().
     */
    void slotShowRMBMenu(Q3ListViewItem *item, const QPoint &point, int column);

    /**
     * This slot is called when the inline tag editor has completed its editing
     * and starts the process of renaming the values.
     *
     * \see editTag()
     */
    void slotInlineEditDone(Q3ListViewItem *, const QString &, int column);

    /**
     * This starts the renaming process by displaying a line edit if the mouse is in
     * an appropriate position.
     */
    void slotRenameTag();

    /**
     * The image fetcher will update the cover asynchronously, this internal
     * slot is called when it happens.
     */
    void slotCoverChanged(int coverId);

    /**
     * Moves the column \a from to the position \a to.  This matches the signature
     * for the signal QHeader::indexChange().
     */
    void slotColumnOrderChanged(int, int from, int to);

public slots:
    /**
     * Toggles a columns visible status.  Useful for KActions.
     *
     * \see hideColumn()
     * \see showColumn()
     */
    void slotToggleColumnVisible(QAction *action);

private slots:
    /**
     * Prompts the user to create a new playlist with from the selected items.
     */
    void slotCreateGroup();

    /**
     * This slot is called when the user drags the slider in the listview header
     * to manually set the size of the column.
     */
    void slotColumnSizeChanged(int column, int oldSize, int newSize);

    /**
     * The slot is called when the completion mode for the line edit in the
     * inline tag editor is changed.  It saves the settings and through the
     * magic of the SharedSettings class will apply it to the other playlists as
     * well.
     */
    void slotInlineCompletionModeChanged(KGlobalSettings::Completion mode);

public slots:
    void slotPlayCurrent();

private:
    friend class PlaylistItem;

    PlaylistCollection *m_collection;

    StringHash m_members;

    WebImageFetcher *m_fetcher;

    int m_currentColumn;
    QAction *m_rmbEdit;
    int m_selectedCount;

    bool m_allowDuplicates;
    bool m_applySharedSettings;
    bool m_columnWidthModeChanged;

    QList<int> m_weightDirty;
    bool m_disableColumnWidthUpdates;

    mutable int m_time;
    mutable PlaylistItemList m_addTime;
    mutable PlaylistItemList m_subtractTime;

    /**
     * The average minimum widths of columns to be used in balancing calculations.
     */
    QVector<int> m_columnWeights;
    bool m_widthsDirty;

    static PlaylistItemList m_history;
    PlaylistSearch m_search;

    bool m_searchEnabled;

    PlaylistItem *m_lastSelected;

    /**
     * Used to store the text for inline editing before it is changed so that
     * we can know if something actually changed and as such if we need to save
     * the tag.
     */
    QString m_editText;

    /**
     * This is only defined if the playlist name is something other than the
     * file name.
     */
    QString m_playlistName;
    QString m_fileName;

    /* popup menu for track items */
    KMenu *m_rmbMenu;

    /* popup menu for table header */
    static KMenu *m_headerMenu;
    static KActionMenu *m_columnVisibleAction;

    PlaylistToolTip *m_toolTip;

    /**
     * This is used to indicate if the list of visible items has changed (via a
     * call to setVisibleItems()) while random play is playing.
     */
    static bool m_visibleChanged;
protected:
    static bool m_shuttingDown;
private:
    static int m_leftColumn;
    static QVector<PlaylistItem *> m_backMenuItems;

    /** dirty bit for the list of filenames. Used to determine if add/remove
     *  was done after m3u file was read from disk, or was newly created..
     */
    bool m_bFileListChanged;

    /**
     * The timestamp for when a track filename was last added, deleted or 
     * renamed in this playlist. If the playlist is read from an m3u file, 
     * this gets the lastModified() time of that file. If playlist is read 
     * from cache, it gets the lastModifed() time of the cache file.
     */
    QDateTime m_fileListLastModified;

    /** 
     * Flag to allow modification of track data. Does not protect the 
     * playlist's name.
     */
    bool m_bContentMutable;

    /** when true, do not issue dataChanged() calls */
    bool m_blockDataChanged;

};

typedef QList<Playlist *> PlaylistList;

bool processEvents();

class FocusUpEvent : public QEvent
{
public:
    FocusUpEvent() : QEvent(id) {}
    Type type() const { return id; }

    static const Type id = static_cast<Type>(QEvent::User + 1);
};

//QDataStream &operator<<(QDataStream &s, const Playlist &p);
//QDataStream &operator>>(QDataStream &s, Playlist &p);

// template method implementations

template <class ItemType>
ItemType *Playlist::createItem(const FileHandle &file, Q3ListViewItem *after,
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
ItemType *Playlist::createItem(SiblingType *sibling, ItemType *after)
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
void Playlist::createItems(const QList<SiblingType *> &siblings, ItemType *after)
{
    if(siblings.isEmpty())
        return;

    foreach(SiblingType *sibling, siblings)
        after = createItem(sibling, after);

    dataChanged();
    slotWeightDirty();
}

#endif

// vim: set et sw=4 tw=0 sta:
