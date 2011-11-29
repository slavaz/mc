/*
   Wrapper for routines to notify the
   tree about the changes made to the directory
   structure.

   Copyright (C) 2011
   The Free Software Foundation, Inc.

   Author:
   Janne Kukonlehto
   Miguel de Icaza

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


/** \file  filenot.c
 *  \brief Source: wrapper for routines to notify the
 *  tree about the changes made to the directory
 *  structure.
 */

#include <config.h>

#include <errno.h>
#include <string.h>

#include "lib/global.h"
#include "lib/fs.h"
#include "lib/util.h"
#include "lib/vfs/vfs.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static char *
get_absolute_name (const char *file)
{
    char dir[MC_MAXPATHLEN];

    if (file[0] == PATH_SEP)
        return g_strdup (file);
    mc_get_current_wd (dir, MC_MAXPATHLEN);
    return concat_dir_and_file (dir, file);
}

/* --------------------------------------------------------------------------------------------- */

static int
my_mkdir_rec (char *s, mode_t mode)
{
    char *p, *q;
    int result;
    vfs_path_t *s_vpath = vfs_path_from_str (s);

    if (!mc_mkdir (s_vpath, mode))
    {
        vfs_path_free (s_vpath);
        return 0;
    }
    else if (errno != ENOENT)
    {
        vfs_path_free (s_vpath);
        return -1;
    }

    /* FIXME: should check instead if s is at the root of that filesystem */
    {
        if (!vfs_file_is_local (s_vpath))
        {
            vfs_path_free (s_vpath);
            return -1;
        }
    }

    if (!strcmp (s, PATH_SEP_STR))
    {
        errno = ENOTDIR;
        vfs_path_free (s_vpath);
        return -1;
    }

    p = concat_dir_and_file (s, "..");
    {
        vfs_path_t *vpath = vfs_path_from_str (p);
        q = vfs_path_to_str (vpath);
        vfs_path_free (vpath);
    }
    g_free (p);

    result = my_mkdir_rec (q, mode);
    if (result == 0)
        result = mc_mkdir (s_vpath, mode);

    vfs_path_free (s_vpath);
    g_free (q);
    return result;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

int
my_mkdir (const char *s, mode_t mode)
{
    int result;
    char *my_s;
    vfs_path_t *s_vpath = vfs_path_from_str (s);

    result = mc_mkdir (s_vpath, mode);

    if (result)
    {
        char *p;
        p = vfs_path_to_str (s_vpath);
        result = my_mkdir_rec (p, mode);
        g_free (p);
    }
    if (result == 0)
    {
        my_s = get_absolute_name (s);

#ifdef FIXME
        tree_add_entry (tree, my_s);
#endif

        g_free (my_s);
    }
    vfs_path_free (s_vpath);
    return result;
}

/* --------------------------------------------------------------------------------------------- */

int
my_rmdir (const char *s)
{
    int result;
    char *my_s;
    vfs_path_t *vpath = vfs_path_from_str (s);
#ifdef FIXME
    WTree *tree = 0;
#endif

    /* FIXME: Should receive a Wtree! */
    result = mc_rmdir (vpath);
    if (result == 0)
    {
        my_s = get_absolute_name (s);

#ifdef FIXME
        tree_remove_entry (tree, my_s);
#endif

        g_free (my_s);
    }
    vfs_path_free (vpath);
    return result;
}

/* --------------------------------------------------------------------------------------------- */
