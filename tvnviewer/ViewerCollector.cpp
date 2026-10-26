// Copyright (C) 2012 GlavSoft LLC.
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

#include "ViewerCollector.h"

ViewerCollector::ViewerCollector(WindowsApplication *application)
: m_application(application),
  m_aliveCount(0),
  m_countToReconnect(0)
{
}

ViewerCollector::~ViewerCollector()
{
  destroyAllInstances();
}

std::shared_ptr<ViewerInstance> ViewerCollector::addInstance(std::unique_ptr<ViewerInstance> instance)
{
  // Counted first: if making the pointer fails, it still calls the deleter.
  m_aliveCount++;
  std::shared_ptr<ViewerInstance> shared(instance.release(), [this](ViewerInstance *released) {
    // Any thread: the last reference is usually the connection's.
    m_application->postToMessageLoop([this, released] {
      deleteInstance(released);
    });
  });
  m_instances.push_back(shared);
  return shared;
}

void ViewerCollector::deleteInstance(ViewerInstance *instance) noexcept
{
  if (instance->requiresReconnect()) {
    ++m_countToReconnect;
  }
  delete instance;
  m_aliveCount--;
  m_instances.remove_if([](const std::weak_ptr<ViewerInstance> &reference) {
    return reference.expired();
  });
}

void ViewerCollector::destroyAllInstances()
{
  for (const std::weak_ptr<ViewerInstance> &reference : m_instances) {
    if (std::shared_ptr<ViewerInstance> instance = reference.lock()) {
      instance->stop();
    }
  }
  while (m_aliveCount > 0) {
    m_application->waitForPostedWork();
    m_application->runPostedWork();
  }
}

void ViewerCollector::decreaseToReconnect()
{
  --m_countToReconnect;
}

bool ViewerCollector::empty() const
{
  // If not active instance and count wait to reconnect is 0,
  // then return true and false otherwise.
  return m_aliveCount == 0 && (m_countToReconnect == 0);
}
