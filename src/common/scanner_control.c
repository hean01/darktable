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

#include "control/conf.h"
#include "common/darktable.h"
#include "common/scanner_control.h"

#define CONFIG_KEY "scan"

typedef struct dt_scanner_t
{
  guint hash;
  dt_scanner_state_t state;
  const SANE_Device *device;
  SANE_Handle handle;
  GList *listeners;

  cairo_surface_t *preview;

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
  scanner->hash = g_str_hash(dt_scanner_name(scanner));
  return scanner;
}


static void
_scanner_dtor(gpointer self)
{
  dt_scanner_close((dt_scanner_t *)self);
  free(self);
}


static void
_scanner_control_remove_devices(dt_scanner_control_t *self)
{
  g_list_free_full(self->devices, _scanner_dtor);
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

static int
_scanner_option_set_bool_value_by_name(const dt_scanner_t *self, const char *name, gboolean bval)
{
  int idx;
  SANE_Status res;

  idx = _scanner_option_index_by_name(self, name);
  if (idx == -1)
    return 1;

  res = sane_control_option(self->handle, idx, SANE_ACTION_SET_VALUE, &bval, NULL);
  if (res != SANE_STATUS_GOOD)
  {
    fprintf(stderr, "[scanner_control] Failed to set bool option '%s' value to %s with reason: %s\n",
            name, bval ? "true" : "false", sane_strstatus(res));
    return 1;
  }

  return 0;
}

static int
_scanner_option_set_int_value_by_name(const dt_scanner_t *self, const char *name, SANE_Int ival)
{
  int idx;
  SANE_Status res;

  idx = _scanner_option_index_by_name(self, name);
  if (idx == -1)
    return 1;

  res = sane_control_option(self->handle, idx, SANE_ACTION_SET_VALUE, &ival, NULL);
  if (res != SANE_STATUS_GOOD)
  {
    fprintf(stderr, "[scanner_control] Failed to set int option '%s' value to %d with reason: %s\n",
            name, ival, sane_strstatus(res));
    return 1;
  }

  return 0;
}


static void
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

static void
_scanner_dispatch_scan_preview_update(const dt_scanner_t *self)
{
  GList *listener;
  dt_scanner_listener_t *l;

  /* dispatch to listeners */
  listener = self->listeners;
  while (listener)
  {
    l = (dt_scanner_listener_t *)listener->data;
    if (l->on_scan_preview_update)
      l->on_scan_preview_update(self, l->opaque);
    listener = g_list_next(listener);
  }
}

static void
_scanner_control_on_option_changed(GtkWidget *w, gpointer opaque)
{
  char *name, *value;
  char key[512];
  struct dt_scanner_t *scanner;

  scanner = (dt_scanner_t *)opaque;

  name = g_object_get_data(G_OBJECT(w), "option-name");
  value = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(w));

  g_snprintf(key, 512, "%s/devices/%x/%s",CONFIG_KEY, scanner->hash, name);

  dt_conf_set_string(key, value);
}

static void
_scanner_option_set_value(const dt_scanner_t *self,
                          const char *name, const char *value)
{
  int idx;
  SANE_Status res;
  SANE_Int ival;
  const SANE_Option_Descriptor *desc;

  /* get index of option name */
  idx = _scanner_option_index_by_name(self, name);
  if (idx == -1)
    return;

  /* get option descriptor and set converted value */
  desc = _scanner_find_option_desc_by_name(self, name);
  switch(desc->type)
  {
  case SANE_TYPE_STRING:
    res = sane_control_option(self->handle, idx, SANE_ACTION_SET_VALUE, (char *)value, NULL);
    break;
  case SANE_TYPE_INT:
    ival = atoi(value);
    res = sane_control_option(self->handle, idx, SANE_ACTION_SET_VALUE, &ival, NULL);
    break;
  default:
    fprintf(stderr, "[scanner_control] Unsupported value type %d", desc->type);
    return;
  }

  if (res != SANE_STATUS_GOOD)
    fprintf(stderr, "[scanner_control] Failed to set option '%s' value to '%s' with reason: %s\n",
            name, value, sane_strstatus(res));

}

static void
_scanner_set_options_from_config(const dt_scanner_t *self)
{
  GSList *options , *item;
  dt_conf_string_entry_t *cse;
  char device_key[512];

  g_snprintf(device_key, 512, "%s/devices/%x", CONFIG_KEY, self->hash);
  options = dt_conf_all_string_entries(device_key);
  if (options == NULL)
  {
    fprintf(stderr, "[scanner_control] No configuration available for scanner %x\n",
            self->hash);
    return;
  }

  item = g_slist_last(options);
  while (item)
  {
    cse = (dt_conf_string_entry_t *)item->data;
    _scanner_option_set_value(self, cse->key, cse->value);
    g_free(cse->key);
    g_free(cse->value);
    g_free(cse);
    options = g_slist_delete_link(options, item);
    item = g_slist_last(options);
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
  int i, cnt, known_scanner;
  char key[512], device_config_key[128];
  char buf[128];
  const SANE_Int *ival;
  const SANE_String_Const *sval;
  SANE_Int current_ival;
  char *current_sval;
  const SANE_Option_Descriptor *option;

  /* do we have stored config values for this scanner */
  g_snprintf(device_config_key, 128, "%s/devices/%x",CONFIG_KEY, self->hash);
  known_scanner = dt_conf_key_exists(device_config_key);

  /* if we never seen this scanner before lets add device model
     subkey for easy recognition */
  if (!known_scanner)
  {
    g_snprintf(key, 512, "%s/model", device_config_key);
    dt_conf_set_string(key, dt_scanner_model(self));
  }

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

    /* lookup if we have a stored value for this scanner option otherwise
       get the value from scanner to be selected */
    g_snprintf(key, 512, "%s/%s", device_config_key, name);
    if (dt_conf_key_exists(key))
    {
      /* get stored config value for this option */
      current_sval = dt_conf_get_string(key);
    }
    else
    {
      /* get actual value from scanner and store it to config */
      current_sval = _scanner_option_get_string_value_by_name (self, name);
      dt_conf_set_string(key, current_sval);
    }

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

    /* cleanup */
    g_free(current_sval);
  }
  else if (option->type == SANE_TYPE_INT && option->constraint_type == SANE_CONSTRAINT_WORD_LIST)
  {
    /* handle list of integers */

    /* lookup if we have a stored value for this scanner option otherwise
       get the value from scanner to be selected */
    g_snprintf(key, 512, "%s/%s", device_config_key, name);
    if (dt_conf_key_exists(key))
    {
      /* get stored config value for this option */
      current_ival = atoi(dt_conf_get_string(key));
    }
    else
    {
      /* get actual value from scanner and store it to config */
      current_ival = _scanner_option_get_int_value_by_name (self, name);
      g_snprintf(buf, 128, "%d", current_ival);
      dt_conf_set_string(key, buf);
    }

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
  }
  else
  {
    fprintf(stderr, "[scanner_control] Unsupported option type %d for '%s'\n",
            option->type, name);

    /* destroy and cleanup */
    gtk_widget_destroy(*label);
    gtk_widget_destroy(*control);
    *label = *control = NULL;

    return FALSE;
  }

  /* connect control option signal handler */
  g_object_set_data(G_OBJECT(*control), "option-name", (gpointer)name);
  g_signal_connect(G_OBJECT(*control), "changed",
                   G_CALLBACK(_scanner_control_on_option_changed), (gpointer)self);

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


const cairo_surface_t *
dt_scanner_preview(const struct dt_scanner_t *self)
{
  return self->preview;
}


void
dt_scanner_scan_preview(const struct dt_scanner_t *self)
{
  int i, byte_count;
  SANE_Int len;
  SANE_Status res;
  SANE_Parameters params;
  uint8_t *pixels;
  uint8_t *buf;
 
  buf = pixels = NULL;
  byte_count = 0;

  _scanner_change_state(self, SCANNER_STATE_BUSY);

  /* setup scan preview options */
  _scanner_option_set_bool_value_by_name(self, "preview", TRUE);
  _scanner_option_set_int_value_by_name(self, "resolution", 200);

  /* get scan parameters */
  res = sane_get_parameters(self->handle, &params);
  if (res != SANE_STATUS_GOOD) {
    fprintf(stderr, "[scanner_control] Failed to get preview scan parameters with reason: %s\n",
            sane_strstatus(res));
    goto bail_out;
  }
  fprintf(stderr,"[scanner_control] Params: format=%d, bytes_per_line=%d, pixels_per_line=%d, lines=%d, depth=%d\n",
          params.format, params.bytes_per_line, params.pixels_per_line,
          params.lines, params.depth);

  /* start scan preview */
  res = sane_start(self->handle);
  if (res != SANE_STATUS_GOOD)
  {
    fprintf(stderr, "[scanner_control] Failed to start preview scan with reason: %s\n",
            sane_strstatus(res));
    goto bail_out;
  }

  /* initialize preview cairo surface */
  if (self->preview)
    cairo_surface_destroy(self->preview);

  ((dt_scanner_t *)self)->preview = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                                               params.pixels_per_line, params.lines);

  pixels = cairo_image_surface_get_data(self->preview);

  /* start read data from scanner */
  /* FIXME: dont assume data is 8bit and RGB interleaved */
  buf = g_malloc(params.bytes_per_line);
  sane_set_io_mode(self->handle, 0);

  while (res != SANE_STATUS_EOF)
  {
    /* read scan line and update cairo surface */
    res = sane_read(self->handle, buf, params.bytes_per_line, &len);
    if (res != SANE_STATUS_GOOD && res != SANE_STATUS_EOF)
    {
      fprintf(stderr, "[scanner_control] Failed to read data from preview scan with reason: %s\n",
              sane_strstatus(res));
      goto bail_out;
    }

    /* write pixels into surface */
    for (i = 0; i < len; i++)
    {
      *pixels = buf[i];

      pixels++; 
      byte_count++;

      /* pixels in CAIRO_FORMAT_RGB24 are stored as 32bit integers where first byte is unused */
      if (byte_count == 3)
      {
        *pixels = 0x00;
        pixels++;
        byte_count = 0;
      }
    } 

    /* signal ui to update pixbuf redraw */
    cairo_surface_mark_dirty(self->preview);
    _scanner_dispatch_scan_preview_update(self);
  }

bail_out:
  g_free(buf);
  /* reset scan preview options */
  _scanner_option_set_bool_value_by_name(self, "preview", TRUE);

  _scanner_change_state(self, SCANNER_STATE_READY);
}

void
dt_scanner_scan(const struct dt_scanner_t *self, dt_scanner_job_t *job)
{
  _scanner_change_state(self, SCANNER_STATE_BUSY);

  /* set options from configuration for specified scanner */
  _scanner_set_options_from_config(self);

  /* TODO: detect if scanner supports IR channel */

  /* TODO: perform scan, and use IR if enabled for dust removal */

  _scanner_change_state(self, SCANNER_STATE_READY);
}
