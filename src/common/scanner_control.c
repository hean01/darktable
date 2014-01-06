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
#include <memory.h>
#include <assert.h>

#include <sane/sane.h>

#include "common/darktable.h"
#include "common/scanner_control.h"

typedef struct dt_scanner_t
{
  const SANE_Device *device;
  SANE_Handle *handle;
} dt_scanner_t;

typedef struct dt_scanner_control_t
{
  GList *devices;
} dt_scanner_control_t;


static dt_scanner_t *
_scanner_ctor(const SANE_Device *device)
{
  dt_scanner_t *scanner;
  scanner = malloc(sizeof(dt_scanner_t));
  memset(scanner, 0, sizeof(dt_scanner_t));
  scanner->device = device;
  return scanner;
}

static void
_scanner_dtor(dt_scanner_t *self)
{
  if (self->handle)
    sane_close(self->handle);
  free(self);
}

struct dt_scanner_control_t *
dt_scanner_control_new()
{
  SANE_Int version;
  SANE_Status res;
  dt_scanner_control_t *scanctl;

  /* initialize sane */
  res = sane_init(&version, NULL);
  if (res != SANE_STATUS_GOOD)
  {
    fprintf(stderr, "Failed to initialize sane.\n");
    assert(0);
  }

  dt_print(DT_DEBUG_SCANCTL,"[scanner_control] SANE version %x initialized.\n", version);

  /* initialize scanner control instance */
  scanctl = malloc(sizeof(dt_scanner_control_t));
  memset(scanctl, 0, sizeof(dt_scanner_control_t));

  return scanctl;
}

void
dt_scanner_control_destroy(struct dt_scanner_control_t *self)
{
  GList *device;

  /* cleanup all devices */
  while (self->devices)
  {
    device = g_list_last(self->devices);
    _scanner_dtor(device->data);
    self->devices = g_list_remove(self->devices, device);
  }

  free(self);
}

void
dt_scanner_control_find_scanners(struct dt_scanner_control_t *self)
{
  SANE_Status res;
  const SANE_Device **iterator;
  const SANE_Device **device_list;
  dt_scanner_t *device;

  /* lets get all devices */
  res = sane_get_devices(&device_list, 0);
  if (res != SANE_STATUS_GOOD)
  {
    fprintf(stderr, "[scanner_control] Failed to enumerate scanners.\n");
    return;
  }

  /* for each device instantiate a dt_scanner_t and add to list */
  iterator = device_list;
  while (*iterator)
  {
    device = _scanner_ctor(*iterator);
    dt_print(DT_DEBUG_SCANCTL,"[scanner_control] %d, %s - %s (%s)\n",
            g_list_length(self->devices), 
            (*iterator)->vendor,
            (*iterator)->model,
            (*iterator)->name);

    self->devices = g_list_append(self->devices, device);
    ++iterator;
  }

}

const GList *
dt_scanner_control_get_scanners(struct dt_scanner_control_t *self)
{
  return self->devices;
}


const char *
dt_scanner_model(struct dt_scanner_t *self)
{
  return self->device->model;
}
