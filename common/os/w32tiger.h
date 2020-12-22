/* Copyright (C) 2011 TigerVNC Team.  All Rights Reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifndef OS_W32TIGER_H
#define OS_W32TIGER_H

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wininet.h>
#include <shlobj.h>
#include <shlguid.h>
#include <afunix.h>
#include <pthread.h>
#include <io.h>

#include "memrchr.h"
#include "crypt.h"

/* */
#define errorNumber WSAGetLastError()

/* MSVC has different names for these */
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define close closesocket
#define access _access

/* MSVC does not support these at all*/
#define __unused_attr
#define __attribute__
#define __thread

/* Missing or mismatched types*/
typedef __int64 ssize_t;
typedef unsigned short sa_family_t;

/* Missing flags */

// unistd.h
#define	F_OK 0
#define	X_OK 0x01
#define	W_OK 0x02
#define	R_OK 0x04

// sys/socket.h
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

/* Missing functions */
#define realpath(N,R) _fullpath((R),(N),_MAX_PATH)

inline int gettimeofday(struct timeval* tp, struct timezone* tzp)
{
    static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);

    SYSTEMTIME  system_time;
    FILETIME    file_time;
    uint64_t    time;

    GetSystemTime(&system_time);
    SystemTimeToFileTime(&system_time, &file_time);
    time = ((uint64_t)file_time.dwLowDateTime);
    time += ((uint64_t)file_time.dwHighDateTime) << 32;

    tp->tv_sec = (long)((time - EPOCH) / 10000000L);
    tp->tv_usec = (long)(system_time.wMilliseconds * 1000);

    return 0;
}

inline unsigned long __builtin_ffs(unsigned long in) {
    unsigned long out = 0;
    BitScanForward(&out, in);
    return out;
}

inline void usleep(int time) {
    __int64 frequency = 0;
    QueryPerformanceFrequency((LARGE_INTEGER *)&frequency);

    __int64 start = 0;
    QueryPerformanceCounter((LARGE_INTEGER *) &start);

    __int64 now = 0;

    do {
        QueryPerformanceCounter((LARGE_INTEGER *) &now);
    } while((now - start) < time);
}

inline int inet_aton(const char* in, struct in_addr* out)
{
    out->s_addr = inet_addr(in);
    return (out->s_addr == INADDR_NONE) ? 0 : 1;
}

inline int bcmp(const void * a, const void * b, size_t n)
{
    return memcmp(a, b, n);
}

#endif