/*
   File locking

   Copyright (C) 2003, 2004, 2005, 2006, 2007, 2011
   The Free Software Foundation, Inc.

   Written by:
   Adam Byrtek, 2003

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/** \file
 *  \brief Source: file locking
 *  \author Adam Byrtek
 *  \date 2003
 *
 *  Locking scheme is based on a documentation found
 *  in JED editor sources. Abstract from lock.c file (by John E. Davis):
 *
 *  The basic idea here is quite simple.  Whenever a buffer is attached to
 *  a file, and that buffer is modified, then attempt to lock the
 *  file. Moreover, before writing to a file for any reason, lock the
 *  file. The lock is really a protocol respected and not a real lock.
 *  The protocol is this: If in the directory of the file is a
 *  symbolic link with name ".#FILE", the FILE is considered to be locked
 *  by the process specified by the link.
 */

#include <config.h>

#include <signal.h>             /* kill() */
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <pwd.h>
#include <stdlib.h>

#include "lib/global.h"
#include "lib/vfs/vfs.h"
#include "lib/util.h"           /* tilde_expand() */
#include "lib/lock.h"
#include "lib/widget.h"         /* query_dialog() */

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define BUF_SIZE 255
#define PID_BUF_SIZE 10

/*** file scope type declarations ****************************************************************/

struct lock_s
{
    char *who;
    pid_t pid;
};

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/** \fn static char * lock_build_name (void)
 *  \brief builds user@host.domain.pid string (need to be freed)
 *  \return a pointer to lock filename
 */

static char *
lock_build_name (void)
{
    char host[BUF_SIZE];
    const char *user = NULL;
    struct passwd *pw;

    pw = getpwuid (getuid ());
    if (pw)
        user = pw->pw_name;
    if (!user)
        user = getenv ("USER");
    if (!user)
        user = getenv ("USERNAME");
    if (!user)
        user = getenv ("LOGNAME");
    if (!user)
        user = "";

    /** \todo Use FQDN, no clean interface, so requires lot of code */
    if (gethostname (host, BUF_SIZE - 1) == -1)
        *host = '\0';

    return g_strdup_printf ("%s@%s.%d", user, host, (int) getpid ());
}

/* --------------------------------------------------------------------------------------------- */

static char *
lock_build_symlink_name (const char *fname)
{
    char *fname_copy, *symlink_name;
    char absolute_fname[PATH_MAX];

    if (mc_realpath (fname, absolute_fname) == NULL)
        return NULL;

    fname = x_basename (absolute_fname);
    fname_copy = g_strdup (fname);
    absolute_fname[fname - absolute_fname] = '\0';
    symlink_name = g_strconcat (absolute_fname, ".#", fname_copy, (char *) NULL);
    g_free (fname_copy);

    return symlink_name;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Extract pid from user@host.domain.pid string
 */

static struct lock_s *
lock_extract_info (const char *str)
{
    size_t i, len;
    const char *p, *s;
    static char pid[PID_BUF_SIZE], who[BUF_SIZE];
    static struct lock_s lock;

    len = strlen (str);

    for (p = str + len - 1; p >= str; p--)
        if (*p == '.')
            break;

    /* Everything before last '.' is user@host */
    i = 0;
    for (s = str; s < p && i < BUF_SIZE; s++)
        who[i++] = *s;
    who[i] = '\0';

    /* Treat text between '.' and ':' or '\0' as pid */
    i = 0;
    for (p = p + 1; (p < str + len) && (*p != ':') && (i < PID_BUF_SIZE); p++)
        pid[i++] = *p;
    pid[i] = '\0';

    lock.pid = (pid_t) atol (pid);
    lock.who = who;
    return &lock;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Extract user@host.domain.pid from lock file (static string)
 */

static char *
lock_get_info (const char *lockfname)
{
    int cnt;
    static char buf[BUF_SIZE];

    cnt = readlink (lockfname, buf, BUF_SIZE - 1);
    if (cnt == -1 || *buf == '\0')
        return NULL;
    buf[cnt] = '\0';
    return buf;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* Tries to raise file lock
   Returns 1 on success,  0 on failure, -1 if abort
   Warning: Might do screen refresh and lose edit->force */

int
lock_file (const vfs_path_t * fname_vpath)
{
    char *lockfname, *newlock, *msg, *lock;
    struct stat statbuf;
    struct lock_s *lockinfo;
    gboolean symlink_ok;
    char *fname = vfs_path_to_str (fname_vpath);

    /* Just to be sure (and don't lock new file) */
    if (fname == NULL || *fname == '\0')
        return 0;

    fname = tilde_expand (fname);

    /* Locking on VFS is not supported */
    if (!vfs_file_is_local (fname_vpath))
    {
        vfs_path_free ((vfs_path_t *) fname_vpath);
        g_free (fname);
        return 0;
    }
    vfs_path_free ((vfs_path_t *) fname_vpath);
    g_free (fname);

    /* Check if already locked */
    lockfname = lock_build_symlink_name (fname);
    vfs_path_free ((vfs_path_t *) fname_vpath);

    if (lockfname == NULL)
        return 0;

    if (lstat (lockfname, &statbuf) == 0)
    {
        lock = lock_get_info (lockfname);
        if (lock == NULL)
        {
            g_free (lockfname);
            return 0;
        }
        lockinfo = lock_extract_info (lock);

        /* Check if locking process alive, ask user if required */
        if (lockinfo->pid == 0 || !(kill (lockinfo->pid, 0) == -1 && errno == ESRCH))
        {
            msg =
                g_strdup_printf (_
                                 ("File \"%s\" is already being edited.\n"
                                  "User: %s\nProcess ID: %d"), x_basename (lockfname) + 2,
                                 lockinfo->who, (int) lockinfo->pid);
            /* TODO: Implement "Abort" - needs to rewind undo stack */
            switch (query_dialog
                    (_("File locked"), msg, D_NORMAL, 2, _("&Grab lock"), _("&Ignore lock")))
            {
            case 0:
                break;
            case 1:
            case -1:
                g_free (lockfname);
                g_free (msg);
                return 0;
            }
            g_free (msg);
        }
        unlink (lockfname);
    }

    /* Create lock symlink */
    newlock = lock_build_name ();
    symlink_ok = (symlink (newlock, lockfname) != -1);
    g_free (newlock);
    g_free (lockfname);

    return symlink_ok ? 1 : 0;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Lowers file lock if possible
 * @returns  Always 0
 */

int
unlock_file (const vfs_path_t * fname_vpath)
{
    char *lockfname, *lock;
    struct stat statbuf;
    char *fname = vfs_path_to_str (fname_vpath);

    /* Just to be sure */
    if (fname == NULL || *fname == '\0')
        return 0;

    fname = tilde_expand (fname);
    lockfname = lock_build_symlink_name (fname);

    vfs_path_free ((vfs_path_t *) fname_vpath);
    g_free (fname);

    if (lockfname == NULL)
        return 0;

    /* Check if lock exists */
    if (lstat (lockfname, &statbuf) == -1)
    {
        g_free (lockfname);
        return 0;
    }

    lock = lock_get_info (lockfname);
    if (lock != NULL)
    {
        /* Don't touch if lock is not ours */
        if (lock_extract_info (lock)->pid != getpid ())
        {
            g_free (lockfname);
            return 0;
        }
    }

    /* Remove lock */
    unlink (lockfname);
    g_free (lockfname);
    return 0;
}

/* --------------------------------------------------------------------------------------------- */
