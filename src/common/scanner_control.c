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
  dt_scanner_state_t state;
  const SANE_Device *device;
  SANE_Handle handle;
  GList *listeners;
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
  dt_scanner_close(self);
  free(self);
}


static void
_scanner_control_remove_devices(dt_scanner_control_t *self)
{
  GList *device;
  while (self->devices)
  {
    device = g_list_last(self->devices);
    _scanner_dtor(device->data);
    self->devices = g_list_remove(self->devices, device);
  }
  self->devices = NULL;
}


static int
_scanner_option_index_by_name(const dt_scanner_t *self, const char *name)
{
  int idx;
  const SANE_Option_Descriptor *opt;

  idx = 0;
  while(1)
  {
    /* get option descriptor */
    opt = sane_get_option_descriptor(self->handle, idx);
    if (opt == NULL)
      return -1;

    if (opt->name && strcmp(opt->name, name) == 0)
      break;

    idx++;
  }

  return idx;
}


static const SANE_Option_Descriptor *
_scanner_find_option_desc_by_name(const dt_scanner_t *self, const char *name)
{
  int idx;

  idx = _scanner_option_index_by_name(self, name);
  if (idx == -1)
    return NULL;

  return sane_get_option_descriptor(self->handle, idx);
}


static char *
_scanner_option_get_string_value_by_name(const dt_scanner_t *self, const char *name)
{
  int idx;
  char buf[256];
  SANE_Status res;

  idx = _scanner_option_index_by_name(self, name);
  if (idx == -1)
    return NULL;

  res = sane_control_option(self->handle, idx, SANE_ACTION_GET_VALUE, buf, NULL);
  if (res != SANE_STATUS_GOOD)
  {
    fprintf(stderr, "[scanner_control] Failed to get option '%s' value with reason: %s\n",
            name, sane_strstatus(res));
    return NULL;
  }

  return g_strdup(buf);
}


static SANE_Int
_scanner_option_get_int_value_by_name(const dt_scanner_t *self, const char *name)
{
  int idx;
  SANE_Int ival;
  SANE_Status res;

  idx = _scanner_option_index_by_name(self, name);
  if (idx == -1)
    return -1;

  res = sane_control_option(self->handle, idx, SANE_ACTION_GET_VALUE, &ival, NULL);
  if (res != SANE_STATUS_GOOD)
  {
    fprintf(stderr, "[scanner_control] Failed to get option '%s' value with reason: %s\n",
            name, sane_strstatus(res));
    return -1;
  }

  return ival;
}


void
_scanner_change_state(const dt_scanner_t *self, dt_scanner_state_t state)
{
  GList *listener;
  dt_scanner_listener_t *l;

  ((dt_scanner_t*)self)->state = state;

  /* dispatch on_state_changed to listeners */
  listener = self->listeners;
  while(listener)
  {
    l = (dt_scanner_listener_t *)listener->data;
    if (l->on_state_changed)
      l->on_state_changed(self, self->state, l->opaque);
    listener = g_list_next(listener);
  }
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
    fprintf(stderr, "Failed to initialize sane with reason: %s\n", sane_strstatus(res));
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
  /* cleanup all devices */
  _scanner_control_remove_devices(self);

  free(self);
}


void
dt_scanner_control_find_scanners(struct dt_scanner_control_t *self)
{
  SANE_Status res;
  const SANE_Device **iterator;
  const SANE_Device **device_list;
  dt_scanner_t *device;

  /* remove all known devices */
  _scanner_control_remove_devices(self);

  /* get all new */
  res = sane_get_devices(&device_list, 0);
  if (res != SANE_STATUS_GOOD)
  {
    fprintf(stderr, "[scanner_control] Failed to enumerate scanners with reason: %s\n", sane_strstatus(res));
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


int
dt_scanner_open(dt_scanner_t *self)
{
  SANE_Status res;

  res = sane_open(self->device->name, &self->handle);
  if (res != SANE_STATUS_GOOD)
  {
    fprintf(stderr, "[scanner_control] Failed to open device '%s' with reason: %s\n",
            self->device->name, sane_strstatus(res));
    return 1;
  }
  return 0;
}


void
dt_scanner_close(dt_scanner_t *self)
{
  /* remove listeners */
  while (self->listeners)
    self->listeners = g_list_delete_link(self->listeners, self->listeners);

  if (self->handle)
    sane_close(self->handle);
}


const char *
dt_scanner_model(const dt_scanner_t *self)
{
  return self->device->model;
}


const char *
dt_scanner_name(const dt_scanner_t *self)
{
  return self->device->name;
}


gboolean
dt_scanner_create_option_widget(const struct dt_scanner_t *self, const char *name,
                                GtkWidget **label, GtkWidget **control)
{
  int i, cnt;
  char buf[128];
  const SANE_Int *ival;
  const SANE_String_Const *sval;
  SANE_Int current_ival;
  char *current_sval;
  const SANE_Option_Descriptor *option;

  /* find option by name */
  option = _scanner_find_option_desc_by_name(self, name);
  if (option == NULL)
  {
    fprintf(stderr, "[scanner_control] No option named '%s' found\n", name);
    return FALSE;
  }

  /* label */
  *label = gtk_label_new(option->title);
  gtk_misc_set_alignment(GTK_MISC(*label), 0.0, 0.5);

  /* create options combobox */
  *control = gtk_combo_box_text_new();
  if (option->desc)
    g_object_set(G_OBJECT(*control), "tooltip-text", option->desc, (char *)NULL);

  if (option->type == SANE_TYPE_STRING)
  {
    /* handle list of strings */
    cnt = 0;

    /* get current selected string value of option */
    current_sval = _scanner_option_get_string_value_by_name (self, name);

    sval = option->constraint.string_list;
    while (*sval)
    {
      /* add new option to combo */
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(*control), *sval);

      /* if this option is current value, select it */
      if (current_sval && strcmp(*sval, current_sval) == 0)
        gtk_combo_box_set_active(GTK_COMBO_BOX(*control), cnt);

      cnt++;
      sval++;
    }

    /* TODO: add signal handler for change */

    /* cleanup */
    g_free(current_sval);
  }
  else if (option->type == SANE_TYPE_INT && option->constraint_type == SANE_CONSTRAINT_WORD_LIST)
  {
    /* handle list of integers */

    /* get current selected int value of option */
    current_ival = _scanner_option_get_int_value_by_name(self, name);

    ival = option->constraint.word_list;
    cnt = *ival;
    for (i = 0; i < cnt; i++)
    {
      ival++;
      g_snprintf(buf, 128, "%d", *ival);
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(*control), buf);
      if (*ival == current_ival)
        gtk_combo_box_set_active(GTK_COMBO_BOX(*control), i);
    }

    /* TODO: add signal handler for change */

  }

  return TRUE;
}


void
dt_scanner_add_listener(const struct dt_scanner_t *self, dt_scanner_listener_t *listener)
{
  /* add listener */
  ((dt_scanner_t *)self)->listeners = g_list_append(self->listeners, listener);
}


void
dt_scanner_remove_listener(const struct dt_scanner_t *self,dt_scanner_listener_t *listener)
{
  /* remove listener */
  ((dt_scanner_t *)self)->listeners = g_list_remove(self->listeners, listener);
}


dt_scanner_state_t
dt_scanner_state(const struct dt_scanner_t *self)
{
  return self->state;
}


void
dt_scanner_scan_preview(const struct dt_scanner_t *self)
{
  _scanner_change_state(self, SCANNER_STATE_BUSY);
  sleep(4);
  _scanner_change_state(self, SCANNER_STATE_READY);
}
