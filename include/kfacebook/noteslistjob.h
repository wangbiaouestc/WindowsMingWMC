/* Copyright 2011 Roeland Jago Douma <unix@rullzer.com>

   This library is free software; you can redistribute it and/or modify
   it under the terms of the GNU Library General Public License as published
   by the Free Software Foundation; either version 2 of the License or
   ( at your option ) version 3 or, at the discretion of KDE e.V.
   ( which shall act as a proxy as in section 14 of the GPLv3 ), any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/
#ifndef KFACEBOOK_NOTESLISTJOB_H
#define KFACEBOOK_NOTESLISTJOB_H

#include "listjobbase.h"
#include "noteinfo.h"

namespace KFacebook {

class LIBKFACEBOOK_EXPORT NotesListJob : public ListJobBase
{
  Q_OBJECT
  public:
    NotesListJob( const QString &accessToken );
    QList<NoteInfoPtr> notes() const;
    int numEntries() const;

  protected:
    void handleItem( const QVariant& item );

  private:
    QList<NoteInfoPtr> mNotes;
};

}

#endif
