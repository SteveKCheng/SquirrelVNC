// Copyright (C) 2009,2010,2011,2012 GlavSoft LLC.
// All rights reserved.
//
//-------------------------------------------------------------------------
// This file is part of SquirrelVNC, a modified version of the TightVNC
// software (https://www.tightvnc.com/).
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//-------------------------------------------------------------------------
//

#ifndef _SINGLETON_H_
#define _SINGLETON_H_

#include "CommonHeader.h"

#include <crtdbg.h>

#include "thread/LocalMutex.h"
#include "thread/AutoLock.h"

//
// Template that realize singleton pattern which does not supports "lazy"
// initialization.
//
// Remark:
// To use this pattern you must create singleton instance by your own hands,
// it means that this realization of singleton does not support "lazy" initialization.
//

template<class T> class Singleton {
public:
  Singleton() {
    AutoLock l(&m_instanceMutex);

    if (s_instance == nullptr) {
      s_instance = (T*)this;
    } else {
      _ASSERT(FALSE);
    }
  }

  Singleton(const Singleton &) = delete;
  Singleton &operator=(const Singleton &) = delete;
  Singleton(Singleton &&) = delete;
  Singleton &operator=(Singleton &&) = delete;

  virtual ~Singleton() {
    AutoLock l(&m_instanceMutex);

    s_instance = nullptr;
  }

  static T* getInstance() {
    {
      AutoLock l(&m_instanceMutex);

      if (s_instance == nullptr) {
        _ASSERT(FALSE);
      }
    }

    return (T*)s_instance;
  }

private:
  static LocalMutex m_instanceMutex;

  static void* s_instance;
};

template<class T> LocalMutex Singleton<T>::m_instanceMutex;
template<class T> void *Singleton<T>::s_instance = nullptr;

#endif
