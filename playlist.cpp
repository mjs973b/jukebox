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

#include "playlist.h"
#include "juk-exception.h"

#include <q3header.h>
#include <kconfig.h>
#include <kapplication.h>
#include <kmessagebox.h>
#include <k3urldrag.h>
#include <kiconloader.h>
#include <klineedit.h>
#include <k3popupmenu.h>
#include <klocale.h>
#include <kdebug.h>
#include <kfiledialog.h>
#include <kglobalsettings.h>
#include <kurl.h>
#include <kio/netaccess.h>
#include <kio/copyjob.h>
#include <kmenu.h>
#include <kactioncollection.h>
#include <kconfiggroup.h>
#include <ktoolbarpopupaction.h>
#include <kactionmenu.h>
#include <ktoggleaction.h>
#include <kselectaction.h>

#include <QCursor>
#include <QDir>
#include <QDirIterator>
#include <QToolTip>
#include <QFile>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QTimer>
#include <QClipboard>
#include <QTextStream>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QPixmap>
#include <QStackedWidget>
#include <id3v1genres.h>

#include <time.h>
#include <cmath>

#include "normalplaylist.h"
#include "playlistitem.h"
#include "playlistcollection.h"
#include "playlistsearch.h"
#include "mediafiles.h"
#include "collectionlist.h"
#include "filerenamer.h"
#include "actioncollection.h"
#include "tracksequencemanager.h"
#include "tag.h"
#include "k3bexporter.h"
#include "upcomingplaylist.h"
#include "deletedialog.h"
#include "webimagefetcher.h"
#include "coverinfo.h"
#include "coverdialog.h"
#include "tagtransactionmanager.h"
#include "cache.h"

/* ptr to the right-click menu for View|Show Columns and the header on the
 * table. This KMenu object is shared by all Playlist Widgets.
 */
KMenu *       Playlist::m_headerMenu;
KActionMenu * Playlist::m_columnVisibleAction;

/**
 * Used to give every track added in the program a unique identifier. See
 * PlaylistItem
 */
quint32 g_trackID = 0;

/* current column resize mode is manual or automatic */
bool Playlist::manualResize()
{
    return ActionCollection::action<KToggleAction>("resizeColumnsManually")->isChecked();
}

/**
 * A tooltip specialized to show full filenames over the file name column.
 */

#ifdef __GNUC__
#warning disabling the tooltip for now
#endif
#if 0
class PlaylistToolTip : public QToolTip
{
public:
    PlaylistToolTip(QWidget *parent, Playlist *playlist) :
        QToolTip(parent), m_playlist(playlist) {}

    virtual void maybeTip(const QPoint &p)
    {
        PlaylistItem *item = static_cast<PlaylistItem *>(m_playlist->itemAt(p));

        if(!item)
            return;

        QPoint contentsPosition = m_playlist->viewportToContents(p);

        int column = m_playlist->header()->sectionAt(contentsPosition.x());

        if(column == PlaylistItem::FileNameColumn ||
           item->cachedWidths()[column] > m_playlist->columnWidth(column) ||
           (column == PlaylistItem::CoverColumn &&
            item->file().coverInfo()->hasCover()))
        {
            QRect r = m_playlist->itemRect(item);
            int headerPosition = m_playlist->header()->sectionPos(column);
            r.setLeft(headerPosition);
            r.setRight(headerPosition + m_playlist->header()->sectionSize(column));

            if(column == PlaylistItem::FileNameColumn)
                tip(r, item->file().absFilePath());
            else if(column == PlaylistItem::CoverColumn) {
                Q3MimeSourceFactory *f = Q3MimeSourceFactory::defaultFactory();
                f->setImage("coverThumb",
                            QImage(item->file().coverInfo()->pixmap(CoverInfo::Thumbnail).convertToImage()));
                tip(r, "<center><img source=\"coverThumb\"/></center>");
            }
            else
                tip(r, item->text(column));
        }
    }

private:
    Playlist *m_playlist;
};
#endif

////////////////////////////////////////////////////////////////////////////////
// Playlist::SharedSettings definition
////////////////////////////////////////////////////////////////////////////////

bool Playlist::m_visibleChanged = false;
bool Playlist::m_shuttingDown = false;

/**
 * Shared settings between the playlists.
 */

class Playlist::SharedSettings
{
public:
    static SharedSettings *instance();
    /** specify number of columns in largest table */
    void growColumnCount(int numCol);
    /**
     * Sets the default column order to that of Playlist @param p.
     */
    void setColumnOrder(const Playlist *l);
    void toggleColumnVisible(int column);
    void setInlineCompletionMode(KGlobalSettings::Completion mode);

    bool isColumnVisible(int col) const;
    int columnFixedWidth(int col) const;
    void setColumnFixedWidth(int col, int newValue);

    /**
     * Apply the settings.
     */
    void apply(Playlist *l) const;

    /** force write of config settings, regardless of configDirty() status. */
    void writeConfig();

    /** @return true when config has changed */
    bool configDirty() const;

protected:
    SharedSettings();
    ~SharedSettings() {}

private:
    static SharedSettings *m_instance;
    QList<int> m_columnOrder;
    /** user-set width in pixels for each column */
    QVector<int> m_columnFixedWidth;
    QVector<bool> m_columnsVisible;
    KGlobalSettings::Completion m_inlineCompletion;
    bool m_configDirty;
};

Playlist::SharedSettings *Playlist::SharedSettings::m_instance = 0;

////////////////////////////////////////////////////////////////////////////////
// Playlist::SharedSettings public members
////////////////////////////////////////////////////////////////////////////////

Playlist::SharedSettings *Playlist::SharedSettings::instance()
{
    static SharedSettings settings;
    return &settings;
}

void Playlist::SharedSettings::growColumnCount(int numCol) {
    int oldlen = m_columnsVisible.size();
    if(oldlen < numCol) {
        m_columnsVisible.resize(numCol);
        for(int i = oldlen; i < numCol; i++) {
            m_columnsVisible[i] = true;
        }
    }
    oldlen = m_columnFixedWidth.size();
    if(oldlen < numCol) {
        m_columnFixedWidth.resize(numCol);
        for(int i = oldlen; i < numCol; i++) {
            m_columnFixedWidth[i] = 66;
        }
    }
}

void Playlist::SharedSettings::setColumnOrder(const Playlist *l)
{
    if(!l)
        return;

    m_columnOrder.clear();

    for(int i = 0; i < l->columns(); ++i)
        m_columnOrder.append(l->header()->mapToIndex(i));

    m_configDirty = true;
}

void Playlist::SharedSettings::toggleColumnVisible(int column)
{
    if(column < 0 || column >= m_columnsVisible.size()) {
        return;
    }

    m_columnsVisible[column] = !m_columnsVisible[column];

    m_configDirty = true;
}

void Playlist::SharedSettings::setInlineCompletionMode(KGlobalSettings::Completion mode)
{
    m_inlineCompletion = mode;
    m_configDirty = true;
}

/* return the official value for column visibility */
bool Playlist::SharedSettings::isColumnVisible(int col) const
{
    if(col < 0 || col >= m_columnsVisible.size()) {
        return false;
    }
    return m_columnsVisible[col];
}

/* return the official value for column width */
int Playlist::SharedSettings::columnFixedWidth(int col) const
{
    if(col < 0 || col >= m_columnFixedWidth.size()) {
        return 0;
    }
    return m_columnsVisible[col] ? m_columnFixedWidth[col] : 0 ;
}

/* assign the official value for fixed column width */
void Playlist::SharedSettings::setColumnFixedWidth(int col, int newValue) {
    if(col < 0 || col >= m_columnFixedWidth.size() || newValue < 1) {
        return;
    }
    m_columnFixedWidth[col] = newValue;
    m_configDirty = true;
}

/* called just before this playlist is about to become visible */
void Playlist::SharedSettings::apply(Playlist *l) const
{
    if(!l)
        return;

    int i = 0;
    foreach(int column, m_columnOrder)
        l->header()->moveSection(i++, column);

    // note: calls l->slotUpdateColumnWidths()
    l->slotColumnResizeModeChanged();

    l->updateLeftColumn();
    l->renameLineEdit()->setCompletionMode(m_inlineCompletion);
}

////////////////////////////////////////////////////////////////////////////////
// Playlist::ShareSettings protected members
////////////////////////////////////////////////////////////////////////////////

Playlist::SharedSettings::SharedSettings()
{
    m_configDirty = false;

    KConfigGroup config(KGlobal::config(), "PlaylistShared");

    bool resizeColumnsManually = config.readEntry("ResizeColumnsManually", false);
    ActionCollection::action("resizeColumnsManually")->setChecked(resizeColumnsManually);

    // Preallocate slots so we don't need to check later.
    int numCol = PlaylistItem::lastColumn() + 1;
    growColumnCount(numCol);

    // save column order
    m_columnOrder = config.readEntry("ColumnOrder", QList<int>());

    // may or may not have values for extra columns
    QList<int> list = config.readEntry("ColumnFixedWidth", QList<int>());
    if(list.size() > numCol) {
        growColumnCount(list.size());
    }
    for(int i = 0; i < m_columnFixedWidth.size() && i < list.size(); i++) {
        m_columnFixedWidth[i] = list[i];
    }

    QList<int> l = config.readEntry("VisibleColumns", QList<int>());

    if(l.isEmpty()) {

        // Provide some default values for column visibility if none were
        // read from the configuration file.

        m_columnsVisible[PlaylistItem::BitrateColumn] = false;
        m_columnsVisible[PlaylistItem::CommentColumn] = false;
        m_columnsVisible[PlaylistItem::FileNameColumn] = false;
        m_columnsVisible[PlaylistItem::FullPathColumn] = false;
    }
    else {
        // Convert the int list into a bool list.

        m_columnsVisible.fill(false);
        for(int i = 0; i < l.size() && i < m_columnsVisible.size(); ++i)
            m_columnsVisible[i] = bool(l[i]);
    }

    m_inlineCompletion = KGlobalSettings::Completion(
        config.readEntry("InlineCompletionMode", int(KGlobalSettings::CompletionAuto)));
}

////////////////////////////////////////////////////////////////////////////////
// Playlist::SharedSettings public members
////////////////////////////////////////////////////////////////////////////////

void Playlist::SharedSettings::writeConfig()
{
    KConfigGroup config(KGlobal::config(), "PlaylistShared");
    config.writeEntry("ColumnOrder", m_columnOrder);

    config.writeEntry("ColumnFixedWidth", m_columnFixedWidth.toList());

    QList<int> l;
    for(int i = 0; i < m_columnsVisible.size(); i++)
        l.append(int(m_columnsVisible[i]));

    config.writeEntry("VisibleColumns", l);

    config.writeEntry("InlineCompletionMode", int(m_inlineCompletion));

    config.writeEntry("ResizeColumnsManually", Playlist::manualResize());

    KGlobal::config()->sync();

    m_configDirty = false;
}

bool Playlist::SharedSettings::configDirty() const {
    return m_configDirty;
}

////////////////////////////////////////////////////////////////////////////////
// public members
////////////////////////////////////////////////////////////////////////////////

PlaylistItemList Playlist::m_history;
QVector<PlaylistItem *> Playlist::m_backMenuItems;
int Playlist::m_leftColumn = 0;

Playlist::Playlist(PlaylistCollection *collection, const QString &name,
                   const QString &iconName) :
    K3ListView(collection->playlistStack()),
    m_collection(collection),
    m_fetcher(new WebImageFetcher(this)),
    m_selectedCount(0),
    m_allowDuplicates(true),
    m_applySharedSettings(true),
    m_columnWidthModeChanged(false),
    m_disableColumnWidthUpdates(true),
    m_time(0),
    m_widthsDirty(true),
    m_searchEnabled(true),
    m_lastSelected(0),
    m_playlistName(name),
    m_rmbMenu(0),
    m_toolTip(0),
    m_bFileListChanged(false),
    m_bContentMutable(true),
    m_blockDataChanged(false)
{
    setup();
    collection->setupPlaylist(this, iconName);
}

Playlist::Playlist(PlaylistCollection *collection, const PlaylistItemList &items,
                   const QString &name, const QString &iconName) :
    K3ListView(collection->playlistStack()),
    m_collection(collection),
    m_fetcher(new WebImageFetcher(this)),
    m_selectedCount(0),
    m_allowDuplicates(true),
    m_applySharedSettings(true),
    m_columnWidthModeChanged(false),
    m_disableColumnWidthUpdates(true),
    m_time(0),
    m_widthsDirty(true),
    m_searchEnabled(true),
    m_lastSelected(0),
    m_playlistName(name),
    m_rmbMenu(0),
    m_toolTip(0),
    m_bFileListChanged(false),
    m_bContentMutable(true),
    m_blockDataChanged(false)
{
    setup();
    collection->setupPlaylist(this, iconName);
    createItems(items);
}

Playlist::Playlist(PlaylistCollection *collection, const QFileInfo &playlistFile,
                   const QString &iconName) :
    K3ListView(collection->playlistStack()),
    m_collection(collection),
    m_fetcher(new WebImageFetcher(this)),
    m_selectedCount(0),
    m_allowDuplicates(true),
    m_applySharedSettings(true),
    m_columnWidthModeChanged(false),
    m_disableColumnWidthUpdates(true),
    m_time(0),
    m_widthsDirty(true),
    m_searchEnabled(true),
    m_lastSelected(0),
    m_fileName(playlistFile.canonicalFilePath()),
    m_rmbMenu(0),
    m_toolTip(0),
    m_bFileListChanged(false),
    m_bContentMutable(true),
    m_blockDataChanged(false)
{
    setup();
    loadFile(m_fileName, playlistFile);
    collection->setupPlaylist(this, iconName);
}

Playlist::Playlist(PlaylistCollection *collection, bool delaySetup, int extraColumns) :
    K3ListView(collection->playlistStack()),
    m_collection(collection),
    m_fetcher(new WebImageFetcher(this)),
    m_selectedCount(0),
    m_allowDuplicates(true),
    m_applySharedSettings(true),
    m_columnWidthModeChanged(false),
    m_disableColumnWidthUpdates(true),
    m_time(0),
    m_widthsDirty(true),
    m_searchEnabled(true),
    m_lastSelected(0),
    m_rmbMenu(0),
    m_toolTip(0),
    m_bFileListChanged(false),
    m_bContentMutable(true),
    m_blockDataChanged(false)
{
    Q_UNUSED(extraColumns);

    setup();

    if(!delaySetup)
        collection->setupPlaylist(this, "audio-midi");
}

Playlist::~Playlist()
{
    /* persist the current settings if the user changed something */
    SharedSettings *ss = SharedSettings::instance();
    if(ss->configDirty()) {
        ss->writeConfig();
    }

    // clearItem() will take care of removing the items from the history,
    // so call clearItems() to make sure it happens.

    setContentMutable(true);
    clearItems(items());

    /* delete m_toolTip; */

    if(!m_shuttingDown)
        m_collection->removePlaylist(this);
}

/*
 * The label for this playlist, or a default if none set.
 * @see PlaylistInterface
 */
QString Playlist::name() const
{
    if(m_playlistName.isEmpty())
        return m_fileName.section(QDir::separator(), -1).section('.', 0, -2);
    else
        return m_playlistName;
}

/*
 * @return the FileHandle for the currently playing track in this playlist, or
 * FileHandle::null() if there is no such track.
 * @see PlaylistInterface
 */
FileHandle Playlist::currentFile() const
{
    return playingItem() ? playingItem()->file() : FileHandle::null();
}

/*
 * @return the total run time of this playlist, in seconds.
 * @see PlaylistInterface
 */
int Playlist::time() const
{
    // Since this method gets a lot of traffic, let's optimize for such.

    if(!m_addTime.isEmpty()) {
        foreach(const PlaylistItem *item, m_addTime) {
            if(item) {
                FileHandle fh = item->file();
                if (!fh.isNull()) {
                    m_time += fh.tag()->seconds();
                }
            }
        }

        m_addTime.clear();
    }

    if(!m_subtractTime.isEmpty()) {
        foreach(const PlaylistItem *item, m_subtractTime) {
            if(item) {
                FileHandle fh = item->file();
                if (!fh.isNull()) {
                    m_time -= fh.tag()->seconds();
                }
            }
        }

        m_subtractTime.clear();
    }

    return m_time;
}

void Playlist::playFirst()
{
    TrackSequenceManager::instance()->setNextItem(static_cast<PlaylistItem *>(
        Q3ListViewItemIterator(const_cast<Playlist *>(this), Q3ListViewItemIterator::Visible).current()));
    ActionCollection::action("forward")->trigger();
}

void Playlist::playNextAlbum()
{
    PlaylistItem *current = TrackSequenceManager::instance()->currentItem();
    if(!current)
        return; // No next album if we're not already playing.

    QString currentAlbum = current->file().tag()->album();
    current = TrackSequenceManager::instance()->nextItem();

    while(current && current->file().tag()->album() == currentAlbum)
        current = TrackSequenceManager::instance()->nextItem();

    TrackSequenceManager::instance()->setNextItem(current);
    ActionCollection::action("forward")->trigger();
}

/*
 * @return the total number of tracks in this playlist, including both hidden
 * and non-hidden items.
 * @see PlaylistInterface
 */
int Playlist::count() const
{
    return childCount();
}

/*
 * Command to move the iterator to next song in this playlist.
 * @see PlaylistInterface
 */
void Playlist::playNext()
{
    setPlaying(TrackSequenceManager::instance()->nextItem());
}

/*
 * Command to clear the iterator for this playlist.
 * @see PlaylistInterface
 */
void Playlist::stop()
{
    m_history.clear();
    setPlaying(0);
}

/*
 * Command to move iterator to previously played track.
 * @see PlaylistInterface
 */
void Playlist::playPrevious()
{
    if(!playingItem())
        return;

    bool random = ActionCollection::action("randomPlay") && ActionCollection::action<KToggleAction>("randomPlay")->isChecked();

    PlaylistItem *previous = 0;

    if(random && !m_history.isEmpty()) {
        PlaylistItemList::Iterator last = --m_history.end();
        previous = *last;
        m_history.erase(last);
    }
    else {
        m_history.clear();
        previous = TrackSequenceManager::instance()->previousItem();
    }

    if(!previous)
        previous = static_cast<PlaylistItem *>(playingItem()->itemAbove());

    setPlaying(previous, false);
}

void Playlist::setName(const QString &n)
{
    m_collection->addNameToDict(n);
    m_collection->removeNameFromDict(m_playlistName);

    m_playlistName = n;
    emit signalNameChanged(m_playlistName);
}

bool Playlist::saveFile(const QString& fileName, bool bDialogOk)
{
    if(fileName.isEmpty()) {
        return false;
    }

    QFile file(fileName);

    // TODO: write to temp file first before corrupting the original
    if(!file.open(QIODevice::WriteOnly)) {
        if (bDialogOk) {
            KMessageBox::error(this, i18n("Could not save to file %1.",
                fileName));
        }
        return false;
    }

    QTextStream stream(&file);

    stream.setCodec("UTF-8");

    QStringList fileList = this->files();

    foreach(const QString &file, fileList) {
        stream << file << endl;
    }

    file.close();

    m_bFileListChanged = false;
    m_fileListLastModified = QFileInfo(file).lastModified();

    return true;
}

void Playlist::setFileListLastModified(const QDateTime& t) {
    m_fileListLastModified = t;
}

/* call initiated from File Menu. Ok to show dialog to user. If fileName
 * is not set, then invoke saveAs().
 */
void Playlist::save()
{
    if(m_fileName.isEmpty()) {
        saveAs();
        return;
    }
    // will show error dialog on failure
    if(!saveFile(m_fileName, true)) {
        // let the user try a new name
        saveAs();
    }
}

/* Save a m3u playlist to a user-specified file name.
 * Call initiated from File Menu. On successful write, set the disk file 
 * name only if this is a Normal Playlist. It is Ok to show a dialog to 
 * the user.
 */
void Playlist::saveAs()
{
    QString fileName = MediaFiles::savePlaylistDialog(name(), this);

    if(fileName.isEmpty()) {
        // user cancelled the dialog
        return;
    }

    // will show error dialog on failure
    bool bSuccess = saveFile(fileName, true);

    // TODO: we need a better way to do this
    bool bIsNormal = this->getType() == Playlist::Type::Normal;

    if (bSuccess && bIsNormal) {

        m_collection->removeFileFromDict(m_fileName);

        m_fileName = fileName;
    
        m_collection->addFileToDict(fileName);

        // If there's no playlist name set, use the file name.
        if(m_playlistName.isEmpty()) {
            emit signalNameChanged( this->name() );
        }
    }
}

/* Write .m3u, but do not update m_fileName */
bool Playlist::exportFile()
{
    QString fileName = MediaFiles::savePlaylistDialog(name(), this);

    if(fileName.isEmpty()) {
        // user cancelled the dialog
        return false;
    }

    // will show error dialog on failure
    return saveFile(fileName, true);
}

void Playlist::updateDeletedItem(PlaylistItem *item)
{
    m_members.remove(item->file().absFilePath());
    m_search.clearItem(item);

    m_history.removeAll(item);
    m_addTime.removeAll(item);
    m_subtractTime.removeAll(item);
}

void Playlist::clearItem(PlaylistItem *item)
{
    if(!this->isContentMutable()) {
        kError() << "Attempt to delete track from read-only playlist";
        return;
    }

    // Automatically updates internal structs via updateDeletedItem
    delete item;

    m_bFileListChanged = true;

    dataChanged();
}

void Playlist::clearItems(const PlaylistItemList &items)
{
    if(!this->isContentMutable()) {
        kError() << "Attempt to delete track(s) from read-only playlist";
        return;
    }

    foreach(PlaylistItem *item, items)
        delete item;

    m_bFileListChanged = true;

    dataChanged();
}

PlaylistItem *Playlist::playingItem() // static
{
    kDebug() << "list has " << PlaylistItem::playingItems().count() << " items";
    // playingItems() is a shared list, one list for all the Playlist
    return PlaylistItem::playingItems().isEmpty() ? 0 : PlaylistItem::playingItems().front();
}

QStringList Playlist::files() const
{
    QStringList list;

    for(Q3ListViewItemIterator it(const_cast<Playlist *>(this)); it.current(); ++it)
        list.append(static_cast<PlaylistItem *>(*it)->file().absFilePath());

    return list;
}

PlaylistItemList Playlist::items()
{
    return items(Q3ListViewItemIterator::IteratorFlag(0));
}

PlaylistItemList Playlist::visibleItems()
{
    return items(Q3ListViewItemIterator::Visible);
}

PlaylistItemList Playlist::selectedItems()
{
    PlaylistItemList list;

    switch(m_selectedCount) {
    case 0:
        break;
        // case 1:
        // list.append(m_lastSelected);
        // break;
    default:
        list = items(Q3ListViewItemIterator::IteratorFlag(Q3ListViewItemIterator::Selected |
                                                         Q3ListViewItemIterator::Visible));
        break;
    }

    return list;
}

PlaylistItem *Playlist::firstChild() const
{
    return static_cast<PlaylistItem *>(K3ListView::firstChild());
}

void Playlist::updateLeftColumn()
{
    int newLeftColumn = leftMostVisibleColumn();

    if(m_leftColumn != newLeftColumn) {
        updatePlaying();
        m_leftColumn = newLeftColumn;
    }
}

void Playlist::setItemsVisible(const PlaylistItemList &items, bool visible) // static
{
    m_visibleChanged = true;

    foreach(PlaylistItem *playlistItem, items)
        playlistItem->setVisible(visible);
}

void Playlist::setSearch(const PlaylistSearch &s)
{
    m_search = s;

    if(!m_searchEnabled)
        return;

    setItemsVisible(s.matchedItems(), true);
    setItemsVisible(s.unmatchedItems(), false);

    TrackSequenceManager::instance()->iterator()->playlistChanged();
}

void Playlist::setSearchEnabled(bool enabled)
{
    if(m_searchEnabled == enabled)
        return;

    m_searchEnabled = enabled;

    if(enabled) {
        setItemsVisible(m_search.matchedItems(), true);
        setItemsVisible(m_search.unmatchedItems(), false);
    }
    else
        setItemsVisible(items(), true);
}

void Playlist::markItemSelected(PlaylistItem *item, bool selected)
{
    if(selected && !item->isSelected()) {
        m_selectedCount++;
        m_lastSelected = item;
    }
    else if(!selected && item->isSelected())
        m_selectedCount--;
}

bool Playlist::isContentMutable() const {
    return m_bContentMutable;
}

void Playlist::setContentMutable(bool b) {
    m_bContentMutable = b;
}

void Playlist::checkForReadOnlyM3uFile() {
    bool bMutable = true;
    bool bIsNormal = this->getType() == Playlist::Type::Normal;
    QString fname = this->fileName();
    if (bIsNormal && !fname.isEmpty()) {
       QFileInfo fileInfo(fname);
       if (fileInfo.exists() && !fileInfo.isWritable()) {
           bMutable = false;
       }
    }
    this->setContentMutable(bMutable);
}

/**
 * This method is called when we're about to make visible the playlist (or
 * playlists) listed in \p sources. See if any of those playlist contain the
 * currently-playing music track so we can call item->setPlaying(), which
 * displays the black-triangle marker.
 *
 * @param sources  the playlist or playlists about to be made visible
 * @param setMaster determine whether item is moved to front (=true) or back
 *                  of the playingItems() list.
 */
void Playlist::synchronizePlayingItems(const PlaylistList &sources, bool setMaster)
{
    foreach(const Playlist *p, sources) {
        if(p->hasPlayingItem()) {
            CollectionListItem *base = playingItem()->collectionItem();
            for(Q3ListViewItemIterator itemIt(this); itemIt.current(); ++itemIt) {
                PlaylistItem *item = static_cast<PlaylistItem *>(itemIt.current());
                if(base == item->collectionItem()) {
                    item->setPlaying(true, setMaster);
                    //PlaylistItemList playing = PlaylistItem::playingItems();
                    TrackSequenceManager::instance()->setCurrent(item);
                    return;
                }
            }
            return;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// public slots
////////////////////////////////////////////////////////////////////////////////

void Playlist::copy()
{
    PlaylistItemList items = selectedItems();
    KUrl::List urls;

    foreach(PlaylistItem *item, items) {
        urls << KUrl::fromPath(item->file().absFilePath());
    }

    QMimeData *mimeData = new QMimeData;
    urls.populateMimeData(mimeData);

    QApplication::clipboard()->setMimeData(mimeData, QClipboard::Clipboard);
}

void Playlist::paste()
{
    decode(QApplication::clipboard()->mimeData(), static_cast<PlaylistItem *>(currentItem()));
}

/* entry point for edit_clear action */
void Playlist::clear()
{
    PlaylistItemList l = selectedItems();
    if(!l.isEmpty()) {
        clearItems(l);
    }
}

void Playlist::slotRefresh()
{
    PlaylistItemList l = selectedItems();
    if(l.isEmpty())
        l = visibleItems();

    KApplication::setOverrideCursor(Qt::waitCursor);
    foreach(PlaylistItem *item, l) {
        item->refreshFromDisk();

        if(!item->file().tag() || !item->file().fileInfo().exists()) {
            kDebug() << "Error while trying to refresh the tag.  "
                           << "This file has probably been removed."
                           << endl;
            delete item->collectionItem();
        }

        processEvents();
    }
    KApplication::restoreOverrideCursor();
}

void Playlist::slotRenameFile()
{
    if(!this->isContentMutable()) {
        kError() << "Attempt to rename track in read-only playlist";
        return;
    }

    FileRenamer renamer;
    PlaylistItemList items = selectedItems();

    if(items.isEmpty())
        return;

    emit signalEnableDirWatch(false);

    m_blockDataChanged = true;
    renamer.rename(items);

    m_bFileListChanged = true;
    m_fileListLastModified = QDateTime::currentDateTime();

    m_blockDataChanged = false;
    dataChanged();

    emit signalEnableDirWatch(true);
}

void Playlist::slotViewCover()
{
    const PlaylistItemList items = selectedItems();
    if (items.isEmpty())
        return;
    foreach(const PlaylistItem *item, items)
        item->file().coverInfo()->popup();
}

void Playlist::slotRemoveCover()
{
    PlaylistItemList items = selectedItems();
    if(items.isEmpty())
        return;
    int button = KMessageBox::warningContinueCancel(this,
                                                    i18n("Are you sure you want to delete these covers?"),
                                                    QString(),
                                                    KGuiItem(i18n("&Delete Covers")));
    if(button == KMessageBox::Continue)
        refreshAlbums(items);
}

void Playlist::slotShowCoverManager()
{
    static CoverDialog *managerDialog = 0;

    if(!managerDialog)
        managerDialog = new CoverDialog(this);

    managerDialog->show();
}

void Playlist::slotAddCover(bool retrieveLocal)
{
    PlaylistItemList items = selectedItems();

    if(items.isEmpty())
        return;

    if(!retrieveLocal) {
        m_fetcher->setFile((*items.begin())->file());
        m_fetcher->searchCover();
        return;
    }

    KUrl file = KFileDialog::getImageOpenUrl(
        KUrl( "kfiledialog://homedir" ), this, i18n("Select Cover Image File"));

    if(file.isEmpty())
        return;

    QString artist = items.front()->file().tag()->artist();
    QString album = items.front()->file().tag()->album();

    coverKey newId = CoverManager::addCover(file, artist, album);

    if(newId != CoverManager::NoMatch)
        refreshAlbums(items, newId);
}

// Called when image fetcher has added a new cover.
void Playlist::slotCoverChanged(int coverId)
{
    kDebug() << "Refreshing information for newly changed covers.\n";
    refreshAlbums(selectedItems(), coverId);
}

void Playlist::slotGuessTagInfo(TagGuesser::Type type)
{
    KApplication::setOverrideCursor(Qt::waitCursor);
    const PlaylistItemList items = selectedItems();
    setDynamicListsFrozen(true);

    m_blockDataChanged = true;

    foreach(PlaylistItem *item, items) {
        item->guessTagInfo(type);
        processEvents();
    }

    // MusicBrainz queries automatically commit at this point.  What would
    // be nice is having a signal emitted when the last query is completed.

    if(type == TagGuesser::FileName)
        TagTransactionManager::instance()->commit();

    m_blockDataChanged = false;

    dataChanged();
    setDynamicListsFrozen(false);
    KApplication::restoreOverrideCursor();
}

void Playlist::slotReload()
{
    // check policy
    if (!this->getPolicy(Playlist::PolicyCanReload)) {
        kError() << "Attempt to reload '" << name() << "' prohibited by policy";
        return;
    }

    QFileInfo fileInfo(m_fileName);
    if(!fileInfo.exists() || !fileInfo.isFile() || !fileInfo.isReadable()) {
        kWarning() << "can't read file '" << m_fileName << "'";
        return;
    }

    setContentMutable(true);
    clearItems(items());
    loadFile(m_fileName, fileInfo);
    checkForReadOnlyM3uFile();
}

void Playlist::slotWeightDirty(int column)
{
    if(column < 0) {
        m_weightDirty.clear();
        for(int i = 0; i < columns(); i++) {
            if(isColumnVisible(i))
                m_weightDirty.append(i);
        }
        return;
    }

    if(!m_weightDirty.contains(column))
        m_weightDirty.append(column);
}

/**
 * Jump to the playlist with the currently playing track, select it and
 * scroll so it is in view.
 */
void Playlist::slotShowPlaying()
{
    PlaylistItem *item = playingItem();
    if(!item)
        return;

    Playlist *pl = item->playlist();

    // Raise the playlist before selecting the items otherwise the tag editor
    // will not update when it gets the selectionChanged() notification
    // because it will think the user is choosing a different playlist but not
    // selecting a different item.

    m_collection->raise4(pl);

    /* using Single Mode means we don't need separate Clear & Set and thus
     * emit only one selectionChanged signal, not two.
     */
    pl->setSelectionModeExt(Single);
    pl->setSelected(item, true);
    pl->setSelectionModeExt(Extended);
    pl->ensureItemVisible(item);
}

void Playlist::slotColumnResizeModeChanged()
{
    if(manualResize())
        setHScrollBarMode(Auto);
    else
        setHScrollBarMode(AlwaysOff);

    slotUpdateColumnWidths();

    // FIXME: forcing this is ugly
    SharedSettings::instance()->writeConfig();
}

void Playlist::dataChanged()
{
    if(m_blockDataChanged || m_shuttingDown)
        return;

    PlaylistCollection::instance()->dataChanged();
}

////////////////////////////////////////////////////////////////////////////////
// protected members
////////////////////////////////////////////////////////////////////////////////

void Playlist::removeFromDisk(const PlaylistItemList &items)
{
    if(isVisible() && !items.isEmpty()) {

        QStringList files;
        foreach(const PlaylistItem *item, items)
            files.append(item->file().absFilePath());

        DeleteDialog dialog(this);

        m_blockDataChanged = true;

        if(dialog.confirmDeleteList(files)) {
            bool shouldDelete = dialog.shouldDelete();
            QStringList errorFiles;

            foreach(PlaylistItem *item, items) {
                if(playingItem() == item)
                    ActionCollection::action("forward")->trigger();

                QString removePath = item->file().absFilePath();
                if((!shouldDelete && KIO::NetAccess::synchronousRun(KIO::trash(removePath), this)) ||
                   (shouldDelete && QFile::remove(removePath)))
                {
                    delete item->collectionItem();
                    m_bFileListChanged = true;
                    m_fileListLastModified = QDateTime::currentDateTime();
                }
                else
                    errorFiles.append(item->file().absFilePath());
            }

            if(!errorFiles.isEmpty()) {
                QString errorMsg = shouldDelete ?
                        i18n("Could not delete these files") :
                        i18n("Could not move these files to the Trash");
                KMessageBox::errorList(this, errorMsg, errorFiles);
            }
        }

        m_blockDataChanged = false;

        dataChanged();
    }
}

Q3DragObject *Playlist::dragObject(QWidget *parent)
{
    PlaylistItemList items = selectedItems();
    KUrl::List urls;

    foreach(PlaylistItem *item, items) {
        urls << KUrl::fromPath(item->file().absFilePath());
    }

    K3URLDrag *urlDrag = new K3URLDrag(urls, parent);

    urlDrag->setPixmap(BarIcon("audio-x-generic"));

    return urlDrag;
}

void Playlist::contentsDragEnterEvent(QDragEnterEvent *e)
{
    K3ListView::contentsDragEnterEvent(e);

    if(CoverDrag::isCover(e->mimeData())) {
        setDropHighlighter(true);
        setDropVisualizer(false);

        e->accept();
        return;
    }

    setDropHighlighter(false);
    setDropVisualizer(true);

    const KUrl::List urls = KUrl::List::fromMimeData(e->mimeData());

    if (urls.isEmpty()) {
        e->ignore();
        return;
    }

    if(!this->getPolicy(Playlist::PolicyCanModifyContent) ||
       !this->isContentMutable()) {
        e->ignore();
        return;
    }

    e->accept();
    return;
}

bool Playlist::acceptDrag(QDropEvent *e) const
{
    return CoverDrag::isCover(e->mimeData()) || KUrl::List::canDecode(e->mimeData());
}

/* create PlaylistItems from Url(s) on clipboard, and add immediately following item */
void Playlist::decode(const QMimeData *s, PlaylistItem *item)
{
    if(!this->isContentMutable()) {
        kError() << "Attempt to drop track(s) on read-only playlist";
        return;
    }

    if(!s->hasUrls()) {         /* introduced in Qt4.2 */
        return;
    }

    if(!KUrl::List::canDecode(s))
        return;

    const KUrl::List urls = KUrl::List::fromMimeData(s);

    if(urls.isEmpty())
        return;

    // handle dropped images

    if(!MediaFiles::isMediaFile(urls.front().path())) {

        QString file;

        if(urls.front().isLocalFile())
            file = urls.front().path();
        else
            KIO::NetAccess::download(urls.front(), file, 0);

        KMimeType::Ptr mimeType = KMimeType::findByPath(file);

        if(item && mimeType->name().startsWith(QLatin1String("image/"))) {
            item->file().coverInfo()->setCover(QImage(file));
            refreshAlbum(item->file().tag()->artist(),
                         item->file().tag()->album());
        }

        KIO::NetAccess::removeTempFile(file);
    }

    QStringList fileList = MediaFiles::convertURLsToLocal(urls, this);

    addFiles(fileList, item);
}

bool Playlist::eventFilter(QObject *watched, QEvent *e)
{
    if(watched == header()) {
        switch(e->type()) {
        case QEvent::MouseMove:
        {
            if((static_cast<QMouseEvent *>(e)->modifiers() & Qt::LeftButton) == Qt::LeftButton &&
                !ActionCollection::action<KToggleAction>("resizeColumnsManually")->isChecked())
            {
                m_columnWidthModeChanged = true;

                ActionCollection::action<KToggleAction>("resizeColumnsManually")->setChecked(true);
                slotColumnResizeModeChanged();
            }

            break;
        }
        case QEvent::MouseButtonRelease:
        {
            if(m_columnWidthModeChanged) {
                m_columnWidthModeChanged = false;
                notifyUserColumnWidthModeChanged();
            }

            if(!manualResize() && m_widthsDirty)
                QTimer::singleShot(0, this, SLOT(slotUpdateColumnWidths()));
            break;
        }
        default:
            break;
        }
    }

    bool rv = K3ListView::eventFilter(watched, e);

    if (e->type() == QEvent::FocusIn) {
        slotUpdateMenus();
    }

    return rv;
}

void Playlist::keyPressEvent(QKeyEvent *event)
{
    if(event->key() == Qt::Key_Up) {
        Q3ListViewItemIterator selected(this, Q3ListViewItemIterator::IteratorFlag(
                                           Q3ListViewItemIterator::Selected |
                                           Q3ListViewItemIterator::Visible));
        if(selected.current()) {
            Q3ListViewItemIterator visible(this, Q3ListViewItemIterator::IteratorFlag(
                                              Q3ListViewItemIterator::Visible));
            if(selected.current() == visible.current())
                KApplication::postEvent(parent(), new FocusUpEvent);
        }

    }

    K3ListView::keyPressEvent(event);
}

void Playlist::contentsDropEvent(QDropEvent *e)
{
    QPoint vp = contentsToViewport(e->pos());
    PlaylistItem *item = static_cast<PlaylistItem *>(itemAt(vp));

    // First see if we're dropping a cover, if so we can get it out of the
    // way early.
    if(item && CoverDrag::isCover(e->mimeData())) {
        coverKey id = CoverDrag::idFromData(e->mimeData());

        // If the item we dropped on is selected, apply cover to all selected
        // items, otherwise just apply to the dropped item.

        if(item->isSelected()) {
            const PlaylistItemList selItems = selectedItems();
            foreach(PlaylistItem *playlistItem, selItems) {
                playlistItem->file().coverInfo()->setCoverId(id);
                playlistItem->refresh();
            }
        }
        else {
            item->file().coverInfo()->setCoverId(id);
            item->refresh();
        }

        return;
    }

    // When dropping on the toUpper half of an item, insert before this item.
    // This is what the user expects, and also allows the insertion at
    // top of the list

    if(!item)
        item = static_cast<PlaylistItem *>(lastItem());
    else if(vp.y() < item->itemPos() + item->height() / 2)
        item = static_cast<PlaylistItem *>(item->itemAbove());

    m_blockDataChanged = true;

    if(e->source() == this) {

        // Since we're trying to arrange things manually, turn off sorting.

        setSorting(columns() + 1);

        const QList<Q3ListViewItem *> items = K3ListView::selectedItems();

        foreach(Q3ListViewItem *listViewItem, items) {
            if(!item) {

                // Insert the item at the top of the list.  This is a bit ugly,
                // but I don't see another way.

                takeItem(listViewItem);
                insertItem(listViewItem);
            }
            else
                listViewItem->moveItem(item);

            item = static_cast<PlaylistItem *>(listViewItem);
        }
    }
    else
        decode(e->mimeData(), item);

    m_blockDataChanged = false;

    dataChanged();
    emit signalPlaylistItemsDropped(this);
    K3ListView::contentsDropEvent(e);
}

void Playlist::contentsMouseDoubleClickEvent(QMouseEvent *e)
{
    // Filter out non left button double clicks, that way users don't have the
    // weird experience of switching songs from a double right-click.

    if(e->button() == Qt::LeftButton)
        K3ListView::contentsMouseDoubleClickEvent(e);
}

void Playlist::showEvent(QShowEvent *e)
{
    if(m_applySharedSettings) {
        SharedSettings::instance()->apply(this);
        m_applySharedSettings = false;
    }

    K3ListView::showEvent(e);
}

/* typically called just before we make this playlist visible */
void Playlist::applySharedSettings()
{
    m_applySharedSettings = true;

    /* persist the current settings if the user changed something */
    SharedSettings *ss = SharedSettings::instance();
    if(ss->configDirty()) {
        ss->writeConfig();
    }
}

void Playlist::read(QDataStream &s)
{
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

void Playlist::viewportPaintEvent(QPaintEvent *pe)
{
    // If there are columns that need to be updated, well, update them.

    if(!m_weightDirty.isEmpty() && !manualResize())
    {
        calculateColumnWeights();
        slotUpdateColumnWidths();
    }

    K3ListView::viewportPaintEvent(pe);
}

void Playlist::viewportResizeEvent(QResizeEvent *re)
{
    // If the width of the view has changed, manually update the column
    // widths.

    if(re->size().width() != re->oldSize().width() && !manualResize())
        slotUpdateColumnWidths();

    K3ListView::viewportResizeEvent(re);
}

void Playlist::insertItem(Q3ListViewItem *item)
{
    // Because we're called from the PlaylistItem ctor, item may not be a
    // PlaylistItem yet (it would be QListViewItem when being inserted.  But,
    // it will be a PlaylistItem by the time it matters, but be careful if
    // you need to use the PlaylistItem from here.

    m_addTime.append(static_cast<PlaylistItem *>(item));
    K3ListView::insertItem(item);
}

void Playlist::takeItem(Q3ListViewItem *item)
{
    // See the warning in Playlist::insertItem.

    m_subtractTime.append(static_cast<PlaylistItem *>(item));
    K3ListView::takeItem(item);
}

int Playlist::addColumn(const QString &label, int)
{
    int newIndex = K3ListView::addColumn(label, 30);
    SharedSettings::instance()->growColumnCount(newIndex+1);
    slotWeightDirty(newIndex);
    return newIndex;
}

PlaylistItem *Playlist::createItem(const FileHandle &file,
                                   Q3ListViewItem *after, bool emitChanged)
{
    return createItem<PlaylistItem>(file, after, emitChanged);
}

void Playlist::createItems(const PlaylistItemList &siblings, PlaylistItem *after)
{
    createItems<PlaylistItem, PlaylistItem>(siblings, after);
}

void Playlist::addFiles(const QStringList &files, PlaylistItem *after)
{
    if(!this->isContentMutable()) {
        kError() << "Attempt to add track(s) to read-only playlist";
        return;
    }

    if(!after)
        after = static_cast<PlaylistItem *>(lastItem());

    KApplication::setOverrideCursor(Qt::waitCursor);

    m_blockDataChanged = true;

    FileHandleList queue;

    foreach(const QString &file, files)
        addFile(file, queue, true, &after);

    if (queue.size() > 0) {
        m_bFileListChanged = true;
        m_fileListLastModified = QDateTime::currentDateTime();
    }

    addFileHelper(queue, &after, true);

    m_blockDataChanged = false;

    slotWeightDirty();
    dataChanged();

    KApplication::restoreOverrideCursor();
}

void Playlist::refreshAlbums(const PlaylistItemList &items, coverKey id)
{
    QList< QPair<QString, QString> > albums;
    bool setAlbumCovers = items.count() == 1;

    foreach(const PlaylistItem *item, items) {
        QString artist = item->file().tag()->artist();
        QString album = item->file().tag()->album();

        if(!albums.contains(qMakePair(artist, album)))
            albums.append(qMakePair(artist, album));

        item->file().coverInfo()->setCoverId(id);
        if(setAlbumCovers)
            item->file().coverInfo()->applyCoverToWholeAlbum(true);
    }

    for(QList< QPair<QString, QString> >::ConstIterator it = albums.constBegin();
        it != albums.constEnd(); ++it)
    {
        refreshAlbum((*it).first, (*it).second);
    }
}

void Playlist::updatePlaying() const
{
    foreach(const PlaylistItem *item, PlaylistItem::playingItems())
        item->listView()->triggerUpdate();
}

void Playlist::refreshAlbum(const QString &artist, const QString &album)
{
    ColumnList columns;
    columns.append(PlaylistItem::ArtistColumn);
    PlaylistSearch::Component artistComponent(artist, false, columns,
                                              PlaylistSearch::Component::Exact);

    columns.clear();
    columns.append(PlaylistItem::AlbumColumn);
    PlaylistSearch::Component albumComponent(album, false, columns,
                                             PlaylistSearch::Component::Exact);

    PlaylistSearch::ComponentList components;
    components.append(artist);
    components.append(album);

    PlaylistList playlists;
    playlists.append(CollectionList::instance());

    PlaylistSearch search(playlists, components);
    const PlaylistItemList matches = search.matchedItems();

    foreach(PlaylistItem *item, matches)
        item->refresh();
}

/* Update widget, menu and SharedSettings.
 * @param c is physical col in widget.
 */
void Playlist::hideColumn(int c, bool updateMenu)
{
    if(updateMenu) {
        foreach (QAction *action, m_headerMenu->actions()) {
            if(!action)
                continue;
    
            if (action->data().toInt() == c) {
                action->setChecked(false);
                break;
            }
        }
    }

    SharedSettings *ss = SharedSettings::instance();
    // call takes a ColumnType
    if(ss->isColumnVisible(c)) {
        ss->toggleColumnVisible(c);
    }

    setColumnWidthMode(c, Manual);
    setColumnWidth(c, 0);

    // Moving the column to the end seems to prevent it from randomly
    // popping up.

    header()->moveSection(c, header()->count());
    header()->setResizeEnabled(false, c);

    if(c == m_leftColumn) {
        updatePlaying();
        m_leftColumn = leftMostVisibleColumn();
    }
}

/* Update widget, menu and SharedSettings.
 * @param c is physical col in widget.
 */
void Playlist::showColumn(int c, bool updateMenu)
{
    if(updateMenu) {
        foreach (QAction *action, m_headerMenu->actions()) {
            if(!action)
                continue;
    
            if (action->data().toInt() == c) {
                action->setChecked(true);
                break;
            }
        }
    }

    SharedSettings *ss = SharedSettings::instance();
    if(!ss->isColumnVisible(c)) {
        ss->toggleColumnVisible(c);
    }

    // For auto-resize mode
    // Just set the width to one to mark the column as visible -- we'll update
    // the real size in the slotUpdateColumnWidths call.

    if(manualResize())
        setColumnWidth(c, ss->columnFixedWidth(c));
    else
        setColumnWidth(c, 1);

    header()->setResizeEnabled(true, c);
    header()->moveSection(c, c); // Approximate old position

    if(c == leftMostVisibleColumn()) {
        updatePlaying();
        m_leftColumn = leftMostVisibleColumn();
    }
}

bool Playlist::isColumnVisible(int c) const
{
    return columnWidth(c) != 0;
}

void Playlist::slotInitialize()
{
    addColumn(i18n("Track Name"));
    addColumn(i18n("Artist"));
    addColumn(i18n("Album"));
    addColumn(i18n("Cover"));
    addColumn(i18nc("cd track number", "Track"));
    addColumn(i18n("Genre"));
    addColumn(i18n("Year"));
    addColumn(i18n("Length"));
    addColumn(i18n("Bitrate"));
    addColumn(i18n("Comment"));
    addColumn(i18n("File Name"));
    addColumn(i18n("File Name (full path)"));

    setColumnAlignment(3, Qt::AlignHCenter);    // Cover
    setColumnAlignment(4, Qt::AlignHCenter);    // Track #

    setAllColumnsShowFocus(true);
    setSelectionMode(Q3ListView::Extended);
    setShowSortIndicator(true);
    setDropVisualizer(true);

    //////////////////////////////////////////////////
    // setup menu for View|Show Columns
    //////////////////////////////////////////////////

    /* we only add items to the menu once, and we do it the first
     * time this method is called. All Playlist Widgets will share this
     * menu. KMenu *m_headerMenu has already been created in this->setup().
     */
    const SharedSettings *ss = SharedSettings::instance();
    int menuItemCount = m_headerMenu->actions().size();
    if (menuItemCount == 0) {
        int numItem = PlaylistItem::lastColumn() + 1;
        QAction *showAction;
        for(int i = 0; i < numItem; ++i) {
            if(i == PlaylistItem::FileNameColumn) {
                m_headerMenu->addSeparator();
            }
    
            showAction = new QAction(header()->label(i), m_headerMenu);
            showAction->setData(i);
            showAction->setCheckable(true);
            showAction->setChecked( ss->isColumnVisible(i) );
            m_headerMenu->addAction(showAction);
        }

        PlaylistCollection *coll = PlaylistCollection::instance();
        connect(m_headerMenu, SIGNAL(triggered(QAction*)), 
                coll->object(), SLOT(slotToggleColumnVisible(QAction*)));
    }

    for(int i = 0; i < header()->count(); ++i) {
        setColumnWidthMode(i, Manual);
    }

    connect(this, SIGNAL(contextMenuRequested(Q3ListViewItem*,QPoint,int)),
            this, SLOT(slotShowRMBMenu(Q3ListViewItem*,QPoint,int)));
    connect(this, SIGNAL(itemRenamed(Q3ListViewItem*,QString,int)),
            this, SLOT(slotInlineEditDone(Q3ListViewItem*,QString,int)));
    connect(this, SIGNAL(doubleClicked(Q3ListViewItem*)),
            this, SLOT(slotPlayCurrent()));
    connect(this, SIGNAL(returnPressed(Q3ListViewItem*)),
            this, SLOT(slotPlayCurrent()));

    connect(header(), SIGNAL(sizeChange(int,int,int)),
            this, SLOT(slotColumnSizeChanged(int,int,int)));

    connect(renameLineEdit(), SIGNAL(completionModeChanged(KGlobalSettings::Completion)),
            this, SLOT(slotInlineCompletionModeChanged(KGlobalSettings::Completion)));

    connect(ActionCollection::action("resizeColumnsManually"), SIGNAL(activated()),
            this, SLOT(slotColumnResizeModeChanged()));

    if(ActionCollection::action<KToggleAction>("resizeColumnsManually")->isChecked())
        setHScrollBarMode(Auto);
    else
        setHScrollBarMode(AlwaysOff);

    setAcceptDrops(true);
    setDropVisualizer(true);

    m_disableColumnWidthUpdates = false;

    setShowToolTips(false);
    /* m_toolTip = new PlaylistToolTip(viewport(), this); */
}

void Playlist::setupItem(PlaylistItem *item)
{
    item->setTrackId(g_trackID);
    g_trackID++;

    if(!m_search.isEmpty())
        item->setVisible(m_search.checkItem(item));

    if(childCount() <= 2 && !manualResize()) {
        slotWeightDirty();
        slotUpdateColumnWidths();
        triggerUpdate();
    }
}

void Playlist::setDynamicListsFrozen(bool frozen)
{
    m_collection->setDynamicListsFrozen(frozen);
}

CollectionListItem *Playlist::collectionListItem(const FileHandle &file)
{
    if(!QFile::exists(file.absFilePath())) {
        kError() << "File" << file.absFilePath() << "does not exist.";
        return 0;
    }

    CollectionListItem *item = CollectionList::instance()->lookup(file.absFilePath());

    if(!item) {
        item = CollectionList::instance()->createItem(file);
    }

    return item;
}

/* make sure all columns have the correct visibility and width */
void Playlist::updateColumnFixedWidth()
{
    const SharedSettings *ss = SharedSettings::instance();
    int numCol = columns();
    for(int c = 0; c < numCol; c++) {
        int width = ss->columnFixedWidth(c);   // 0 if col not visible
        if(this->columnWidth(c) != width) {
            if(width > 0) {
                showColumn(c, false);
            } else {
                hideColumn(c, false);
            }
        }
    }
}

void Playlist::setFileListChanged(bool b) {
    m_bFileListChanged = b;
}

bool Playlist::hasFileListChanged() const
{
    return m_bFileListChanged;
}

////////////////////////////////////////////////////////////////////////////////
// protected slots
////////////////////////////////////////////////////////////////////////////////

void Playlist::slotPopulateBackMenu() const
{
    if(!playingItem())
        return;

    QMenu *menu = ActionCollection::action<KToolBarPopupAction>("back")->menu();
    menu->clear();
    m_backMenuItems.clear();
    m_backMenuItems.reserve(10);

    int count = 0;
    PlaylistItemList::ConstIterator it = m_history.constEnd();

    QAction *action;

    while(it != m_history.constBegin() && count < 10) {
        ++count;
        --it;
        action = new QAction((*it)->file().tag()->title(), menu);
        action->setData(count - 1);
        menu->addAction(action);
        m_backMenuItems << *it;
    }
}

void Playlist::slotPlayFromBackMenu(QAction *backAction) const
{
    int number = backAction->data().toInt();

    if(number >= m_backMenuItems.size())
        return;

    TrackSequenceManager::instance()->setNextItem(m_backMenuItems[number]);
    ActionCollection::action("forward")->trigger();
}

////////////////////////////////////////////////////////////////////////////////
// private members
////////////////////////////////////////////////////////////////////////////////

void Playlist::setup()
{
    setItemMargin(3);

    connect(header(), SIGNAL(indexChange(int,int,int)), this, SLOT(slotColumnOrderChanged(int,int,int)));

    connect(m_fetcher, SIGNAL(signalCoverChanged(int)), this, SLOT(slotCoverChanged(int)));

    // update menu enable state
    connect(this, SIGNAL(selectionChanged()), this, SLOT(slotUpdateMenus()));

    // Prevent list of selected items from changing while internet search is in
    // progress.
    connect(this, SIGNAL(selectionChanged()), m_fetcher, SLOT(abortSearch()));

    // use insert order
    setSorting(-1);

    /* This apparently must be created very early in initialization for other
     * Playlist code requiring m_headerMenu. m_columnVisibleAction and 
     * m_headerMenu are both class static variables.  */
    if (!m_columnVisibleAction) {
        // lazy create, then shared by all instances of Playlist
        m_columnVisibleAction = new KActionMenu(i18n("&Show Columns"), this);
        ActionCollection::actions()->addAction("showColumns", m_columnVisibleAction);

        m_headerMenu = m_columnVisibleAction->menu();
    }

    // TODO: Determine if other stuff in setup must happen before slotInitialize().

    // Explicitly call slotInitialize() so that the columns are added before
    // SharedSettings::apply() sets the visible and hidden ones.
    slotInitialize();
}

/* fileName must be an .m3u file */
void Playlist::loadFile(const QString &fileName, const QFileInfo &fileInfo)
{
    QFile file(fileName);
    if(!file.open(QIODevice::ReadOnly))
        return;

    QTextStream stream(&file);

    // Turn off non-explicit sorting.

    setSorting(PlaylistItem::lastColumn() + 1);

    PlaylistItem *after = 0;

    m_disableColumnWidthUpdates = true;

    m_blockDataChanged = true;

    while(!stream.atEnd()) {
        QString itemName = stream.readLine().trimmed();

        QFileInfo item(itemName);

        if(item.isRelative())
            item.setFile(QDir::cleanPath(fileInfo.absolutePath() + '/' + itemName));

        if(item.exists() && item.isFile() && item.isReadable() &&
           MediaFiles::isMediaFile(item.fileName()))
        {
            if(after)
                after = createItem(FileHandle(item, item.absoluteFilePath()), after, false);
            else
                after = createItem(FileHandle(item, item.absoluteFilePath()), 0, false);
        }
    }

    m_blockDataChanged = false;

    file.close();

    // this playlist content matches the disk file
    m_bFileListChanged = false;
    m_fileListLastModified = QFileInfo(file).lastModified();

    dataChanged();

    m_disableColumnWidthUpdates = false;
}

/**
 * Update the TrackSequenceManager with a new PlaylistItem to make current,
 * move the black triangle marker to the specified entry. If somehow the
 * newItem is the same as old item, then do nothing.
 *
 * @param newItem  the table row of track to make current, or 0 to clear the 
 *                 current item.
 * @param addToHistory  true to add the old item to recent-history list.
 *
 * Note: This is a static function.
 */
void Playlist::setPlaying(PlaylistItem *newItem, bool addToHistory)
{
    PlaylistItem *curItem = playingItem();
    if(curItem == newItem)
        return;

    if(curItem) {
        if(addToHistory) {
            Playlist *pl = curItem->playlist();
            if(pl == pl->m_collection->upcomingPlaylist())
                m_history.append(curItem->collectionItem());
            else
                m_history.append(curItem);
        }
        curItem->setPlaying(false);
    }

    // remember newItem, and tell PlayerManager to pick it up
    TrackSequenceManager::instance()->setCurrent(newItem);
#ifdef __GNUC__
#warning "kde4: port it"
#endif
    //kapp->dcopClient()->emitDCOPSignal("Player", "trackChanged()", data);

    if(!newItem)
        return;

    newItem->setPlaying(true);

    bool enableBack = !m_history.isEmpty();
    ActionCollection::action<KToolBarPopupAction>("back")->menu()->setEnabled(enableBack);
}

/* Determine if the currently playing item belongs to this playlist.
 *
 * This is part of the search to find the current track item, so it can 
 * be marked with a black triangle.
 *
 * @return true if the currently playing track belongs to this playlist.
 */
bool Playlist::hasPlayingItem() const
{
    PlaylistItem *item = playingItem();
    return item && this == item->playlist();
}

int Playlist::leftMostVisibleColumn() const
{
    int i = 0;
    while(!isColumnVisible(header()->mapToSection(i)) && i < PlaylistItem::lastColumn())
        i++;

    return header()->mapToSection(i);
}

PlaylistItemList Playlist::items(Q3ListViewItemIterator::IteratorFlag flags)
{
    PlaylistItemList list;

    for(Q3ListViewItemIterator it(this, flags); it.current(); ++it)
        list.append(static_cast<PlaylistItem *>(it.current()));

    return list;
}

void Playlist::calculateColumnWeights()
{
    if(m_disableColumnWidthUpdates)
        return;

    PlaylistItemList l = items();
    QList<int>::Iterator columnIt;

    int numColumn = columns();
    QVector<double> averageWidth(numColumn);
    double itemCount = l.size();

    QVector<int> cachedWidth;

    // Calculate a weight proportional to string length for each column.
    // Here we're not using a real average, but averaging the squares of the
    // column widths and then using the square root of that value.  This gives
    // a nice weighting to the longer columns without doing something arbitrary
    // like adding a fixed amount of padding.

    // cachedWidth values are assigned by CollectionList
    foreach(PlaylistItem *item, l) {
        cachedWidth = item->cachedWidths();
        int width;
        for(int i = 0; i < numColumn; ++i) {
            if(i < cachedWidth.size()) {
                width = cachedWidth[i];
            } else {
                width = item->width(fontMetrics(), this, i);
            }
            averageWidth[i] += std::pow(double(width), 2.0) / itemCount;
        }
    }

    if(m_columnWeights.isEmpty())
        m_columnWeights.fill(-1, columns());

    foreach(int column, m_weightDirty) {
        m_columnWeights[column] = int(std::sqrt(averageWidth[column]) + 0.5);
    }

    m_weightDirty.clear();
}

void Playlist::addFile(const QString &file, FileHandleList &files, bool importPlaylists,
                       PlaylistItem **after)
{
    if(!this->isContentMutable()) {
        kError() << "Attempt to add track to read-only playlist";
        return;
    }

    if(hasItem(file) && !m_allowDuplicates)
        return;

    addFileHelper(files, after);

    // Our biggest thing that we're fighting during startup is too many stats
    // of files.  Make sure that we don't do one here if it's not needed.

    const CollectionListItem *item = CollectionList::instance()->lookup(file);

    if(item && !item->file().isNull()) {
        FileHandle cached(item->file());
        cached.tag();
        files.append(cached);
        return;
    }

    const QFileInfo fileInfo(QDir::cleanPath(file));
    if(!fileInfo.exists())
        return;

    const QString canonicalPath = fileInfo.canonicalFilePath();

    if(fileInfo.isFile() && fileInfo.isReadable()) {
        if(MediaFiles::isMediaFile(file)) {
            FileHandle f(fileInfo, canonicalPath);
            f.tag();
            files.append(f);
        }
    }

    if(importPlaylists && MediaFiles::isPlaylistFile(file)) {
        importRecentPlaylistFile(fileInfo);
        return;
    }

    if(fileInfo.isDir()) {
        foreach(const QString &directory, m_collection->excludedFolders()) {
            if(canonicalPath.startsWith(directory))
                return; // Exclude it
        }

        QDirIterator dirIterator(canonicalPath, QDir::AllEntries | QDir::NoDotAndDotDot);

        while(dirIterator.hasNext()) {
            // We set importPlaylists to the value from the add directories
            // dialog as we want to load all of the ones that the user has
            // explicitly asked for, but not those that we find in toLower
            // directories.

            addFile(dirIterator.next(), files,
                    m_collection->importPlaylists(), after);
        }
    }
}

void Playlist::addFileHelper(FileHandleList &files, PlaylistItem **after, bool ignoreTimer)
{
    static QTime time = QTime::currentTime();

    // Process new items every 10 seconds, when we've loaded 1000 items, or when
    // it's been requested in the API.

    if(ignoreTimer || time.elapsed() > 10000 ||
       (files.count() >= 1000 && time.elapsed() > 1000))
    {
        time.restart();

        const bool focus = hasFocus();
        const bool visible = isVisible() && files.count() > 20;

        if(visible)
            m_collection->raiseDistraction();

        foreach(const FileHandle &fileHandle, files)
            *after = createItem(fileHandle, *after, false);

        files.clear();

        if(visible)
            m_collection->lowerDistraction();

        if(focus)
            setFocus();
    }
}

/**
 * Handle an m3u file found during directory scan. The complication is that,
 * if this playlist object already exists, we need to determine whether 
 * this new one or the existing one is newest, and keep that one.
 *
 * @param fileInfo  the new .m3u file
 */
void Playlist::importRecentPlaylistFile(const QFileInfo& fileInfo) {
    QString fname = fileInfo.canonicalFilePath();
    // check if this playlist already exists in collection
    Playlist *pl = m_collection->findPlaylistByFilename(fname);
    if(pl) {
        /* we already have a playlist with this .m3u name. We intentionally
         * ignore the pl->isContentMutable() state, as pl->slotReload() knows
         * how to handle that. If the user changes that .m3u file, it
         * should be used.
         */

        // check timestamps
        if(fileInfo.lastModified() > pl->m_fileListLastModified) {
            // this might happen if .m3u was modified outside of this app
            // clear the existing pl object and populate from m3u disk file
            pl->slotReload();
            return;
        }
        // else the existing pl object is the latest one

    } else {
        // create new playlist and read file
        pl = new NormalPlaylist(m_collection, fileInfo);

        // set the isContentMutable() flag
        pl->checkForReadOnlyM3uFile();
    }
}

////////////////////////////////////////////////////////////////////////////////
// private slots
////////////////////////////////////////////////////////////////////////////////

/* set table column visibility and column width. This method handles both
 * manual resize and auto-resize modes. Do not call triggerUpdate() from this
 * method.
 */
void Playlist::slotUpdateColumnWidths()
{
    if(m_disableColumnWidthUpdates) {
        return;
    }
    
    if(manualResize()) {
        updateColumnFixedWidth();
        return;
    }

    // update the column visibility
    const SharedSettings *ss = SharedSettings::instance();
    QList<int> visibleColumns;
    for(int i = 0; i < columns(); i++) {
        bool b = ss->isColumnVisible(i);
        if(b != isColumnVisible(i)) {
            if(b) {
                showColumn(i);
            } else {
                hideColumn(i);
            }
        }
        if(b) {
            visibleColumns.append(i);
        }
    }

    // count() is number of table rows
    if(count() == 0) {
        foreach(int column, visibleColumns)
            setColumnWidth(column, header()->fontMetrics().width(header()->label(column)) + 10);

        return;
    }

    // Make sure that the column weights have been initialized before trying to
    // update the columns.

    if(m_columnWeights.isEmpty()) {
        return;
    }

    // First build a list of minimum widths based on the strings in the listview
    // header.  We won't let the width of the column go below this width.

    QVector<int> minimumWidth(columns(), 0);
    int minimumWidthTotal = 0;

    // Also build a list of either the minimum *or* the fixed width -- whichever is
    // greater.

    QVector<int> minimumFixedWidth(columns(), 0);
    int minimumFixedWidthTotal = 0;

    foreach(int column, visibleColumns) {
        minimumWidth[column] = header()->fontMetrics().width(header()->label(column)) + 10;
        minimumWidthTotal += minimumWidth[column];

        minimumFixedWidth[column] = qMax(minimumWidth[column], 30);
        minimumFixedWidthTotal += minimumFixedWidth[column];
    }

    // Make sure that the width won't get any smaller than this.  We have to
    // account for the scrollbar as well.  Since this method is called from the
    // resize event this will set a pretty hard toLower bound on the size.

    setMinimumWidth(minimumWidthTotal + verticalScrollBar()->width());

    // If we've got enough room for the fixed widths (larger than the minimum
    // widths) then instead use those for our "minimum widths".

    if(minimumFixedWidthTotal < visibleWidth()) {
        minimumWidth = minimumFixedWidth;
        minimumWidthTotal = minimumFixedWidthTotal;
    }

    // We've got a list of columns "weights" based on some statistics gathered
    // about the widths of the items in that column.  We need to find the total
    // useful weight to use as a divisor for each column's weight.

    double totalWeight = 0;
    foreach(int column, visibleColumns)
        totalWeight += m_columnWeights[column];

    // Computed a "weighted width" for each visible column.  This would be the
    // width if we didn't have to handle the cases of minimum and maximum widths.

    QVector<int> weightedWidth(columns(), 0);
    foreach(int column, visibleColumns)
        weightedWidth[column] = int(double(m_columnWeights[column]) / totalWeight * visibleWidth() + 0.5);

    // The "extra" width for each column.  This is the weighted width less the
    // minimum width or zero if the minimum width is greater than the weighted
    // width.

    QVector<int> extraWidth(columns(), 0);

    // This is used as an indicator if we have any columns where the weighted
    // width is less than the minimum width.  If this is false then we can
    // just use the weighted width with no problems, otherwise we have to
    // "readjust" the widths.

    bool readjust = false;

    // If we have columns where the weighted width is less than the minimum width
    // we need to steal that space from somewhere.  The amount that we need to
    // steal is the "neededWidth".

    int neededWidth = 0;

    // While we're on the topic of stealing -- we have to have somewhere to steal
    // from.  availableWidth is the sum of the amount of space beyond the minimum
    // width that each column has been allocated -- the sum of the values of
    // extraWidth[].

    int availableWidth = 0;

    // Fill in the values discussed above.

    foreach(int column, visibleColumns) {
        if(weightedWidth[column] < minimumWidth[column]) {
            readjust = true;
            extraWidth[column] = 0;
            neededWidth += minimumWidth[column] - weightedWidth[column];
        }
        else {
            extraWidth[column] = weightedWidth[column] - minimumWidth[column];
            availableWidth += extraWidth[column];
        }
    }

    // The adjustmentRatio is the amount of the "extraWidth[]" that columns will
    // actually be given.

    double adjustmentRatio = (double(availableWidth) - double(neededWidth)) / double(availableWidth);

    // This will be the sum of the total space that we actually use.  Because of
    // rounding error this won't be the exact available width.

    int usedWidth = 0;

    // Now set the actual column widths.  If the weighted widths are all greater
    // than the minimum widths, just use those, otherwise use the "reajusted
    // weighted width".

    foreach(int column, visibleColumns) {
        int width;
        if(readjust) {
            int adjustedExtraWidth = int(double(extraWidth[column]) * adjustmentRatio + 0.5);
            width = minimumWidth[column] + adjustedExtraWidth;
        }
        else
            width = weightedWidth[column];

        setColumnWidth(column, width);
        usedWidth += width;
    }

    // Fill the remaining gap for a clean fit into the available space.

    int remainingWidth = visibleWidth() - usedWidth;
    setColumnWidth(visibleColumns.back(), columnWidth(visibleColumns.back()) + remainingWidth);

    m_widthsDirty = false;
}

void Playlist::slotAddToUpcoming()
{
    m_collection->setUpcomingPlaylistEnabled(true);
    m_collection->upcomingPlaylist()->appendItems(selectedItems());
}

/**
 * Update actions that depend on selected items or focus. This method 
 * should be called when this widget gets focus or the selected items 
 * change.
 */
void Playlist::slotUpdateMenus() {
   
    // this handles actions from the Edit Menu and the right-click menu
    // if no rows are selected, then all these menu items are disabled
    int nRow = selectedItems().count();

    // use read/write status for this playlist
    bool bMutable = this->getPolicy(Playlist::PolicyCanModifyContent) && this->isContentMutable();

    bool bEnablePaste = false;
    if(bMutable) {
        // looking for mime-type "text/uri-list"
        const QMimeData *mime = QApplication::clipboard()->mimeData();
        if (mime && mime->hasUrls()) {
            bEnablePaste = true;
        }
    }

    // Edit Menu
    QAction *act = ActionCollection::action("edit_undo");
    act->setEnabled(false);

    act = ActionCollection::action("edit_copy");
    act->setEnabled(nRow > 0);

    // TODO: check that there's abs filename(s) on clipboard
    act = ActionCollection::action("edit_paste");
    act->setEnabled(bEnablePaste);

    // remove track from playlist
    act = ActionCollection::action("edit_clear");
    act->setEnabled(nRow > 0 && bMutable);

    // Context Menu
    // TODO: check that cover exists
    act = ActionCollection::action("viewCover" );
    act->setEnabled(nRow > 0);
}

/* right-click context menu for table. Called when mouse button pressed. */
void Playlist::slotShowRMBMenu(Q3ListViewItem *item, const QPoint &point, int column)
{
    Q_UNUSED(item);

    // Create the RMB menu on first use.
    if(!m_rmbMenu) {

        // Probably more of these actions should be ported over to using KActions.

        m_rmbMenu = new KMenu(this);

        m_rmbMenu->addAction(SmallIcon("go-jump-today"),
            i18n("Add to Play Queue"), this, SLOT(slotAddToUpcoming()));
        m_rmbMenu->addSeparator();

        m_rmbMenu->addAction( ActionCollection::action("edit_copy") );
        m_rmbMenu->addAction( ActionCollection::action("edit_paste") );
        m_rmbMenu->addSeparator();
        m_rmbMenu->addAction( ActionCollection::action("viewCover") );
        m_rmbMenu->addAction( ActionCollection::action("showEditor") );
    }

    //slotUpdateMenus();

    m_rmbMenu->popup(point);

    m_currentColumn = column;
}

void Playlist::slotRenameTag()
{
    // setup completions and validators

    CollectionList *list = CollectionList::instance();

    KLineEdit *edit = renameLineEdit();

    switch(m_currentColumn)
    {
    case PlaylistItem::ArtistColumn:
        edit->completionObject()->setItems(list->uniqueSet(CollectionList::Artists));
        break;
    case PlaylistItem::AlbumColumn:
        edit->completionObject()->setItems(list->uniqueSet(CollectionList::Albums));
        break;
    case PlaylistItem::GenreColumn:
    {
        QStringList genreList;
        TagLib::StringList genres = TagLib::ID3v1::genreList();
        for(TagLib::StringList::Iterator it = genres.begin(); it != genres.end(); ++it)
            genreList.append(TStringToQString((*it)));
        edit->completionObject()->setItems(genreList);
        break;
    }
    default:
        edit->completionObject()->clear();
        break;
    }

    m_editText = currentItem()->text(m_currentColumn);

    rename(currentItem(), m_currentColumn);
}

bool Playlist::editTag(PlaylistItem *item, const QString &text, int column)
{
    Tag *newTag = TagTransactionManager::duplicateTag(item->file().tag());

    switch(column)
    {
    case PlaylistItem::TrackColumn:
        newTag->setTitle(text);
        break;
    case PlaylistItem::ArtistColumn:
        newTag->setArtist(text);
        break;
    case PlaylistItem::AlbumColumn:
        newTag->setAlbum(text);
        break;
    case PlaylistItem::TrackNumberColumn:
    {
        bool ok;
        int value = text.toInt(&ok);
        if(ok)
            newTag->setTrack(value);
        break;
    }
    case PlaylistItem::GenreColumn:
        newTag->setGenre(text);
        break;
    case PlaylistItem::YearColumn:
    {
        bool ok;
        int value = text.toInt(&ok);
        if(ok)
            newTag->setYear(value);
        break;
    }
    }

    TagTransactionManager::instance()->changeTagOnItem(item, newTag);
    return true;
}

void Playlist::slotInlineEditDone(Q3ListViewItem *, const QString &, int column)
{
    QString text = renameLineEdit()->text();
    bool changed = false;

    PlaylistItemList l = selectedItems();

    // See if any of the files have a tag different from the input.

    for(PlaylistItemList::ConstIterator it = l.constBegin(); it != l.constEnd() && !changed; ++it)
        if((*it)->text(column) != text)
            changed = true;

    if(!changed ||
       (l.count() > 1 && KMessageBox::warningContinueCancel(
           0,
           i18n("This will edit multiple files. Are you sure?"),
           QString(),
           KGuiItem(i18n("Edit")),
           KStandardGuiItem::cancel(),
           "DontWarnMultipleTags") == KMessageBox::Cancel))
    {
        return;
    }

    foreach(PlaylistItem *item, l)
        editTag(item, text, column);

    TagTransactionManager::instance()->commit();

    CollectionList::instance()->dataChanged();
    dataChanged();
    update();
}

void Playlist::slotColumnOrderChanged(int, int from, int to)
{
    if(from == 0 || to == 0) {
        updatePlaying();
        m_leftColumn = header()->mapToSection(0);
    }

    SharedSettings::instance()->setColumnOrder(this);
}

/* called when user selects a menu item. On entry, SharedSettings has old state.
 * \p action has new state.
 */
void Playlist::slotToggleColumnVisible(QAction *action)
{
    int col = action->data().toInt();

    if(action->isChecked()) {
        if(col == PlaylistItem::FileNameColumn) {
            hideColumn(PlaylistItem::FullPathColumn, true);
        } else if(col == PlaylistItem::FullPathColumn) {
            hideColumn(PlaylistItem::FileNameColumn, true);
        }
    }

    if(action->isChecked()) {
        showColumn(col);
    } else {
        hideColumn(col);
    }

    slotUpdateColumnWidths();

    redisplaySearch();

    triggerUpdate();
}

void Playlist::slotCreateGroup()
{
    QString name = m_collection->playlistNameDialog(i18n("Create New Playlist"));

    if(!name.isEmpty())
        new NormalPlaylist(m_collection, selectedItems(), name);
}

void Playlist::notifyUserColumnWidthModeChanged()
{
    KMessageBox::information(this,
                             i18n("Manual column widths have been enabled. You can "
                                  "switch back to automatic column sizes in the view "
                                  "menu."),
                             i18n("Manual Column Widths Enabled"),
                             "ShowManualColumnWidthInformation");
}

/* This is called when user changes column width with mouse, but also when
 * Q3ListView::setColumnWidth() is called (e.g. a column is hidden.)
 * Ignore \p newsize if < 1.
 * @param column  physical column in table
 */
void Playlist::slotColumnSizeChanged(int column, int, int newSize)
{
    m_widthsDirty = true;
    if(manualResize() && newSize > 0) {
        SharedSettings *ss = SharedSettings::instance();
        ss->setColumnFixedWidth(column, newSize);
    }
}

void Playlist::slotInlineCompletionModeChanged(KGlobalSettings::Completion mode)
{
    SharedSettings::instance()->setInlineCompletionMode(mode);
}

void Playlist::slotPlayCurrent()
{
    Q3ListViewItemIterator it(this, Q3ListViewItemIterator::Selected);
    PlaylistItem *next = static_cast<PlaylistItem *>(it.current());
    TrackSequenceManager::instance()->setNextItem(next);
    ActionCollection::action("forward")->trigger();
}

////////////////////////////////////////////////////////////////////////////////
// helper functions
////////////////////////////////////////////////////////////////////////////////

#if 0
QDataStream &operator<<(QDataStream &s, const Playlist &p)
{
    qDebug() << "Playlist::operator<<: Write " << p.name();
    s << p.name();
    s << p.fileName();
    s << p.files();

    return s;
}

QDataStream &operator>>(QDataStream &s, Playlist &p)
{
    p.read(s);
    return s;
}
#endif

bool processEvents()
{
    static QTime time = QTime::currentTime();

    if(time.elapsed() > 100) {
        time.restart();
        kapp->processEvents();
        return true;
    }
    return false;
}

#include "playlist.moc"

// vim: set et sw=4 tw=0 sta:
