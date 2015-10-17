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

#include "normalplaylist.h"
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
    m_viewModeIndex(0),
    m_dropItem(0),
    m_showTimer(0)
{
    new PlaylistCollection(player, playlistStack, this),

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

    CollectionList::initialize(PlaylistCollection::instance());

    viewModeAction->setCurrentItem(m_viewModeIndex);
    m_viewModes[m_viewModeIndex]->setShown(true);

    //TrackSequenceManager::instance()->setCurrentPlaylist(CollectionList::instance());
    //this->raise2(CollectionList::instance());

    connect(viewModeAction, SIGNAL(triggered(int)), this, SLOT(slotSetViewMode(int)));

    connect(this, SIGNAL(selectionChanged()),
            this, SLOT(slotSelectionChanged()));

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

    this->installEventFilter(this);

    // hook up to the D-Bus
    (void) new DBusCollectionProxy(this, PlaylistCollection::instance());
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

    // write playlists cache file
    delete PlaylistCollection::instance();
}

/* If icon exists for playlist, select it in this widget. This method is
 * called after a new playlist is created or existing playlist is duplicated
 * to select the new object & update the qstackwidget.
 */
void PlaylistBox::raise2(Playlist *playlist)
{
    if(!playlist)
        return;

    if(m_selectedList.count() == 1 && m_selectedList.front()->playlist() == playlist) {
        kDebug() << "bailing out, already selected";
        return;
    }

    Item *i = m_playlistDict.value(playlist, 0);

    if(i) {
        // issues signal, which calls slotSelectionChanged()
        setSingleItem(i);
        ensureItemVisible(currentItem());
    }
    else {
        // got a non-icon dynamic playlist
        PlaylistCollection::instance()->raise3(playlist);
        slotSelectionChanged();
    }
}

void PlaylistBox::duplicate()
{
    Item *item = static_cast<Item *>(currentItem());
    if(!item || !item->playlist())
        return;

    QString name = PlaylistCollection::instance()->playlistNameDialog(i18nc("verb, copy the playlist", "Duplicate"), item->text(0));

    if(name.isNull())
        return;

    //FIXME: duplicate only works for Normal playlist
    Playlist *p = new NormalPlaylist(PlaylistCollection::instance(), name);
    p->createItems(item->playlist()->items());
}

void PlaylistBox::slotScanFolders()
{
    kDebug() << "Starting folder scan";
    QTime stopwatch; stopwatch.start();

    PlaylistCollection::instance()->scanFolders();

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

/**
 * Try to restore the playlist selection from the last time app was run. If the 
 * playlist name no longer exists, do nothing.
 */
void PlaylistBox::restorePrevSelection()
{
    KConfigGroup config(KGlobal::config(), "PlaylistBox");
    QString lastName = config.readEntry("LastSelect", "");
    if (!lastName.isEmpty()) {
        Q3ListViewItem *item = this->findItem(lastName, /*col*/ 0);
        if (item) {
            this->clearSelection();
            this->setSelected(item, true);
        }
    }
}

QList<Playlist*> PlaylistBox::getAllPlaylists() const
{
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

/* slot for edit_paste */
void PlaylistBox::paste()
{
    Item *i = static_cast<Item *>(currentItem());
    decode(QApplication::clipboard()->mimeData(), i);
}

/* slot for edit_clear */
void PlaylistBox::clear()
{
    // do nothing
}

/* slot for edit_select_all */
void PlaylistBox::selectAll()
{
    K3ListView::selectAll(true);
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
    PlaylistCollection::instance()->setHistoryPlaylistEnabled(enable);
}

void PlaylistBox::setupPlaylist3(Playlist *playlist, const QString &iconName, Item *parentItem)
{
    //connect(playlist, SIGNAL(signalPlaylistItemsDropped(Playlist*)),
    //        SLOT(slotPlaylistItemsDropped(Playlist*)));

    PlaylistCollection::instance()->setupPlaylist2(playlist, iconName);

    if(parentItem)
        new Item(parentItem, iconName, playlist->name(), playlist);
    else
        new Item(this, iconName, playlist->name(), playlist);
}

void PlaylistBox::removePlaylist(Playlist *playlist)
{
    // Could be false if setup() wasn't run yet.
    if(m_playlistDict.contains(playlist)) {
        PlaylistCollection::instance()->removeNameFromDict(m_playlistDict[playlist]->text(0));
        delete m_playlistDict[playlist]; // Delete the Item*
    }

    PlaylistCollection::instance()->removeFileFromDict(playlist->fileName());
    m_playlistDict.remove(playlist);
}

bool PlaylistBox::eventFilter(QObject *watched, QEvent *e) {
    bool rv = K3ListView::eventFilter(watched, e);

    if (e->type() == QEvent::FocusIn) {
        //kDebug() << "FocusIn";
        slotUpdateMenus();
    }

    return rv;
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

    // remember the name of the selected icon, force to "" if none
    QList<PlaylistBox::Item *> items = this->selectedBoxItems();
    QString itemName;
    if (!items.isEmpty()) {
        PlaylistBox::Item *item = dynamic_cast<PlaylistBox::Item *>(items.front());
        if (item && item->playlist()) {
            itemName = item->playlist()->name();
        }
    }
    config.writeEntry("LastSelect", itemName);

    KGlobal::config()->sync();
}

/**
 * This is called by the 'File|Remove Playlists...' menu item. Remove selected 
 * playlists where PolicyCanDelete & isContentMutable() is true.
 * Prompt the user whether to remove the .m3u file from disk too. If it happens that 
 * there are no disk files, then prompt with the playlist names which will be deleted.
 * If user selects Cancel, then delete neither the playlist objects or the disk files.
 */
void PlaylistBox::remove()
{
    QList<Item*> items = selectedBoxItems();

    QStringList files;
    QStringList names;
    PlaylistList removeQueue;

    foreach(Item *item, items) {
        if(item && item->playlist()) {
            Playlist *pl = item->playlist();
            if (pl->getPolicy(Playlist::PolicyCanDelete) && pl->isContentMutable()) {
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

/* Write any modified User playlists to disk using their m3u filename, but
 * only do playlists which have PolicyPromptToSave set to true (others
 * are saved in the cache.) If bDialogOk allows, prompt for 
 * whether or not to save; for a newly-created playlist, prompt for 
 * filename.
 *
 * If dialogs are suppressed, then our policy is to assume a "Yes" 
 * response, and not report any errors..
 */
void PlaylistBox::savePlaylistsToDisk(bool bDialogOk)
{
    Playlist *pl;
    for(Q3ListViewItem *i = this->firstChild(); i; i = i->nextSibling()) {
        Item *item = static_cast<Item *>(i);
        pl = item->playlist();
        if(pl && pl->getPolicy(Playlist::PolicyPromptToSave) && 
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

    this->raise2(m_dropItem->playlist());
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

    if (!pl->getPolicy(Playlist::PolicyCanModifyContent)) {
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
        Playlist *pl2 = PlaylistCollection::instance()->currentPlaylist();
        playlistItem->retag(files, pl2);
        TagTransactionManager::instance()->commit();
        pl2->update();
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

        Playlist *pl = target->playlist();
        if(pl && !pl->getPolicy(Playlist::PolicyCanModifyContent)) {
            e->setAccepted(false);
            return;
        }

        if(pl && !pl->isContentMutable()) {
            e->setAccepted(false);
            return;
        }

        // This is a semi-dirty hack to check if the items are coming from within
        // JuK.  If they are not coming from a Playlist (or subclass) then the
        // dynamic_cast will fail and we can safely assume that the item is
        // coming from outside of JuK.

        if(dynamic_cast<Playlist *>(e->source())) {
            if(pl &&
               pl != CollectionList::instance() &&
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

/* Q3ListBox items, in top-to-bottom sequence */
QList<PlaylistBox::Item*> PlaylistBox::getQ3SelectedItems() const
{
    QList<Item*> l;

    for(Q3ListViewItemIterator it(const_cast<PlaylistBox *>(this),
                                 Q3ListViewItemIterator::Selected); it.current(); ++it)
        l.append(static_cast<Item *>(*it));

    return l;
}

/* return copy of our local user-ordered selection list */
QList<PlaylistBox::Item*> PlaylistBox::selectedBoxItems() const
{
    QList<Item*> l(m_selectedList);
    return l;
}

void PlaylistBox::setSingleItem(Q3ListViewItem *item)
{
    setSelectionModeExt(Single);
    //K3ListView::setCurrentItem(item);
    setSelected(item, true);
    setSelectionModeExt(Extended);
}

/* Count this widget's Playlist child items. Includes pre-defined ones like
 * "Artist" if tree mode is active.
 */
int PlaylistBox::countPlaylistInView()
{
    int sum = this->childCount();     // level 1 items
    if(m_viewModeIndex == 2) {
        /* assume only the first level 1 child has children, and assume all
         * items in level 1-3 are Playlists.
         */
        const Q3ListViewItem *levelOne = this->firstChild();
        if(levelOne) {
            sum += levelOne->childCount();      // level 2 items
            const Q3ListViewItem *levelTwo = levelOne->firstChild();
            while(levelTwo) {
                sum += levelTwo->childCount();      // level 3 items
                levelTwo = levelTwo->nextSibling();
            }
            levelOne = levelOne->nextSibling();
        }
    }
    return sum;
}

/* Update our internal version of selected playlists. This is how we support
 * a user-specified selection-order. This method is called when the K3ListView
 * reports the PlaylistBox selection changed. We need to figure out what
 * changed and update m_selectedList.
 */
void PlaylistBox::updateLocalSelectionList()
{
    int playlistInView = this->countPlaylistInView();
    QList<Item*> listA = this->getQ3SelectedItems();

    /* this first check matches frequently, so optimize for it. It's also
     * good just in case m_selectedList somehow gets out of sync with the
     * underlying K3ListView. If all are selected, then use top-to-bottom
     * ordering.
     */
    if(listA.count() < 2 || listA.count() == playlistInView) {
        m_selectedList = listA;
    } else {
        /* update m_selectedList the hard way */

        // identify items that got un-selected
        QList<Item*> deselectedItems;
        foreach(Item *item, m_selectedList) {
            if(!listA.contains(item)) {
                deselectedItems.append(item);
            }
        }
        // remove the un-selected items
        foreach(Item *item, deselectedItems) {
            m_selectedList.removeAll(item);
        }
        // identify items which are newly selected & append
        foreach(Item *item, listA) {
            if(!m_selectedList.contains(item)) {
                m_selectedList.append(item);
            }
        }
    }
    //kDebug() << "m_selectedList.count=" << m_selectedList.count();
}

////////////////////////////////////////////////////////////////////////////////
// PlaylistBox private slots
////////////////////////////////////////////////////////////////////////////////

void PlaylistBox::slotSelectionChanged()
{
    updateLocalSelectionList();

    QList<Item*> items = selectedBoxItems();

    /* set the enable/disable state of the menu items */

    bool bCanReload = true;
    bool bCanDelete = true;     /* the .m3u playlist */
    bool bCanRename = true;
    bool bCanModifyContent = true;
    bool bIsContentMutable = true;
    bool bFileListChanged  = true;

    PlaylistList playlists;

    /* for multi-selection, all selected items must allow the operation
     * for the Menu item to get enabled.
     */
    foreach(Item *it, items) {
        Playlist *p = it->playlist();
        if(p) {
            // the canXYZ() methods are class policy, not mutable state
            bool isNormal = p->getType() == Playlist::Type::Normal;
            if(!p->getPolicy(Playlist::PolicyCanReload)) {
                bCanReload = false;
            } else if(isNormal && p->fileName().isEmpty()) {
                bCanReload = false;
            }
            if(!p->getPolicy(Playlist::PolicyCanDelete)) {
                bCanDelete = false;
            }
            if(!p->getPolicy(Playlist::PolicyCanRename)) {
                bCanRename = false;
            }
            if(!p->getPolicy(Playlist::PolicyCanModifyContent)) {
                bCanModifyContent = false;
            }
            if(!p->isContentMutable()) {
                bIsContentMutable = false;
            }
            if(!p->hasFileListChanged()) {
                bFileListChanged = false;
            }
            playlists.append(p);
        }
    }

    // policy: can not delete a playlist with read-only .m3u
    bCanDelete = bCanDelete && bIsContentMutable;

    bool bCanSave = bCanModifyContent && bFileListChanged;

    bool bCanDuplicate = false;
    bool bCanEditSearch = false;
    bool bCanExport = false;
    bool bCanImport = false;
    int selectCnt = playlists.count();

    if (selectCnt == 0) {
        bCanReload = false;
        bCanDelete = false;
        bCanRename = false;
        bCanSave = false;
    } else if (selectCnt == 1) {
        bCanDuplicate = playlists.front()->count() > 0;
        bCanEditSearch = (playlists.front()->getType() == Playlist::Type::Search);;
        bCanExport = bCanDuplicate;
        bCanImport = bCanModifyContent && bIsContentMutable;
    } else if (selectCnt > 1) {
        bCanRename = false;
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
    slotUpdateMenus();

    if(m_k3bAction)
        m_k3bAction->setEnabled(selectCnt > 0);

    if(selectCnt == 1) {
        Playlist *pl = playlists.front();
        PlaylistCollection::instance()->raise3(pl);
    }
    else if(selectCnt > 1)
        PlaylistCollection::instance()->createDynamicPlaylist(playlists);
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

/**
 * Update menu items that depend on selection or focus. This method should 
 * be called when this widget gets focus or the selected items change.
 */
void PlaylistBox::slotUpdateMenus()
{
    QList<Item*> list = this->selectedBoxItems();
    int nRow = list.count();
    Playlist *pl = 0;
    if(nRow == 1) {
        Item *item = list.front();
        pl = item->playlist();
    }

    bool bEnablePaste = false;
    if (pl) {
        // determine read/write status for the selected playlist
        bEnablePaste = pl->getPolicy(Playlist::PolicyCanModifyContent) && pl->isContentMutable();
        if (bEnablePaste) {
            // looking for mime-type "text/uri-list"
            const QMimeData *mime = QApplication::clipboard()->mimeData();
            bEnablePaste = mime && mime->hasUrls();
        }
    }

    // Edit Menu

    QAction *act = ActionCollection::action("edit_undo");
    act->setEnabled(false);

    act = ActionCollection::action("edit_copy");
    act->setEnabled(false);

    // TODO: check if abs file name(s) on clipboard
    act = ActionCollection::action("edit_paste");
    act->setEnabled(bEnablePaste);

    act = ActionCollection::action("edit_clear");
    act->setEnabled(false);
}

void PlaylistBox::slotShowContextMenu(Q3ListViewItem *, const QPoint &point, int)
{
    m_contextMenu->popup(point);
}

/* This method is called when URLs are dropped on the track table (note: not
 * on this widget).
 *
 * The catch is, the track table must be visible for this to happen. So the
 * playlist icon is already selected. So this method is always unnecessary.
 */
void PlaylistBox::slotPlaylistItemsDropped(Playlist *p)
{
    //this->raise2(p);
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

    PlaylistCollection::instance()->setUpcomingPlaylistEnabled(enable);
    action<KToggleAction>("showUpcoming")->setChecked(enable);
}


void PlaylistBox::slotLoadCachedPlaylists()
{
    kDebug() << "Loading cached playlists.";
    QTime stopwatch;
    stopwatch.start();

    Cache::loadPlaylists(PlaylistCollection::instance());

    kDebug() << "Cached playlists loaded, took" << stopwatch.elapsed() << "ms";

    // Auto-save playlists after they change.
    m_savePlaylistTimer = new QTimer(this);
    m_savePlaylistTimer->setInterval(3000); // 3 seconds with no change? -> commit
    m_savePlaylistTimer->setSingleShot(true);
    connect(m_savePlaylistTimer, SIGNAL(timeout()), SLOT(slotSavePlaylistsToCache()));

    QTimer::singleShot(0, CollectionList::instance(), SLOT(slotCheckCache()));
    QTimer::singleShot(0, this, SLOT(slotScanFolders()));
}

////////////////////////////////////////////////////////////////////////////////
// PlaylistBox::Item protected methods
////////////////////////////////////////////////////////////////////////////////

PlaylistBox::Item *PlaylistBox::Item::m_collectionItem = 0;

PlaylistBox::Item::Item(PlaylistBox *listBox, const QString &icon, const QString &text, Playlist *l)
    : QObject(listBox), K3ListViewItem(listBox, 0, text),
      m_playlist(l), m_text(text), m_iconName(icon), m_sortedFirst(false)
{
    init();
}

PlaylistBox::Item::Item(Item *parent, const QString &icon, const QString &text, Playlist *l)
    : QObject(parent->listView()), K3ListViewItem(parent, text),
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

    PlaylistCollection *coll = PlaylistCollection::instance();
    if(m_playlist == coll->upcomingPlaylist() && otherItem->m_playlist != CollectionList::instance())
        return -1;
    if(otherItem->m_playlist == coll->upcomingPlaylist() && m_playlist != CollectionList::instance())
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

////////////////////////////////////////////////////////////////////////////////
// PlaylistBox::Item private methods
////////////////////////////////////////////////////////////////////////////////

void PlaylistBox::Item::init()
{
    PlaylistBox *list = listView();

    list->setupItem(this);

    int iconSize = list->viewModeIndex() == 0 ? 32 : 16;
    setPixmap(0, SmallIcon(m_iconName, iconSize));
    PlaylistCollection *coll = PlaylistCollection::instance();
    coll->addNameToDict(m_text);

    if(m_playlist) {
        connect(m_playlist, SIGNAL(signalNameChanged(QString)),
                this, SLOT(slotSetName(QString)));
        connect(m_playlist, SIGNAL(signalEnableDirWatch(bool)),
                coll->object(), SLOT(slotEnableDirWatch(bool)));
    }

    if(m_playlist == CollectionList::instance()) {
        m_sortedFirst = true;
        m_collectionItem = this;
        list->viewMode()->setupDynamicPlaylists();
    }

    if(m_playlist == coll->historyPlaylist() || m_playlist == coll->upcomingPlaylist())
        m_sortedFirst = true;
}

#include "playlistbox.moc"

// vim: set et sw=4 tw=0 sta:
