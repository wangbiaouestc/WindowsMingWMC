/* Copyright 2010 Thomas McGuire <mcguire@kde.org>

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
#ifndef KFACEBOOK_FRIENDLISTJOB_H
#define KFACEBOOK_FRIENDLISTJOB_H

#include "userinfo.h"
#include "facebookjobs.h"

namespace KFacebook {

/// Get a list of friends of the user
class LIBKFACEBOOK_EXPORT FriendListJob : public FacebookGetJob
{
  Q_OBJECT
  public:
    FriendListJob( const QString &accessToken );
    QList<UserInfoPtr> friends() const;

  protected:
    virtual void handleData( const QVariant& data );
  private:
    QList<UserInfoPtr> mFriends;
};

}

#endif
