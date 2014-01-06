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

#include "common/darktable.h"
#include "common/scanner_control.h"
#include "libs/lib.h"
#include "gui/gtk.h"
#include "dtgtk/label.h"

DT_MODULE(1)

typedef struct dt_lib_scanner_t
{
  /** Gui part of the module */
  struct
  {
    GtkComboBoxText *scanners;
  } gui;

  /** Data part of the module */
  struct
  {
    struct dt_scanner_t *scanner;
  } data;
} dt_lib_scanner_t;

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
  const GList *scanners;
  struct dt_scanner_t *scanner;
  GtkBox *vbox1;
  dt_lib_scanner_t *lib;


  self->widget = gtk_vbox_new(TRUE, 5);
  self->data = malloc(sizeof(dt_lib_scanner_t));
  memset(self->data,0,sizeof(dt_lib_scanner_t));

  lib = self->data;

  self->widget = gtk_vbox_new(FALSE, 5);
  
  vbox1 = GTK_BOX(gtk_vbox_new(TRUE, 5));

  /* add combobox with available scanners */
  lib->gui.scanners = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
  scanners = dt_scanner_control_get_scanners(darktable.scanctl);
  while(scanners)
  {
    scanner = scanners->data;
    gtk_combo_box_text_append_text(lib->gui.scanners, dt_scanner_model(scanner));
    scanners = g_list_next(scanners);
  }

  /* TODO: setup signal handler for combobox */

  gtk_combo_box_set_active(GTK_COMBO_BOX(lib->gui.scanners), 0);

  gtk_box_pack_start(vbox1, GTK_WIDGET(lib->gui.scanners), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(vbox1), TRUE, TRUE, 0);
}

void
gui_cleanup (dt_lib_module_t *self)
{
  g_free(self->data);
  self->data = NULL;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
