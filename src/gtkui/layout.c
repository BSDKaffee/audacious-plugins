/*
 * layout.c
 * Copyright 2011 John Lindgren
 *
 * This file is part of Audacious.
 *
 * Audacious is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2 or version 3 of the License.
 *
 * Audacious is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * Audacious. If not, see <http://www.gnu.org/licenses/>.
 *
 * The Audacious team does not consider modular code linking to Audacious or
 * using our public API to be a derived work.
 */

#include <limits.h>
#include <string.h>

#include <gtk/gtk.h>

#include <audacious/gtk-compat.h>
#include <audacious/i18n.h>
#include <audacious/misc.h>
#include <audacious/plugins.h>

#include "config.h"
#include "layout.h"

#define LAYOUT_FILE "gtkui-layout"

enum {DOCK_LEFT, DOCK_RIGHT, DOCK_TOP, DOCK_BOTTOM, DOCKS};

#define IS_VERTICAL(d) ((d) & 2)
#define IS_AFTER(d) ((d) & 1)

#define NULL_ON_DESTROY(w) g_signal_connect ((w), "destroy", (GCallback) \
 gtk_widget_destroyed, & (w))

typedef struct {
    gchar * name;
    GtkWidget * widget, * vbox, * paned, * window;
    gint dock, x, y, w, h;
} Item;

static GList * items = NULL;

static GtkWidget * layout = NULL;
static GtkWidget * center = NULL;
static GtkWidget * docks[DOCKS] = {NULL, NULL, NULL, NULL};
static GtkWidget * menu = NULL;

GtkWidget * layout_new (void)
{
    g_return_val_if_fail (! layout, NULL);
    layout = gtk_alignment_new (0, 0, 1, 1);
    gtk_alignment_set_padding ((GtkAlignment *) layout, 3, 3, 3, 3);
    NULL_ON_DESTROY (layout);
    return layout;
}

void layout_add_center (GtkWidget * widget)
{
    g_return_if_fail (layout && ! center && widget);
    center = widget;
    gtk_container_add ((GtkContainer *) layout, center);
    NULL_ON_DESTROY (center);
}

static void layout_move (GtkWidget * widget, gint dock);

static void layout_dock_left (GtkWidget * widget)
{
    layout_move (widget, DOCK_LEFT);
}

static void layout_dock_right (GtkWidget * widget)
{
    layout_move (widget, DOCK_RIGHT);
}

static void layout_dock_top (GtkWidget * widget)
{
    layout_move (widget, DOCK_TOP);
}

static void layout_dock_bottom (GtkWidget * widget)
{
    layout_move (widget, DOCK_BOTTOM);
}

static void layout_undock (GtkWidget * widget)
{
    layout_move (widget, -1);
}

static void layout_disable (GtkWidget * widget)
{
    PluginHandle * plugin = aud_plugin_by_widget (widget);
    g_return_if_fail (plugin);
    aud_plugin_enable (plugin, FALSE);
}

static gboolean menu_cb (GtkWidget * widget, GdkEventButton * event)
{
    g_return_val_if_fail (widget && event, FALSE);

    if (event->type != GDK_BUTTON_PRESS || event->button != 3)
        return FALSE;

    if (menu)
        gtk_widget_destroy (menu);

    menu = gtk_menu_new ();
    g_signal_connect (menu, "destroy", (GCallback) gtk_widget_destroyed, & menu);

    const gchar * names[6] = {N_("Dock at Left"), N_("Dock at Right"),
     N_("Dock at Top"), N_("Dock at Bottom"), N_("Undock"), N_("Disable")};
    void (* const funcs[6]) (GtkWidget * widget) = {layout_dock_left,
     layout_dock_right, layout_dock_top, layout_dock_bottom, layout_undock,
     layout_disable};

    for (gint i = 0; i < 6; i ++)
    {
        GtkWidget * item = gtk_menu_item_new_with_label (_(names[i]));
        gtk_menu_shell_append ((GtkMenuShell *) menu, item);
        g_signal_connect_swapped (item, "activate", (GCallback) funcs[i], widget);
    }

    gtk_widget_show_all (menu);
    gtk_menu_popup ((GtkMenu *) menu, NULL, NULL, NULL, NULL, event->button, event->time);

    return TRUE;
}

static GtkWidget * vbox_new (GtkWidget * widget, const gchar * name)
{
    g_return_val_if_fail (widget && name, NULL);

    GtkWidget * vbox = gtk_vbox_new (FALSE, 0);

    GtkWidget * ebox = gtk_event_box_new ();
    gtk_box_pack_start ((GtkBox *) vbox, ebox, FALSE, FALSE, 0);
    g_signal_connect_swapped (ebox, "button-press-event", (GCallback) menu_cb,
     widget);

    GtkWidget * label = gtk_label_new (NULL);
    gchar * markup = g_markup_printf_escaped ("<small><b>%s</b></small>", name);
    gtk_label_set_markup ((GtkLabel *) label, markup);
    g_free (markup);
    gtk_misc_set_alignment ((GtkMisc *) label, 0, 0);
    gtk_container_add ((GtkContainer *) ebox, label);

    gtk_box_pack_start ((GtkBox *) vbox, widget, TRUE, TRUE, 0);

    gtk_widget_show_all (vbox);

    return vbox;
}

typedef struct {
    GtkWidget * paned;
    GtkWidget * widget;
    gboolean vertical;
    gint w, h;
} RestoreSizeData;

static gboolean restore_size_cb (RestoreSizeData * d)
{
    GtkAllocation rect;
    gtk_widget_get_allocation (d->widget, & rect);
    gint pos = gtk_paned_get_position ((GtkPaned *) d->paned);
    pos -= d->vertical ? d->h - rect.height : d->w - rect.width;
    gtk_paned_set_position ((GtkPaned *) d->paned, pos);

    g_slice_free (RestoreSizeData, d);
    return FALSE;
}

static GtkWidget * paned_new (gboolean vertical, gboolean after, gint w, gint h)
{
    GtkWidget * paned = vertical ? gtk_vpaned_new () : gtk_hpaned_new ();

    GtkWidget * mine = gtk_alignment_new (0, 0, 1, 1);
    GtkWidget * next = gtk_alignment_new (0, 0, 1, 1);
    gtk_paned_pack1 ((GtkPaned *) paned, after ? next : mine, after, FALSE);
    gtk_paned_pack2 ((GtkPaned *) paned, after ? mine : next, ! after, FALSE);

    g_object_set_data ((GObject *) paned, "mine", mine);
    g_object_set_data ((GObject *) paned, "next", next);

    gtk_widget_show_all (paned);

    if (vertical ? h : w)
    {
        if (after)
        {
            /* hack to set the size of the second pane */
            RestoreSizeData * d = g_slice_new (RestoreSizeData);
            d->paned = paned;
            d->widget = mine;
            d->vertical = vertical;
            d->w = w;
            d->h = h;
            g_idle_add ((GSourceFunc) restore_size_cb, d);
        }
        else
            gtk_paned_set_position ((GtkPaned *) paned, vertical ? h : w);
    }

    return paned;
}

static Item * item_new (const gchar * name)
{
    Item * item = g_slice_new (Item);
    item->name = g_strdup (name);
    item->widget = item->vbox = item->paned = item->window = NULL;
    item->dock = item->x = item->y = -1;
    item->w = item->h = 0;
    items = g_list_append (items, item);
    return item;
}

static gint item_by_widget (Item * item, GtkWidget * widget)
{
    return (item->widget != widget);
}

static gint item_by_name (Item * item, const gchar * name)
{
    return strcmp (item->name, name);
}

static gboolean delete_cb (GtkWidget * widget)
{
    layout_disable (widget);
    return TRUE;
}

static GtkWidget * dock_get_parent (gint dock)
{
    g_return_val_if_fail (dock >= 0 && dock < DOCKS, NULL);

    for (gint scan = dock; scan --; )
    {
        if (docks[scan])
            return g_object_get_data ((GObject *) docks[scan], "next");
    }

    return layout;
}

static Item * item_get_prev (Item * item)
{
    GList * this = g_list_find (items, item);
    g_return_val_if_fail (this, NULL);

    for (GList * node = this->prev; node; node = node->prev)
    {
        Item * test = node->data;
        if (test->widget && test->dock == item->dock)
            return test;
    }

    return NULL;
}

static Item * item_get_next (Item * item)
{
    GList * this = g_list_find (items, item);
    g_return_val_if_fail (this, NULL);

    for (GList * node = this->next; node; node = node->next)
    {
        Item * test = node->data;
        if (test->widget && test->dock == item->dock)
            return test;
    }

    return NULL;
}

static GtkWidget * item_get_parent (Item * item)
{
    Item * prev = item_get_prev (item);
    return prev ? g_object_get_data ((GObject *) prev->paned, "next") :
     g_object_get_data ((GObject *) docks[item->dock], "mine");
}

static void item_save_size (Item * item)
{
    g_return_if_fail (item->widget && item->vbox);

    GtkAllocation rect;
    gtk_widget_get_allocation (item->vbox, & rect);
    item->w = rect.width;
    item->h = rect.height;

    if (item->dock < 0)
    {
        g_return_if_fail (item->window);
        gtk_window_get_position ((GtkWindow *) item->window, & item->x, & item->y);
    }
}

static void item_add (Item * item)
{
    g_return_if_fail (item->name && item->widget && item->vbox && ! item->paned
     && ! item->window && item->dock < DOCKS);

    if (item->dock < 0)
    {
        item->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        NULL_ON_DESTROY (item->window);

        gtk_window_set_title ((GtkWindow *) item->window, item->name);
        gtk_container_set_border_width ((GtkContainer *) item->window, 3);
        g_signal_connect_swapped (item->window, "delete-event", (GCallback)
         delete_cb, item->widget);

        if (item->x >= 0 && item->y >= 0)
            gtk_window_move ((GtkWindow *) item->window, item->x, item->y);
        if (item->w > 0 && item->h > 0)
            gtk_window_set_default_size ((GtkWindow *) item->window, item->w,
             item->h);

        gtk_container_add ((GtkContainer *) item->window, item->vbox);
        gtk_widget_show_all (item->window);
    }
    else
    {
        /* Screwy logic to figure out where we need to add a GtkPaned and which
         * widget goes in which pane of it. */
        gboolean swap = FALSE;
        Item * where = item;
        GtkWidget * parent, * paned;

        if (docks[item->dock])
        {
            if (! item_get_next (item))
            {
                swap = TRUE;
                where = item_get_prev (item);
                g_return_if_fail (where && ! where->paned);
                item_save_size (where);
            }

            parent = item_get_parent (where);
            g_return_if_fail (parent);

            paned = paned_new (! IS_VERTICAL (where->dock), FALSE, where->w,
             where->h);
            where->paned = paned;
            NULL_ON_DESTROY (where->paned);
        }
        else
        {
            parent = dock_get_parent (item->dock);
            g_return_if_fail (parent);

            paned = paned_new (IS_VERTICAL (item->dock), IS_AFTER (item->dock),
             item->w, item->h);
            docks[item->dock] = paned;
            NULL_ON_DESTROY (docks[item->dock]);
        }

        GtkWidget * mine = g_object_get_data ((GObject *) paned, "mine");
        GtkWidget * next = g_object_get_data ((GObject *) paned, "next");
        GtkWidget * child = gtk_bin_get_child ((GtkBin *) parent);
        g_return_if_fail (mine && next && child);

        g_object_ref (child);
        gtk_container_remove ((GtkContainer *) parent, child);
        gtk_container_add ((GtkContainer *) parent, paned);
        gtk_container_add ((GtkContainer *) (swap ? next : mine), item->vbox);
        gtk_container_add ((GtkContainer *) (swap ? mine : next), child);
        g_object_unref (child);
    }
}

static void item_remove (Item * item)
{
    g_return_if_fail (item->widget && item->vbox);

    item_save_size (item);

    if (item->dock < 0)
    {
        g_return_if_fail (item->window);
        gtk_container_remove ((GtkContainer *) item->window, item->vbox);
        gtk_widget_destroy (item->window);
    }
    else
    {
        /* Screwy logic to figure out which GtkPaned we need to remove and which
         * pane of it has the widget we need to keep. */
        gboolean swap = FALSE;
        Item * where = item;
        GtkWidget * parent, * paned;

        Item * prev = item_get_prev (item);
        if (item->paned || prev)
        {
            if (! item->paned)
            {
                swap = TRUE;
                where = item_get_prev (item);
                g_return_if_fail (where && where->paned);
            }

            parent = item_get_parent (where);
            g_return_if_fail (parent);

            paned = where->paned;
        }
        else
        {
            parent = dock_get_parent (item->dock);
            g_return_if_fail (parent);

            paned = docks[item->dock];
        }

        GtkWidget * mine = g_object_get_data ((GObject *) paned, "mine");
        GtkWidget * next = g_object_get_data ((GObject *) paned, "next");
        GtkWidget * child = gtk_bin_get_child ((GtkBin *) (swap ? mine : next));
        g_return_if_fail (mine && next && child);

        g_object_ref (child);
        gtk_container_remove ((GtkContainer *) (swap ? next : mine), item->vbox);
        gtk_container_remove ((GtkContainer *) (swap ? mine : next), child);
        gtk_container_remove ((GtkContainer *) parent, paned);
        gtk_container_add ((GtkContainer *) parent, child);
        g_object_unref (child);
    }
}

void layout_add (GtkWidget * widget, const gchar * name)
{
    g_return_if_fail (layout && center && widget && name && strlen (name) <= 256
     && ! strchr (name, '\n'));

    GList * node = g_list_find_custom (items, name, (GCompareFunc) item_by_name);
    Item * item = node ? node->data : NULL;

    if (item)
    {
        g_return_if_fail (! item->widget && ! item->vbox && ! item->window);
        if (item->dock >= DOCKS)
            item->dock = -1;
    }
    else
        item = item_new (name);

    item->widget = widget;
    NULL_ON_DESTROY (item->widget);
    item->vbox = vbox_new (widget, name);
    NULL_ON_DESTROY (item->vbox);

    item_add (item);
}

static void layout_move (GtkWidget * widget, gint dock)
{
    g_return_if_fail (layout && center && widget && dock < DOCKS);

    GList * node = g_list_find_custom (items, widget, (GCompareFunc) item_by_widget);
    g_return_if_fail (node && node->data);
    Item * item = node->data;

    g_return_if_fail (item->vbox);
    g_object_ref (item->vbox);

    item_remove (item);
    items = g_list_remove_link (items, node);
    item->dock = dock;
    items = g_list_concat (items, node);
    item_add (item);

    g_object_unref (item->vbox);
}

void layout_remove (GtkWidget * widget)
{
    g_return_if_fail (layout && center && widget);

    /* menu may hold pointers to this widget */
    if (menu)
        gtk_widget_destroy (menu);

    GList * node = g_list_find_custom (items, widget, (GCompareFunc) item_by_widget);
    g_return_if_fail (node && node->data);
    Item * item = node->data;

    item_remove (item);
    g_return_if_fail (! item->widget && ! item->vbox && ! item->window);
}

void layout_save (void)
{
    gchar scratch[PATH_MAX];
    FILE * handle;

    snprintf (scratch, sizeof scratch, "%s/" LAYOUT_FILE,
     aud_get_path (AUD_PATH_USER_DIR));
    handle = fopen (scratch, "w");
    g_return_if_fail (handle);

    for (GList * node = items; node; node = node->next)
    {
        Item * item = node->data;
        g_return_if_fail (item && item->name);

        if (item->widget)
            item_save_size (item);

        fprintf (handle, "item %s\npane %d\nx %d\ny %d\nw %d\nh %d\n",
         item->name, item->dock, item->x, item->y, item->w, item->h);
    }

    fclose (handle);
}

static gchar parse_key[512];
static gchar * parse_value;

static void parse_next (FILE * handle)
{
    parse_value = NULL;

    if (fgets (parse_key, sizeof parse_key, handle) == NULL)
        return;

    gchar * space = strchr (parse_key, ' ');
    if (space == NULL)
        return;

    * space = 0;
    parse_value = space + 1;

    gchar * newline = strchr (parse_value, '\n');
    if (newline != NULL)
        * newline = 0;
}

static gboolean parse_integer (const gchar * key, gint * value)
{
    return (parse_value != NULL && ! strcmp (parse_key, key) && sscanf
     (parse_value, "%d", value) == 1);
}

static const gchar * parse_string (const gchar * key)
{
    return (parse_value != NULL && ! strcmp (parse_key, key)) ? parse_value : NULL;
}

void layout_load (void)
{
    g_return_if_fail (! items);

    gchar scratch[PATH_MAX];
    FILE * handle;

    snprintf (scratch, sizeof scratch, "%s/" LAYOUT_FILE,
     aud_get_path (AUD_PATH_USER_DIR));
    handle = fopen (scratch, "r");
    if (! handle)
        return;

    while (1)
    {
        parse_next (handle);
        const gchar * name = parse_string ("item");
        if (! name)
            break;

        Item * item = item_new (name);

        parse_next (handle);
        if (! parse_integer ("pane", & item->dock))
            break;
        parse_next (handle);
        if (! parse_integer ("x", & item->x))
            break;
        parse_next (handle);
        if (! parse_integer ("y", & item->y))
            break;
        parse_next (handle);
        if (! parse_integer ("w", & item->w))
            break;
        parse_next (handle);
        if (! parse_integer ("h", & item->h))
            break;
    }

    fclose (handle);
}

void layout_cleanup (void)
{
    for (GList * node = items; node; node = node->next)
    {
        Item * item = node->data;
        g_return_if_fail (item && ! item->widget && ! item->vbox && ! item->window);
        g_free (item->name);
        g_slice_free (Item, item);
    }

    g_list_free (items);
    items = NULL;
}