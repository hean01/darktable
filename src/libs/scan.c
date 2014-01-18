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

typedef struct dt_lib_scan_t
{
  /** Gui part of the module */
  struct
  {
    GtkWidget *scan;
  } gui;

  /* data part of module */
  struct
  {
    dt_scanner_listener_t *scanner_listener;
  } data;

} dt_lib_scan_t;

static void
_scanner_view_scan_active_scanner_changed(gpointer instance, const struct dt_scanner_t *scanner, gpointer opaque)
{
  dt_lib_scan_t *lib;
  lib = ((dt_lib_module_t *)opaque)->data;

  /* add listener to the new scanner */
  dt_scanner_add_listener(scanner, lib->data.scanner_listener);
}

static void
_scan_on_scan_clicked(GtkWidget *w, gpointer opaque)
{
  dt_job_t j;
  const char *jobcode;
  const struct dt_scanner_t *scanner;

  scanner = dt_view_scan_get_scanner(darktable.view_manager);
  jobcode = dt_view_scan_get_job_code(darktable.view_manager);

  /* create scan job and put on job queue */
  dt_scanner_scan_job_init(&j, scanner, jobcode);
  dt_control_add_job(darktable.control, &j);
}

/* Called when state of active scanner is changed. */
static void
_scan_on_scanner_state_changed(const struct dt_scanner_t *scanner,
                                  dt_scanner_state_t state, void *opaque)
{
  dt_lib_scan_t *lib;
  lib = ((dt_lib_module_t *)opaque)->data;

  /* lock down ui elements */
  if (state == SCANNER_STATE_BUSY)
  {
    gtk_widget_set_sensitive(GTK_WIDGET(lib->gui.scan), FALSE);
  }
  else
  {
    gtk_widget_set_sensitive(GTK_WIDGET(lib->gui.scan), TRUE);
  }

  dt_control_queue_redraw();
}

const char*
name ()
{
  return _("scan");
}

uint32_t
views()
{
  return DT_VIEW_SCAN;
}

uint32_t
container()
{
  return DT_UI_CONTAINER_PANEL_RIGHT_CENTER;
}


void
gui_reset (dt_lib_module_t *self)
{
}

int
position ()
{
  return 990;
}

void
gui_init (dt_lib_module_t *self)
{
  GtkWidget *w;
  GtkWidget *vbox1;
  dt_lib_scan_t *lib;
  const struct dt_scanner_t *scanner;

  self->widget = gtk_vbox_new(TRUE, 5);
  self->data = malloc(sizeof(dt_lib_scan_t));
  memset(self->data,0,sizeof(dt_lib_scan_t));

  lib = self->data;

  /* initialize scanner listener */
  lib->data.scanner_listener = malloc(sizeof(dt_scanner_listener_t));
  memset(lib->data.scanner_listener, 0, sizeof(dt_scanner_listener_t));
  lib->data.scanner_listener->opaque = self;
  lib->data.scanner_listener->on_state_changed = _scan_on_scanner_state_changed;

  vbox1 = self->widget = gtk_vbox_new(FALSE, 5);

  /* Add scan button */
  lib->gui.scan = w = gtk_button_new_with_label(_("scan batch"));
  gtk_box_pack_start(GTK_BOX(vbox1), w, TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(w), "clicked",
                   G_CALLBACK(_scan_on_scan_clicked), self);

  /* we want to act upon scan view scanner changes */
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_VIEW_SCAN_ACTIVE_SCANNER_CHANGED,
                            G_CALLBACK(_scanner_view_scan_active_scanner_changed), self);

  /* intialize ui from current scanner */
  scanner = dt_view_scan_get_scanner(darktable.view_manager);
  dt_scanner_add_listener(scanner, lib->data.scanner_listener);
}

void
gui_cleanup (dt_lib_module_t *self)
{
  dt_lib_scan_t *lib;

  lib = ((dt_lib_module_t *)self)->data;
  dt_scanner_remove_listener(dt_view_scan_get_scanner(darktable.view_manager), lib->data.scanner_listener);

  g_free(lib->data.scanner_listener);

  /* disconnect from signals */
  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(_scanner_view_scan_active_scanner_changed), self);

  g_free(self->data);
  self->data = NULL;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
