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

#include "playlistbox.h"

#include <kicon.h>
#include <kiconloader.h>
#include <kmessagebox.h>
#include <kmenu.h>
#include <kaction.h>
#include <kactioncollection.h>
#include <kdebug.h>
#include <ktoggleaction.h>
#include <kselectaction.h>
#include <kconfiggroup.h>

#include <Q3Header>
#include <QPainter>
#include <QTimer>
#include <QDragLeaveEvent>
#include <QList>
#include <QDragMoveEvent>
#include <QKeyEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QFileInfo>
#include <QTime>
#include <QApplication>
#include <QClipboard>

#include "playlist.h"
#include "collectionlist.h"
#include "dynamicplaylist.h"
#include "upcomingplaylist.h"
#include "historyplaylist.h"
#include "viewmode.h"
#include "searchplaylist.h"
#include "treeviewitemplaylist.h"
#include "actioncollection.h"
#include "cache.h"
#include "k3bexporter.h"
#include "tracksequencemanager.h"
#include "tagtransactionmanager.h"
#include "playermanager.h"
#include "dbuscollectionproxy.h"

using namespace ActionCollection;

////////////////////////////////////////////////////////////////////////////////
// PlaylistBox public methods
////////////////////////////////////////////////////////////////////////////////

PlaylistBox::PlaylistBox(PlayerManager *player, QWidget *parent, QStackedWidget *playlistStack) :
    K3ListView(parent),
    PlaylistCollection(player, playlistStack),
    m_viewModeIndex(0),
    m_hasSelection(false),
    m_doingMultiSelect(false),
    m_dropItem(0),
    m_showTimer(0)
{
    readConfig();
    addColumn("Playlists", width());

    header()->blockSignals(true);
    header()->hide();
    header()->blockSignals(false);

    setSorting(0);
    setFullWidth(true);
    setItemMargin(3);

    setAcceptDrops(true);
    setSelectionModeExt(Extended);

    m_contextMenu = new KMenu(this);

    K3bPlaylistExporter *exporter = new K3bPlaylistExporter(this);
    m_k3bAction = exporter->action();

    m_contextMenu->addAction( action("file_save") );
    m_contextMenu->addSeparator();
    m_contextMenu->addAction( action("renamePlaylist") );
    m_contextMenu->addAction( action("editSearch") );
    m_contextMenu->addAction( action("duplicatePlaylist") );

    if(m_k3bAction)
        m_contextMenu->addAction( m_k3bAction );

    // add the view modes stuff

    KSelectAction *viewModeAction =
        new KSelectAction( KIcon("view-choose"), i18n("View Modes"), ActionCollection::actions());
    ActionCollection::actions()->addAction("viewModeMenu", viewModeAction);

    ViewMode* viewmode = new ViewMode(this);
    m_viewModes.append(viewmode);
    viewModeAction->addAction(KIcon("view-list-details"), viewmode->name());

    CompactViewMode* compactviewmode = new CompactViewMode(this);
    m_viewModes.append(compactviewmode);
    viewModeAction->addAction(KIcon("view-list-text"), compactviewmode->name());

    TreeViewMode* treeviewmode = new TreeViewMode(this);
    m_viewModes.append(treeviewmode);
    viewModeAction->addAction(KIcon("view-list-tree"), treeviewmode->name());

    CollectionList::initialize(this);

    viewModeAction->setCurrentItem(m_viewModeIndex);
    m_viewModes[m_viewModeIndex]->setShown(true);

    TrackSequenceManager::instance()->setCurrentPlaylist(CollectionList::instance());
    raise(CollectionList::instance());

    connect(viewModeAction, SIGNAL(triggered(int)), this, SLOT(slotSetViewMode(int)));

    connect(this, SIGNAL(selectionChanged()),
            this, SLOT(slotPlaylistChanged()));

    connect(this, SIGNAL(doubleClicked(Q3ListViewItem*)),
            this, SLOT(slotDoubleClicked(Q3ListViewItem*)));

    connect(this, SIGNAL(contextMenuRequested(Q3ListViewItem*,QPoint,int)),
            this, SLOT(slotShowContextMenu(Q3ListViewItem*,QPoint,int)));

    TagTransactionManager *tagManager = TagTransactionManager::instance();
    connect(tagManager, SIGNAL(signalAboutToModifyTags()), SLOT(slotFreezePlaylists()));
    connect(tagManager, SIGNAL(signalDoneModifyingTags()), SLOT(slotUnfreezePlaylists()));

    setupUpcomingPlaylist();

    connect(CollectionList::instance(), SIGNAL(signalNewTag(QString,uint)),
            this, SLOT(slotAddItem(QString,uint)));
    connect(CollectionList::instance(), SIGNAL(signalRemovedTag(QString,uint)),
            this, SLOT(slotRemoveItem(QString,uint)));
    connect(CollectionList::instance(), SIGNAL(cachedItemsLoaded()),
            this, SLOT(slotLoadCachedPlaylists()));

    m_savePlaylistTimer = 0;

    KToggleAction *historyAction =
        new KToggleAction(KIcon("view-history"), i18n("Show &History"), ActionCollection::actions());
    ActionCollection::actions()->addAction("showHistory", historyAction);
    connect(historyAction, SIGNAL(triggered(bool)),
            this, SLOT(slotSetHistoryPlaylistEnabled(bool)));

    //m_showTimer = new QTimer(this);
    //connect(m_showTimer, SIGNAL(timeout()), SLOT(slotShowDropTarget()));

    // hook up to the D-Bus
    (void) new DBusCollectionProxy(this, this);
}

PlaylistBox::~PlaylistBox()
{
    PlaylistList l;
    CollectionList *collection = CollectionList::instance();
    for(Q3ListViewItem *i = this->firstChild(); i; i = i->nextSibling()) {
        Item *item = static_cast<Item *>(i);
        if(item->playlist() && item->playlist() != collection)
            l.append(item->playlist());
    }

    Cache::savePlaylists(l);
    saveConfig();
}

void PlaylistBox::raise(Playlist *playlist)
{
    if(!playlist)
        return;

    Item *i = m_playlistDict.value(playlist, 0);

    if(i) {
        clearSelection();
        setSelected(i, true);

        setSingleItem(i);
        ensureItemVisible(currentItem());
    }
    else
        PlaylistCollection::raise(playlist);

    slotPlaylistChanged();
}

void PlaylistBox::duplicate()
{
    Item *item = static_cast<Item *>(currentItem());
    if(!item || !item->playlist())
        return;

    QString name = playlistNameDialog(i18nc("verb, copy the playlist", "Duplicate"), item->text(0));

    if(name.isNull())
        return;

    Playlist *p = new Playlist(this, name);
    p->createItems(item->playlist()->items());
}

void PlaylistBox::scanFolders()
{
    kDebug() << "Starting folder scan";
    QTime stopwatch; stopwatch.start();

    PlaylistCollection::scanFolders();

    kDebug() << "Folder scan complete, took" << stopwatch.elapsed() << "ms";

    // set the read/write state for each playlist based on m3u writability
    QList<Playlist*> list = this->getAllPlaylists();
    QString fname;
    foreach(Playlist *pl, list) {
        pl->checkForReadOnlyM3uFile();
    }

    // we're done
    kDebug() << "Startup complete!";
    emit startupComplete();
}

QList<Playlist*> PlaylistBox::getAllPlaylists() const {
    QList<Playlist*> list;
    for(Q3ListViewItem *i = this->firstChild(); i; i = i->nextSibling()) {
        Item *item = static_cast<Item *>(i);
        Playlist *pl = item->playlist();
        if(pl) {
            list.append(pl);
        }
    }
    return list;
}

////////////////////////////////////////////////////////////////////////////////
// PlaylistBox public slots
////////////////////////////////////////////////////////////////////////////////

void PlaylistBox::paste()
{
    Item *i = static_cast<Item *>(currentItem());
    decode(QApplication::clipboard()->mimeData(), i);
}

////////////////////////////////////////////////////////////////////////////////
// PlaylistBox protected methods
////////////////////////////////////////////////////////////////////////////////

void PlaylistBox::slotFreezePlaylists()
{
    setDynamicListsFrozen(true);
}

void PlaylistBox::slotUnfreezePlaylists()
{
    setDynamicListsFrozen(false);
}

void PlaylistBox::slotPlaylistDataChanged()
{
    if(m_savePlaylistTimer)
        m_savePlaylistTimer->start(); // Restarts the timer if it's already running.
}

void PlaylistBox::slotSetHistoryPlaylistEnabled(bool enable)
{
    setHistoryPlaylistEnabled(enable);
}

void PlaylistBox::setupPlaylist(Playlist *playlist, const QString &iconName)
{
    setupPlaylist(playlist, iconName, 0);
}

void PlaylistBox::setupPlaylist(Playlist *playlist, const QString &iconName, Item *parentItem)
{
    connect(playlist, SIGNAL(signalPlaylistItemsDropped(Playlist*)),
            SLOT(slotPlaylistItemsDropped(Playlist*)));

    PlaylistCollection::setupPlaylist(playlist, iconName);

    if(parentItem)
        new Item(parentItem, iconName, playlist->name(), playlist);
    else
        new Item(this, iconName, playlist->name(), playlist);
}

void PlaylistBox::removePlaylist(Playlist *playlist)
{
    // Could be false if setup() wasn't run yet.
    if(m_playlistDict.contains(playlist)) {
        removeNameFromDict(m_playlistDict[playlist]->text(0));
        delete m_playlistDict[playlist]; // Delete the Item*
    }

    removeFileFromDict(playlist->fileName());
    m_playlistDict.remove(playlist);
}

////////////////////////////////////////////////////////////////////////////////
// PlaylistBox private methods
////////////////////////////////////////////////////////////////////////////////

void PlaylistBox::readConfig()
{
    KConfigGroup config(KGlobal::config(), "PlaylistBox");
    m_viewModeIndex = config.readEntry("ViewMode", 0);
}

void PlaylistBox::saveConfig()
{
    KConfigGroup config(KGlobal::config(), "PlaylistBox");
    config.writeEntry("ViewMode", action<KSelectAction>("viewModeMenu")->currentItem());
    KGlobal::config()->sync();
}

/**
 * This is called by the 'File|Remove Playlists...' menu item. Remove selected 
 * playlists where canDelete() & isContentMutable() is true. 
 * Prompt the user whether to remove the .m3u file from disk too. If it happens that 
 * there are no disk files, then prompt with the playlist names which will be deleted.
 * If user selects Cancel, then delete neither the playlist objects or the disk files.
 */
void PlaylistBox::remove()
{
    ItemList items = selectedBoxItems();

    QStringList files;
    QStringList names;
    PlaylistList removeQueue;

    foreach(Item *item, items) {
        if(item && item->playlist()) {
            Playlist *pl = item->playlist();
            if (pl->canDelete() && pl->isContentMutable()) {
                if (!pl->fileName().isEmpty() && QFileInfo(pl->fileName()).exists()) {
                    files.append(pl->fileName());
                }

                removeQueue.append(pl);
                names.append(pl->name());
            }
        }
    }

    if (names.isEmpty()) {
        return;
    }

    // Playlist won't have a fileName if never saved
    int respRemoveFiles = KMessageBox::No;
    if(!files.isEmpty()) {
        respRemoveFiles = KMessageBox::warningYesNoCancelList(
            this,
            i18n("Do you want to delete these files from the disk as well?"),
            files,
            QString(),
            KStandardGuiItem::del(),
            KGuiItem(i18n("Keep")));

        if(respRemoveFiles == KMessageBox::No) {
            // protect against code mistakes below
            files.clear();
        } 
        else if(respRemoveFiles == KMessageBox::Cancel) {
            return;
        }
    }
    else {
        int resp = KMessageBox::warningContinueCancelList(
            this,
            i18n("Are you sure you want to remove these playlists from your collection?"),
            names,
            i18n("Remove Items?"),
            KGuiItem(i18n("&Remove"), "user-trash"));

        if(resp == KMessageBox::Cancel) {
            return;
        }
    }

    // identify a new playlist icon for the PlaylistBox selection to move to
    if(items.back()->nextSibling() && static_cast<Item *>(items.back()->nextSibling())->playlist()) {
        setSingleItem(items.back()->nextSibling());
    }
    else {
        Item *i = static_cast<Item *>(items.front()->itemAbove());
        while(i && !i->playlist()) {
            i = static_cast<Item *>(i->itemAbove());
        }

        if(!i) {
            i = Item::collectionItem();
        }

        setSingleItem(i);
    }

    // delete the playlist objects
    foreach(Playlist *pl, removeQueue) {
        delete pl;
    }

    // remove the disk files _after_ we delete the playlist objects, in case of crash
    if(respRemoveFiles == KMessageBox::Yes) {
        QStringList filesNotDeleted;
        foreach(QString path, files) {
            if(!QFile::remove(path)) {
                filesNotDeleted.append(path);
            }
        }

        if(!filesNotDeleted.isEmpty()) {
            KMessageBox::errorList(this, i18n("Could not delete these files."), filesNotDeleted);
        }
    }
}

void PlaylistBox::setDynamicListsFrozen(bool frozen)
{
    for(QList<ViewMode *>::Iterator it = m_viewModes.begin();
        it != m_viewModes.end();
        ++it)
    {
        (*it)->setDynamicListsFrozen(frozen);
    }
}

/**
 * This writes the 'playlists' cache file in the kde4 dir.
 *
 * For the special playlists like History, Play Queue, Folder or Search,
 * this is the only place they are written to disk. The user's regular .m3u 
 * playlists are also saved, but it's not clear why since the data is 
 * immediately discarded after it is read at app startup.
 */
void PlaylistBox::slotSavePlaylistsToCache()
{
    kDebug() << "Auto-saving playlists.\n";

    PlaylistList list;
    Playlist *pl;
    CollectionList *collection = CollectionList::instance();
    for(Q3ListViewItem *i = this->firstChild(); i; i = i->nextSibling()) {
        Item *item = static_cast<Item *>(i);
        pl = item->playlist();
        if(pl && pl != collection) {
            list.append(pl);
        }
    }

    // re-write the cache file
    Cache::savePlaylists(list);
}

/* Write any modified User playlists to disk. Skip Playlist Objects which 
 * are read-only or is the CollectionList. If bDialogOk allows, prompt for 
 * whether or not to save; for a newly-created playlist, prompt for 
 * filename.
 *
 * If dialogs are suppressed, then our policy is to assume a "Yes" 
 * response, and not report any errors..
 */
void PlaylistBox::savePlaylistsToDisk(bool bDialogOk)
{
    Playlist *pl;
    CollectionList *collection = CollectionList::instance();
    for(Q3ListViewItem *i = this->firstChild(); i; i = i->nextSibling()) {
        Item *item = static_cast<Item *>(i);
        pl = item->playlist();
        if(pl && pl != collection && pl->canModifyContent() && 
           pl->hasFileListChanged()) {
           
            int retval;
            if (bDialogOk) {
                retval = KMessageBox::questionYesNo(
                    this,
                    i18n("Playlist '%1' has changed. Save to disk?", pl->name()),
                    QString(),                      // Caption
                    KStandardGuiItem::save(),       // Yes
                    KStandardGuiItem::dontSave()    // No
                );
            } else {
                retval = KMessageBox::Yes;
            }

            if (retval == KMessageBox::Yes) {
                bool bSaved = pl->saveFile(pl->fileName(), bDialogOk);

                if (!bSaved && bDialogOk) {
                    /* we get here if playlist is newly created (no filename),
                     * or if the specified .m3u file is read-only. If this 2nd 
                     * write attempt also fails, just continue. The user may
                     * have pressed Cancel on the SaveAs Dialog.
                     */
                    pl->saveAs();
                }
            }
        }
    }
}

void PlaylistBox::slotShowDropTarget()
{
    if(!m_dropItem) {
        kError() << "Trying to show the playlist of a null item!\n";
        return;
    }

    raise(m_dropItem->playlist());
}

void PlaylistBox::slotAddItem(const QString &tag, unsigned column)
{
    for(QList<ViewMode *>::Iterator it = m_viewModes.begin(); it != m_viewModes.end(); ++it)
        (*it)->addItems(QStringList(tag), column);
}

void PlaylistBox::slotRemoveItem(const QString &tag, unsigned column)
{
    for(QList<ViewMode *>::Iterator it = m_viewModes.begin(); it != m_viewModes.end(); ++it)
        (*it)->removeItem(tag, column);
}

/**
 * Handle the "Drop" part of Drag/Drop.
 *
 * @param s     a list of "file:" URL(s) in mime format. if s is null, this
 *              method does nothing.
 * @param item  the target item that was dropped onto. if item or 
 *              item->playlist() is null, this method does nothing.
 */
void PlaylistBox::decode(const QMimeData *s, Item *item)
{
    if(!s) {
        kError() << "QMimeData is null";
        return;
    }

    if(!item) {
        kDebug() << "item is null";
        return;
    }
     
    Playlist *pl = item->playlist();
    if(!pl) {
        kDebug() << "no playlist specified";
        return;
    }

    if (!pl->canModifyContent()) {
        // it's a bug if this happens
        kError() << "Attempt to drop on read-only target";
        return;
    }

    if(!pl->isContentMutable()) {
        // it's a bug if this happens
        kError() << "Attempt to drop on read-only target";
        return;
    }

    const KUrl::List urls = KUrl::List::fromMimeData(s);

    if(urls.isEmpty()) {
        kError() << "KUrl list is empty";
        return;
    }

    QStringList files;
    foreach(const KUrl& url, urls) {
        files.append( url.path() );
    }

    TreeViewItemPlaylist *playlistItem;
    playlistItem = dynamic_cast<TreeViewItemPlaylist *>(pl);
    if(playlistItem) {
        playlistItem->retag(files, currentPlaylist());
        TagTransactionManager::instance()->commit();
        currentPlaylist()->update();
        return;
    }

    pl->addFiles(files);
}

void PlaylistBox::contentsDropEvent(QDropEvent *e)
{
    //m_showTimer->stop();

    Item *i = static_cast<Item *>(itemAt(contentsToViewport(e->pos())));
    decode(e->mimeData(), i);

    if(m_dropItem) {
        Item *old = m_dropItem;
        m_dropItem = 0;
        old->repaint();
    }
}

/**
 * During a drag/drop operation, this method is called periodically so the 
 * system can figure out if the cursor is over a valid drop target or
 * not. This method calls e->setAccepted() to provide that answer.
 */
void PlaylistBox::contentsDragMoveEvent(QDragMoveEvent *e)
{
    // If we can decode the input source, there is a non-null item at the "move"
    // position, the playlist for that Item is non-null, is not the
    // selected playlist and is not the CollectionList, then accept the event.
    //
    // Otherwise, do not accept the event.

    if (!KUrl::List::canDecode(e->mimeData())) {
        e->setAccepted(false);
        return;
    }

    // is set to null if not over any target
    Item *target = static_cast<Item *>(itemAt(contentsToViewport(e->pos())));

    if(target) {

        if(target->playlist() && !target->playlist()->canModifyContent()) {
            e->setAccepted(false);
            return;
        }

        if(target->playlist() && !target->playlist()->isContentMutable()) {
            e->setAccepted(false);
            return;
        }

        // This is a semi-dirty hack to check if the items are coming from within
        // JuK.  If they are not coming from a Playlist (or subclass) then the
        // dynamic_cast will fail and we can safely assume that the item is
        // coming from outside of JuK.

        if(dynamic_cast<Playlist *>(e->source())) {
            if(target->playlist() &&
               target->playlist() != CollectionList::instance() &&
               !target->isSelected())
            {
                e->setAccepted(true);
            }
            else
                e->setAccepted(false);
        }
        else // the dropped items are coming from outside of JuK
            e->setAccepted(true);

        if(m_dropItem != target) {
            Item *old = m_dropItem;
            //m_showTimer->stop();

            if(e->isAccepted()) {
                m_dropItem = target;
                target->repaint();
                //m_showTimer->setSingleShot(true);
                //m_showTimer->start(1500);
            }
            else
                m_dropItem = 0;

            if(old)
                old->repaint();
        }
    }
}

void PlaylistBox::contentsDragLeaveEvent(QDragLeaveEvent *e)
{
    if(m_dropItem) {
        Item *old = m_dropItem;
        m_dropItem = 0;
        old->repaint();
    }
    K3ListView::contentsDragLeaveEvent(e);
}

void PlaylistBox::contentsMousePressEvent(QMouseEvent *e)
{
    if(e->button() == Qt::LeftButton)
        m_doingMultiSelect = true;
    K3ListView::contentsMousePressEvent(e);
}

void PlaylistBox::contentsMouseReleaseEvent(QMouseEvent *e)
{
    if(e->button() == Qt::LeftButton) {
        m_doingMultiSelect = false;
        slotPlaylistChanged();
    }
    K3ListView::contentsMouseReleaseEvent(e);
}

void PlaylistBox::keyPressEvent(QKeyEvent *e)
{
    if((e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) && e->modifiers() == Qt::ShiftButton)
        m_doingMultiSelect = true;
    K3ListView::keyPressEvent(e);
}

void PlaylistBox::keyReleaseEvent(QKeyEvent *e)
{
    if(m_doingMultiSelect && e->key() == Qt::Key_Shift) {
        m_doingMultiSelect = false;
        slotPlaylistChanged();
    }
    K3ListView::keyReleaseEvent(e);
}

PlaylistBox::ItemList PlaylistBox::selectedBoxItems() const
{
    ItemList l;

    for(Q3ListViewItemIterator it(const_cast<PlaylistBox *>(this),
                                 Q3ListViewItemIterator::Selected); it.current(); ++it)
        l.append(static_cast<Item *>(*it));

    return l;
}

void PlaylistBox::setSingleItem(Q3ListViewItem *item)
{
    setSelectionModeExt(Single);
    K3ListView::setCurrentItem(item);
    setSelectionModeExt(Extended);
}

////////////////////////////////////////////////////////////////////////////////
// PlaylistBox private slots
////////////////////////////////////////////////////////////////////////////////

void PlaylistBox::slotPlaylistChanged()
{
    // Don't update while the mouse is pressed down.

    if(m_doingMultiSelect)
        return;

    ItemList items = selectedBoxItems();
    m_hasSelection = !items.isEmpty();

    /* set the enable/disable state of the menu items */

    bool bCanReload = true;
    bool bCanDelete = true;     /* the .m3u playlist */
    bool bCanRename = true;
    bool bCanModifyContent = true;
    bool bIsContentMutable = true;
    bool bFileListChanged  = true;
    bool bCanEditSearch    = true;

    PlaylistList playlists;

    /* for multi-selection, all selected items must allow the operation
     * for the Menu item to get enabled.
     */
    for(ItemList::ConstIterator it = items.constBegin(); it != items.constEnd(); ++it) {

        Playlist *p = (*it)->playlist();
        if(p) {
            // the canXYZ() methods are class policy, not mutable state
            bool isNormal = p->canDelete() && p->canRename() && p->canModifyContent();
            if(!p->canReload()) {
                bCanReload = false;
            } else if(isNormal && p->fileName().isEmpty()) {
                bCanReload = false;
            }
            if(!p->canDelete()) {
                bCanDelete = false;
            }
            if(!p->canRename()) {
                bCanRename = false;
            }
            if(!p->canModifyContent()) {
                bCanModifyContent = false;
            }
            if(!p->isContentMutable()) {
                bIsContentMutable = false;
            }
            if(!p->hasFileListChanged()) {
                bFileListChanged = false;
            }
            if(!p->canEditSearchPattern()) {
                bCanEditSearch = false;
            }
            playlists.append(p);
        }
    }

    // policy: can not delete a playlist with read-only .m3u
    bCanDelete = bCanDelete && bIsContentMutable;

    bool bCanSave = bCanModifyContent && bFileListChanged;

    bool bCanDuplicate = false;
    bool bCanExport = false;
    bool bCanImport = false;
    int selectCnt = playlists.count();

    if (selectCnt == 0) {
        bCanReload = false;
        bCanDelete = false;
        bCanRename = false;
        bCanEditSearch = false;
        bCanSave = false;
    } else if (selectCnt == 1) {
        bCanDuplicate = playlists.front()->count() > 0;
        bCanExport = bCanDuplicate;
        bCanImport = bCanModifyContent && bIsContentMutable;
    } else if (selectCnt > 1) {
        bCanRename = false;
        bCanEditSearch = false;
    }

    // File menu
    action("file_open"         )->setEnabled(bCanImport);
    action("file_save"         )->setEnabled(bCanSave);
    action("file_save_as"      )->setEnabled(bCanExport);
    action("renamePlaylist"    )->setEnabled(bCanRename);
    action("deleteItemPlaylist")->setEnabled(bCanDelete);
    action("reloadPlaylist"    )->setEnabled(bCanReload);
    action("duplicatePlaylist" )->setEnabled(bCanDuplicate);
    action("editSearch"        )->setEnabled(bCanEditSearch);

    // Edit menu
    action("edit_copy"         )->setEnabled(bCanDuplicate); // copy playlist
    action("edit_cut"          )->setEnabled(bCanDelete); // delete playlist
    action("edit_paste"        )->setEnabled(bCanImport); // paste track(s)

    if(m_k3bAction)
        m_k3bAction->setEnabled(selectCnt > 0);

    if(selectCnt == 1) {
        PlaylistCollection::raise(playlists.front());
    }
    else if(selectCnt > 1)
        createDynamicPlaylist(playlists);
}

void PlaylistBox::slotDoubleClicked(Q3ListViewItem *item)
{
    if(!item)
        return;

    TrackSequenceManager *manager = TrackSequenceManager::instance();
    Item *playlistItem = static_cast<Item *>(item);

    manager->setCurrentPlaylist(playlistItem->playlist());

    manager->setCurrent(0); // Reset playback
    PlaylistItem *next = manager->nextItem(); // Allow manager to choose

    if(next) {
        emit startFilePlayback(next->file());
        playlistItem->playlist()->setPlaying(next);
    }
    else
        action("stop")->trigger();
}

void PlaylistBox::slotShowContextMenu(Q3ListViewItem *, const QPoint &point, int)
{
    m_contextMenu->popup(point);
}

void PlaylistBox::slotPlaylistItemsDropped(Playlist *p)
{
    raise(p);
}

void PlaylistBox::slotSetViewMode(int index)
{
    if(index == m_viewModeIndex)
        return;

    viewMode()->setShown(false);
    m_viewModeIndex = index;
    viewMode()->setShown(true);
}

void PlaylistBox::setupItem(Item *item)
{
    m_playlistDict.insert(item->playlist(), item);
    viewMode()->queueRefresh();
}

void PlaylistBox::setupUpcomingPlaylist()
{
    KConfigGroup config(KGlobal::config(), "Playlists");
    bool enable = config.readEntry("showUpcoming", false);

    setUpcomingPlaylistEnabled(enable);
    action<KToggleAction>("showUpcoming")->setChecked(enable);
}


void PlaylistBox::slotLoadCachedPlaylists()
{
    kDebug() << "Loading cached playlists.";
    QTime stopwatch;
    stopwatch.start();

    Cache::loadPlaylists(this);

    kDebug() << "Cached playlists loaded, took" << stopwatch.elapsed() << "ms";

    // Auto-save playlists after they change.
    m_savePlaylistTimer = new QTimer(this);
    m_savePlaylistTimer->setInterval(3000); // 3 seconds with no change? -> commit
    m_savePlaylistTimer->setSingleShot(true);
    connect(m_savePlaylistTimer, SIGNAL(timeout()), SLOT(slotSavePlaylistsToCache()));

    clearSelection();
    setSelected(m_playlistDict[CollectionList::instance()], true);

    QTimer::singleShot(0, CollectionList::instance(), SLOT(slotCheckCache()));
    QTimer::singleShot(0, object(), SLOT(slotScanFolders()));
}

////////////////////////////////////////////////////////////////////////////////
// PlaylistBox::Item protected methods
////////////////////////////////////////////////////////////////////////////////

PlaylistBox::Item *PlaylistBox::Item::m_collectionItem = 0;

PlaylistBox::Item::Item(PlaylistBox *listBox, const QString &icon, const QString &text, Playlist *l)
    : QObject(listBox), K3ListViewItem(listBox, 0, text),
      PlaylistObserver(l),
      m_playlist(l), m_text(text), m_iconName(icon), m_sortedFirst(false)
{
    init();
}

PlaylistBox::Item::Item(Item *parent, const QString &icon, const QString &text, Playlist *l)
    : QObject(parent->listView()), K3ListViewItem(parent, text),
    PlaylistObserver(l),
    m_playlist(l), m_text(text), m_iconName(icon), m_sortedFirst(false)
{
    init();
}

PlaylistBox::Item::~Item()
{

}

int PlaylistBox::Item::compare(Q3ListViewItem *i, int col, bool) const
{
    Item *otherItem = static_cast<Item *>(i);
    PlaylistBox *playlistBox = static_cast<PlaylistBox *>(listView());

    if(m_playlist == playlistBox->upcomingPlaylist() && otherItem->m_playlist != CollectionList::instance())
        return -1;
    if(otherItem->m_playlist == playlistBox->upcomingPlaylist() && m_playlist != CollectionList::instance())
        return 1;

    if(m_sortedFirst && !otherItem->m_sortedFirst)
        return -1;
    else if(otherItem->m_sortedFirst && !m_sortedFirst)
        return 1;

    return text(col).toLower().localeAwareCompare(i->text(col).toLower());
}

void PlaylistBox::Item::paintCell(QPainter *painter, const QColorGroup &colorGroup, int column, int width, int align)
{
    PlaylistBox *playlistBox = static_cast<PlaylistBox *>(listView());
    playlistBox->viewMode()->paintCell(this, painter, colorGroup, column, width, align);
}

void PlaylistBox::Item::setText(int column, const QString &text)
{
    m_text = text;
    K3ListViewItem::setText(column, text);
}

void PlaylistBox::Item::setup()
{
    listView()->viewMode()->setupItem(this);
}

////////////////////////////////////////////////////////////////////////////////
// PlaylistBox::Item protected slots
////////////////////////////////////////////////////////////////////////////////

void PlaylistBox::Item::slotSetName(const QString &name)
{
    if(listView()) {
        setText(0, name);
        setSelected(true);

        listView()->sort();
        listView()->ensureItemVisible(listView()->currentItem());
        listView()->viewMode()->queueRefresh();
    }
}

void PlaylistBox::Item::updateCurrent()
{
}

void PlaylistBox::Item::updateData()
{
    listView()->slotPlaylistDataChanged();
}

////////////////////////////////////////////////////////////////////////////////
// PlaylistBox::Item private methods
////////////////////////////////////////////////////////////////////////////////

void PlaylistBox::Item::init()
{
    PlaylistBox *list = listView();

    list->setupItem(this);

    int iconSize = list->viewModeIndex() == 0 ? 32 : 16;
    setPixmap(0, SmallIcon(m_iconName, iconSize));
    list->addNameToDict(m_text);

    if(m_playlist) {
        connect(m_playlist, SIGNAL(signalNameChanged(QString)),
                this, SLOT(slotSetName(QString)));
        connect(m_playlist, SIGNAL(signalEnableDirWatch(bool)),
                list->object(), SLOT(slotEnableDirWatch(bool)));
    }

    if(m_playlist == CollectionList::instance()) {
        m_sortedFirst = true;
        m_collectionItem = this;
        list->viewMode()->setupDynamicPlaylists();
    }

    if(m_playlist == list->historyPlaylist() || m_playlist == list->upcomingPlaylist())
        m_sortedFirst = true;
}

#include "playlistbox.moc"

// vim: set et sw=4 tw=0 sta:
