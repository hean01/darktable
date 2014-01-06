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
}
dt_scan_view_t;

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
//  dt_scan_view_t *view;

  self->data = malloc(sizeof(dt_scan_view_t));
  memset(self->data, 0, sizeof(dt_scan_view_t));
//  view = (dt_scan_view_t *)self->data;

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
expose(dt_view_t *self, cairo_t *cri, int32_t width_i, int32_t height_i,
       int32_t pointerx, int32_t pointery)
{
  GList *modules;
  dt_lib_module_t *module;

#if 0
  dt_scan_view_t *view;

  view = (dt_scan_view_t *)self->data;
#endif

  /* clear background */
  cairo_set_source_rgb (cri, .2, .2, .2);
  cairo_rectangle(cri, 0, 0, width_i, height_i);
  cairo_fill (cri);

  /* draw the preview scan if any */

  /* dispatch post expose to view modules */
  modules = darktable.lib->plugins;
  while(modules)
  {
    module = (dt_lib_module_t *)(modules->data);
    if( (module->views() & self->view(self)) && module->gui_post_expose )
      module->gui_post_expose(module, cri, width_i, height_i, pointerx, pointery);
    modules = g_list_next(modules);
  }

}

int
try_enter(dt_view_t *self)
{
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
#if 0
  dt_scan_view_t *view;
  view = (dt_scan_view_t *)self->data;
#endif
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
