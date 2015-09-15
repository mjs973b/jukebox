/**
 * Copyright (c) 2009 Nikolaj Hald Nielsen <nhn@kde.org>
 * Copyright (c) 2009 Mark Kretschmann <kretschmann@kde.org>
 * Copyright (c) 2015 Mike Scheutzow <mjs973@gmail.com>
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

#include "volumepopupbutton.h"
#include "slider.h"

#include <kdebug.h>
#include <KLocale>
#include <KVBox>
#include <KIcon>

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QToolBar>
#include <QWheelEvent>
#include <QWidgetAction>

#include "playermanager.h"
//#include "juk.h"

VolumePopupButton::VolumePopupButton( QWidget * parent, PlayerManager *mgr ) :
    QToolButton( parent ),
    m_prevVolume(0.0),
    m_curVolume(0.0),
    player(mgr)
{
    //create the volume popup
    m_volumeMenu = new QMenu( this );

    KVBox *mainBox = new KVBox( this );

    m_volumeLabel= new QLabel( mainBox );
    m_volumeLabel->setAlignment( Qt::AlignHCenter );

    KHBox *sliderBox = new KHBox( mainBox );
    m_volumeSlider = new VolumeSlider( 100, sliderBox, false );
    m_volumeSlider->setFixedHeight( 170 );
    mainBox->setMargin( 0 );
    mainBox->setSpacing( 0 );
    sliderBox->setSpacing( 0 );
    sliderBox->setMargin( 0 );
    mainBox->setSizePolicy( QSizePolicy::MinimumExpanding, QSizePolicy::Fixed );
    sliderBox->setSizePolicy( QSizePolicy::MinimumExpanding, QSizePolicy::Fixed );

    QWidgetAction *sliderActionWidget = new QWidgetAction( this );
    sliderActionWidget->setDefaultWidget( mainBox );

    // volumeChanged is a customSignal, is not emited by setValue()
    connect( m_volumeSlider, SIGNAL(volumeChanged(float)), player, SLOT(setVolume(float)) );

    QToolBar *muteBar = new QToolBar( QString(), mainBox );
    muteBar->setContentsMargins( 0, 0, 0, 0 );
    muteBar->setIconSize( QSize( 16, 16 ) );

    // our popup's mute-toggle  button
    m_muteAction = new QAction( KIcon( "audio-volume-muted" ), QString(), muteBar );
    m_muteAction->setToolTip( i18n( "Mute/Unmute" ) );

    connect( m_muteAction, SIGNAL(triggered(bool)), this, SLOT(slotToggleMute(bool)) );
    connect( player, SIGNAL(mutedChanged(bool)), this, SLOT(slotMuteStateChanged(bool)) );

    m_volumeMenu->addAction( sliderActionWidget );
    muteBar->addAction( m_muteAction );

    /* set icon and label to match create state of AudioOutput, as the 
     * desired volume value is not available yet (because the player
     * object is not set up yet.) Someone must call PlayerManager::setVolume() 
     * later.
     */
    slotVolumeChanged( 1.0 );

    // let player notify us when volume changes
    connect( player, SIGNAL(volumeChanged(float)), this, SLOT(slotVolumeChanged(float)) );
}

/* update our widgets using current volume from PlayerManager. Generally
 * not necessary to call this.
 */
void VolumePopupButton::refresh()
{
    kDebug() << "called";
    slotVolumeChanged( player->volume() );
}

/* user has clicked our popup's mute-toggle button. 
 * For mute method, we don't use the Player's Mute feature, but instead
 * mimic MPlayer's toggle-to-zero behavior.
 */
void VolumePopupButton::slotToggleMute(bool) {
    if (m_curVolume < 0.01) {
        player->setVolume( m_prevVolume );
    } else {
        player->setVolume( 0.0 );
    }
}

/* called by the player when someone has changed the volume. Update
 * all our widgets, but do NOT change player volume from this method.
 * The Player is already set to new volume value when this is called.
 * @param newVolume has range 0.0 - 1.0.
 */
void VolumePopupButton::slotVolumeChanged( float newVolume )
{
    //kDebug() << "newVolume is " << newVolume;

    bool isMuted = player->muted() || newVolume < 0.01;

    // update icon for our toolbar button
    if ( isMuted )
        setIcon( KIcon( "audio-volume-muted" ) );
    else if ( newVolume < 0.34 )
        setIcon( KIcon( "audio-volume-low" ) );
    else if ( newVolume < 0.67 )
        setIcon( KIcon( "audio-volume-medium" ) );
    else
        setIcon( KIcon( "audio-volume-high" ) );

    m_volumeLabel->setText( i18n( "%1%" , int( newVolume * 100 ) ) );

    // only update if user not dragging it's slider
    if (!m_volumeSlider->isSliderDown()) {
        // emits valueChanged() but not volumeChanged()
        m_volumeSlider->setValue( newVolume * 100 );
    }

    // mimic MPlayer's auto-unmute behavior 
    if ( player->muted() && newVolume >= 0.01 ) {
        player->setMuted(false);
    }

    // tooltip for toolbar button
    const KLocalizedString tip = ki18n( "Volume: %1%" );
    this->setToolTip( tip.subs( int( 100 * newVolume ) ).toString() );

    m_prevVolume = m_curVolume;
    m_curVolume  = newVolume;
}

/* called by the player when some external method has changed the mute 
 * state. Update our widgets, but do not try to change player state from 
 * here.
 */
void
VolumePopupButton::slotMuteStateChanged( bool muted )
{
    Q_UNUSED(muted)
    // update our toolbar icon based on player's current volume level
    slotVolumeChanged( player->volume() );
}

void
VolumePopupButton::mouseReleaseEvent( QMouseEvent * event )
{
    if( event->button() == Qt::LeftButton )
    {
        if ( m_volumeMenu->isVisible() )
            m_volumeMenu->hide();
        else
        {
            const QPoint pos( 0, height() );
            m_volumeMenu->exec( mapToGlobal( pos ) );
        }
    }

    QToolButton::mouseReleaseEvent( event );
}

void
VolumePopupButton::wheelEvent( QWheelEvent * event )
{
    event->accept();
    float volume = qBound( 0.0f, player->volume() + float( event->delta() ) / 4000.0f, 1.0f );
    player->setVolume( volume );
}

#include "volumepopupbutton.moc"
