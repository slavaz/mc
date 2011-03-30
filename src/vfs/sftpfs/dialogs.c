/* Dialog boxes for sftpfs.

   Copyright (C)
   2011 Free Software Foundation, Inc.

   Authors:
   2011 Maslakov Ilia

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

/** \file dialogs.c
 *  \brief Source: Dialog boxes for the SFTP
 */

#include <config.h>

#include "lib/global.h"
#include "lib/mcconfig.h"       /* Load/save user formats */
#include "lib/widget.h"

#include "sftpfs.h"
#include "dialogs.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define VFSX 56
#define VFSY 17

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static const char *
sftpfs_prepare_buffers (const char *from, char *to, size_t to_len)
{
    if (from != NULL)
        g_strlcpy (to, from, to_len);
    else
        *to = '\0';
    return to;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
configure_sftpfs (void)
{
    char buffer1[BUF_4K] = "\0";
    char buffer2[BUF_4K] = "\0";
    char *tmp_pubkey = NULL;
    char *tmp_privkey = NULL;

    QuickWidget confvfs_widgets[] = {
        /*  0 */ QUICK_BUTTON (30, VFSX, VFSY - 3, VFSY, N_("&Cancel"), B_CANCEL, NULL),
        /*  1 */ QUICK_BUTTON (12, VFSX, VFSY - 3, VFSY, N_("&Save"), B_EXIT, NULL),
        /*  2 */ QUICK_INPUT (4, VFSX, 6, VFSY,
                              sftpfs_prepare_buffers (sftpfs_pubkey, buffer1, sizeof (buffer1)),
                              VFSX - 12, 2, "input-pub-key", &tmp_pubkey),
        /*  3 */ QUICK_LABEL (4, VFSX, 5, VFSY, N_("SSH public key:")),
        /*  4 */ QUICK_INPUT (4, VFSX, 4, VFSY,
                              sftpfs_prepare_buffers (sftpfs_privkey, buffer2, sizeof (buffer2)),
                              VFSX - 12, 2, "input-priv-key", &tmp_privkey),
        /*  5 */ QUICK_LABEL (4, VFSX, 3, VFSY, N_("SSH private key:")),

        QUICK_END
    };

    QuickDialog confvfs_dlg = {
        VFSX, VFSY, -1, -1, N_("SFTP File System Settings"),
        "[Virtual FS]", confvfs_widgets,
        NULL,
        FALSE
    };

    if (quick_dialog (&confvfs_dlg) == B_EXIT)
    {
        g_free (sftpfs_pubkey);
        g_free (sftpfs_privkey);
        sftpfs_pubkey = tmp_pubkey;
        sftpfs_privkey = tmp_privkey;
        mc_log ("sftpfs_save_param\n");
        sftpfs_save_param ();
    }
}

/* --------------------------------------------------------------------------------------------- */
