/***************************************************************************
                          juk.cpp  -  description
                             -------------------
    begin                : Mon Feb  4 23:40:41 EST 2002
    copyright            : (C) 2002 by Scott Wheeler
    email                : wheeler@kde.org
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <config.h>

#include <kcmdlineargs.h>
#include <kstatusbar.h>
#include <kdebug.h>
#include <kmessagebox.h>
#include <kiconloader.h>
#include <kapplication.h>

#include <qslider.h>

#include "juk.h"
#include "slideraction.h"
#include "statuslabel.h"
#include "splashscreen.h"
#include "systemtray.h"
#include "keydialog.h"
#include "tagguesserconfigdlg.h"
#include "filerenamerconfigdlg.h"
#include "actioncollection.h"
#include "cache.h"
#include "playlistsplitter.h"
#include "collectionlist.h"

using namespace ActionCollection;

////////////////////////////////////////////////////////////////////////////////
// public members
////////////////////////////////////////////////////////////////////////////////

JuK::JuK(QWidget *parent, const char *name) :
    KMainWindow(parent, name, WDestructiveClose),
    m_player(PlayerManager::instance()),
    m_shuttingDown(false)
{
    // Expect segfaults if you change this order.

    readSettings();

    if(m_showSplash && !m_startDocked) {
	SplashScreen::instance()->show();
	kapp->processEvents();
    }

    setupActions();
    setupLayout();
    createGUI();
    readConfig();
    setupSystemTray();
    setupGlobalAccels();
    processArgs();

    SplashScreen::finishedLoading();
    QTimer::singleShot(0, CollectionList::instance(), SLOT(slotCheckCache()));

    m_sliderAction->slotUpdateOrientation();
}

JuK::~JuK()
{

}

KActionCollection *JuK::actionCollection() const
{
    return ActionCollection::actions();
}

////////////////////////////////////////////////////////////////////////////////
// private members
////////////////////////////////////////////////////////////////////////////////

void JuK::setupLayout()
{
    m_statusLabel = new StatusLabel(statusBar());
    statusBar()->addWidget(m_statusLabel, 1);

    m_splitter = new PlaylistSplitter(this, "playlistSplitter");
    setCentralWidget(m_splitter);

    PlayerManager::instance()->setStatusLabel(m_statusLabel);

    m_splitter->setFocus();
    resize(750, 500);
}

void JuK::setupActions()
{
    KStdAction::quit(this, SLOT(slotQuit()), actions());
    KStdAction::cut(kapp,   SLOT(cut()),   actions());
    KStdAction::copy(kapp,  SLOT(copy()),  actions());
    KStdAction::paste(kapp, SLOT(paste()), actions());
    KStdAction::clear(kapp, SLOT(clear()), actions());
    KStdAction::selectAll(kapp, SLOT(selectAll()), actions());

    m_showHistoryAction = new KToggleAction(i18n("Show &History"),    "history",  0, actions(), "showHistory");
    m_randomPlayAction =  new KToggleAction(i18n("&Random Play"),                 0, actions(), "randomPlay");

    m_showHistoryAction->setCheckedState(i18n("Hide &History"));

    new KAction(i18n("&Play"),  "player_play",  0, m_player, SLOT(play()),  actions(), "play");
    new KAction(i18n("P&ause"), "player_pause", 0, m_player, SLOT(pause()), actions(), "pause");
    new KAction(i18n("&Stop"),  "player_stop",  0, m_player, SLOT(stop()),  actions(), "stop");

    // m_backAction = new KToolBarPopupAction(i18n("Previous &Track"), "player_start", 0,
    //                                        this, SLOT(back()), actions(), "back");
    // TODO: switch this back to being a popup action

    new KAction(i18n("Previous &Track"), "player_start", 0, m_player, SLOT(back()),    actions(), "back");
    new KAction(i18n("&Next Track"),     "player_end",   0, m_player, SLOT(forward()), actions(), "forward");

    new KToggleAction(i18n("&Loop Playlist"), 0, 0, actions(), "loopPlaylist");

    // the following are not visible by default

    new KAction(i18n("Mute"),         "mute",        0, m_player, SLOT(mute()),        actions(), "mute");
    new KAction(i18n("Volume Up"),    "volumeUp",    0, m_player, SLOT(volumeUp()),    actions(), "volumeUp");
    new KAction(i18n("Volume Down"),  "volumeDown",  0, m_player, SLOT(volumeDown()),  actions(), "volumeDown");
    new KAction(i18n("Play / Pause"), "playPause",   0, m_player, SLOT(playPause()),   actions(), "playPause");
    new KAction(i18n("Seek Forward"), "seekForward", 0, m_player, SLOT(seekForward()), actions(), "seekForward");
    new KAction(i18n("Seek Back"),    "seekBack",    0, m_player, SLOT(seekBack()),    actions(), "seekBack");


    //////////////////////////////////////////////////
    // settings menu
    //////////////////////////////////////////////////

    setStandardToolBarMenuEnabled(true);

    m_toggleSplashAction =
	new KToggleAction(i18n("Show Splash Screen on Startup"),
			  KShortcut(), actions(), "showSplashScreen");
    m_toggleSplashAction->setCheckedState(i18n("Hide Splash Screen on Startup"));
    m_toggleSystemTrayAction =
	new KToggleAction(i18n("&Dock in System Tray"),
			  KShortcut(), actions(), "toggleSystemTray");
    m_toggleDockOnCloseAction =
	new KToggleAction(i18n("&Stay in System Tray on Close"),
			  KShortcut(), actions(), "dockOnClose");
    m_togglePopupsAction =
	new KToggleAction(i18n("Popup &Track Announcement"),
			  KShortcut(), this, 0, actions(), "togglePopups");

    connect(m_toggleSystemTrayAction, SIGNAL(toggled(bool)),
	    this, SLOT(slotToggleSystemTray(bool)));


    m_outputSelectAction = PlayerManager::playerSelectAction(actions());

    if(m_outputSelectAction)
        m_outputSelectAction->setCurrentItem(0);

    new KAction(i18n("&Tag Guesser..."), 0, 0, this, SLOT(slotConfigureTagGuesser()),
		actions(), "tagGuesserConfig");

    new KAction(i18n("&File Renamer..."), 0, 0, this, SLOT(slotConfigureFileRenamer()),
		actions(), "fileRenamerConfig");

    KStdAction::keyBindings(this, SLOT(slotEditKeys()), actions());

    //////////////////////////////////////////////////
    // just in the toolbar
    //////////////////////////////////////////////////

    m_sliderAction = new SliderAction(i18n("Track Position"), actions(),
				      "trackPositionAction");
}

void JuK::setupSystemTray()
{
    if(m_toggleSystemTrayAction && m_toggleSystemTrayAction->isChecked()) {
	m_systemTray = new SystemTray(this, "systemTray");
	m_systemTray->show();

	m_toggleDockOnCloseAction->setEnabled(true);
	m_togglePopupsAction->setEnabled(true);

	connect(m_systemTray, SIGNAL(quitSelected()), this, SLOT(slotQuit()));
    }
    else {
	m_systemTray = 0;
	m_toggleDockOnCloseAction->setEnabled(false);
	m_togglePopupsAction->setEnabled(false);
    }
}

void JuK::setupGlobalAccels()
{
    m_accel = new KGlobalAccel(this);

    KeyDialog::insert(m_accel, "PlayPause",   i18n("Play / Pause"), action("playPause"),   SLOT(activate()));
    KeyDialog::insert(m_accel, "Stop",        i18n("Stop Playing"), action("stop"),        SLOT(activate()));
    KeyDialog::insert(m_accel, "Back",        i18n("Back"),         action("back"),        SLOT(activate()));
    KeyDialog::insert(m_accel, "Forward",     i18n("Forward"),      action("forward"),     SLOT(activate()));
    KeyDialog::insert(m_accel, "SeekBack",    i18n("Seek Back"),    action("seekBack"),    SLOT(activate()));
    KeyDialog::insert(m_accel, "SeekForward", i18n("Seek Forward"), action("seekForward"), SLOT(activate()));
    KeyDialog::insert(m_accel, "VolumeUp",    i18n("Volume Up"),    action("volumeUp"),    SLOT(activate()));
    KeyDialog::insert(m_accel, "VolumeDown",  i18n("Volume Down"),  action("volumeDown"),  SLOT(activate()));
    KeyDialog::insert(m_accel, "Mute",        i18n("Mute"),         action("mute"),        SLOT(activate()));
    KeyDialog::insert(m_accel, "ShowHide",    i18n("Show / Hide"),  this,                  SLOT(slotShowHide()));

    m_accel->setConfigGroup("Shortcuts");
    m_accel->readSettings();
    m_accel->updateConnections();
}

void JuK::processArgs()
{
    KCmdLineArgs *args = KCmdLineArgs::parsedArgs();
    QStringList files;

    for(int i = 0; i < args->count(); i++)
        files.append(args->arg(i));

    // TODO: make this work again
    // m_splitter->open(files);
}

void JuK::keyPressEvent(QKeyEvent *e)
{
    if (e->key() >= Qt::Key_Back && e->key() <= Qt::Key_MediaLast)
        e->accept();
    KMainWindow::keyPressEvent(e);
}

/**
 * These are settings that need to be know before setting up the GUI.
 */

void JuK::readSettings()
{
    KConfig *config = KGlobal::config();
    { // general settings
        KConfigGroupSaver saver(config, "Settings");
        m_showSplash = config->readBoolEntry("ShowSplashScreen", true);
        m_startDocked = config->readBoolEntry("StartDocked", false);
    }
}

void JuK::readConfig()
{
    // Automagically save and m_restore many window settings.
    setAutoSaveSettings();

    KConfig *config = KGlobal::config();
    { // player settings
        KConfigGroupSaver saver(config, "Player");
        if(m_sliderAction->volumeSlider()) {
	    int maxVolume = m_sliderAction->volumeSlider()->maxValue();
	    int volume = config->readNumEntry("Volume", maxVolume);
            m_sliderAction->volumeSlider()->setVolume(volume);
        }
        bool randomPlay = config->readBoolEntry("RandomPlay", false);
        m_randomPlayAction->setChecked(randomPlay);

        bool loopPlaylist = config->readBoolEntry("LoopPlaylist", false);
        ActionCollection::action<KToggleAction>("loopPlaylist")->setChecked(loopPlaylist);
    }
    { // view settings
        KConfigGroupSaver saver(config, "View");

	// The history list will actually be created by the playlist restoration
	// code, but we want to remember the checkbox's setting and hope that
	// it's in synch with the code that does the real work.

	bool showHistory = config->readBoolEntry("ShowHistory", false);
	m_showHistoryAction->setChecked(showHistory);
    }
    { // general settings
        KConfigGroupSaver saver(config, "Settings");

        bool dockInSystemTray = config->readBoolEntry("DockInSystemTray", true);
        m_toggleSystemTrayAction->setChecked(dockInSystemTray);

        bool dockOnClose = config->readBoolEntry("DockOnClose", true);
        m_toggleDockOnCloseAction->setChecked(dockOnClose);

        bool showPopups = config->readBoolEntry("TrackPopup", false);
        m_togglePopupsAction->setChecked(showPopups);

        if(m_outputSelectAction)
            m_outputSelectAction->setCurrentItem(config->readNumEntry("MediaSystem", 0));
    }

    m_toggleSplashAction->setChecked(m_showSplash);
}

void JuK::saveConfig()
{
    KConfig *config = KGlobal::config();
    { // player settings
        KConfigGroupSaver saver(config, "Player");
	config->writeEntry("Volume", m_sliderAction->volumeSlider()->volume());
	config->writeEntry("RandomPlay", m_randomPlayAction->isChecked());

	KToggleAction *a = ActionCollection::action<KToggleAction>("loopPlaylist");
	config->writeEntry("LoopPlaylist", a->isChecked());
    }
    { // view settings
        KConfigGroupSaver saver(config, "View");
	config->writeEntry("ShowHistory", m_showHistoryAction->isChecked());
    }
    { // general settings
        KConfigGroupSaver saver(config, "Settings");
        config->writeEntry("ShowSplashScreen", m_toggleSplashAction->isChecked());
        config->writeEntry("StartDocked", m_startDocked);
        config->writeEntry("DockInSystemTray", m_toggleSystemTrayAction->isChecked());
        config->writeEntry("DockOnClose", m_toggleDockOnCloseAction->isChecked());
        config->writeEntry("TrackPopup", m_togglePopupsAction->isChecked());
        if(m_outputSelectAction)
            config->writeEntry("MediaSystem", m_outputSelectAction->currentItem());
    }
    config->sync();
}

bool JuK::queryExit()
{
    m_startDocked = !isVisible();

    hide();
    action("stop")->activate();
    delete m_systemTray;
    m_systemTray = 0;

    Cache::instance()->save();
    saveConfig();
    delete m_splitter;
    return true;
}

bool JuK::queryClose()
{
    if(!m_shuttingDown &&
       !kapp->sessionSaving() &&
       m_systemTray &&
       m_toggleDockOnCloseAction->isChecked())
    {
	KMessageBox::information(this,
	    i18n("<qt>Closing the main window will keep JuK running in the system tray. "
		 "Use Quit from the File menu to quit the application.</qt>"),
	    i18n("Docking in System Tray"), "hideOnCloseInfo");
	hide();
	return false;
    }
    else
	return true;
}

////////////////////////////////////////////////////////////////////////////////
// private slot definitions
////////////////////////////////////////////////////////////////////////////////

void JuK::slotShowHide()
{
    setShown(!isShown());
}

void JuK::slotQuit()
{
    kapp->quit();
}

////////////////////////////////////////////////////////////////////////////////
// settings menu
////////////////////////////////////////////////////////////////////////////////

void JuK::slotToggleSystemTray(bool enabled)
{
    if(enabled && !m_systemTray)
	setupSystemTray();
    else if(!enabled && m_systemTray) {
	delete m_systemTray;
	m_systemTray = 0;
	m_toggleDockOnCloseAction->setEnabled(false);
	m_togglePopupsAction->setEnabled(false);
    }
}

void JuK::slotEditKeys()
{
    KeyDialog::configure(m_accel, actions(), this);
}

void JuK::slotConfigureTagGuesser()
{
    TagGuesserConfigDlg(this).exec();
}

void JuK::slotConfigureFileRenamer()
{
    FileRenamerConfigDlg(this).exec();
}

#include "juk.moc"
