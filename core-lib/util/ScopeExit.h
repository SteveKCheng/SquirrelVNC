// Copyright (C) 2026 Steve Cheng.
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

#ifndef _SCOPE_EXIT_H_
#define _SCOPE_EXIT_H_

#include <type_traits>
#include <utility>

//
// Calls a function when the scope it belongs to is left, whether by
// reaching its end, return, break or an exception, unless release() was
// called first. It follows std::experimental::scope_exit of the C++
// Extensions for Library Fundamentals, Version 3 (N4948 [scope.exit]).
//
// Typical use, with a lambda right after the resource is acquired:
//
//   HANDLE file = CreateFile(...);
//   if (file == INVALID_HANDLE_VALUE) {
//     return false;
//   }
//   ScopeExit closeFile([&] { CloseHandle(file); });
//
// For clean-up only on failure, release() the guard once the function
// has succeeded.
//
// EF is a function object type, or an lvalue reference to a function or
// a function object; class template argument deduction gives the
// function object's own (decayed) type. It must be destructible, and
// callable with no arguments. Its call should not throw: called from the
// destructor during the unwinding from another exception, an exception
// from it terminates the program.
//
// Objects of this class cannot be copied or assigned. A move constructor
// is given, so that a guard can be returned from a function; the guard
// moved from is released.
//
template<class EF>
class ScopeExit final
{
public:
  //
  // Pre-conditions: calling fn, or the copy of it kept, is well-formed.
  // Post-conditions: the guard is active, holding fn: moved from if fn is
  // an rvalue whose move cannot throw, otherwise copied.
  // Throws: whatever copying or moving fn throws, after calling fn: the
  // clean-up still happens, before the scope is left with the exception.
  //
  // [[nodiscard]], which the specification does not have, makes the
  // compiler warn about a guard made as a temporary, which would call
  // the function at once: "ScopeExit([&] { ... });".
  //
  template<class Fn>
    requires (!std::is_same_v<std::remove_cvref_t<Fn>, ScopeExit> &&
              std::is_constructible_v<EF, Fn>)
  [[nodiscard]] explicit ScopeExit(Fn &&fn)
    noexcept(std::is_nothrow_constructible_v<EF, Fn> ||
             std::is_nothrow_constructible_v<EF, Fn &>)
  try
  : m_exitFunction(chooseInitializer<Fn>(fn)),
    m_isActive(true)
  {
  } catch (...) {
    // fn is a parameter, not a member, so the handler may use it. The
    // exception is rethrown when the handler ends.
    fn();
  }

  //
  // Post-conditions: this guard holds other's function, and is active if
  // other was; other is released, so that the function is called once.
  //
  ScopeExit(ScopeExit &&other)
    noexcept(std::is_nothrow_move_constructible_v<EF> ||
             std::is_nothrow_copy_constructible_v<EF>)
    requires (std::is_nothrow_move_constructible_v<EF> ||
              std::is_copy_constructible_v<EF>)
  : m_exitFunction(chooseMoveInitializer(other.m_exitFunction)),
    m_isActive(other.m_isActive)
  {
    other.release();
  }

  ScopeExit(const ScopeExit &) = delete;
  ScopeExit &operator=(const ScopeExit &) = delete;
  ScopeExit &operator=(ScopeExit &&) = delete;

  //
  // Post-conditions: the function has been called, if the guard is still
  // active.
  //
  ~ScopeExit() noexcept
  {
    if (m_isActive) {
      m_exitFunction();
    }
  }

  //
  // Post-conditions: the function will not be called.
  //
  void release() noexcept
  {
    m_isActive = false;
  }

private:
  //
  // What to initialize m_exitFunction from: fn moved (forwarded) if it is
  // an rvalue whose move cannot throw; otherwise fn as an lvalue, so that
  // it is still intact to call if copying it throws.
  //
  template<class Fn>
  static constexpr decltype(auto) chooseInitializer(std::remove_reference_t<Fn> &fn) noexcept
  {
    if constexpr (!std::is_lvalue_reference_v<Fn> && std::is_nothrow_constructible_v<EF, Fn>) {
      return std::forward<Fn>(fn);
    } else {
      return (fn);
    }
  }

  //
  // The function moved if that cannot throw, otherwise copied: a move
  // that throws part-way could leave neither guard able to call it.
  //
  static constexpr decltype(auto) chooseMoveInitializer(EF &fn) noexcept
  {
    if constexpr (std::is_nothrow_move_constructible_v<EF>) {
      return std::forward<EF>(fn);
    } else {
      return (fn);
    }
  }

  EF m_exitFunction;
  bool m_isActive;
};

template<class EF>
ScopeExit(EF) -> ScopeExit<EF>;

#endif // _SCOPE_EXIT_H_
