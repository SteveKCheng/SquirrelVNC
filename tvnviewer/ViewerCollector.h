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

#ifndef _VIEWER_COLLECTOR_H_
#define _VIEWER_COLLECTOR_H_

#include "ViewerInstance.h"
#include "win-system/WindowsApplication.h"

#include <cstddef>
#include <list>
#include <memory>

//
// Keeps track of the viewer instances, for cooperative termination only.
// It owns none: each is owned by the references of its window and of its
// connection (ViewerInstance), and the deleter given here deletes it on the
// GUI thread. What is kept here lets the application stop the instances
// still alive when it ends, and wait until all have been deleted.
// Used only on the GUI thread, except for the deleter.
//
class ViewerCollector final
{
public:
  //
  // Pre-conditions: application outlives this object; its message loop, or
  // destroyAllInstances(), runs the deletions.
  //
  explicit ViewerCollector(WindowsApplication *application);
  ViewerCollector(const ViewerCollector &) = delete;
  ViewerCollector &operator=(const ViewerCollector &) = delete;
  ViewerCollector(ViewerCollector &&) = delete;
  ViewerCollector &operator=(ViewerCollector &&) = delete;

  //
  // Calls destroyAllInstances(), so its pre-conditions apply.
  //
  ~ViewerCollector();

  //
  // Takes instance into shared ownership, and returns the first reference.
  // Whichever thread drops the last reference, the deleter posts the
  // deletion to the top of the application's message loop
  // (WindowsApplication::postToMessageLoop()), where no window procedure of
  // the instance is on the stack; until it has run, the instance counts as
  // alive (empty()).
  //
  // Pre-conditions: instance is not null.
  // Post-conditions: the returned pointer owns instance; its deleter refers
  // to this object, which outlives it (destroyAllInstances()).
  //
  std::shared_ptr<ViewerInstance> addInstance(std::unique_ptr<ViewerInstance> instance);

  //
  // Cooperative termination: stops every instance still alive
  // (ViewerInstance::stop()), then waits until all have been deleted,
  // running their deletions itself (WindowsApplication::runPostedWork()).
  //
  // Pre-conditions: the message loop has ended, so no window procedure is
  // on the stack; the network thread is running.
  // Post-conditions: no instance is alive.
  //
  void destroyAllInstances();

  // This function decrease counter of connection, requires reconnect.
  void decreaseToReconnect();

  //
  // Whether no instance is alive (not yet deleted), and no reconnection is
  // pending.
  //
  bool empty() const;

private:
  //
  // The deletion of an instance, posted by its deleter: counts a pending
  // reconnection, deletes it, and forgets it.
  //
  void deleteInstance(ViewerInstance *instance) noexcept;

  WindowsApplication *m_application;

  // Every instance not yet deleted, without owning it; expired entries are
  // dropped when an instance is deleted.
  std::list<std::weak_ptr<ViewerInstance>> m_instances;

  // The number of instances not yet deleted. Not the size of m_instances:
  // a reference expires before its deletion has run.
  size_t m_aliveCount;

  // This variable contain count of instance, when need to reconnect.
  // If this count isn't 0, shutdown application is denied.
  int m_countToReconnect;
};

#endif
