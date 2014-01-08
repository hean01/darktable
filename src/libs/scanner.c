/*
    This file is part of darktable,
    copyright (c) 2014 henrik andersson.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <assert.h>

#include "control/control.h"
#include "control/jobs/scanner_jobs.h"
#include "common/darktable.h"
#include "common/scanner_control.h"
#include "libs/lib.h"
#include "gui/gtk.h"
#include "dtgtk/button.h"

DT_MODULE(1)

typedef struct dt_lib_scanner_t
{
  /** Gui part of the module */
  struct
  {
    GtkComboBoxText *scanners;
    GtkWidget *refresh;
    GtkHBox *options;
    GtkWidget *preview;
  } gui;

  /* data part of module */
  struct
  {
    dt_scanner_listener_t *scanner_listener;
  } data;

} dt_lib_scanner_t;

static void
_scanner_populate_scanner_list(dt_lib_module_t *self)
{
  int idx, active_idx;
  GtkTreeIter iter;
  GList *scanners;
  dt_lib_scanner_t *lib;
  struct dt_scanner_t *scanner;
  const struct dt_scanner_t *active_scanner;

  lib = self->data;

  /* remove all items in combobox
   * TODO: use gtk_combo_box_text_remove_all() but it's Gtk3 only
   */
  if (gtk_tree_model_get_iter_first(gtk_combo_box_get_model(GTK_COMBO_BOX(lib->gui.scanners)), &iter))
    gtk_combo_box_text_remove(lib->gui.scanners, 0);

  /* add available scanners */
  scanners = (GList *)dt_scanner_control_get_scanners(darktable.scanctl);
  if (g_list_length(scanners) == 0)
  {
    dt_control_log("No scanners found...");
    /* TODO: switch back to lighttable mode */
    return;
  }

  /* we have scanners lets populate the list */
  active_idx = idx = 0;
  active_scanner = dt_view_scan_get_scanner(darktable.view_manager);
  while(scanners)
  {
    scanner = scanners->data;
    gtk_combo_box_text_append_text(lib->gui.scanners,
                                   dt_scanner_model(scanner));

    if (strcmp(dt_scanner_name(scanner), dt_scanner_name(active_scanner)) == 0)
      active_idx = idx;

    scanners = g_list_next(scanners);
    idx++;
  }

  gtk_combo_box_set_active(GTK_COMBO_BOX(lib->gui.scanners), active_idx);
}

static char * _scanner_options[] = {
  "source",
  "mode",
  "depth",
  "resolution",
  NULL
};

static void
_scanner_rebuild_scanner_options(dt_lib_module_t *self, const struct dt_scanner_t *scanner)
{
  char **sopt;
  GtkWidget *labels, *controls;
  GtkWidget *label, *widget;
  dt_lib_scanner_t *lib;
  lib = (dt_lib_scanner_t *)self->data;

  /* destroy previous options if any */
  if (lib->gui.options)
    gtk_widget_destroy(GTK_WIDGET(lib->gui.options));

  lib->gui.options = GTK_HBOX(gtk_hbox_new(FALSE, 5));
  labels = gtk_vbox_new(TRUE, 0);
  controls = gtk_vbox_new(TRUE, 0);

  /* get active scanner from view */
  scanner = dt_view_scan_get_scanner(darktable.view_manager);
  if (scanner == NULL)
    abort();

  /* for each scanner option add to ui */
  sopt = _scanner_options;
  while (*sopt)
  {
    if (dt_scanner_create_option_widget(scanner, *sopt, &label, &widget))
    {
      gtk_box_pack_start(GTK_BOX(labels), label, TRUE, TRUE, 2);
      gtk_box_pack_start(GTK_BOX(controls), widget, TRUE, TRUE, 2);
    }
    sopt++;
  }

  /* and new options to ui */
  gtk_box_pack_start(GTK_BOX(lib->gui.options), labels, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(lib->gui.options), controls, TRUE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(lib->gui.options), TRUE, FALSE, 5);
  gtk_box_reorder_child(GTK_BOX(self->widget), GTK_WIDGET(lib->gui.options), 1);
}

static void
_scanner_refresh_button_click(GtkWidget *w, gpointer opaque)
{
  /* find available scanners */
  dt_scanner_control_find_scanners(darktable.scanctl);

  /* repopulate the combobox */
  _scanner_populate_scanner_list(opaque);
}

static void
_scanner_scan_preview_click(GtkWidget *w, gpointer opaque)
{
  dt_job_t job;
  dt_scanner_preview_job_init(&job, dt_view_scan_get_scanner(darktable.view_manager));
  dt_control_add_job(darktable.control, &job);
}

static void
_scanner_scanners_combobox_changed(GtkWidget *w, gpointer opaque)
{
  gint idx;
  GList *scanners;
  dt_lib_scanner_t *lib;
  struct dt_scanner_t *scanner;

  lib = ((dt_lib_module_t *)opaque)->data;

  /* get selected index, if not selected use 0 */
  idx = gtk_combo_box_get_active(GTK_COMBO_BOX(lib->gui.scanners));

  /* reset and fetch new list of scanners */
  scanners = (GList *)dt_scanner_control_get_scanners(darktable.scanctl);
  scanner = g_list_nth_data(scanners, idx);

  if (scanner == NULL)
    abort();

  /* assign scanner to view */
  dt_view_scan_set_scanner(darktable.view_manager, scanner);

}

static void
_scanner_view_scan_active_scanner_changed(const struct dt_scanner_t *scanner, gpointer opaque)
{
  dt_lib_scanner_t *lib;
  lib = ((dt_lib_module_t *)opaque)->data;

  /* add listener to the new scanner */
  dt_scanner_add_listener(scanner, lib->data.scanner_listener);

  /* rebuild scanner specific options */
  _scanner_rebuild_scanner_options(opaque, scanner);
}

/* Called when state of active scanner is changed. */
static void
_scanner_on_scanner_state_changed(const struct dt_scanner_t *scanner,
                                  dt_scanner_state_t state, void *opaque)
{
  dt_lib_scanner_t *lib;
  lib = ((dt_lib_module_t *)opaque)->data;

  /* lock down ui elements */
  if (state == SCANNER_STATE_BUSY)
  {
    gtk_widget_set_sensitive(GTK_WIDGET(lib->gui.scanners), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(lib->gui.refresh), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(lib->gui.options), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(lib->gui.preview), FALSE);
  }
  else
  {
    gtk_widget_set_sensitive(GTK_WIDGET(lib->gui.scanners), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(lib->gui.refresh), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(lib->gui.options), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(lib->gui.preview), TRUE);
  }

  dt_control_queue_redraw();
}

const char*
name ()
{
  return _("scanner");
}

uint32_t
views()
{
  return DT_VIEW_SCAN;
}

uint32_t
container()
{
  return DT_UI_CONTAINER_PANEL_LEFT_CENTER;
}


void
gui_reset (dt_lib_module_t *self)
{
}

int
position ()
{
  return 999;
}

void
gui_init (dt_lib_module_t *self)
{
  GtkWidget *w;
  GtkWidget *vbox1, *hbox1;
  dt_lib_scanner_t *lib;
  const struct dt_scanner_t *scanner;

  self->widget = gtk_vbox_new(TRUE, 5);
  self->data = malloc(sizeof(dt_lib_scanner_t));
  memset(self->data,0,sizeof(dt_lib_scanner_t));

  lib = self->data;

  /* initialize scanner listener */
  lib->data.scanner_listener = malloc(sizeof(dt_scanner_listener_t));
  memset(lib->data.scanner_listener, 0, sizeof(dt_scanner_listener_t));
  lib->data.scanner_listener->opaque = self;
  lib->data.scanner_listener->on_state_changed = _scanner_on_scanner_state_changed;

  vbox1 = self->widget = gtk_vbox_new(FALSE, 5);
  hbox1 = gtk_hbox_new(FALSE, 5);
  
  /* add combobox with available scanners and a refresh button */
  lib->gui.scanners = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
  gtk_box_pack_start(GTK_BOX(hbox1), GTK_WIDGET(lib->gui.scanners),
                     TRUE, TRUE, 0);

  /* populate the scanner list and select active scanner */
  _scanner_populate_scanner_list(self);

  g_signal_connect (G_OBJECT (lib->gui.scanners), "changed",
                    G_CALLBACK (_scanner_scanners_combobox_changed),
                    self);

  /* Add refresh button */
  lib->gui.refresh = w = dtgtk_button_new(dtgtk_cairo_paint_refresh, 0);
  gtk_box_pack_start(GTK_BOX(hbox1), w, TRUE, TRUE, 0);
  g_object_set(G_OBJECT(w), "tooltip-text", _("search for scanners"),
               (char *)NULL);
  gtk_widget_set_size_request(w, 18, 18);
  g_signal_connect(G_OBJECT(w), "clicked",
                   G_CALLBACK(_scanner_refresh_button_click), self);

  gtk_box_pack_start(GTK_BOX(vbox1), GTK_WIDGET(hbox1), TRUE, TRUE, 0);

  /* Add scan preview button */
  lib->gui.preview = w = gtk_button_new_with_label(_("Scan preview"));
  gtk_box_pack_start(GTK_BOX(vbox1), w, TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(w), "clicked",
                   G_CALLBACK(_scanner_scan_preview_click), self);

  /* we want to act upon scan view scanner changes */
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_VIEW_SCAN_ACTIVE_SCANNER_CHANGED,
                            G_CALLBACK(_scanner_view_scan_active_scanner_changed), self);

  /* intialize ui from current scanner */
  scanner = dt_view_scan_get_scanner(darktable.view_manager);
  dt_scanner_add_listener(scanner, lib->data.scanner_listener);
  _scanner_rebuild_scanner_options(self, scanner);
  _scanner_on_scanner_state_changed(scanner, dt_scanner_state(scanner), self);

}

void
gui_cleanup (dt_lib_module_t *self)
{
  dt_lib_scanner_t *lib;

  lib = ((dt_lib_module_t *)self)->data;

  g_free(lib->data.scanner_listener);

  /* disconnect from signals */
  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(_scanner_view_scan_active_scanner_changed), self);

  g_free(self->data);
  self->data = NULL;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
