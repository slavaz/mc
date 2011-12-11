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
#include <stdlib.h>

#include "lib/global.h"
#include "lib/mcconfig.h"       /* Load/save user formats */
#include "lib/widget.h"

#include "sftpfs.h"
#include "dialogs.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define VFSX 60
#define VFSY 21

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
sftpfs_conn_callback (Dlg_head * h, Widget * sender, dlg_msg_t msg, int parm, void *data)
{

    switch (msg)
    {
    case DLG_INIT:
        sender = dlg_find_by_id (h, 4);
        /* FALLTHROUGH */

    case DLG_ACTION:
        /* message from radiobuttons checkbutton */
        if (sender != NULL && sender->id == 4)
        {
            const gboolean disable = (((WRadio *) sender)->sel != 1);
            Widget *w;

            /* input */
            w = dlg_find_by_id (h, 2);
            widget_disable (*w, disable);
            if (msg == DLG_ACTION)
                send_message (w, WIDGET_DRAW, 0);
            /* label */
            w = dlg_find_by_id (h, 3);
            widget_disable (*w, disable);
            if (msg == DLG_ACTION)
                send_message (w, WIDGET_DRAW, 0);

            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;

    default:
        return default_dlg_callback (h, sender, msg, parm, data);
    }
}

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
    return;
}

/* --------------------------------------------------------------------------------------------- */

void
configure_sftpfs_conn (const char *sftpfs_sessionname)
{
    char buffer1[10] = "\0";            /* port */
    char buffer2[200] = "\0";           /* host */
    char buffer3[BUF_4K] = "\0";        /* private key */
    char buffer5[200] = "\0";           /* session name */
    char buffer6[100] = "\0";           /* user name */

    char *tmp_privkey = NULL;
    char *tmp_sftp_host = NULL;
    char *tmp_sftp_port = NULL;
    char *tmp_sessionname = g_strdup (sftpfs_sessionname);
    int tmp_auth_method = sftpfs_auth_method;
    char *tmp_username = NULL;
    char *tmp_pre_port = g_strdup_printf ("%i", sftpfs_port);

    const char *auth_names[] = {
        N_("&Password"),
        N_("SSH &key"),
        N_("SSH-&Agent"),
    };

    QuickWidget sftpfs_widgets[] = {
        /*  0 */ QUICK_BUTTON (35, VFSX, VFSY - 3, VFSY, N_("&Cancel"), B_CANCEL, NULL),
        /*  1 */ QUICK_BUTTON (15, VFSX, VFSY - 3, VFSY, N_("&Save"), B_EXIT, NULL),
        /*  2 */ QUICK_INPUT (4, VFSX, 14, VFSY,
                              sftpfs_prepare_buffers (sftpfs_privkey, buffer3, sizeof (buffer3)),
                              VFSX - 9, 2, "input-sftp-priv-key", &tmp_privkey),
        /*  3 */ QUICK_LABEL (4, VFSX, 13, VFSY, N_("SSH private key:")),

        /*  4 */ QUICK_RADIO (4, VFSX, 10, VFSY, 3, auth_names, (int *) &tmp_auth_method),
        /*  5 */ QUICK_LABEL (4, VFSX, 9, VFSY, N_("Auth method:")),
        /*  6 */ QUICK_INPUT (4, VFSX, 8, VFSY,
                              sftpfs_prepare_buffers (sftpfs_user, buffer6, sizeof (buffer6)),
                              VFSX - 9, 2, "input-sftp-user", &tmp_username),
        /*  7 */ QUICK_LABEL (4, VFSX, 7, VFSY, N_("User name:")),
        /*  8 */ QUICK_INPUT (45, VFSX, 6, VFSY,
                              sftpfs_prepare_buffers (tmp_pre_port, buffer1, sizeof (buffer1)),
                              10, 0, "input-sftp-port", &tmp_sftp_port),
        /*  9 */ QUICK_LABEL (45, VFSX, 5, VFSY, N_("Port:")),
        /* 10 */ QUICK_INPUT (4, VFSX, 6, VFSY,
                              sftpfs_prepare_buffers (sftpfs_host, buffer2, sizeof (buffer2)),
                              40, 2, "input-sftp-host", &tmp_sftp_host),
        /* 11 */ QUICK_LABEL (4, VFSX, 5, VFSY, N_("Host:")),
        /* 12 */ QUICK_INPUT (4, VFSX, 4, VFSY,
                              sftpfs_prepare_buffers (sftpfs_sessionname, buffer5, sizeof (buffer5)),
                              VFSX - 9, 2, "input-sftp-session", &tmp_sessionname),
        /* 13 */ QUICK_LABEL (4, VFSX, 3, VFSY, N_("Session name:")),

        QUICK_END
    };

    QuickDialog confvfs_dlg = {
        VFSX, VFSY, -1, -1, N_("SFTP File System Settings"),
        "[SFTP (SSH File Transfer Protocol) filesystem]", sftpfs_widgets,
        sftpfs_conn_callback,
        FALSE
    };

    g_free (tmp_pre_port);

    if (quick_dialog (&confvfs_dlg) != B_CANCEL)
    {
        char *new_sessionname = NULL;

        g_free (sftpfs_pubkey);
        g_free (sftpfs_privkey);
        g_free (sftpfs_host);
        g_free (sftpfs_user);

        sftpfs_privkey = tmp_privkey;
        sftpfs_pubkey = g_strdup_printf ("%s.pub", tmp_privkey);
        sftpfs_host = tmp_sftp_host;
        sftpfs_port = atoi (tmp_sftp_port);
        sftpfs_auth_method = tmp_auth_method;
        sftpfs_user = tmp_username;

        if ((tmp_sessionname != NULL) && (tmp_sessionname[0] != '\0'))
            sftpfs_save_param (tmp_sessionname);
        else
        {
            new_sessionname = g_strdup_printf ("%s@%s", sftpfs_user, sftpfs_host);
            sftpfs_save_param (new_sessionname);
            g_free (new_sessionname);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */
