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

#include <stdio.h>
#include <stdlib.h>

#include "views/view.h"
#include "libs/lib.h"
#include "control/control.h"
#include "common/darktable.h"
#include "common/scanner_control.h"

DT_MODULE(1)

typedef struct dt_scan_view_t
{
  struct dt_scanner_t *scanner;
  dt_scanner_listener_t *scanner_listener;
}
dt_scan_view_t;

static void
_scan_view_set_scanner(const dt_view_t *self, struct dt_scanner_t *scanner)
{
  dt_scan_view_t *view;
  view = (dt_scan_view_t *)self->data;

  /* close previously active scanner */
  if (view->scanner)
    dt_scanner_close(view->scanner);

  /* try open the new one */
  if (dt_scanner_open(scanner) != 0)
  {
    dt_control_log("Failed to open selected scanner...");
    /* TODO: Bail out from view... */
  }

  view->scanner = scanner;
  dt_control_log("Using scanner %s", dt_scanner_model(scanner));

  /* add view as listener to scanner */
  dt_scanner_add_listener(view->scanner, view->scanner_listener);

  /* notify about the scanner to be used */
  dt_control_signal_raise(darktable.signals, DT_SIGNAL_VIEW_SCAN_ACTIVE_SCANNER_CHANGED,
                          view->scanner, NULL);
}

static const struct dt_scanner_t *
_scan_view_get_scanner(const dt_view_t *self)
{
  dt_scan_view_t *view;
  view = (dt_scan_view_t *)self->data;
  return view->scanner;
}

static void
_scan_view_on_preview_update(const struct dt_scanner_t *scanner, void *opaque)
{
  /* on preview update callback, redraw center view */
  dt_control_queue_redraw_center();
}

const char *
name(dt_view_t *self)
{
  return _("scan");
}

uint32_t
view(dt_view_t *self)
{
  return DT_VIEW_SCAN;
}

void
init(dt_view_t *self)
{
  dt_scan_view_t *view;

  self->data = malloc(sizeof(dt_scan_view_t));
  memset(self->data, 0, sizeof(dt_scan_view_t));

  view = (dt_scan_view_t *)self->data;

  /* setup the scan view proxy */
  darktable.view_manager->proxy.scan.view = self;
  darktable.view_manager->proxy.scan.set_scanner = _scan_view_set_scanner;
  darktable.view_manager->proxy.scan.get_scanner = _scan_view_get_scanner;

  /* create scanner listener */
  view->scanner_listener = malloc(sizeof(dt_scanner_listener_t));
  memset(view->scanner_listener, 0, sizeof(dt_scanner_listener_t));
  view->scanner_listener->opaque = self;
  view->scanner_listener->on_scan_preview_update = _scan_view_on_preview_update;
}

void
cleanup(dt_view_t *self)
{
  free(self->data);
}

void
configure(dt_view_t *self, int wd, int ht)
{
}

void
expose(dt_view_t *self, cairo_t *cr, int32_t width_i, int32_t height_i,
       int32_t pointerx, int32_t pointery)
{
  GList *modules;
  dt_lib_module_t *module;
  dt_scan_view_t *view;
  cairo_surface_t *preview;

  view = (dt_scan_view_t *)self->data;

  /* clear background */
  cairo_set_source_rgb (cr, .2, .2, .2);
  cairo_rectangle(cr, 0, 0, width_i, height_i);
  cairo_fill (cr);

  /* draw the preview scan if any */
  preview = (cairo_surface_t *)dt_scanner_preview(view->scanner);
  if (preview)
  {
    cairo_rectangle(cr, 0, 0, width_i, height_i);
    cairo_set_source_surface (cr, preview, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgb (cr, .3, .3, .3);
    cairo_stroke(cr);
  }

  /* dispatch post expose to view modules */
  modules = darktable.lib->plugins;
  while(modules)
  {
    module = (dt_lib_module_t *)(modules->data);
    if( (module->views() & self->view(self)) && module->gui_post_expose )
      module->gui_post_expose(module, cr, width_i, height_i, pointerx, pointery);
    modules = g_list_next(modules);
  }
}

int
try_enter(dt_view_t *self)
{
  dt_scan_view_t *view;
  const GList *scanners;

  view = (dt_scan_view_t *)self->data;

  /* If we do have a scanner lets enter */
  if (view->scanner)
    return 0;

  /* No active scanner assume first time enter for this instance
     and find new scanners and activate the first in list.
  */
  scanners = NULL;
  dt_scanner_control_find_scanners(darktable.scanctl);
  scanners = dt_scanner_control_get_scanners(darktable.scanctl);
  if (scanners == NULL)
  {
    dt_control_log(_("no scanners available for use..."));
    return 1;
  }

  /* Set first scanner in list as active one for the view.
     TODO: set from stored last_ui configuration
  */
  _scan_view_set_scanner(self, scanners->data);

  return 0;
}

void
enter(dt_view_t *self)
{
#if 0
  dt_scan_view_t *view;
  view = (dt_scan_view_t *)self->data;
#endif
}

void
leave(dt_view_t *self)
{
  dt_scan_view_t *view;
  view = (dt_scan_view_t *)self->data;

  /* remove view as listener of scanner */
  dt_scanner_add_listener(view->scanner, view->scanner_listener);
}

void
reset(dt_view_t *self)
{
#if 0
  dt_scan_view_t *view;
  view = (dt_scan_view_t *)self->data;
#endif
}

void
mouse_moved(dt_view_t *self, double x, double y, double pressure, int which)
{
#if 0
  dt_scan_view_t *view;
  view = (dt_scan_view_t *)self->data;
#endif
  dt_control_queue_redraw_center();
}

void
init_key_accels(dt_view_t *self)
{
}

void
connect_key_accels(dt_view_t *self)
{
}

int
button_pressed(dt_view_t *self, double x, double y, double pressure, int which, int type, uint32_t state)
{
#if 0
  dt_scan_view_t *view;
  view = (dt_scan_view_t *)self->data;
#endif
  return 0;
}

int button_released(dt_view_t *self, double x, double y, int which, int type, uint32_t state)
{
#if 0
  dt_scan_view_t *view;
  view = (dt_scan_view_t *)self->data;
#endif
  return 0;
}
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
