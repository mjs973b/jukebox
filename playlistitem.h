/***************************************************************************
                          playlistitem.h  -  description
                             -------------------
    begin                : Sun Feb 17 2002
    copyright            : (C) 2002 by Scott Wheeler
    email                : scott@slackorama.net
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef PLAYLISTITEM_H
#define PLAYLISTITEM_H

#include <klistview.h>

#include <qfileinfo.h>
#include <qobject.h>

#include "tag.h"
#include "audiodata.h"
#include "cache.h"

class Playlist;
class PlaylistItem;
class CollectionListItem;

typedef QPtrList<PlaylistItem> PlaylistItemList;

class PlaylistItem : public QObject, public KListViewItem 
{
    friend class Playlist;

    Q_OBJECT
public:
    enum ColumnType { TrackColumn = 0, ArtistColumn = 1, AlbumColumn = 2, TrackNumberColumn = 3,
                      GenreColumn = 4, YearColumn = 5, LengthColumn = 6, FileNameColumn = 7 };

    // The constructors are in the protected secion.  See the note there.

    virtual ~PlaylistItem();

    // These can't be const members because they fetch the data "on demand".

    Tag *getTag();
    AudioData *getAudioData();

    void setFile(const QString &file);

    // QFileInfo-ish methods

    QString fileName() const;
    QString filePath() const;
    QString absFilePath() const;
    QString dirPath(bool absPath = false) const;
    bool isWritable() const;

public slots:
    virtual void refresh();
    virtual void refreshFromDisk();

protected:
    /** 
     * Items should always be created using Playlist::createItem() or through a
     * subclss or friend class. 
     */
    PlaylistItem(CollectionListItem *item, Playlist *parent);
    PlaylistItem(CollectionListItem *item, Playlist *parent, QListViewItem *after);
    PlaylistItem(Playlist *parent);

    class Data;
    Data *getData();
    void setData(Data *d);

protected slots:
    void refreshImpl();

signals:
    void refreshed();

private:
    void setup(CollectionListItem *item, Playlist *parent);
    virtual int compare(QListViewItem *item, int column, bool ascending) const;
    int compare(PlaylistItem *firstItem, PlaylistItem *secondItem, int column, bool ascending) const;

    Data *data;
};

class PlaylistItem::Data : public QFileInfo
{
public:
    static Data *newUser(const QFileInfo &file);
    Data *newUser();
    void deleteUser();

    void refresh();

    Tag *getTag();
    AudioData *getAudioData();

    void setFile(const QString &file);

protected:
    // Because we're trying to use this as a shared item, we want all access
    // to be through pointers (so that it's safe to use delete this).  Thus
    // creation of the object should be done by the newUser methods above
    // and deletion should be handled by deleteUser.  Making the constructor
    // and destructor protected ensures this.

    Data(const QFileInfo &file);
    virtual ~Data();

private:
    int referenceCount;

    CacheItem *cache;
    Tag *tag;
    AudioData *audioData;
};


#endif
