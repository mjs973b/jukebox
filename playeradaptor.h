/*
 * This file was generated by dbusidl2cpp version 0.6
 * Command line was: dbusidl2cpp -m -a playeradaptor -- org.kde.juk.player.xml
 *
 * dbusidl2cpp is Copyright (C) 2006 Trolltech AS. All rights reserved.
 *
 * This is an auto-generated file.
 * This file may have been hand-edited. Look for HAND-EDIT comments
 * before re-generating it.
 */

#ifndef PLAYERADAPTOR_H_275541151657197
#define PLAYERADAPTOR_H_275541151657197

#include <QtCore/QObject>
#include <QtDBus>
class QByteArray;
template<class T> class QList;
template<class Key, class Value> class QMap;
class QString;
class QStringList;
class QVariant;

/*
 * Adaptor class for interface org.kde.juk.player
 */
class PlayerAdaptor: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.juk.player")
    Q_CLASSINFO("D-Bus Introspection", ""
"  <interface name=\"org.kde.juk.player\" >\n"
"    <method name=\"playing\" >\n"
"      <arg direction=\"out\" type=\"b\" />\n"
"    </method>\n"
"    <method name=\"paused\" >\n"
"      <arg direction=\"out\" type=\"b\" />\n"
"    </method>\n"
"    <method name=\"volume\" >\n"
"      <arg direction=\"out\" type=\"d\" />\n"
"    </method>\n"
"    <method name=\"status\" >\n"
"      <arg direction=\"out\" type=\"i\" />\n"
"    </method>\n"
"    <method name=\"trackProperties\" >\n"
"      <arg direction=\"out\" type=\"as\" />\n"
"    </method>\n"
"    <method name=\"trackProperty\" >\n"
"      <arg direction=\"out\" type=\"s\" />\n"
"      <arg direction=\"in\" type=\"s\" name=\"property\" />\n"
"    </method>\n"
"    <method name=\"currentFile\" >\n"
"      <arg direction=\"out\" type=\"s\" />\n"
"    </method>\n"
"    <method name=\"play\" />\n"
"    <method name=\"play\" >\n"
"      <arg direction=\"in\" type=\"s\" name=\"file\" />\n"
"    </method>\n"
"    <method name=\"pause\" />\n"
"    <method name=\"stop\" />\n"
"    <method name=\"playPause\" />\n"
"    <method name=\"back\" />\n"
"    <method name=\"forward\" />\n"
"    <method name=\"seekBack\" />\n"
"    <method name=\"seekForward\" />\n"
"    <method name=\"volumeUp\" />\n"
"    <method name=\"volumeDown\" />\n"
"    <method name=\"mute\" />\n"
"    <method name=\"setVolume\" >\n"
"      <arg direction=\"in\" type=\"i\" name=\"volume\" />\n"
"    </method>\n"
"    <method name=\"seek\" >\n"
"      <arg direction=\"in\" type=\"i\" name=\"time\" />\n"
"    </method>\n"
"    <method name=\"playingString\" >\n"
"      <arg direction=\"out\" type=\"s\" />\n"
"    </method>\n"
"    <method name=\"currentTime\" >\n"
"      <arg direction=\"out\" type=\"i\" />\n"
"    </method>\n"
"    <method name=\"totalTime\" >\n"
"      <arg direction=\"out\" type=\"i\" />\n"
"    </method>\n"
"    <method name=\"randomPlayMode\" >\n"
"      <arg direction=\"out\" type=\"s\" />\n"
"    </method>\n"
"    <method name=\"setRandomPlayMode\" >\n"
"      <arg direction=\"in\" type=\"i\" name=\"randomMode\" />\n"
"    </method>\n"
"  </interface>\n"
        "")
public:
    PlayerAdaptor(QObject *parent);
    virtual ~PlayerAdaptor();

public: // PROPERTIES
public Q_SLOTS: // METHODS
    void back();
    QString currentFile();
    int currentTime();
    void forward();
    void mute();
    void pause();
    bool paused();
    void play(const QString &file);
    void play();
    void playPause();
    bool playing();
    QString playingString();
    QString randomPlayMode();
    void seek(int time);
    void seekBack();
    void seekForward();
    void setRandomPlayMode(int randomMode);
    void setVolume(int volume);
    int status();
    void stop();
    int totalTime();
    QStringList trackProperties();
    QString trackProperty(const QString &property);
    double volume();
    void volumeDown();
    void volumeUp();
Q_SIGNALS: // SIGNALS
};

#endif
