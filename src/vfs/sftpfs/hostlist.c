/*
   sftp hostlist

   Copyright (C) 2011
   The Free Software Foundation, Inc.

   Written by:
   ilia maslakov 2011

   based on hotlist.c

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

/** \file hostlist.c
 *  \brief Source: hostlist
 */

#include <config.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/global.h"

#include "lib/tty/tty.h"        /* COLS */
#include "lib/tty/key.h"        /* KEY_M_CTRL */
#include "lib/skin.h"           /* colors */
#include "lib/mcconfig.h"       /* Load/save directories hotlist */
#include "lib/fileloc.h"
#include "lib/strutil.h"
#include "lib/vfs/vfs.h"
#include "lib/util.h"
#include "lib/widget.h"
#include "lib/fileloc.h"

#include "src/setup.h"          /* For profile_bname */
#include "src/history.h"

#include "src/filemanager/midnight.h"           /* current_panel */
#include "src/filemanager/command.h"            /* cmdline */
#include "src/keybind-defaults.h"               /* main_map */

#include "dialogs.h"
#include "sftpfs.h"
#include "hostlist.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

static WListbox *l_hostlist;
static WButtonBar *hostlist_bar;
static Dlg_head *hostlist_dlg;

static int list_left_pos = 0;
static int list_top_pos = 0;
static int list_height = 10;
static int list_width = 10;
static gboolean hostlist_empty = TRUE;

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
hostlist_unlink_entry (char *entry)
{
    char *profile;
    mc_config_t *sftpfs_config;
    char *title;
    int result;

    if (entry == NULL)
        return;

    title = g_strconcat (_("Remove:"), " ", str_trunc (entry, 30), (char *) NULL);

    if (safe_delete)
        query_set_sel (1);
    result = query_dialog (title, _("Are you sure you want to remove this entry?"),
                           D_ERROR, 2, _("&Yes"), _("&No"));
    g_free (title);

    if (result != 0)
        return;

    profile = g_build_filename (mc_config_get_path (), SFTP_HOSTLIST_FILE, NULL);
    sftpfs_config = mc_config_init (profile);
    g_free (profile);
    mc_config_del_group (sftpfs_config, entry);
    mc_config_save_file (sftpfs_config, NULL);
    mc_config_deinit (sftpfs_config);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
hostlist_fill_listbox (void)
{
    char *profile;
    mc_config_t *sftpfs_config;
    gchar **group_names, **orig_group_names;
    int i = 0;

    profile = g_build_filename (mc_config_get_path (), SFTP_HOSTLIST_FILE, NULL);
    sftpfs_config = mc_config_init (profile);
    g_free (profile);

    orig_group_names = group_names = mc_config_get_groups (sftpfs_config , NULL);

    while (*group_names != NULL)
    {
        listbox_add_item (l_hostlist, LISTBOX_APPEND_AT_END, 0, *group_names, NULL);
        group_names++;
        i++;
    }

    g_strfreev (orig_group_names);
    mc_config_deinit (sftpfs_config);
    if (i == 0)
    {
        hostlist_empty = TRUE;
        listbox_add_item (l_hostlist, LISTBOX_APPEND_AT_END, 0, _("Press S-F4 to edit new session"), NULL);
        return FALSE;
    }

    hostlist_empty = FALSE;
    return TRUE;

}

/* --------------------------------------------------------------------------------------------- */

static void
hostlist_reload_list (void)
{
    char *text = NULL;
    char *sel = NULL;
    int cur_pos = 0;

    if (!hostlist_empty)
    {
        listbox_get_current (l_hostlist, &text, NULL);
        if (text != NULL)
        {
            sel = g_strdup (text);
            listbox_remove_list (l_hostlist);
        }
    }
    else
    {
        /* remove entry "Press S-F4..." */
        listbox_remove_list (l_hostlist);
    }

    (void) hostlist_fill_listbox ();

    if (!hostlist_empty && text != NULL)
    {
        cur_pos = listbox_search_text (l_hostlist, sel);
        g_free (sel);
    }
    listbox_select_entry (l_hostlist, cur_pos);
}

/* --------------------------------------------------------------------------------------------- */

static void
hostlist_refresh (Dlg_head * dlg)
{
    common_dialog_repaint (dlg);
    buttonbar_redraw (hostlist_bar);
}

/* --------------------------------------------------------------------------------------------- */

static void
hostlist_set_pos (void)
{
    list_height = LINES - 8;

    if (mc_global.keybar_visible)
        list_height--;

    if (mc_global.message_visible)
        list_height--;

    if (command_prompt)
        list_height--;

    if (menubar_visible)
        list_height--;

    if (horizontal_split)
    {
        list_height =  list_height / 2;
        list_width = COLS;
        list_left_pos = 3;
        if (MENU_PANEL_IDX == 1)
            list_top_pos = list_height + 6;
        else
            list_top_pos = 2;
        list_width -= 8;
    }
    else
    {
        list_top_pos = 4;
        list_width = COLS / 2;
        if (MENU_PANEL_IDX == 1)
            list_left_pos = list_width + 3;
        else
            list_left_pos = 3;
        list_width -= 6;
    }
}

/* --------------------------------------------------------------------------------------------- */

static int
hostlist_button_callback (WButton * button, int action)
{
    (void) button;

    switch (action)
    {
    case B_ENTER:
        {
            return MSG_HANDLED;
            /* Fall through - go up */
        }
        /* Fall through if list empty - just go up */
    default:
        return MSG_HANDLED;
    }
}



/* --------------------------------------------------------------------------------------------- */

static inline cb_ret_t
hostlist_handle_key (Dlg_head * h, int key)
{
    char *text = NULL;
    int cur_pos = 0;
    switch (key)
    {
    case '\n':
    case KEY_ENTER:
        if (hostlist_empty)
        {
            configure_sftpfs_conn (NULL);
            hostlist_reload_list ();
            send_message ((Widget *) l_hostlist, WIDGET_DRAW, 0);
            hostlist_refresh (h);
        }
        else if (hostlist_button_callback (NULL, B_ENTER))
        {
            h->ret_value = B_ENTER;
            dlg_stop (h);
        }
        return MSG_HANDLED;

    case KEY_F (4):
        if (!hostlist_empty)
            listbox_get_current (l_hostlist, &text, NULL);
        if (text != NULL)
            sftpfs_load_param  (text);
        configure_sftpfs_conn (text);
        hostlist_reload_list ();
        send_message ((Widget *) l_hostlist, WIDGET_DRAW, 0);
        return MSG_HANDLED;

    case KEY_F (8):
        listbox_get_current (l_hostlist, &text, NULL);
        if (text != NULL)
        {
            cur_pos = listbox_search_text (l_hostlist, text);
            hostlist_unlink_entry (text);
        }
        hostlist_reload_list ();
        listbox_select_entry (l_hostlist, cur_pos);
        send_message ((Widget *) l_hostlist, WIDGET_DRAW, 0);
        return MSG_HANDLED;

    case KEY_F (14):
        configure_sftpfs_conn (NULL);
        hostlist_reload_list ();
        send_message ((Widget *) l_hostlist, WIDGET_DRAW, 0);
        hostlist_refresh (h);
        return MSG_HANDLED;
    default:
        return MSG_NOT_HANDLED;
    }
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
hostlist_callback (Dlg_head * h, Widget * sender, dlg_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case DLG_POST_KEY:
        dlg_select_widget (l_hostlist);
        /* always stay on hostlist */
        /* fall through */

    case DLG_INIT:
        tty_setcolor (MENU_ENTRY_COLOR);
        return MSG_HANDLED;

    case DLG_RESIZE:
        /* simply call dlg_set_size() with new size */
        hostlist_set_pos ();
        dlg_set_size (h, list_height, list_width);
        dlg_set_position (h, list_top_pos, list_left_pos,
                          list_top_pos + list_height, list_left_pos + list_width);
        widget_set_size ((Widget *) hostlist_bar, LINES - 1, 0, 1, COLS);
        return MSG_HANDLED;

    case DLG_UNHANDLED_KEY:
        return hostlist_handle_key (h, parm);

    default:
        return default_dlg_callback (h, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static lcback_ret_t
l_call (WListbox * list)
{
    Dlg_head *dlg = list->widget.owner;

    if (list->count != 0)
    {
        void *data = NULL;

        listbox_get_current (list, NULL, &data);

        if (data != NULL)
        {
            dlg->ret_value = B_ENTER;
            dlg_stop (dlg);
            return LISTBOX_DONE;
        }
        else
        {
            dlg->ret_value = B_ENTER;
            dlg_stop (dlg);
            return LISTBOX_DONE;
        }
    }

    return LISTBOX_CONT;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
hostlist_init (void)
{
    const char *title = N_("SFTP sessions");
    const char *help_node = "[SFTP (SSH File Transfer Protocol) filesystem]";

#ifdef ENABLE_NLS
    title = _(title);
#endif

    hostlist_set_pos ();
    do_refresh ();

    hostlist_dlg =
        create_dlg (TRUE, list_top_pos, list_left_pos, list_height, list_width, dialog_colors,
                    hostlist_callback, help_node, title, DLG_NONE);

    /* get new listbox */
    l_hostlist = listbox_new (2, 1, list_height - 4, list_width - 3, FALSE, l_call);
    (void) hostlist_fill_listbox ();
    add_widget_autopos (hostlist_dlg, l_hostlist, WPOS_KEEP_ALL);

    hostlist_bar = buttonbar_new (TRUE);
    add_widget (hostlist_dlg, hostlist_bar);

    /* restore ButtonBar coordinates after add_widget() */
    ((Widget *) hostlist_bar)->x = 0;
    ((Widget *) hostlist_bar)->y = LINES - 1;

    buttonbar_set_label (hostlist_bar, 1, Q_ ("ButtonBar|Help"), NULL, NULL);
    buttonbar_set_label (hostlist_bar, 2, "", NULL, NULL);
    buttonbar_set_label (hostlist_bar, 3, "", NULL, NULL);
    buttonbar_set_label (hostlist_bar, 4, Q_ ("ButtonBar|Edit"), NULL, NULL);
    buttonbar_set_label (hostlist_bar, 5, "", NULL, NULL);
    buttonbar_set_label (hostlist_bar, 6, "", NULL, NULL);
    buttonbar_set_label (hostlist_bar, 7, "", NULL, NULL);
    buttonbar_set_label (hostlist_bar, 8, Q_ ("ButtonBar|Delete"), NULL, NULL);
    buttonbar_set_label (hostlist_bar, 9, "", NULL, NULL);
    buttonbar_set_label (hostlist_bar, 10, Q_ ("ButtonBar|Quit"), NULL, NULL);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Destroy the list dialog.
 * Don't confuse with hostlist_done () for the list in memory.
 */

void
hostlist_done (void)
{
    destroy_dlg (hostlist_dlg);
    l_hostlist = NULL;
    if (0)
        update_panels (UP_OPTIMIZE, UP_KEEPSEL);
    repaint_screen ();
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

char *
hostlist_show (void)
{
    char *target = NULL;

    (void) hostlist_init ();

    /* display file info */
    tty_setcolor (SELECTED_COLOR);

    switch (run_dlg (hostlist_dlg))
    {
    case B_ENTER:
        {
            char *text = NULL;
            listbox_get_current (l_hostlist, &text, NULL);
            target = g_strdup (text);

            break;
        }

    default:
        break;
    }                           /* switch */

    hostlist_done ();
    return target;
}

/* --------------------------------------------------------------------------------------------- */
