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

#ifndef SLIDERACTION_H
#define SLIDERACTION_H

#include <kaction.h>
#include <QBoxLayout>

#include "volumepopupbutton.h"

class TimeSlider;
class PlayerManager;

class TrackPositionAction : public KAction
{
    Q_OBJECT
public:
    TrackPositionAction(const QString &text, QObject *parent, PlayerManager *mgr);
protected:
    virtual QWidget *createWidget(QWidget *parent);
private slots:
    void seekableChanged(bool seekable);
    void totalTimeChanged(int ms);
private:
    TimeSlider *m_slider;
    PlayerManager *m_player;
};

class VolumeAction : public KAction
{
    Q_OBJECT
public:
    VolumeAction(const QString &text, QObject *parent, PlayerManager *mgr);
    VolumePopupButton *button() const { return m_button; }
protected:
    virtual QWidget *createWidget(QWidget *parent);
private:
    VolumePopupButton *m_button;
    PlayerManager *m_player;
};

#endif

// vim: set et sw=4 tw=0 sta:
