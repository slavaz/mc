/*
   Widget based utility functions.

   Copyright (C) 1994, 1995, 1998, 1999, 2000, 2001, 2002, 2003, 2004,
   2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012
   The Free Software Foundation, Inc.

   Authors:
   Miguel de Icaza, 1994, 1995, 1996
   Radek Doulik, 1994, 1995
   Jakub Jelinek, 1995
   Andrej Borsenkow, 1995
   Andrew Borodin <aborodin@vmail.ru>, 2009, 2010, 2011, 2012

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

/** \file quick2.c
 *  \brief Source: quick dialog engine
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>              /* fprintf() */

#include "lib/global.h"
#include "lib/strutil.h"        /* str_term_width1() */
#include "lib/util.h"           /* tilde_expand() */
#include "lib/widget.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#ifdef ENABLE_NLS
#define I18N(x) (x = x != NULL && *x != '\0' ? _(x) : x)
#else
#define I18N(x) (x = x)
#endif

/*** file scope type declarations ****************************************************************/

typedef struct
{
    Widget *w;
    quick_widget_t *qw;
} quick_widget_item_t;

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static WInput *
quick_create_input (int y, int x, const quick_widget_t * qw)
{
    WInput *in;

    in = input_new (y, x, input_get_default_colors (), 8, qw->u.input.text, qw->u.input.histname,
                    INPUT_COMPLETE_DEFAULT);
    in->is_password = (qw->u.input.flags == 1);
    if ((qw->u.input.flags & 2) != 0)
        in->completion_flags |= INPUT_COMPLETE_CD;
    if ((qw->u.input.flags & 4) != 0)
        in->strip_password = TRUE;

    return in;
}

/* --------------------------------------------------------------------------------------------- */

static void
quick_create_labeled_input (GArray * widgets, int *y, int x, quick_widget_t * qw, int *width)
{
    quick_widget_item_t in, label;

    label.qw = g_new0 (quick_widget_t, 1);
    label.qw->widget_type = quick2_label;

    switch (qw->u.input.label_location)
    {
    case input_label_above:
        label.w = WIDGET (label_new (*y, x, I18N (qw->u.input.label_text)));
        *y += label.w->lines;
        g_array_append_val (widgets, label);

        in.w = WIDGET (quick_create_input ((*y)++, x, qw));
        in.qw = qw;
        g_array_append_val (widgets, in);

        *width = max (label.w->cols, in.w->cols);
        break;

    case input_label_left:
        label.w = WIDGET (label_new (*y, x, I18N (qw->u.input.label_text)));
        g_array_append_val (widgets, label);

        in.w = WIDGET (quick_create_input ((*y)++, x + label.w->cols + 1, qw));
        in.qw = qw;
        g_array_append_val (widgets, in);

        *width = label.w->cols + in.w->cols + 1;
        break;

    case input_label_right:
        in.w = WIDGET (quick_create_input (*y, x, qw));
        in.qw = qw;
        g_array_append_val (widgets, in);

        label.w = WIDGET (label_new ((*y)++, x + in.w->cols + 1, I18N (qw->u.input.label_text)));
        g_array_append_val (widgets, label);

        *width = label.w->cols + in.w->cols + 1;
        break;

    case input_label_below:
        in.w = WIDGET (quick_create_input ((*y)++, x, qw));
        in.qw = qw;
        g_array_append_val (widgets, in);

        label.w = WIDGET (label_new (*y, x, I18N (qw->u.input.label_text)));
        *y += label.w->lines;
        g_array_append_val (widgets, label);

        *width = max (label.w->cols, in.w->cols);
        break;

    default:
        return;
    }

    ((WInput *) in.w)->label = (WLabel *) label.w;
    /* cross references */
    label.qw->u.label.input = in.qw;
    in.qw->u.input.label = label.qw;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

int
quick2_dialog_skip (quick_dialog_t * qd, int nskip)
{
    int len;
    int blen = 0;
    int x, y;                   /* current positions */
    int y1 = 0;                 /* bottom of 1st column in case of two columns */
    int y2 = -1;                /* start of two columns */
    int column_width;           /* width of each column */
    gboolean have_groupbox = FALSE;
    gboolean two_columns = FALSE;
    gboolean two_columns_dlg = FALSE;
    gboolean put_buttons = FALSE;

    /* x position of 1st column is 3 */
    const int x1 = 3;
    /* x position of 2nd column is 4 and it will be fixed later, after creation of all widgets */
    int x2 = 4;

    GArray *widgets;
    size_t i;
    quick_widget_t *qw;
    WGroupbox *g = NULL;
    Dlg_head *dd;
    int return_val;

    len = str_term_width1 (I18N (qd->title)) + 6;
    qd->cols = max (qd->cols, len);

    /* assume single column */
    column_width = len - 2;
    y = 2;
    x = x1;

    /* create widgets */
    widgets = g_array_sized_new (FALSE, FALSE, sizeof (quick_widget_item_t), 8);

    for (qw = qd->widgets; qw->widget_type != quick2_end; qw++)
    {
        quick_widget_item_t item = { NULL, qw };
        int width = 0;

        switch (qw->widget_type)
        {
        case quick2_checkbox:
            item.w = WIDGET (check_new (y++, x, *qw->u.checkbox.state, I18N (qw->u.checkbox.text)));
            g_array_append_val (widgets, item);
            width = item.w->cols;
            if (g != NULL)
                width += 2;
            column_width = max (column_width, width);
            break;

        case quick2_button:
            /* single button */
            item.w = WIDGET (button_new (y++, x, qw->u.button.action,
                                         qw->u.button.action == B_ENTER ?
                                         DEFPUSH_BUTTON : NORMAL_BUTTON,
                                         I18N (qw->u.button.text), qw->u.button.callback));
            g_array_append_val (widgets, item);
            width = item.w->cols;
            if (g != NULL)
                width += 2;
            column_width = max (column_width, width);
            break;

        case quick2_input:
            *qw->u.input.result = NULL;
            if (qw->u.input.label_location != input_label_none)
                quick_create_labeled_input (widgets, &y, x, qw, &width);
            else
            {
                item.w = WIDGET (quick_create_input (y++, x, qw));
                g_array_append_val (widgets, item);
                width = item.w->cols;
            }
            if (g != NULL)
                width += 2;
            column_width = max (column_width, width);
            break;

        case quick2_label:
            item.w = WIDGET (label_new (y, x, I18N (qw->u.label.text)));
            g_array_append_val (widgets, item);
            y += item.w->lines;
            width = item.w->cols;
            if (g != NULL)
                width += 2;
            column_width = max (column_width, width);
            break;

        case quick2_radio:
            {
                WRadio *r;
                char **items = NULL;

                /* create the copy of radio_items to avoid mwmory leak */
                items = g_new (char *, qw->u.radio.count + 1);
                for (i = 0; i < (size_t) qw->u.radio.count; i++)
                    items[i] = g_strdup (_(qw->u.radio.items[i]));
                items[i] = NULL;

                r = radio_new (y, x, qw->u.radio.count, (const char **) items);
                r->pos = r->sel = *qw->u.radio.value;
                g_strfreev (items);
                item.w = WIDGET (r);
                g_array_append_val (widgets, item);
                y += item.w->lines;
                width = item.w->cols;
                if (g != NULL)
                    width += 2;
                column_width = max (column_width, width);
            }
            break;

        case quick2_start_groupbox:
            I18N (qw->u.groupbox.title);
            len = str_term_width1 (qw->u.groupbox.title);
            g = groupbox_new (y++, x, 1, len + 4, qw->u.groupbox.title);
            item.w = WIDGET (g);
            g_array_append_val (widgets, item);
            have_groupbox = TRUE;
            break;

        case quick2_stop_groupbox:
            if (g != NULL)
            {
                Widget *w = WIDGET (g);

                y++;
                w->lines = y - w->y;
                g = NULL;

                g_array_append_val (widgets, item);
            }
            break;

        case quick2_separator:
            if (qw->u.separator.line)
            {
                item.w = WIDGET (hline_new (y, x, 1));
                g_array_append_val (widgets, item);
            }
            y++;
            break;

        case quick2_start_columns:
            two_columns_dlg = TRUE;
            y2 = y;
            g_array_append_val (widgets, item);
            break;

        case quick2_next_column:
            x = x2;
            y1 = y;
            y = y2;
            break;

        case quick2_stop_columns:
            x = x1;
            y = max (y1, y);
            g_array_append_val (widgets, item);
            break;

        case quick2_buttons:
            /* start put several buttons in bottom line */
            if (qw->u.separator.space)
            {
                if (qw->u.separator.line)
                    item.w = WIDGET (hline_new (y, 1, -1));
                y++;
            }

            g_array_append_val (widgets, item);

            /* several buttons in bottom line */
            qw++;
            blen = 0;
            for (; qw->widget_type == quick2_button; qw++)
            {
                item.w = WIDGET (button_new (y, x++, qw->u.button.action,
                                             qw->u.button.action == B_ENTER ?
                                             DEFPUSH_BUTTON : NORMAL_BUTTON,
                                             I18N (qw->u.button.text), qw->u.button.callback));
                item.qw = qw;
                g_array_append_val (widgets, item);
                blen += item.w->cols + 1;
            }

            /* stop dialog build here */
            blen--;
            qw->widget_type = quick2_end;
            qw--;
            break;

        default:
            break;
        }
    }

    /* adjust dialog width */
    qd->cols = max (qd->cols, blen + 6);
    if (have_groupbox)
        column_width += 2;
    if (two_columns_dlg)
        len = column_width * 2 + 7;
    else
        len = column_width + 6;
    qd->cols = max (qd->cols, len);

    if (qd->x == -1 || qd->y == -1)
        dd = create_dlg (TRUE, 0, 0, y + 3, qd->cols,
                         dialog_colors, qd->callback, qd->mouse, qd->help, qd->title,
                         DLG_CENTER | DLG_TRYUP);
    else
        dd = create_dlg (TRUE, qd->y, qd->x, y + 3, qd->cols,
                         dialog_colors, qd->callback, qd->mouse, qd->help, qd->title, DLG_NONE);

    /* add widgets into the dialog */
    column_width = qd->cols - 6;
    x2 = x1 + (qd->cols - 7) / 2  + 1;
    g = NULL;
    two_columns = FALSE;
    x = (WIDGET (dd)->cols - blen) / 2;

    for (i = 0; i < widgets->len; i++)
    {
        quick_widget_item_t *item;

        item = &g_array_index (widgets, quick_widget_item_t, i);

        /* adjust widget width and x position */
        switch (item->qw->widget_type)
        {
        case quick2_label:
            {
                quick_widget_t *in = item->qw->u.label.input;

                if (in != NULL && in->u.input.label_location == input_label_right)
                {
                    /* location of this label will be adjusted later */
                    break;
                }
            }
            /* fall through */
        case quick2_checkbox:
        case quick2_radio:
            if (item->w->x != x1)
                item->w->x = x2;
            if (g != NULL)
                item->w->x += 2;
            break;

        case quick2_button:
            if (!put_buttons)
            {
                if (item->w->x != x1)
                    item->w->x = x2;
                if (g != NULL)
                    item->w->x += 2;
            }
            else
            {
                item->w->x = x;
                x += item->w->cols + 1;
            }
            break;

        case quick2_input:
            {
                Widget *label = WIDGET (((WInput *) item->w)->label);
                int width = column_width;

                if (g != NULL)
                    width -= 4;

                switch (item->qw->u.input.label_location)
                {
                case input_label_left:
                    /* label was adjusted before; adjust input line */
                    item->w->x = label->x + label->cols + 1 - WIDGET (label->owner)->x;
                    item->w->cols = width - label->cols - 1;
                    break;

                case input_label_right:
                    label->x = item->w->x + item->w->cols + 1 - WIDGET (item->w->owner)->x;
                    item->w->cols = width - label->cols - 1;
                    break;

                default:
                    if (item->w->x != x1)
                        item->w->x = x2;
                    if (g != NULL)
                        item->w->x += 2;
                    item->w->cols = width;
                    break;
                }

                /* forced update internal variables of inpuit line */
                input_set_origin ((WInput *) (item->w), item->w->x, item->w->cols);
            }
            break;

        case quick2_start_groupbox:
            g = (WGroupbox *) item->w;
            if (item->w->x != x1)
                item->w->x = x2;
            item->w->cols = column_width;
            break;

        case quick2_stop_groupbox:
            g = NULL;
            break;

        case quick2_separator:
            if (item->w != NULL)
            {
                if (g != NULL)
                {
                    Widget *wg = WIDGET (g);

                    ((WHLine *) item->w)->auto_adjust_cols = FALSE;
                    item->w->x = wg->x + 1 - WIDGET (wg->owner)->x;
                    item->w->cols = wg->cols;
                }
                else if (two_columns)
                {
                    ((WHLine *) item->w)->auto_adjust_cols = FALSE;
                    if (item->w->x != x1)
                        item->w->x = x2;
                    item->w->x--;
                    item->w->cols = column_width + 2;
                }
                else
                    ((WHLine *) item->w)->auto_adjust_cols = TRUE;
            }
            break;

        case quick2_start_columns:
            two_columns = TRUE;
            column_width = (qd->cols - 7) / 2;
            break;

        case quick2_stop_columns:
            two_columns = FALSE;
            column_width = qd->cols - 6;
            break;

        case quick2_buttons:
            /* several buttons in bottom line */
            put_buttons = TRUE;
            break;

        default:
            break;
        }

        if (item->w != NULL)
        {
            unsigned long id;

            /* add widget into dialog */
            item->w->options |= item->qw->options;      /* FIXME: cannot reset flags, setup only */
            id = add_widget (dd, item->w);
            if (item->qw->id != NULL)
                *item->qw->id = id;
        }
    }

    while (nskip-- != 0)
    {
        dd->current = g_list_next (dd->current);
        if (dd->current == NULL)
            dd->current = dd->widgets;
    }

    return_val = run_dlg (dd);

    /* Get the data if we found something interesting */
    if (return_val != B_CANCEL)
        for (i = 0; i < widgets->len; i++)
        {
            quick_widget_item_t *item;

            item = &g_array_index (widgets, quick_widget_item_t, i);

            switch (item->qw->widget_type)
            {
            case quick_checkbox:
                *item->qw->u.checkbox.state = ((WCheck *) item->w)->state & C_BOOL;
                break;

            case quick_input:
                if ((qw->u.input.flags & 2) != 0)
                    *item->qw->u.input.result = tilde_expand (((WInput *) item->w)->buffer);
                else
                    *item->qw->u.input.result = g_strdup (((WInput *) item->w)->buffer);
                break;

            case quick_radio:
                *item->qw->u.radio.value = ((WRadio *) item->w)->sel;
                break;

            default:
                break;
            }
        }

    destroy_dlg (dd);

    /* destroy input labels created before */
    for (i = 0; i < widgets->len; i++)
    {
        quick_widget_item_t *item;

        item = &g_array_index (widgets, quick_widget_item_t, i);
        if (item->qw->widget_type == quick2_input)
            g_free (item->qw->u.input.label);
    }

    g_array_free (widgets, TRUE);

    return return_val;
}

/* --------------------------------------------------------------------------------------------- */
