/***************************************************************************
                          id3tag.h  -  description
                             -------------------
    begin                : Sun Feb 17 2002
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

#ifndef ID3TAG_H
#define ID3TAG_H

#include <qstring.h>

#include <id3/tag.h>

#include "tag.h"

class ID3Tag : public Tag
{
public:
    ID3Tag(const QString &fileName);
    virtual ~ID3Tag();

    virtual void save();
    virtual bool hasTag() const;

    virtual QString track() const;
    virtual QString artist() const;
    virtual QString album() const;
    virtual Genre   genre() const;
    virtual int trackNumber() const;
    virtual QString trackNumberString() const;
    virtual int year() const;
    virtual QString yearString() const;
    virtual QString comment() const;

    virtual void setTrack(const QString &value);
    virtual void setArtist(const QString &value);
    virtual void setAlbum(const QString &value);
    virtual void setGenre(const Genre &value);
    virtual void setTrackNumber(int value);
    virtual void setYear(int value);
    virtual void setComment(const QString &value);

    virtual QString bitrateString() const;
    virtual QString lengthString() const;
    virtual int seconds() const;

private:
    ID3_Tag m_tag;
    QString m_fileName;
    bool m_changed;

    QString m_tagTrack;
    QString m_tagArtist;
    QString m_tagAlbum;
    Genre m_tagGenre;
    int m_tagTrackNumber;
    QString m_tagTrackNumberString;
    int m_tagYear;
    QString m_tagYearString;
    QString m_tagComment;
    bool m_tagExists;

    KFileMetaInfo m_metaInfo;
};

#endif
