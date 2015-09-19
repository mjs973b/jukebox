/**
 * Copyright (C) 2004 Scott Wheeler <wheeler@kde.org>
 * Copyright (C) 2007 Matthias Kretz <kretz@kde.org>
 * Copyright (C) 2008, 2009 Michael Pyne <mpyne@kde.org>
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

#include "playermanager.h"

#include <kdebug.h>
#include <kmessagebox.h>
#include <klocale.h>
#include <kactioncollection.h>
#include <kselectaction.h>
#include <ktoggleaction.h>
#include <kurl.h>

#include <Phonon/AudioOutput>
#include <Phonon/MediaObject>
#include <Phonon/VolumeFaderEffect>

#include <QPixmap>
#include <QTimer>

#include <math.h>

#include "playlistinterface.h"
#include "playeradaptor.h"
#include "slideraction.h"
#include "statuslabel.h"
#include "actioncollection.h"
#include "collectionlist.h"
#include "coverinfo.h"
#include "tag.h"
#include "scrobbler.h"
#include "juk.h"

enum PlayerManagerStatus { StatusStopped = -1, StatusPaused = 1, StatusPlaying = 2 };

////////////////////////////////////////////////////////////////////////////////
// protected members
////////////////////////////////////////////////////////////////////////////////

PlayerManager::PlayerManager() :
    QObject(),
    m_playlistInterface(0),
    m_statusLabel(0),
    m_curVolume(1.0),
    m_muted(false),
    m_setup(false),
    m_crossfadeTracks(true),
    m_curOutputPath(0),
    m_prevTrackTime(-1)
{
}

PlayerManager::~PlayerManager()
{

}

////////////////////////////////////////////////////////////////////////////////
// public members
////////////////////////////////////////////////////////////////////////////////

bool PlayerManager::playing() const
{
    if(!m_setup)
        return false;

    Phonon::State state = m_media[m_curOutputPath]->state();
    return (state == Phonon::PlayingState || state == Phonon::BufferingState);
}

bool PlayerManager::paused() const
{
    if(!m_setup)
        return false;

    return m_media[m_curOutputPath]->state() == Phonon::PausedState;
}

bool PlayerManager::muted() const
{
    if(!m_setup)
        return false;

    return m_output[m_curOutputPath]->isMuted();
}

float PlayerManager::volume() const
{
    return m_curVolume;
}

int PlayerManager::status() const
{
    if(!m_setup)
        return StatusStopped;

    if(paused())
        return StatusPaused;

    if(playing())
        return StatusPlaying;

    return 0;
}

int PlayerManager::totalTime() const
{
    return totalTimeMSecs() / 1000;
}

int PlayerManager::currentTime() const
{
    return currentTimeMSecs() / 1000;
}

int PlayerManager::totalTimeMSecs() const
{
    if(!m_setup)
        return 0;

    return m_media[m_curOutputPath]->totalTime();
}

int PlayerManager::currentTimeMSecs() const
{
    if(!m_setup)
        return 0;

    return m_media[m_curOutputPath]->currentTime();
}

bool PlayerManager::seekable() const
{
    if(!m_setup)
        return false;

    return m_media[m_curOutputPath]->isSeekable();
}

QStringList PlayerManager::trackProperties()
{
    return FileHandle::properties();
}

QString PlayerManager::trackProperty(const QString &property) const
{
    if(!playing() && !paused())
        return QString();

    return m_file.property(property);
}

QPixmap PlayerManager::trackCover(const QString &size) const
{
    if(!playing() && !paused())
        return QPixmap();

    if(size.toLower() == "small")
        return m_file.coverInfo()->pixmap(CoverInfo::Thumbnail);
    if(size.toLower() == "large")
        return m_file.coverInfo()->pixmap(CoverInfo::FullSize);

    return QPixmap();
}

FileHandle PlayerManager::playingFile() const
{
    return m_file;
}

QString PlayerManager::playingString() const
{
    if(!playing() || m_file.isNull())
        return QString();

    return m_file.tag()->playingString();
}

void PlayerManager::setPlaylistInterface(PlaylistInterface *interface)
{
    m_playlistInterface = interface;
}

void PlayerManager::setStatusLabel(StatusLabel *label)
{
    m_statusLabel = label;
}

////////////////////////////////////////////////////////////////////////////////
// public slots
////////////////////////////////////////////////////////////////////////////////

void PlayerManager::play(const FileHandle &file)
{
    if(!m_setup)
        setup();

    if(!m_media[0] || !m_media[1] || !m_playlistInterface)
        return;

    stopCrossfade();

    // The "currently playing" media object.
    Phonon::MediaObject *mediaObject = m_media[m_curOutputPath];
    
    if(file.isNull()) {
        if(paused())
            mediaObject->play();
        else if(playing()) {
            mediaObject->seek(0);
            emit seeked(0);
        }
        else {
            m_playlistInterface->playNext();
            m_file = m_playlistInterface->currentFile();

            if(!m_file.isNull())
            {
                mediaObject->setCurrentSource(KUrl::fromPath(m_file.absFilePath()));
                mediaObject->play();

                emit signalItemChanged(m_file);
            }
        }
    }
    else {
        mediaObject->setCurrentSource(KUrl::fromPath(file.absFilePath()));
        mediaObject->play();

        if(m_file != file)
            emit signalItemChanged(file);

        m_file = file;
    }

    // Our state changed handler will perform the follow up actions necessary
    // once we actually start playing.
}

void PlayerManager::play(const QString &file)
{
    CollectionListItem *item = CollectionList::instance()->lookup(file);
    if(item) {
        Playlist::setPlaying(item);
        play(item->file());
    }
}

void PlayerManager::play()
{
    play(FileHandle::null());
}

void PlayerManager::pause()
{
    if(!m_setup)
        return;

    if(paused()) {
        play();
        return;
    }

    ActionCollection::action("pause")->setEnabled(false);

    m_media[m_curOutputPath]->pause();
}

void PlayerManager::stop()
{
    if(!m_setup || !m_playlistInterface)
        return;

    ActionCollection::action("pause")->setEnabled(false);
    ActionCollection::action("stop")->setEnabled(false);
    ActionCollection::action("back")->setEnabled(false);
    ActionCollection::action("forward")->setEnabled(false);
    ActionCollection::action("forwardAlbum")->setEnabled(false);

    // Fading out playback is for chumps.
    stopCrossfade();
    m_media[0]->stop();
    m_media[1]->stop();

    if(!m_file.isNull()) {
        m_file = FileHandle::null();
        emit signalItemChanged(m_file);
    }
}

/**
 * Set output volume. If new volume is different than current volume,
 * the volumeChanged() signal is emitted. Out-of-range values are 
 * not applied (so that the code gets fixed.)
 *
 * @param volume  legal range is 0.0 to 1.0.
 */
void PlayerManager::setVolume(float volume)
{
    if(!m_setup)
        setup();

    kDebug() << "new volume = " << volume;
    if (volume < 0.0 || volume > 1.0 || volume == m_curVolume) {
        return;
    }

    m_curVolume = volume;
    /* true means applied to AudioOutput when MediaObject in PlayingState */
    m_outputVolumeSet[0] = false;
    m_outputVolumeSet[1] = false;

    Phonon::MediaObject *media = m_media[m_curOutputPath];
    Phonon::AudioOutput *out = m_output[m_curOutputPath];
    if (out) {
        if (media && media->state() == Phonon::PlayingState) {
            m_outputVolumeSet[m_curOutputPath] = true;
        }
        out->setVolume(volume);
        // AudioOutput will emit volumeChanged() signal
    } else {
        emit volumeChanged(volume);
    }
}

/* seekTime in milliseconds */
void PlayerManager::seek(int seekTime)
{
    kDebug() << "seekTime=" << seekTime;

    if(!m_setup || m_media[m_curOutputPath]->currentTime() == seekTime)
        return;

    if (m_crossfadeTracks) {
        kDebug() << "Stopping crossfade to seek from" << m_media[m_curOutputPath]->currentTime()
             << "to" << seekTime;
    }
    stopCrossfade();
    m_media[m_curOutputPath]->seek(seekTime);
    emit seeked(seekTime);
}

void PlayerManager::seekForward()
{
    Phonon::MediaObject *mediaObject = m_media[m_curOutputPath];
    const qint64 total = mediaObject->totalTime();
    const qint64 newtime = mediaObject->currentTime() + total / 100;
    const qint64 seekTo = qMin(total, newtime);

    stopCrossfade();
    mediaObject->seek(seekTo);
    emit seeked(seekTo);
}

void PlayerManager::seekBack()
{
    Phonon::MediaObject *mediaObject = m_media[m_curOutputPath];
    const qint64 total = mediaObject->totalTime();
    const qint64 newtime = mediaObject->currentTime() - total / 100;
    const qint64 seekTo = qMax(qint64(0), newtime);

    stopCrossfade();
    mediaObject->seek(seekTo);
    emit seeked(seekTo);
}

void PlayerManager::playPause()
{
    playing() ? ActionCollection::action("pause")->trigger() : ActionCollection::action("play")->trigger();
}

void PlayerManager::forward()
{
    // advance cursor to next song
    m_playlistInterface->playNext();
    FileHandle file = m_playlistInterface->currentFile();

    if(!file.isNull())
        play(file);
    else
        stop();
}

void PlayerManager::back()
{
    // move cursor to previous song
    m_playlistInterface->playPrevious();
    FileHandle file = m_playlistInterface->currentFile();

    if(!file.isNull())
        play(file);
    else
        stop();
}

void PlayerManager::volumeUp()
{
    if(!m_setup)
        return;

    setVolume(volume() + 0.04); // 4% up
}

void PlayerManager::volumeDown()
{
    if(!m_output)
        return;

    setVolume(volume() - 0.04); // 4% down
}

void PlayerManager::setMuted(bool m)
{
    kDebug() << " new mute value is " << m;
    if(!m_setup)
        return;

    m_output[m_curOutputPath]->setMuted(m);
}

bool PlayerManager::mute()
{
    if(!m_setup)
        return false;

    bool newState = !muted();
    setMuted(newState);
    return newState;
}

////////////////////////////////////////////////////////////////////////////////
// private slots
////////////////////////////////////////////////////////////////////////////////

void PlayerManager::slotNeedNextUrl()
{
    if(m_file.isNull() || !m_crossfadeTracks)
        return;

    m_playlistInterface->playNext();
    FileHandle nextFile = m_playlistInterface->currentFile();

    if(!nextFile.isNull()) {
        m_file = nextFile;
        crossfadeToFile(m_file);
    }
}

void PlayerManager::slotFinished()
{
    // It is possible to end up in this function if a file simply fails to play or if the
    // user moves the slider all the way to the end, therefore see if we can keep playing
    // and if we can, do so.  Otherwise, stop.  Note that this slot should
    // only be called by the currently "main" output path (i.e. not from the
    // crossfading one).  However life isn't always so nice apparently, so do some
    // sanity-checking.

    Phonon::MediaObject *mediaObject = qobject_cast<Phonon::MediaObject *>(sender());
    if(mediaObject != m_media[m_curOutputPath])
        return;

    m_playlistInterface->playNext();
    m_file = m_playlistInterface->currentFile();

    if(m_file.isNull()) {
        stop();
    }
    else {
        emit signalItemChanged(m_file);
        m_media[m_curOutputPath]->setCurrentSource(QUrl::fromLocalFile(m_file.absFilePath()));
        m_media[m_curOutputPath]->play();
    }
}

void PlayerManager::slotLength(qint64 msec)
{
    m_statusLabel->setItemTotalTime(msec / 1000);
    emit totalTimeChanged(msec);
}

/**
 * Notify listeners of the current time offset in the playing track.
 * This is driven by the MediaObject::tick() signal, and occurs 
 * 3 to 5 times per second. We emit PlayManager::tick() signal for our 
 * listeners (who typically use it to update widgets.)
 * @param msec  time offset in milliseconds
 */
void PlayerManager::slotTick(qint64 msec)
{
    /* The Phonon MediaObject issues duplicate tick() updates; we drop 
     * the duplicates here by checking value of previous announcement
     * (seen on Phonon 4.6.2 w/ VLC-backend v0.6.2)
     */
    if(!m_setup || !m_playlistInterface || msec == m_prevTrackTime) {
        return;
    }

    m_prevTrackTime = msec;

    if(m_statusLabel)
        m_statusLabel->setItemCurrentTime(msec / 1000);

    //kDebug() << sender() << " emit tick " << msec;
    emit tick(msec);
}

/* called when either MediaObject changes state */
void PlayerManager::slotStateChanged(Phonon::State newstate, Phonon::State oldstate)
{
    // Use sender() since either media object may have sent the signal.
    Phonon::MediaObject *mediaObject = qobject_cast<Phonon::MediaObject *>(sender());
    if(!mediaObject)
        return;

    // Handle errors for either media object
    if(newstate == Phonon::ErrorState) {
        QString errorMessage =
            i18nc(
              "%1 will be the /path/to/file, %2 will be some string from Phonon describing the error",
              "JuK is unable to play the audio file<nl/><filename>%1</filename><nl/>"
                "for the following reason:<nl/><message>%2</message>",
              m_file.absFilePath(),
              mediaObject->errorString()
            );

        switch(mediaObject->errorType()) {
            case Phonon::NoError:
                kDebug() << "received a state change to ErrorState but errorType is NoError!?";
                break;

            case Phonon::NormalError:
                forward();
                KMessageBox::information(0, errorMessage);
                break;

            case Phonon::FatalError:
                stop();
                KMessageBox::sorry(0, errorMessage);
                break;
        }
    }

    // Now bail out if we're not dealing with the currently playing media
    // object.

    if(mediaObject != m_media[m_curOutputPath])
        return;

    // Handle state changes for the playing media object.
    if(newstate == Phonon::StoppedState && oldstate != Phonon::LoadingState) {
        // If this occurs it should be due to a transitory shift (i.e. playing a different
        // song when one is playing now), since it didn't occur in the error handler.  Just
        // in case we really did abruptly stop, handle that case in a couple of seconds.
        QTimer::singleShot(2000, this, SLOT(slotUpdateGuiIfStopped()));

        JuK::JuKInstance()->setWindowTitle(i18n("Jukebox"));

        emit signalStop();
    }
    else if(newstate == Phonon::PausedState) {
        emit signalPause();
    }
    else if (newstate == Phonon::PlayingState) {

        /* For versions of Phonon earlier than 4.7, AudioOutput::setVolume() 
         * is ignored if we are in the stopped state.
         * So we must set it after we reach PlayingState. 
         * See disucssion in bugs.kde.org #321172.
         */
        if (!m_outputVolumeSet[m_curOutputPath]) {
            kDebug() << "volume=" << m_curVolume;
            m_outputVolumeSet[m_curOutputPath] = true;
            m_output[m_curOutputPath]->setVolume( m_curVolume );
        }

        ActionCollection::action("pause")->setEnabled(true);
        ActionCollection::action("stop")->setEnabled(true);
        ActionCollection::action("forward")->setEnabled(true);
        if(ActionCollection::action<KToggleAction>("albumRandomPlay")->isChecked())
            ActionCollection::action("forwardAlbum")->setEnabled(true);
        ActionCollection::action("back")->setEnabled(true);

        JuK::JuKInstance()->setWindowTitle(i18nc(
            "%1 is the artist and %2 is the title of the currently playing track.", 
            "%1 - %2 :: Jukebox",
            m_file.tag()->artist(),
            m_file.tag()->title()));

        emit signalPlay();
    }
    // else {    /* Buffering State */
    //}
}

void PlayerManager::slotSeekableChanged(bool isSeekable)
{
    // Use sender() since either media object may have sent the signal.
    Phonon::MediaObject *mediaObject = qobject_cast<Phonon::MediaObject *>(sender());
    if(!mediaObject)
        return;
    if(mediaObject != m_media[m_curOutputPath])
        return;

    emit seekableChanged(isSeekable);
}

void PlayerManager::slotMutedChanged(bool muted)
{
    // Use sender() since either output object may have sent the signal.
    Phonon::AudioOutput *output = qobject_cast<Phonon::AudioOutput *>(sender());
    if(!output)
        return;

    if(output != m_output[m_curOutputPath])
        return;

    emit mutedChanged(muted);
}

/* called when AudioOutput volume changes */
void PlayerManager::slotVolumeChanged(qreal volume)
{
    //kDebug() << " new volume " << volume;

    // Use sender() since either output object may have sent the signal.
    Phonon::AudioOutput *output = qobject_cast<Phonon::AudioOutput *>(sender());
    if(!output)
        return;

    if(output != m_output[m_curOutputPath])
        return;

    emit volumeChanged(volume);
}

////////////////////////////////////////////////////////////////////////////////
// private members
////////////////////////////////////////////////////////////////////////////////

void PlayerManager::setup()
{
    // All of the actions required by this class should be listed here.

    if(!ActionCollection::action("pause") ||
       !ActionCollection::action("stop") ||
       !ActionCollection::action("back") ||
       !ActionCollection::action("forwardAlbum") ||
       !ActionCollection::action("forward") ||
       !ActionCollection::action("trackPositionAction"))
    {
        kWarning() << "Could not find all of the required actions.";
        return;
    }

    if(m_setup)
        return;
    m_setup = true;

    // We use two audio paths at all times to make cross fading easier (and to also easily
    // support not using cross fading with the same code).  The currently playing audio
    // path is controlled using m_curOutputPath.

    for(int i = 0; i < 2; ++i) {
        m_output[i] = new Phonon::AudioOutput(Phonon::MusicCategory, this);
        connect(m_output[i], SIGNAL(mutedChanged(bool)), SLOT(slotMutedChanged(bool)));
        connect(m_output[i], SIGNAL(volumeChanged(qreal)), SLOT(slotVolumeChanged(qreal)));

        m_media[i] = new Phonon::MediaObject(this);
        m_audioPath[i] = Phonon::createPath(m_media[i], m_output[i]);
        m_media[i]->setTickInterval(200);
        m_media[i]->setPrefinishMark(2000);

        // Pre-cache a volume fader object
        m_fader[i] = new Phonon::VolumeFaderEffect(m_media[i]);
        m_audioPath[i].insertEffect(m_fader[i]);
        m_fader[i]->setVolume(1.0f);

        connect(m_media[i], SIGNAL(stateChanged(Phonon::State,Phonon::State)), SLOT(slotStateChanged(Phonon::State,Phonon::State)));
        connect(m_media[i], SIGNAL(prefinishMarkReached(qint32)), SLOT(slotNeedNextUrl()));
        connect(m_media[i], SIGNAL(totalTimeChanged(qint64)), SLOT(slotLength(qint64)));
        connect(m_media[i], SIGNAL(seekableChanged(bool)), SLOT(slotSeekableChanged(bool)));
    }
    connect(m_media[0], SIGNAL(tick(qint64)), SLOT(slotTick(qint64)));
    connect(m_media[0], SIGNAL(finished()), SLOT(slotFinished()));

    // initialize action states

    ActionCollection::action("pause")->setEnabled(false);
    ActionCollection::action("stop")->setEnabled(false);
    ActionCollection::action("back")->setEnabled(false);
    ActionCollection::action("forward")->setEnabled(false);
    ActionCollection::action("forwardAlbum")->setEnabled(false);

    QDBusConnection::sessionBus().registerObject("/Player", this);
}

void PlayerManager::slotUpdateGuiIfStopped()
{
    if(m_media[0]->state() == Phonon::StoppedState && m_media[1]->state() == Phonon::StoppedState)
        stop();
}

/* setup newFile to become the current track, tell MediaObject to Play it,
 * then configure faders to smoothly shift from old track to current track
 * over a 2 second interval.
 */
void PlayerManager::crossfadeToFile(const FileHandle &newFile)
{
    int nextOutputPath = 1 - m_curOutputPath;

    // Don't need this anymore
    disconnect(m_media[m_curOutputPath], SIGNAL(finished()), this, 0);
    connect(m_media[nextOutputPath], SIGNAL(finished()), SLOT(slotFinished()));

    m_fader[nextOutputPath]->setVolume(0.0f);

    // fore-warn listeners that newFile is about to become the current track
    emit signalItemChanged(newFile);

    m_media[nextOutputPath]->setCurrentSource(QUrl::fromLocalFile(newFile.absFilePath()));
    m_media[nextOutputPath]->play();

    /* only one media object should announce current track time, or listeners
     * will get confused
     */
    disconnect(m_media[m_curOutputPath], SIGNAL(tick(qint64)), this, 0);
    connect(m_media[nextOutputPath], SIGNAL(tick(qint64)), SLOT(slotTick(qint64)));

    m_fader[m_curOutputPath]->setVolume(1.0f);
    m_fader[m_curOutputPath]->fadeTo(0.0f, 2000);

    m_fader[nextOutputPath]->fadeTo(1.0f, 2000);

    m_curOutputPath = nextOutputPath;
}

void PlayerManager::stopCrossfade()
{
    // According to the Phonon docs, setVolume immediately takes effect,
    // which is "good enough for government work" ;)

    // 1 - curOutputPath is the other output path...
    m_fader[m_curOutputPath]->setVolume(1.0f);
    m_fader[1 - m_curOutputPath]->setVolume(0.0f);

    // We don't actually need to physically stop crossfading as the playback
    // code will call ->play() when necessary anyways.  If we hit stop()
    // here instead of pause() then we will trick our stateChanged handler
    // into thinking Phonon had a spurious stop and we'll switch tracks
    // unnecessarily.  (This isn't a problem after crossfade completes due to
    // the signals being disconnected).

    m_media[1 - m_curOutputPath]->pause();
}

QString PlayerManager::randomPlayMode() const
{
    if(ActionCollection::action<KToggleAction>("randomPlay")->isChecked())
        return "Random";
    if(ActionCollection::action<KToggleAction>("albumRandomPlay")->isChecked())
        return "AlbumRandom";
    return "NoRandom";
}

void PlayerManager::setCrossfadeEnabled(bool crossfadeEnabled)
{
    m_crossfadeTracks = crossfadeEnabled;
}

void PlayerManager::setRandomPlayMode(const QString &randomMode)
{
    if(randomMode.toLower() == "random")
        ActionCollection::action<KToggleAction>("randomPlay")->setChecked(true);
    if(randomMode.toLower() == "albumrandom")
        ActionCollection::action<KToggleAction>("albumRandomPlay")->setChecked(true);
    if(randomMode.toLower() == "norandom")
        ActionCollection::action<KToggleAction>("disableRandomPlay")->setChecked(true);
}

#include "playermanager.moc"

// vim: set et sw=4 tw=0 sta:
