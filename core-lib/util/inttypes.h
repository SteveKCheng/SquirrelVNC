// Copyright (C) 2008,2009,2010,2011,2012 GlavSoft LLC.
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

#ifndef __INTTYPES_H__
#define __INTTYPES_H__


// UINT8..UINT64 and INT8..INT64 are this project's established spellings for
// the standard fixed-width integer types. They now alias those types, so the
// widths are guaranteed by the standard and the aliases interoperate with
// std::uintN_t / uintN_t directly.
//
// The header is also included from C (x11region.c), so it takes the <cstdint>
// names in C++ and the <stdint.h> names in C.
#ifdef __cplusplus
#include <cstdint>

// Unsigned types
typedef std::uint8_t   UINT8;
typedef std::uint16_t  UINT16;
typedef std::uint32_t  UINT32;
typedef std::uint64_t  UINT64;

// Signed types
typedef std::int8_t    INT8;
typedef std::int16_t   INT16;
typedef std::int32_t   INT32;
typedef std::int64_t   INT64;

#else
#include <stdint.h>

// Unsigned types
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;

// Signed types
typedef int8_t    INT8;
typedef int16_t   INT16;
typedef int32_t   INT32;
typedef int64_t   INT64;

#endif

#endif // __INTTYPES_H__
