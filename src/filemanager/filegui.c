/*
   File management GUI for the text mode edition

   The copy code was based in GNU's cp, and was written by:
   Torbjorn Granlund, David MacKenzie, and Jim Meyering.

   The move code was based in GNU's mv, and was written by:
   Mike Parker and David MacKenzie.

   Janne Kukonlehto added much error recovery to them for being used
   in an interactive program.

   Copyright (C) 1994, 1995, 1996, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005, 2006, 2007, 2009, 2011
   The Free Software Foundation, Inc.

   Written by:
   Janne Kukonlehto, 1994, 1995
   Fred Leeflang, 1994, 1995
   Miguel de Icaza, 1994, 1995, 1996
   Jakub Jelinek, 1995, 1996
   Norbert Warmuth, 1997
   Pavel Machek, 1998
   Slava Zanko, 2009

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

/*
 * Please note that all dialogs used here must be safe for background
 * operations.
 */

/** \file  filegui.c
 *  \brief Source: file management GUI for the text mode edition
 */

/* {{{ Include files */

#include <config.h>

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(STAT_STATVFS) \
     && (defined(HAVE_STRUCT_STATVFS_F_BASETYPE) \
         || defined(HAVE_STRUCT_STATVFS_F_FSTYPENAME))
#include <sys/statvfs.h>
#define STRUCT_STATFS struct statvfs
#define STATFS statvfs
#elif defined(HAVE_STATFS) && !defined(STAT_STATFS4)
#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#elif defined(HAVE_SYS_MOUNT_H) && defined(HAVE_SYS_PARAM_H)
#include <sys/param.h>
#include <sys/mount.h>
#elif defined(HAVE_SYS_STATFS_H)
#include <sys/statfs.h>
#endif
#define STRUCT_STATFS struct statfs
#define STATFS statfs
#endif

#include <unistd.h>

#include "lib/global.h"

#include "lib/tty/key.h"        /* tty_get_event */
#include "lib/mcconfig.h"
#include "lib/search.h"
#include "lib/vfs/vfs.h"
#include "lib/strescape.h"
#include "lib/strutil.h"
#include "lib/timefmt.h"        /* file_date() */
#include "lib/util.h"
#include "lib/widget.h"

#include "src/setup.h"          /* verbose */

#include "midnight.h"
#include "fileopctx.h"          /* FILE_CONT */

#include "filegui.h"

/* }}} */

/*** global variables ****************************************************************************/

int classic_progressbar = 1;

/*** file scope macro definitions ****************************************************************/

/* Hack: the vfs code should not rely on this */
#define WITH_FULL_PATHS 1

/* File operate window sizes */
#define WX 58
#define WY 11
#define FCOPY_LABEL_X 3

#define truncFileString(ui, s)       str_trunc (s, 52)
#define truncFileStringSecure(ui, s) path_trunc (s, 52)

/*** file scope type declarations ****************************************************************/

/* *INDENT-OFF* */
typedef enum {
    MSDOS_SUPER_MAGIC     = 0x4d44,
    NTFS_SB_MAGIC         = 0x5346544e,
    FUSE_MAGIC         = 0x65735546,
    PROC_SUPER_MAGIC      = 0x9fa0,
    SMB_SUPER_MAGIC       = 0x517B,
    NCP_SUPER_MAGIC       = 0x564c,
    USBDEVICE_SUPER_MAGIC = 0x9fa2
} filegui_nonattrs_fs_t;
/* *INDENT-ON* */

/* Used for button result values */
typedef enum
{
    REPLACE_YES = B_USER,
    REPLACE_NO,
    REPLACE_APPEND,
    REPLACE_ALWAYS,
    REPLACE_UPDATE,
    REPLACE_NEVER,
    REPLACE_ABORT,
    REPLACE_SIZE,
    REPLACE_REGET
} replace_action_t;

/* This structure describes the UI and internal data required by a file
 * operation context.
 */
typedef struct
{
    /* ETA and bps */
    gboolean showing_eta;
    gboolean showing_bps;

    /* Dialog and widgets for the operation progress window */
    Dlg_head *op_dlg;
    WLabel *file_string[2];
    WLabel *file_label[2];
    WGauge *progress_file_gauge;
    WLabel *progress_file_label;

    WGauge *progress_total_gauge;

    WLabel *total_files_processed_label;
    WLabel *time_label;
    WLabel *total_bytes_label;

    /* Query replace dialog */
    Dlg_head *replace_dlg;
    const char *replace_filename;
    replace_action_t replace_result;

    struct stat *s_stat, *d_stat;
} FileOpContextUI;

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static gboolean
filegui__check_attrs_on_fs (const char *fs_path)
{
#ifdef STATFS
    STRUCT_STATFS stfs;

    if (!setup_copymove_persistent_attr)
        return FALSE;

    if (STATFS (fs_path, &stfs) != 0)
        return TRUE;

#ifdef __linux__
    switch ((filegui_nonattrs_fs_t) stfs.f_type)
    {
    case MSDOS_SUPER_MAGIC:
    case NTFS_SB_MAGIC:
    case PROC_SUPER_MAGIC:
    case SMB_SUPER_MAGIC:
    case NCP_SUPER_MAGIC:
    case USBDEVICE_SUPER_MAGIC:
        return FALSE;
    default:
        break;
    }
#elif defined(HAVE_STRUCT_STATFS_F_FSTYPENAME) \
      || defined(HAVE_STRUCT_STATVFS_F_FSTYPENAME)
    if (!strcmp (stfs.f_fstypename, "msdos")
        || !strcmp (stfs.f_fstypename, "msdosfs")
        || !strcmp (stfs.f_fstypename, "ntfs")
        || !strcmp (stfs.f_fstypename, "procfs")
        || !strcmp (stfs.f_fstypename, "smbfs") || strstr (stfs.f_fstypename, "fusefs"))
        return FALSE;
#elif defined(HAVE_STRUCT_STATVFS_F_BASETYPE)
    if (!strcmp (stfs.f_basetype, "pcfs")
        || !strcmp (stfs.f_basetype, "ntfs")
        || !strcmp (stfs.f_basetype, "proc")
        || !strcmp (stfs.f_basetype, "smbfs") || !strcmp (stfs.f_basetype, "fuse"))
        return FALSE;
#endif
#endif /* STATFS */

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
file_frmt_time (char *buffer, double eta_secs)
{
    int eta_hours, eta_mins, eta_s;
    eta_hours = eta_secs / (60 * 60);
    eta_mins = (eta_secs - (eta_hours * 60 * 60)) / 60;
    eta_s = eta_secs - (eta_hours * 60 * 60 + eta_mins * 60);
    g_snprintf (buffer, BUF_TINY, _("%d:%02d.%02d"), eta_hours, eta_mins, eta_s);
}

/* --------------------------------------------------------------------------------------------- */

static void
file_eta_prepare_for_show (char *buffer, double eta_secs, gboolean always_show)
{
    char _fmt_buff[BUF_TINY];
    if (eta_secs <= 0.5 && !always_show)
    {
        *buffer = '\0';
        return;
    }
    if (eta_secs <= 0.5)
        eta_secs = 1;
    file_frmt_time (_fmt_buff, eta_secs);
    g_snprintf (buffer, BUF_TINY, _("ETA %s"), _fmt_buff);
}

/* --------------------------------------------------------------------------------------------- */

static void
file_bps_prepare_for_show (char *buffer, long bps)
{
    if (bps > 1024 * 1024)
    {
        g_snprintf (buffer, BUF_TINY, _("%.2f MB/s"), bps / (1024 * 1024.0));
    }
    else if (bps > 1024)
    {
        g_snprintf (buffer, BUF_TINY, _("%.2f KB/s"), bps / 1024.0);
    }
    else if (bps > 1)
    {
        g_snprintf (buffer, BUF_TINY, _("%ld B/s"), bps);
    }
    else
        *buffer = '\0';
}

/* --------------------------------------------------------------------------------------------- */
/*
 * FIXME: probably it is better to replace this with quick dialog machinery,
 * but actually I'm not familiar with it and have not much time :(
 *   alex
 */
static replace_action_t
overwrite_query_dialog (FileOpContext * ctx, enum OperationMode mode)
{
#define ADD_RD_BUTTON(i) \
    add_widget (ui->replace_dlg, \
            button_new (rd_widgets [i].ypos, rd_widgets [i].xpos, rd_widgets [i].value, \
                        NORMAL_BUTTON, rd_widgets [i].text, 0))

#define ADD_RD_LABEL(i, p1, p2) \
    g_snprintf (buffer, sizeof (buffer), rd_widgets [i].text, p1, p2); \
    add_widget (ui->replace_dlg, label_new (rd_widgets [i].ypos, rd_widgets [i].xpos, buffer))

    /* dialog sizes */
    const int rd_ylen = 17;
    int rd_xlen = 60;

    struct
    {
        const char *text;
        int ypos, xpos;
        int value;              /* 0 for labels */
    } rd_widgets[] =
    {
    /* *INDENT-OFF* */
        /*  0 */
        { N_("Target file already exists!"), 3, 4, 0 },
        /*  1 */
        { "%s", 4, 4, 0 },
        /*  2 */ /* cannot use PRIuMAX here; %llu is used instead */
        { N_("Source date: %s, size %llu"), 6, 4, 0 },
        /*  3 */  /* cannot use PRIuMAX here; %llu is used instead */
        { N_("Target date: %s, size %llu"), 7, 4, 0 },
        /*  4 */
        { N_("&Abort"), 14, 25, REPLACE_ABORT },
        /*  5 */
        { N_("If &size differs"), 12, 28, REPLACE_SIZE },
        /*  6 */
        { N_("Non&e"), 11, 47, REPLACE_NEVER },
        /*  7 */
        { N_("&Update"), 11, 36, REPLACE_UPDATE },
        /*  8 */
        { N_("A&ll"), 11, 28, REPLACE_ALWAYS },
        /*  9 */
        { N_("Overwrite all targets?"), 11, 4, 0 },
        /* 10 */
        { N_("&Reget"), 10, 28, REPLACE_REGET },
        /* 11 */
        { N_("A&ppend"), 9, 45, REPLACE_APPEND },
        /* 12 */
        { N_("&No"), 9, 37, REPLACE_NO },
        /* 13 */
        { N_("&Yes"), 9, 28, REPLACE_YES },
        /* 14 */
        { N_("Overwrite this target?"), 9, 4, 0 }
    /* *INDENT-ON* */
    };

    const int num = sizeof (rd_widgets) / sizeof (rd_widgets[0]);
    int *widgets_len;

    FileOpContextUI *ui = ctx->ui;

    char buffer[BUF_SMALL];
    const char *title;
    const char *stripped_name = strip_home_and_password (ui->replace_filename);
    int stripped_name_len;

    int result;

    widgets_len = g_new0 (int, num);

    if (mode == Foreground)
        title = _("File exists");
    else
        title = _("Background process: File exists");

    stripped_name_len = str_term_width1 (stripped_name);

    {
        int i, l1, l2, l, row;

        for (i = 0; i < num; i++)
        {
#ifdef ENABLE_NLS
            if (i != 1)         /* skip filename */
                rd_widgets[i].text = _(rd_widgets[i].text);
#endif /* ENABLE_NLS */
            widgets_len[i] = str_term_width1 (rd_widgets[i].text);
        }

        /*
         * longest of "Overwrite..." labels
         * (assume "Target date..." are short enough)
         */
        l1 = max (widgets_len[9], widgets_len[14]);

        /* longest of button rows */
        i = num;
        for (row = l = l2 = 0; i--;)
            if (rd_widgets[i].value != 0)
            {
                if (row != rd_widgets[i].ypos)
                {
                    row = rd_widgets[i].ypos;
                    l2 = max (l2, l);
                    l = 0;
                }
                l += widgets_len[i] + 4;
            }

        l2 = max (l2, l);       /* last row */
        rd_xlen = max (rd_xlen, l1 + l2 + 8);
        rd_xlen = max (rd_xlen, str_term_width1 (title) + 2);
        rd_xlen = max (rd_xlen, min (COLS, stripped_name_len + 8));

        /* Now place widgets */
        l1 += 5;                /* start of first button in the row */
        i = num;
        for (l = l1, row = 0; --i > 1;)
            if (rd_widgets[i].value != 0)
            {
                if (row != rd_widgets[i].ypos)
                {
                    row = rd_widgets[i].ypos;
                    l = l1;
                }
                rd_widgets[i].xpos = l;
                l += widgets_len[i] + 4;
            }

        /* Abort button is centered */
        rd_widgets[4].xpos = (rd_xlen - widgets_len[4] - 3) / 2;
    }

    /* FIXME - missing help node */
    ui->replace_dlg =
        create_dlg (TRUE, 0, 0, rd_ylen, rd_xlen, alarm_colors, NULL, "[Replace]",
                    title, DLG_CENTER | DLG_REVERSE);

    /* prompt -- centered */
    add_widget (ui->replace_dlg,
                label_new (rd_widgets[0].ypos, (rd_xlen - widgets_len[0]) / 2, rd_widgets[0].text));
    /* file name -- centered */
    stripped_name = str_trunc (stripped_name, rd_xlen - 8);
    stripped_name_len = str_term_width1 (stripped_name);
    add_widget (ui->replace_dlg,
                label_new (rd_widgets[1].ypos, (rd_xlen - stripped_name_len) / 2, stripped_name));

    /* source date and size */
    ADD_RD_LABEL (2, file_date (ui->s_stat->st_mtime), (unsigned long long) ui->s_stat->st_size);
    /* destination date and size */
    ADD_RD_LABEL (3, file_date (ui->d_stat->st_mtime), (unsigned long long) ui->d_stat->st_size);

    ADD_RD_BUTTON (4);          /* Abort */
    ADD_RD_BUTTON (5);          /* If size differs */
    ADD_RD_BUTTON (6);          /* None */
    ADD_RD_BUTTON (7);          /* Update */
    ADD_RD_BUTTON (8);          /* All" */
    ADD_RD_LABEL (9, 0, 0);     /* Overwrite all targets? */

    /* "this target..." widgets */
    if (!S_ISDIR (ui->d_stat->st_mode))
    {
        if ((ctx->operation == OP_COPY) && (ui->d_stat->st_size != 0)
            && (ui->s_stat->st_size > ui->d_stat->st_size))
            ADD_RD_BUTTON (10); /* Reget */

        ADD_RD_BUTTON (11);     /* Append */
    }
    ADD_RD_BUTTON (12);         /* No */
    ADD_RD_BUTTON (13);         /* Yes */
    ADD_RD_LABEL (14, 0, 0);    /* Overwrite this target? */

    result = run_dlg (ui->replace_dlg);
    destroy_dlg (ui->replace_dlg);

    g_free (widgets_len);

    return (result == B_CANCEL) ? REPLACE_ABORT : (replace_action_t) result;
#undef ADD_RD_LABEL
#undef ADD_RD_BUTTON
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
is_wildcarded (char *p)
{
    for (; *p; p++)
    {
        if (*p == '*')
            return TRUE;
        if (*p == '\\' && p[1] >= '1' && p[1] <= '9')
            return TRUE;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

FileProgressStatus
check_progress_buttons (FileOpContext * ctx)
{
    int c;
    Gpm_Event event;
    FileOpContextUI *ui;

    g_return_val_if_fail (ctx->ui != NULL, FILE_CONT);

    ui = ctx->ui;

    event.x = -1;               /* Don't show the GPM cursor */
    c = tty_get_event (&event, FALSE, FALSE);
    if (c == EV_NONE)
        return FILE_CONT;

    /* Reinitialize to avoid old values after events other than
       selecting a button */
    ui->op_dlg->ret_value = FILE_CONT;

    dlg_process_event (ui->op_dlg, c, &event);
    switch (ui->op_dlg->ret_value)
    {
    case FILE_SKIP:
        return FILE_SKIP;
    case B_CANCEL:
    case FILE_ABORT:
        return FILE_ABORT;
    default:
        return FILE_CONT;
    }
}


/* --------------------------------------------------------------------------------------------- */
/* {{{ File progress display routines */

void
file_op_context_create_ui_without_init (FileOpContext * ctx, gboolean with_eta,
                                        filegui_dialog_type_t dialog_type)
{
    FileOpContextUI *ui;
    int minus = 0, total_reserve = 0;
    const char *abort_button_label = N_("&Abort");
    const char *skip_button_label = N_("&Skip");
    int abort_button_width, skip_button_width, buttons_width;
    int dlg_width;

    g_return_if_fail (ctx != NULL);
    g_return_if_fail (ctx->ui == NULL);

#ifdef ENABLE_NLS
    abort_button_label = _(abort_button_label);
    skip_button_label = _(skip_button_label);
#endif

    abort_button_width = str_term_width1 (abort_button_label) + 3;
    skip_button_width = str_term_width1 (skip_button_label) + 3;
    buttons_width = abort_button_width + skip_button_width + 2;

    dlg_width = max (WX, buttons_width + 6);

    ui = g_new0 (FileOpContextUI, 1);
    ctx->ui = ui;

    ctx->dialog_type = dialog_type;

    switch (dialog_type)
    {
    case FILEGUI_DIALOG_ONE_ITEM:
        total_reserve = 0;
        minus = verbose ? 0 : 2;
        break;
    case FILEGUI_DIALOG_MULTI_ITEM:
        total_reserve = 5;
        minus = verbose ? 0 : 7;
        break;
    case FILEGUI_DIALOG_DELETE_ITEM:
        total_reserve = -5;
        minus = 0;
        break;
    }

    ctx->recursive_result = RECURSIVE_YES;

    ui->replace_result = REPLACE_YES;
    ui->showing_eta = with_eta;
    ui->showing_bps = with_eta;

    ui->op_dlg =
        create_dlg (TRUE, 0, 0, WY - minus + 1 + total_reserve, dlg_width,
                    dialog_colors, NULL, NULL, op_names[ctx->operation], DLG_CENTER | DLG_REVERSE);

    add_widget (ui->op_dlg,
                button_new (WY - minus - 2 + total_reserve,
                            dlg_width / 2 + 1, FILE_ABORT,
                            NORMAL_BUTTON, abort_button_label, NULL));
    add_widget (ui->op_dlg,
                button_new (WY - minus - 2 + total_reserve,
                            dlg_width / 2 - 1 - skip_button_width, FILE_SKIP,
                            NORMAL_BUTTON, skip_button_label, NULL));


    if (verbose && dialog_type == FILEGUI_DIALOG_MULTI_ITEM)
    {
        add_widget (ui->op_dlg, hline_new (8, 1, dlg_width - 2));

        add_widget (ui->op_dlg, ui->total_bytes_label = label_new (8, FCOPY_LABEL_X + 15, ""));

        add_widget (ui->op_dlg, ui->progress_total_gauge =
                    gauge_new (9, FCOPY_LABEL_X + 3, 0, 100, 0));

        add_widget (ui->op_dlg, ui->total_files_processed_label =
                    label_new (11, FCOPY_LABEL_X, ""));

        add_widget (ui->op_dlg, ui->time_label = label_new (12, FCOPY_LABEL_X, ""));
    }

    add_widget (ui->op_dlg, ui->progress_file_label = label_new (7, FCOPY_LABEL_X, ""));

    add_widget (ui->op_dlg, ui->progress_file_gauge = gauge_new (6, FCOPY_LABEL_X + 3, 0, 100, 0));

    add_widget (ui->op_dlg, ui->file_string[1] = label_new (5, FCOPY_LABEL_X, ""));

    add_widget (ui->op_dlg, ui->file_label[1] = label_new (4, FCOPY_LABEL_X, ""));
    add_widget (ui->op_dlg, ui->file_string[0] = label_new (3, FCOPY_LABEL_X, ""));
    add_widget (ui->op_dlg, ui->file_label[0] = label_new (2, FCOPY_LABEL_X, ""));

    if ((right_panel == current_panel) && !classic_progressbar)
    {
        ui->progress_file_gauge->from_left_to_right = FALSE;
        if (verbose && dialog_type == FILEGUI_DIALOG_MULTI_ITEM)
            ui->progress_total_gauge->from_left_to_right = FALSE;
    }
}

/* --------------------------------------------------------------------------------------------- */

void
file_op_context_create_ui (FileOpContext * ctx, gboolean with_eta,
                           filegui_dialog_type_t dialog_type)
{
    FileOpContextUI *ui;

    g_return_if_fail (ctx != NULL);
    g_return_if_fail (ctx->ui == NULL);

    file_op_context_create_ui_without_init (ctx, with_eta, dialog_type);
    ui = ctx->ui;

    /* We will manage the dialog without any help, that's why
       we have to call init_dlg */
    init_dlg (ui->op_dlg);
}

/* --------------------------------------------------------------------------------------------- */

void
file_op_context_destroy_ui (FileOpContext * ctx)
{
    g_return_if_fail (ctx != NULL);

    if (ctx->ui != NULL)
    {
        FileOpContextUI *ui = (FileOpContextUI *) ctx->ui;

        dlg_run_done (ui->op_dlg);
        destroy_dlg (ui->op_dlg);
        g_free (ui);
        ctx->ui = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
   show progressbar for file
 */

void
file_progress_show (FileOpContext * ctx, off_t done, off_t total,
                    const char *stalled_msg, gboolean force_update)
{
    FileOpContextUI *ui;
    char buffer[BUF_TINY];
    char buffer2[BUF_TINY];
    char buffer3[BUF_TINY];

    if (!verbose)
        return;

    g_return_if_fail (ctx != NULL);
    g_return_if_fail (ctx->ui != NULL);

    ui = ctx->ui;

    if (total == 0)
    {
        gauge_show (ui->progress_file_gauge, 0);
        return;
    }

    gauge_set_value (ui->progress_file_gauge, 1024, (int) (1024 * done / total));
    gauge_show (ui->progress_file_gauge, 1);

    if (!force_update)
        return;

    if (ui->showing_eta && ctx->eta_secs > 0.5)
    {
        file_eta_prepare_for_show (buffer2, ctx->eta_secs, FALSE);
        file_bps_prepare_for_show (buffer3, ctx->bps);
        g_snprintf (buffer, BUF_TINY, "%s (%s) %s", buffer2, buffer3, stalled_msg);
    }
    else
    {
        g_snprintf (buffer, BUF_TINY, "%s", stalled_msg);
    }

    label_set_text (ui->progress_file_label, buffer);
}

/* --------------------------------------------------------------------------------------------- */

void
file_progress_show_count (FileOpContext * ctx, size_t done, size_t total)
{
    char buffer[BUF_TINY];
    FileOpContextUI *ui;

    g_return_if_fail (ctx != NULL);
    g_return_if_fail (ctx->ui != NULL);

    ui = ctx->ui;
    g_snprintf (buffer, BUF_TINY, _("Files processed: %zu of %zu"), done, total);
    label_set_text (ui->total_files_processed_label, buffer);
}

/* --------------------------------------------------------------------------------------------- */

void
file_progress_show_total (FileOpTotalContext * tctx, FileOpContext * ctx, uintmax_t copyed_bytes,
                          gboolean show_summary)
{
    char buffer[BUF_TINY];
    char buffer2[BUF_TINY];
    char buffer3[BUF_TINY];
    char buffer4[BUF_TINY];
    struct timeval tv_current;
    FileOpContextUI *ui;

    g_return_if_fail (ctx != NULL);
    g_return_if_fail (ctx->ui != NULL);

    ui = ctx->ui;

    if (ctx->progress_bytes != 0)
    {
        gauge_set_value (ui->progress_total_gauge, 1024,
                         (int) (1024 * copyed_bytes / ctx->progress_bytes));
        gauge_show (ui->progress_total_gauge, 1);
    }
    else
        gauge_show (ui->progress_total_gauge, 0);

    if (!show_summary && tctx->bps == 0)
        return;

    gettimeofday (&tv_current, NULL);
    file_frmt_time (buffer2, tv_current.tv_sec - tctx->transfer_start.tv_sec);
    file_eta_prepare_for_show (buffer3, tctx->eta_secs, TRUE);
    file_bps_prepare_for_show (buffer4, (long) tctx->bps);

    g_snprintf (buffer, BUF_TINY, _("Time: %s  %s (%s)"), buffer2, buffer3, buffer4);
    label_set_text (ui->time_label, buffer);

    size_trunc_len (buffer2, 5, tctx->copyed_bytes, 0, panels_options.kilobyte_si);
    size_trunc_len (buffer3, 5, ctx->progress_bytes, 0, panels_options.kilobyte_si);

    g_snprintf (buffer, BUF_TINY, _("Total: %s of %s"), buffer2, buffer3);

    label_set_text (ui->total_bytes_label, buffer);
}

/* }}} */

/* --------------------------------------------------------------------------------------------- */

void
file_progress_show_source (FileOpContext * ctx, const char *s)
{
    FileOpContextUI *ui;

    g_return_if_fail (ctx != NULL);
    g_return_if_fail (ctx->ui != NULL);

    ui = ctx->ui;

    if (s != NULL)
    {
#ifdef WITH_FULL_PATHS
        size_t i;

        i = strlen (current_panel->cwd);

        /* We remove the full path we have added before */
        if (strncmp (s, current_panel->cwd, i) == 0)
            if (s[i] == PATH_SEP)
                s += i + 1;
#endif /* WITH_FULL_PATHS */

        label_set_text (ui->file_label[0], _("Source"));
        label_set_text (ui->file_string[0], truncFileString (ui, s));
    }
    else
    {
        label_set_text (ui->file_label[0], "");
        label_set_text (ui->file_string[0], "");
    }
}

/* --------------------------------------------------------------------------------------------- */

void
file_progress_show_target (FileOpContext * ctx, const char *s)
{
    FileOpContextUI *ui;

    g_return_if_fail (ctx != NULL);
    g_return_if_fail (ctx->ui != NULL);

    ui = ctx->ui;

    if (s != NULL)
    {
        label_set_text (ui->file_label[1], _("Target"));
        label_set_text (ui->file_string[1], truncFileStringSecure (ui, s));
    }
    else
    {
        label_set_text (ui->file_label[1], "");
        label_set_text (ui->file_string[1], "");
    }
}

/* --------------------------------------------------------------------------------------------- */

void
file_progress_show_deleting (FileOpContext * ctx, const char *s)
{
    FileOpContextUI *ui;

    g_return_if_fail (ctx != NULL);
    g_return_if_fail (ctx->ui != NULL);

    ui = ctx->ui;
    label_set_text (ui->file_label[0], _("Deleting"));
    label_set_text (ui->file_label[0], truncFileStringSecure (ui, s));
}

/* --------------------------------------------------------------------------------------------- */

FileProgressStatus
file_progress_real_query_replace (FileOpContext * ctx,
                                  enum OperationMode mode, const char *destname,
                                  struct stat *_s_stat, struct stat *_d_stat)
{
    FileOpContextUI *ui;

    g_return_val_if_fail (ctx != NULL, FILE_CONT);
    g_return_val_if_fail (ctx->ui != NULL, FILE_CONT);

    ui = ctx->ui;

    if (ui->replace_result < REPLACE_ALWAYS)
    {
        ui->replace_filename = destname;
        ui->s_stat = _s_stat;
        ui->d_stat = _d_stat;
        ui->replace_result = overwrite_query_dialog (ctx, mode);
    }

    switch (ui->replace_result)
    {
    case REPLACE_UPDATE:
        do_refresh ();
        if (_s_stat->st_mtime > _d_stat->st_mtime)
            return FILE_CONT;
        else
            return FILE_SKIP;

    case REPLACE_SIZE:
        do_refresh ();
        if (_s_stat->st_size == _d_stat->st_size)
            return FILE_SKIP;
        else
            return FILE_CONT;

    case REPLACE_REGET:
        /* Careful: we fall through and set do_append */
        ctx->do_reget = _d_stat->st_size;

    case REPLACE_APPEND:
        ctx->do_append = TRUE;

    case REPLACE_YES:
    case REPLACE_ALWAYS:
        do_refresh ();
        return FILE_CONT;
    case REPLACE_NO:
    case REPLACE_NEVER:
        do_refresh ();
        return FILE_SKIP;
    case REPLACE_ABORT:
    default:
        return FILE_ABORT;
    }
}

/* --------------------------------------------------------------------------------------------- */

char *
file_mask_dialog (FileOpContext * ctx, FileOperation operation,
                  gboolean only_one,
                  const char *format, const void *text, const char *def_text, gboolean * do_bg)
{
    const size_t FMDY = 13;
    const size_t FMDX = 68;
    size_t fmd_xlen;

    /* buttons */
    const size_t gap = 1;
    size_t b0_len, b2_len;
    size_t b1_len = 0;

    int source_easy_patterns = easy_patterns;
    size_t i, len;
    char fmd_buf[BUF_MEDIUM];
    char *source_mask, *orig_mask, *dest_dir, *tmp;
    char *def_text_secure;
    int val;

    QuickWidget fmd_widgets[] = {
        /* 0 */ QUICK_BUTTON (42, 64, 10, FMDY, N_("&Cancel"), B_CANCEL, NULL),
#ifdef WITH_BACKGROUND
        /* 1 */ QUICK_BUTTON (25, 64, 10, FMDY, N_("&Background"), B_USER, NULL),
#define OFFSET 0
#else
#define OFFSET 1
#endif /* WITH_BACKGROUND */
        /*  2 - OFFSET */
        QUICK_BUTTON (14, FMDX, 10, FMDY, N_("&OK"), B_ENTER, NULL),
        /*  3 - OFFSET */
        QUICK_CHECKBOX (42, FMDX, 8, FMDY, N_("&Stable Symlinks"), &ctx->stable_symlinks),
        /*  4 - OFFSET */
        QUICK_CHECKBOX (31, FMDX, 7, FMDY, N_("Di&ve into subdir if exists"),
                        &ctx->dive_into_subdirs),
        /*  5 - OFFSET */
        QUICK_CHECKBOX (3, FMDX, 8, FMDY, N_("Preserve &attributes"), &ctx->op_preserve),
        /*  6 - OFFSET */
        QUICK_CHECKBOX (3, FMDX, 7, FMDY, N_("Follow &links"), &ctx->follow_links),
        /*  7 - OFFSET */
        QUICK_INPUT (3, FMDX, 6, FMDY, "", 58, 0, "input2", &dest_dir),
        /*  8 - OFFSET */
        QUICK_LABEL (3, FMDX, 5, FMDY, N_("to:")),
        /*  9 - OFFSET */
        QUICK_CHECKBOX (37, FMDX, 4, FMDY, N_("&Using shell patterns"), &source_easy_patterns),
        /* 10 - OFFSET */
        QUICK_INPUT (3, FMDX, 3, FMDY, easy_patterns ? "*" : "^(.*)$", 58, 0, "input-def",
                     &source_mask),
        /* 11 - OFFSET */
        QUICK_LABEL (3, FMDX, 2, FMDY, fmd_buf),
        QUICK_END
    };

    g_return_val_if_fail (ctx != NULL, NULL);

#ifdef ENABLE_NLS
    /* buttons */
    for (i = 0; i <= 2 - OFFSET; i++)
        fmd_widgets[i].u.button.text = _(fmd_widgets[i].u.button.text);

    /* checkboxes */
    for (i = 3 - OFFSET; i <= 9 - OFFSET; i++)
        if (i != 7 - OFFSET)
            fmd_widgets[i].u.checkbox.text = _(fmd_widgets[i].u.checkbox.text);
#endif /* !ENABLE_NLS */

    fmd_xlen = max (FMDX, (size_t) COLS * 2 / 3);

    len = str_term_width1 (fmd_widgets[6 - OFFSET].u.checkbox.text)
        + str_term_width1 (fmd_widgets[4 - OFFSET].u.checkbox.text) + 15;
    fmd_xlen = max (fmd_xlen, len);

    len = str_term_width1 (fmd_widgets[5 - OFFSET].u.checkbox.text)
        + str_term_width1 (fmd_widgets[3 - OFFSET].u.checkbox.text) + 15;
    fmd_xlen = max (fmd_xlen, len);

    /* buttons */
    b2_len = str_term_width1 (fmd_widgets[2 - OFFSET].u.button.text) + 6 + gap; /* OK */
#ifdef WITH_BACKGROUND
    b1_len = str_term_width1 (fmd_widgets[1].u.button.text) + 4 + gap;  /* Background */
#endif
    b0_len = str_term_width1 (fmd_widgets[0].u.button.text) + 4;        /* Cancel */
    len = b0_len + b1_len + b2_len;
    fmd_xlen = min (max (fmd_xlen, len + 6), (size_t) COLS);

    if (only_one)
    {
        int flen;

        flen = str_term_width1 (format);
        i = fmd_xlen - flen - 4;        /* FIXME */
        g_snprintf (fmd_buf, sizeof (fmd_buf), format, str_trunc ((const char *) text, i));
    }
    else
    {
        g_snprintf (fmd_buf, sizeof (fmd_buf), format, *(const int *) text);
        fmd_xlen = max (fmd_xlen, (size_t) str_term_width1 (fmd_buf) + 6);
    }

    for (i = sizeof (fmd_widgets) / sizeof (fmd_widgets[0]); i > 0;)
        fmd_widgets[--i].x_divisions = fmd_xlen;

    i = (fmd_xlen - len) / 2;
    /* OK button */
    fmd_widgets[2 - OFFSET].relative_x = i;
    i += b2_len;
#ifdef WITH_BACKGROUND
    /* Background button */
    fmd_widgets[1].relative_x = i;
    i += b1_len;
#endif
    /* Cancel button */
    fmd_widgets[0].relative_x = i;

#define chkbox_xpos(i) \
    fmd_widgets [i].relative_x = fmd_xlen - str_term_width1 (fmd_widgets [i].u.checkbox.text) - 6
    chkbox_xpos (3 - OFFSET);
    chkbox_xpos (4 - OFFSET);
    chkbox_xpos (9 - OFFSET);
#undef chkbox_xpos

    /* inputs */
    fmd_widgets[7 - OFFSET].u.input.len = fmd_widgets[10 - OFFSET].u.input.len = fmd_xlen - 6;

    /* unselect checkbox if target filesystem don't support attributes */
    ctx->op_preserve = filegui__check_attrs_on_fs (def_text);

    /* filter out a possible password from def_text */
    tmp = strip_password (g_strdup (def_text), 1);
    if (source_easy_patterns)
        def_text_secure = strutils_glob_escape (tmp);
    else
        def_text_secure = strutils_regex_escape (tmp);
    g_free (tmp);

    /* destination */
    fmd_widgets[7 - OFFSET].u.input.text = def_text_secure;

    ctx->stable_symlinks = FALSE;
    *do_bg = FALSE;

    {
        struct stat buf;

        QuickDialog Quick_input = {
            fmd_xlen, FMDY, -1, -1, op_names[operation],
            "[Mask Copy/Rename]", fmd_widgets, NULL, TRUE
        };

      ask_file_mask:
        val = quick_dialog_skip (&Quick_input, 4);

        if (val == B_CANCEL)
        {
            g_free (def_text_secure);
            return NULL;
        }

        if (ctx->follow_links)
            ctx->stat_func = mc_stat;
        else
            ctx->stat_func = mc_lstat;

        if (ctx->op_preserve)
        {
            ctx->preserve = TRUE;
            ctx->umask_kill = 0777777;
            ctx->preserve_uidgid = (geteuid () == 0);
        }
        else
        {
            int i2;
            ctx->preserve = ctx->preserve_uidgid = FALSE;
            i2 = umask (0);
            umask (i2);
            ctx->umask_kill = i2 ^ 0777777;
        }

        if ((dest_dir == NULL) || (*dest_dir == '\0'))
        {
            g_free (def_text_secure);
            g_free (source_mask);
            return dest_dir;
        }

        ctx->search_handle = mc_search_new (source_mask, -1);

        if (ctx->search_handle == NULL)
        {
            message (D_ERROR, MSG_ERROR, _("Invalid source pattern `%s'"), source_mask);
            g_free (dest_dir);
            g_free (source_mask);
            goto ask_file_mask;
        }

        g_free (def_text_secure);
        g_free (source_mask);

        ctx->search_handle->is_case_sensitive = TRUE;
        if (source_easy_patterns)
            ctx->search_handle->search_type = MC_SEARCH_T_GLOB;
        else
            ctx->search_handle->search_type = MC_SEARCH_T_REGEX;

        tmp = dest_dir;
        dest_dir = tilde_expand (tmp);
        g_free (tmp);

        ctx->dest_mask = strrchr (dest_dir, PATH_SEP);
        if (ctx->dest_mask == NULL)
            ctx->dest_mask = dest_dir;
        else
            ctx->dest_mask++;
        orig_mask = ctx->dest_mask;
        if (!*ctx->dest_mask
            || (!ctx->dive_into_subdirs && !is_wildcarded (ctx->dest_mask)
                && (!only_one
                    || (!mc_stat (dest_dir, &buf) && S_ISDIR (buf.st_mode))))
            || (ctx->dive_into_subdirs
                && ((!only_one && !is_wildcarded (ctx->dest_mask))
                    || (only_one && !mc_stat (dest_dir, &buf) && S_ISDIR (buf.st_mode)))))
            ctx->dest_mask = g_strdup ("\\0");
        else
        {
            ctx->dest_mask = g_strdup (ctx->dest_mask);
            *orig_mask = '\0';
        }
        if (!*dest_dir)
        {
            g_free (dest_dir);
            dest_dir = g_strdup ("./");
        }
        if (val == B_USER)
            *do_bg = TRUE;
    }

    return dest_dir;
}

/* --------------------------------------------------------------------------------------------- */
