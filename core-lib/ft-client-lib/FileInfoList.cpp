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

#include "FileInfoList.h"

FileInfoList::FileInfoList(FileInfo fileInfo)
: m_parent(nullptr), m_prev(nullptr)
{
  setFileInfo(fileInfo);
}

FileInfoList::FileInfoList(const FileInfo *filesInfo, size_t count)
: m_parent(nullptr), m_prev(nullptr)
{
  setFileInfo(filesInfo[0]);
  appendAfter(this, filesInfo + 1, count - 1);
}

FileInfoList::~FileInfoList()
{
  // m_child is freed recursively, as deep as the tree of directories.
  // The following elements are freed in a loop: a list from a server can
  // hold millions of them, too many to free recursively on the stack.
  // Each assignment takes the next pointer of the element it frees first,
  // so that element has none left to free.
  std::unique_ptr<FileInfoList> next = std::move(m_next);
  while (next) {
    next = std::move(next->m_next);
  }
}

void FileInfoList::appendAfter(FileInfoList *last, const FileInfo *filesInfo, size_t count)
{
  for (size_t i = 0; i < count; i++) {
    last->m_next = std::make_unique<FileInfoList>(filesInfo[i]);
    last->m_next->m_prev = last;
    last = last->m_next.get();
  }
}

void FileInfoList::setChild(const FileInfo *filesInfo, size_t count)
{
  // Cleanup
  m_child.reset();
  if (count == 0) {
    return;
  }
  // Parent to child relationship, made whole before it is set.
  std::unique_ptr<FileInfoList> child = std::make_unique<FileInfoList>(filesInfo, count);
  // Child to parent relationship
  child->m_parent = this;
  m_child = std::move(child);
}

FileInfoList *FileInfoList::getChild()
{
  return m_child.get();
}

FileInfoList *FileInfoList::getParent()
{
  return m_parent;
}

FileInfoList *FileInfoList::getRoot()
{
  // In our list only first element of leaf can has parent
  FileInfoList *first = getFirst();
  while (first->getParent() != nullptr) {
    first = first->getParent()->getFirst();
  }
  // first have no parent, so first is root
  return first;
}

FileInfoList *FileInfoList::getFirst()
{
  // A loop, not recursion, for the reason given in the destructor.
  FileInfoList *first = this;
  while (first->getPrev() != nullptr) {
    first = first->getPrev();
  }
  return first;
}

FileInfoList *FileInfoList::getNext()
{
  return m_next.get();
}

FileInfoList *FileInfoList::getPrev()
{
  return m_prev;
}

void FileInfoList::setFileInfo(FileInfo fileInfo)
{
  m_fileInfo = fileInfo;
}

FileInfo *FileInfoList::getFileInfo()
{
  return &m_fileInfo;
}

void FileInfoList::getAbsolutePath(StringStorage *storage, TCHAR directorySeparator)
{
  FileInfoList *first = getFirst();

  StringStorage parentAbsolutePath(_T(""));

  //
  // if parent of first element of this leaf exists
  // than absolute path prefix is parent absolute path with
  // added directory separator symbol in the end.
  //

  bool firstHasParent = (first != nullptr) && (first->getParent() != nullptr);

  if (firstHasParent) {
    FileInfoList *firstParent = first->getParent();
    firstParent->getAbsolutePath(&parentAbsolutePath, directorySeparator);
    if (!parentAbsolutePath.endsWith(directorySeparator)) {
      parentAbsolutePath.appendChar(directorySeparator);
    } // if parent path have no directory separator in the end
  } // if first has parent

  const TCHAR *fileName = m_fileInfo.getFileName();

  storage->setString(parentAbsolutePath.getString());
  storage->appendString(fileName);
}

