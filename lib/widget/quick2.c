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
    Widget *widget;
    quick_widget_t *quick_widget;
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
quick_create_labeled_input (GArray * widgets, int *y, int x, quick_widget_t * quick_widget, int *width)
{
    quick_widget_item_t in, label;

    label.quick_widget = g_new0 (quick_widget_t, 1);
    label.quick_widget->widget_type = quick2_label;

    switch (quick_widget->u.input.label_location)
    {
    case input_label_above:
        label.widget = WIDGET (label_new (*y, x, I18N (quick_widget->u.input.label_text)));
        *y += label.widget->lines;
        g_array_append_val (widgets, label);

        in.widget = WIDGET (quick_create_input ((*y)++, x, quick_widget));
        in.quick_widget = quick_widget;
        g_array_append_val (widgets, in);

        *width = max (label.widget->cols, in.widget->cols);
        break;

    case input_label_left:
        label.widget = WIDGET (label_new (*y, x, I18N (quick_widget->u.input.label_text)));
        g_array_append_val (widgets, label);

        in.widget = WIDGET (quick_create_input ((*y)++, x + label.widget->cols + 1, quick_widget));
        in.quick_widget = quick_widget;
        g_array_append_val (widgets, in);

        *width = label.widget->cols + in.widget->cols + 1;
        break;

    case input_label_right:
        in.widget = WIDGET (quick_create_input (*y, x, quick_widget));
        in.quick_widget = quick_widget;
        g_array_append_val (widgets, in);

        label.widget = WIDGET (label_new ((*y)++, x + in.widget->cols + 1, I18N (quick_widget->u.input.label_text)));
        g_array_append_val (widgets, label);

        *width = label.widget->cols + in.widget->cols + 1;
        break;

    case input_label_below:
        in.widget = WIDGET (quick_create_input ((*y)++, x, quick_widget));
        in.quick_widget = quick_widget;
        g_array_append_val (widgets, in);

        label.widget = WIDGET (label_new (*y, x, I18N (quick_widget->u.input.label_text)));
        *y += label.widget->lines;
        g_array_append_val (widgets, label);

        *width = max (label.widget->cols, in.widget->cols);
        break;

    default:
        return;
    }

    ((WInput *) in.widget)->label = (WLabel *) label.widget;
    /* cross references */
    label.quick_widget->u.label.input = in.quick_widget;
    in.quick_widget->u.input.label = label.quick_widget;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

int
quick2_dialog_skip (quick_dialog_t * quick_dlg, int nskip)
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
    quick_widget_t *quick_widget;
    WGroupbox *g = NULL;
    Dlg_head *dd;
    int return_val;

    len = str_term_width1 (I18N (quick_dlg->title)) + 6;
    quick_dlg->cols = max (quick_dlg->cols, len);

    /* assume single column */
    column_width = len - 2;
    y = 2;
    x = x1;

    /* create widgets */
    widgets = g_array_sized_new (FALSE, FALSE, sizeof (quick_widget_item_t), 8);

    for (quick_widget = quick_dlg->widgets; quick_widget->widget_type != quick2_end; quick_widget++)
    {
        quick_widget_item_t item = { NULL, quick_widget };
        int width = 0;

        switch (quick_widget->widget_type)
        {
        case quick2_checkbox:
            item.widget = WIDGET (check_new (y++, x, *quick_widget->u.checkbox.state, I18N (quick_widget->u.checkbox.text)));
            g_array_append_val (widgets, item);
            width = item.widget->cols;
            if (g != NULL)
                width += 2;
            column_width = max (column_width, width);
            break;

        case quick2_button:
            /* single button */
            item.widget = WIDGET (button_new (y++, x, quick_widget->u.button.action,
                                         quick_widget->u.button.action == B_ENTER ?
                                         DEFPUSH_BUTTON : NORMAL_BUTTON,
                                         I18N (quick_widget->u.button.text), quick_widget->u.button.callback));
            g_array_append_val (widgets, item);
            width = item.widget->cols;
            if (g != NULL)
                width += 2;
            column_width = max (column_width, width);
            break;

        case quick2_input:
            *quick_widget->u.input.result = NULL;
            if (quick_widget->u.input.label_location != input_label_none)
                quick_create_labeled_input (widgets, &y, x, quick_widget, &width);
            else
            {
                item.widget = WIDGET (quick_create_input (y++, x, quick_widget));
                g_array_append_val (widgets, item);
                width = item.widget->cols;
            }
            if (g != NULL)
                width += 2;
            column_width = max (column_width, width);
            break;

        case quick2_label:
            item.widget = WIDGET (label_new (y, x, I18N (quick_widget->u.label.text)));
            g_array_append_val (widgets, item);
            y += item.widget->lines;
            width = item.widget->cols;
            if (g != NULL)
                width += 2;
            column_width = max (column_width, width);
            break;

        case quick2_radio:
            {
                WRadio *r;
                char **items = NULL;

                /* create the copy of radio_items to avoid mwmory leak */
                items = g_new (char *, quick_widget->u.radio.count + 1);
                for (i = 0; i < (size_t) quick_widget->u.radio.count; i++)
                    items[i] = g_strdup (_(quick_widget->u.radio.items[i]));
                items[i] = NULL;

                r = radio_new (y, x, quick_widget->u.radio.count, (const char **) items);
                r->pos = r->sel = *quick_widget->u.radio.value;
                g_strfreev (items);
                item.widget = WIDGET (r);
                g_array_append_val (widgets, item);
                y += item.widget->lines;
                width = item.widget->cols;
                if (g != NULL)
                    width += 2;
                column_width = max (column_width, width);
            }
            break;

        case quick2_start_groupbox:
            I18N (quick_widget->u.groupbox.title);
            len = str_term_width1 (quick_widget->u.groupbox.title);
            g = groupbox_new (y++, x, 1, len + 4, quick_widget->u.groupbox.title);
            item.widget = WIDGET (g);
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
            if (quick_widget->u.separator.line)
            {
                item.widget = WIDGET (hline_new (y, x, 1));
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
            if (quick_widget->u.separator.space)
            {
                if (quick_widget->u.separator.line)
                    item.widget = WIDGET (hline_new (y, 1, -1));
                y++;
            }

            g_array_append_val (widgets, item);

            /* several buttons in bottom line */
            quick_widget++;
            blen = 0;
            for (; quick_widget->widget_type == quick2_button; quick_widget++)
            {
                item.widget = WIDGET (button_new (y, x++, quick_widget->u.button.action,
                                             quick_widget->u.button.action == B_ENTER ?
                                             DEFPUSH_BUTTON : NORMAL_BUTTON,
                                             I18N (quick_widget->u.button.text), quick_widget->u.button.callback));
                item.quick_widget = quick_widget;
                g_array_append_val (widgets, item);
                blen += item.widget->cols + 1;
            }

            /* stop dialog build here */
            blen--;
            quick_widget->widget_type = quick2_end;
            quick_widget--;
            break;

        default:
            break;
        }
    }

    /* adjust dialog width */
    quick_dlg->cols = max (quick_dlg->cols, blen + 6);
    if (have_groupbox)
        column_width += 2;
    if (two_columns_dlg)
        len = column_width * 2 + 7;
    else
        len = column_width + 6;
    quick_dlg->cols = max (quick_dlg->cols, len);

    if (quick_dlg->x == -1 || quick_dlg->y == -1)
        dd = create_dlg (TRUE, 0, 0, y + 3, quick_dlg->cols,
                         dialog_colors, quick_dlg->callback, quick_dlg->mouse, quick_dlg->help, quick_dlg->title,
                         DLG_CENTER | DLG_TRYUP);
    else
        dd = create_dlg (TRUE, quick_dlg->y, quick_dlg->x, y + 3, quick_dlg->cols,
                         dialog_colors, quick_dlg->callback, quick_dlg->mouse, quick_dlg->help, quick_dlg->title, DLG_NONE);

    /* add widgets into the dialog */
    column_width = quick_dlg->cols - 6;
    x2 = x1 + (quick_dlg->cols - 7) / 2  + 1;
    g = NULL;
    two_columns = FALSE;
    x = (WIDGET (dd)->cols - blen) / 2;

    for (i = 0; i < widgets->len; i++)
    {
        quick_widget_item_t *item;

        item = &g_array_index (widgets, quick_widget_item_t, i);

        /* adjust widget width and x position */
        switch (item->quick_widget->widget_type)
        {
        case quick2_label:
            {
                quick_widget_t *input = item->quick_widget->u.label.input;

                if (input != NULL && input->u.input.label_location == input_label_right)
                {
                    /* location of this label will be adjusted later */
                    break;
                }
            }
            /* fall through */
        case quick2_checkbox:
        case quick2_radio:
            if (item->widget->x != x1)
                item->widget->x = x2;
            if (g != NULL)
                item->widget->x += 2;
            break;

        case quick2_button:
            if (!put_buttons)
            {
                if (item->widget->x != x1)
                    item->widget->x = x2;
                if (g != NULL)
                    item->widget->x += 2;
            }
            else
            {
                item->widget->x = x;
                x += item->widget->cols + 1;
            }
            break;

        case quick2_input:
            {
                Widget *label = WIDGET (((WInput *) item->widget)->label);
                int width = column_width;

                if (g != NULL)
                    width -= 4;

                switch (item->quick_widget->u.input.label_location)
                {
                case input_label_left:
                    /* label was adjusted before; adjust input line */
                    item->widget->x = label->x + label->cols + 1 - WIDGET (label->owner)->x;
                    item->widget->cols = width - label->cols - 1;
                    break;

                case input_label_right:
                    label->x = item->widget->x + item->widget->cols + 1 - WIDGET (item->widget->owner)->x;
                    item->widget->cols = width - label->cols - 1;
                    break;

                default:
                    if (item->widget->x != x1)
                        item->widget->x = x2;
                    if (g != NULL)
                        item->widget->x += 2;
                    item->widget->cols = width;
                    break;
                }

                /* forced update internal variables of inpuit line */
                input_set_origin ((WInput *) (item->widget), item->widget->x, item->widget->cols);
            }
            break;

        case quick2_start_groupbox:
            g = (WGroupbox *) item->widget;
            if (item->widget->x != x1)
                item->widget->x = x2;
            item->widget->cols = column_width;
            break;

        case quick2_stop_groupbox:
            g = NULL;
            break;

        case quick2_separator:
            if (item->widget != NULL)
            {
                if (g != NULL)
                {
                    Widget *wg = WIDGET (g);

                    ((WHLine *) item->widget)->auto_adjust_cols = FALSE;
                    item->widget->x = wg->x + 1 - WIDGET (wg->owner)->x;
                    item->widget->cols = wg->cols;
                }
                else if (two_columns)
                {
                    ((WHLine *) item->widget)->auto_adjust_cols = FALSE;
                    if (item->widget->x != x1)
                        item->widget->x = x2;
                    item->widget->x--;
                    item->widget->cols = column_width + 2;
                }
                else
                    ((WHLine *) item->widget)->auto_adjust_cols = TRUE;
            }
            break;

        case quick2_start_columns:
            two_columns = TRUE;
            column_width = (quick_dlg->cols - 7) / 2;
            break;

        case quick2_stop_columns:
            two_columns = FALSE;
            column_width = quick_dlg->cols - 6;
            break;

        case quick2_buttons:
            /* several buttons in bottom line */
            put_buttons = TRUE;
            break;

        default:
            break;
        }

        if (item->widget != NULL)
        {
            unsigned long id;

            /* add widget into dialog */
            item->widget->options |= item->quick_widget->options;      /* FIXME: cannot reset flags, setup only */
            id = add_widget (dd, item->widget);
            if (item->quick_widget->id != NULL)
                *item->quick_widget->id = id;
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

            switch (item->quick_widget->widget_type)
            {
            case quick_checkbox:
                *item->quick_widget->u.checkbox.state = ((WCheck *) item->widget)->state & C_BOOL;
                break;

            case quick_input:
                if ((quick_widget->u.input.flags & 2) != 0)
                    *item->quick_widget->u.input.result = tilde_expand (((WInput *) item->widget)->buffer);
                else
                    *item->quick_widget->u.input.result = g_strdup (((WInput *) item->widget)->buffer);
                break;

            case quick_radio:
                *item->quick_widget->u.radio.value = ((WRadio *) item->widget)->sel;
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
        if (item->quick_widget->widget_type == quick2_input)
            g_free (item->quick_widget->u.input.label);
    }

    g_array_free (widgets, TRUE);

    return return_val;
}

/* --------------------------------------------------------------------------------------------- */
