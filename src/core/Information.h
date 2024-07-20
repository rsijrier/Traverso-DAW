/*
    Copyright (C) 2005-2007 Remon Sijrier 
 
    This file is part of Traverso
 
    Traverso is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
 
*/

#ifndef INFORMATION_H
#define INFORMATION_H

#include <QObject>

struct InfoStruct
{
        QString 	message;
        int		type;
};

static const int INFO = 0;
static const int WARNING = 1;
static const int CRITICAL = 2;


class Information : public QObject
{
        Q_OBJECT

public:
        void information(const QString& s);
        void warning(const QString& s);
        void critical(const QString& s);

private:
        Information();
        Information(const Information&) : QObject()
        {}


        // allow this function to create one instance
        friend Information& info();

signals:
        void message(InfoStruct );
	
private slots:
	void audiodevice_message(const QString &message, int severity);
    void tsar_message(const QString& message);
};

// use this function to propagate the Information
Information& info();

#endif

//eof

