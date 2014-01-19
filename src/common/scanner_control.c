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
#include <inttypes.h>

#include <tiffio.h>

#include <sane/sane.h>

#include "bauhaus/bauhaus.h"
#include "control/conf.h"
#include "common/darktable.h"
#include "common/scanner_control.h"

#define CONFIG_KEY "scan"

/** \brief A scanner description.
    \remark A dt_scanner_t  should be referenced when stored somewhere else
            such in the scan view and by scan jobs when initialized.
 */
typedef struct dt_scanner_t
{
  guint hash;
  uint32_t ref_cnt;
  uint32_t open_ref_cnt;
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


typedef struct dt_scan_backend_t
{
  const dt_scanner_t *scanner;
  void *backend_data;
  int (*init)(struct dt_scan_backend_t *self, SANE_Parameters params, const char *filename);
  void (*cleanup)(struct dt_scan_backend_t *self, const char *filename);
  void (*scanline)(struct dt_scan_backend_t *self, SANE_Parameters params, uint32_t line, float *scanline);
} dt_scan_backend_t;


/* increments reference to a scanner instance */
static void _scanner_ref(gpointer self);
/* decrements reference to a scanner instance, if last _scanner_dtor will be called */
static void _scanner_unref(gpointer self);
/* disptach scan preview update callback */
static void _scanner_dispatch_scan_preview_update(const dt_scanner_t *self);


/*
 * tiff backend
 */
static int
_backend_tiff_init(dt_scan_backend_t *self, SANE_Parameters params, const char *filename)
{
  TIFF *tiff;

  /* open tiff file */
  tiff = TIFFOpen(filename, "w");
  if (tiff == NULL)
    return 1;

  /* setup headers */
  TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, params.pixels_per_line);
  TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, params.lines);
  TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16);
  TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_DEFLATE);
  TIFFSetField(tiff, TIFFTAG_FILLORDER, FILLORDER_MSB2LSB);
  TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 1);
  /* TODO: Maybe we should set scan resolution for convinience */
#if 0
  TIFFSetField(tiff, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
  TIFFSetField(tiff, TIFFTAG_XRESOLUTION, 300.0);
  TIFFSetField(tiff, TIFFTAG_YRESOLUTION, 300.0);
  TIFFSetField(tiff, TIFFTAG_ZIPQUALITY, 9);
#endif
  TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);

  self->backend_data = tiff;
  return 0;
}

static void
_backend_tiff_scanline(dt_scan_backend_t *self, SANE_Parameters params, uint32_t line, float *scanline)
{
  int i;
  uint16_t *pb, *buffer;

  /* TODO: fix a tiff backend struct as backend_data and preallocate */
  pb = buffer = g_malloc(2 * params.pixels_per_line * 3);

  /* downscale into uint16_t buffer */
  for (i = 0; i < params.pixels_per_line * 4; i += 4)
  {
    pb[0] = 0xffff * scanline[i + 0];
    pb[1] = 0xffff * scanline[i + 1];
    pb[2] = 0xffff * scanline[i + 2];
    pb += 3;
  }

  /* write strip to tiff */
  TIFFWriteScanline(self->backend_data, buffer, line, 0);
  g_free(buffer);
}

static void
_backend_tiff_cleanup(struct dt_scan_backend_t *self, const char *filename)
{
  TIFFClose(self->backend_data);
}

/*
 * cairo surface backend
 */
static int
_backend_cairo_init(dt_scan_backend_t *self, SANE_Parameters params, const char *filename)
{
  if (self->scanner->preview)
    cairo_surface_destroy(self->scanner->preview);
  /* Always use RGB24 format of cairo surface */
  ((dt_scanner_t *)self->scanner)->preview = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                                               params.pixels_per_line, params.lines);
  self->backend_data = cairo_image_surface_get_data(self->scanner->preview);
  return 0;
}

static void
_backend_cairo_scanline(dt_scan_backend_t *self, SANE_Parameters params, uint32_t line, float *scanline)
{
  int i;
  uint8_t *pixels;
  pixels = self->backend_data;

  /* scanline is always RGBA independent of what format is used to scan */

  for (i = 0; i < params.pixels_per_line * 4; i+=4)
  {
    pixels[3] = 0x00;
    pixels[0] = 0xff * scanline[i + 0];
    pixels[1] = 0xff * scanline[i + 1];
    pixels[2] = 0xff * scanline[i + 2];

    pixels += 4;
  }
  self->backend_data = pixels; 

  /* signal ui to update */
  cairo_surface_mark_dirty(self->scanner->preview);
  _scanner_dispatch_scan_preview_update(self->scanner);
}



static dt_scanner_t *
_scanner_ctor(const SANE_Device *device)
{
  dt_scanner_t *scanner;
  scanner = malloc(sizeof(dt_scanner_t));
  memset(scanner, 0, sizeof(dt_scanner_t));
  scanner->device = device;
  scanner->hash = g_str_hash(dt_scanner_name(scanner));

  _scanner_ref(scanner);

  return scanner;
}


static void
_scanner_dtor(gpointer self)
{
  dt_scanner_close((dt_scanner_t *)self);
  free(self);
}

static void
_scanner_ref(gpointer self)
{
  ((dt_scanner_t *)self)->ref_cnt++;
}

static void
_glist_scanner_ref(gpointer data, gpointer user_data)
{
  _scanner_ref(data);
}

static void
_scanner_unref(gpointer self)
{
  dt_scanner_t * scanner;
  scanner = self;
  if (--scanner->ref_cnt == 0)
    _scanner_dtor(scanner);
}


static void
_scanner_control_remove_devices(dt_scanner_control_t *self)
{
  dt_print(DT_DEBUG_SCANCTL,"[scanner_control] Removing all devices.\n");
  g_list_free_full(self->devices, _scanner_unref);
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

static SANE_Fixed
_scanner_option_get_fixed_value_by_name(const dt_scanner_t *self, const char *name)
{
  int idx;
  SANE_Fixed fval;
  SANE_Status res;

  idx = _scanner_option_index_by_name(self, name);
  if (idx == -1)
    return -1;

  res = sane_control_option(self->handle, idx, SANE_ACTION_GET_VALUE, &fval, NULL);
  if (res != SANE_STATUS_GOOD)
  {
    fprintf(stderr, "[scanner_control] Failed to get option '%s' value with reason: %s\n",
            name, sane_strstatus(res));
    return -1;
  }

  return fval;
}

#if 0
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
#endif

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
_scanner_control_on_option_combo_changed(GtkWidget *w, gpointer opaque)
{
  int idx;
  const GList *values;
  char *name, *value;
  char key[512];
  struct dt_scanner_t *scanner;

  scanner = (dt_scanner_t *)opaque;
  name = g_object_get_data(G_OBJECT(w), "option-name");

  /* get current selected value */
  values = dt_bauhaus_combobox_get_labels(w);
  idx = dt_bauhaus_combobox_get(w);
  value = g_list_nth_data((GList *)values, idx);

  g_snprintf(key, 512, "%s/devices/%x/%s", CONFIG_KEY, scanner->hash, name);

  dt_conf_set_string(key, value);
}

static void
_scanner_control_on_option_slider_changed(GtkWidget *w, gpointer opaque)
{
  char *name;
  double value;
  char key[512];
  struct dt_scanner_t *scanner;

  scanner = (dt_scanner_t *)opaque;
  name = g_object_get_data(G_OBJECT(w), "option-name");
  value = dt_bauhaus_slider_get(w);
  g_snprintf(key, 512, "%s/devices/%x/%s", CONFIG_KEY, scanner->hash, name);
  dt_conf_set_float(key, value);
}

static void
_scanner_option_set_value(const dt_scanner_t *self,
                          const char *name, const char *value)
{
  int idx;
  SANE_Status res;
  SANE_Int ival;
  SANE_Fixed fval;
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
  case SANE_TYPE_FIXED:
    fval = SANE_FIX(strtod(value, NULL));
    res = sane_control_option(self->handle, idx, SANE_ACTION_SET_VALUE, &fval, NULL);
    break;
  default:
    fprintf(stderr, "[scanner_control] Unsupported value type %d\n", desc->type);
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

  dt_print(DT_DEBUG_SCANCTL, "[scanner_control] Find available scanners.\n");

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
  /* lets increment reference to all scanners before returning list of devices
     it's the callers responsibility to unref the scanners when used. */
  g_list_foreach(self->devices, (GFunc)_glist_scanner_ref, NULL);
  return self->devices;
}

const struct dt_scanner_t *
dt_scanner_control_get_scanner_by_index(struct dt_scanner_control_t *self, uint32_t index)
{
  dt_scanner_t *scanner;

  scanner = g_list_nth_data(self->devices, index);
  if (scanner != NULL)
    _scanner_ref(scanner);

  return scanner;
}

const struct dt_scanner_t *
dt_scanner_control_get_scanner_by_name(struct dt_scanner_control_t *self, const char *name)
{
  GList *item;
  dt_scanner_t *scanner;

  scanner = NULL;
  item = self->devices;
  while(item)
  {
    if (strcmp(name, dt_scanner_name((dt_scanner_t *)item->data)) == 0)
    {
      scanner = item->data;
      break;
    }

    item = g_list_next(item);
  }

  if (scanner != NULL)
    _scanner_ref(scanner);

  return scanner;
}


int
dt_scanner_open(const dt_scanner_t *self)
{
  SANE_Status res;
  if (self->handle == NULL)
  {
    dt_print(DT_DEBUG_SCANCTL,"[scanner_control] Opening device '%s'.\n", self->device->name);
    res = sane_open(self->device->name, &((dt_scanner_t *)self)->handle);
    if (res != SANE_STATUS_GOOD)
    {
      fprintf(stderr, "[scanner_control] Failed to open device '%s' with reason: %s\n",
              self->device->name, sane_strstatus(res));
      return 1;
    }
  }

  /* increase open reference count */
  ((dt_scanner_t *)self)->open_ref_cnt++;
  return 0;
}


void
dt_scanner_close(const dt_scanner_t *self)
{
  /* if not open do nothing */
  if (self->handle == NULL)
    return;

  /* check open reference count */
  if (--((dt_scanner_t*)self)->open_ref_cnt > 0)
    return;

  dt_print(DT_DEBUG_SCANCTL,"[scanner_control] Closing device '%s'.\n", self->device->name);

  /* remove listeners */
  while (self->listeners)
    ((dt_scanner_t *)self)->listeners = g_list_delete_link(self->listeners, self->listeners);

  /* if scanner is opened, close the handle */
  sane_close(self->handle);
  ((dt_scanner_t *)self)->handle = NULL;
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
dt_scanner_create_option_widget(const struct dt_scanner_t *self, const char *name, GtkWidget **control)
{
  int i, cnt, known_scanner;
  char key[512], device_config_key[128];
  char buf[128];
  const SANE_Int *ival;
  const SANE_String_Const *sval;
  SANE_Int current_ival;
  char *current_sval;
  SANE_Fixed current_fval;
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

  /* create options control */
  if (option->type == SANE_TYPE_STRING && option->constraint_type == SANE_CONSTRAINT_STRING_LIST)
  {
    /* handle list of strings */
    cnt = 0;
    *control = dt_bauhaus_combobox_new(NULL);
    dt_bauhaus_widget_set_label(*control, NULL, option->title);

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
      current_sval = _scanner_option_get_string_value_by_name(self, name);
      dt_conf_set_string(key, current_sval);
    }

    sval = option->constraint.string_list;
    while (*sval)
    {
      /* add new option to combo */
      dt_bauhaus_combobox_add(*control, *sval);

      /* if this option is current value, select it */
      if (current_sval && strcmp(*sval, current_sval) == 0)
        dt_bauhaus_combobox_set(*control, cnt);

      cnt++;
      sval++;
    }

    /* setup signal for control */
    g_signal_connect(G_OBJECT(*control), "value-changed",
                     G_CALLBACK(_scanner_control_on_option_combo_changed), (gpointer)self);

    /* cleanup */
    g_free(current_sval);
  }
  else if (option->type == SANE_TYPE_INT && option->constraint_type == SANE_CONSTRAINT_WORD_LIST)
  {
    /* handle list of integers */
    *control = dt_bauhaus_combobox_new(NULL);
    dt_bauhaus_widget_set_label(*control, NULL, option->title);

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
      current_ival = _scanner_option_get_int_value_by_name(self, name);
      g_snprintf(buf, 128, "%d", current_ival);
      dt_conf_set_string(key, buf);
    }

    ival = option->constraint.word_list;
    cnt = *ival;
    for (i = 0; i < cnt; i++)
    {
      ival++;
      g_snprintf(buf, 128, "%d", *ival);
      dt_bauhaus_combobox_add(*control, buf);
      if (*ival == current_ival)
        dt_bauhaus_combobox_set(*control, i);
    }

    g_signal_connect(G_OBJECT(*control), "value-changed",
                     G_CALLBACK(_scanner_control_on_option_combo_changed), (gpointer)self);
  }
  else if (option->type == SANE_TYPE_FIXED && option->constraint_type == SANE_CONSTRAINT_RANGE)
  {
    /* handle fixed range */

    /* lookup if we have a stored value for this scanner option otherwise
       get the value from scanner to be selected */
    g_snprintf(key, 512, "%s/%s", device_config_key, name);
    if (dt_conf_key_exists(key))
    {
      /* get stored config value for this option */
      current_fval = SANE_FIX(strtod(dt_conf_get_string(key), NULL));
    }
    else
    {
      /* get actual value from scanner and store it to config */
      current_fval = _scanner_option_get_fixed_value_by_name(self, name);
      g_snprintf(buf, 128, "%f", SANE_UNFIX(current_fval));
      dt_conf_set_string(key, buf);
    }

    *control = dt_bauhaus_slider_new_with_range(NULL,
                                                SANE_UNFIX(option->constraint.range->min),
                                                SANE_UNFIX(option->constraint.range->max),
                                                SANE_UNFIX(option->constraint.range->quant),
                                                SANE_UNFIX(current_fval), 3);
    dt_bauhaus_widget_set_label(*control, NULL, option->title);

    g_signal_connect (G_OBJECT (*control), "value-changed",
                      G_CALLBACK(_scanner_control_on_option_slider_changed), (gpointer)self);
  }
  else
  {
    fprintf(stderr, "[scanner_control] Unsupported option type %d for '%s'\n",
            option->type, name);

    /* destroy and cleanup */
    *control = NULL;

    return FALSE;
  }

  /* set tooltip of control */
  if (option->desc)
    g_object_set(G_OBJECT(*control), "tooltip-text", option->desc, (char *)NULL);

  g_object_set_data(G_OBJECT(*control), "option-name", (gpointer)name);

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

/* convert input buffer 8bit grayscale into RGBA float buffer
   returns number of pixels processed.
*/
static int
_scanline_gray8(const SANE_Parameters *params, uint8_t *buf, size_t ipcnt, float *out, size_t opcnt)
{
  int i;
  int pcnt;
  float *pout;

  /* how many pixels can we write into out */
  pcnt = ipcnt;
  if (pcnt > opcnt)
    pcnt = opcnt;

  /* convert 8bit grayscale data into RGBA float values */
  pout = out;
  for (i = 0; i < pcnt; i++)
  {
    pout[0] = pout[1] = pout[2] = buf[i] / (float)0xff;
    pout[3] = 0.0f;
    pout += 4;
  }
  return pcnt;
}

/* convert input buffer 16bit grayscale into RGBA float buffer
   returns number of pixels processed.
*/
static int
_scanline_gray16(const SANE_Parameters *params, uint8_t *buf, size_t ipcnt, float *out, size_t opcnt)
{
  int i;
  int pcnt;
  uint16_t *pin;
  float *pout;

  /* how many pixels can we write into out */
  pcnt = ipcnt;
  if (pcnt > opcnt)
    pcnt = opcnt;

  /* convert 16bit grayscale data into RGBA float values */
  pin = (uint16_t *)buf;
  pout = out;
  for (i = 0; i < pcnt; i++)
  {
    pout[0] = pout[1] = pout[2] = *pin / (float)0xffff;
    pout[3] = 0.0f;

    pin++;
    pout += 4;
  }
  return pcnt;
}

static int
_scanline_rgb8(const SANE_Parameters *params, uint8_t *buf, size_t ipcnt, float *out, size_t opcnt)
{
  int i;
  int pcnt;
  float *pout;

  /* how many pixels can we write into out */
  pcnt = ipcnt;
  if (pcnt > opcnt)
    pcnt = opcnt;

  /* convert 8bit RGB data into RGBA float values */
  pout = out;
  for (i = 0; i < pcnt*3; i+=3)
  {
    pout[0] = buf[i+0] / (float)0xff;
    pout[1] = buf[i+1] / (float)0xff;
    pout[2] = buf[i+2] / (float)0xff;
    pout[3] = 0.0f;
    pout += 4;
  }
  return pcnt;
}

static int
_scanline_rgb16(const SANE_Parameters *params, uint8_t *buf, size_t ipcnt, float *out, size_t opcnt)
{
  int i;
  int pcnt;
  uint16_t *pin;
  float *pout;

  /* how many pixels can we write into out */
  pcnt = ipcnt;
  if (pcnt > opcnt)
    pcnt = opcnt;

  /* convert 16bit RGB data into RGBA float values */
  pin = (uint16_t *)buf;
  pout = out;
  for (i = 0; i < pcnt; i++)
  {
    pout[0] = pin[0] / (float)0xffff;
    pout[1] = pin[1] / (float)0xffff;
    pout[2] = pin[2] / (float)0xffff;
    pout[3] = 0.0f;
    pin += 3;
    pout += 4;
  }
  return pcnt;
}

static int
_scanner_scan_to_backend(const struct dt_scanner_t *self,
                         dt_scan_backend_t *backend, const char *filename)
{
  int result;
  SANE_Int len;
  SANE_Status res;
  SANE_Parameters params;
  uint8_t *buf;
  int want_bytes;
  int scanline_fill, scanline_count;
  int pixels_processed, bytes_left;
  float *scanline;

  backend->scanner = self;
  result = 0;
  buf = NULL;
  scanline = NULL;

  /* get scan parameters */
  res = sane_get_parameters(self->handle, &params);
  if (res != SANE_STATUS_GOOD) {
    fprintf(stderr, "[scanner_control] Failed to get preview scan parameters with reason: %s\n",
            sane_strstatus(res));
    return 1;
  }

  fprintf(stderr,"[scanner_control] Final scan params: format=%d, bytes_per_line=%d, pixels_per_line=%d, lines=%d, depth=%d\n",
          params.format, params.bytes_per_line, params.pixels_per_line,
          params.lines, params.depth);

  /* verify supported params */
  if (params.format != SANE_FRAME_GRAY && params.format != SANE_FRAME_RGB)
  {
    fprintf(stderr, "[scanner_control] Unsupported frame type %d\n", params.format);
    return 1;
  }

  if (params.depth != 8 && params.depth != 16)
  {
    fprintf(stderr, "[scanner_control] Unsupported depth %d\n", params.depth);
    return 1;
  }

  /* initialize backend */
  if (backend->init)
    backend->init(backend, params, filename);

  /* start scan */
  res = sane_start(self->handle);
  if (res != SANE_STATUS_GOOD)
  {
    fprintf(stderr, "[scanner_control] Failed to start scan with reason: %s\n",
            sane_strstatus(res));
    return 1;
  }

  sane_set_io_mode(self->handle, 0);
  buf = g_malloc(params.bytes_per_line);
  scanline = g_malloc(sizeof(float) * params.pixels_per_line * 4);
  scanline_fill = 0;
    
  /* read data from scanner */
  res = SANE_STATUS_GOOD;
  bytes_left = 0;
  pixels_processed = 0;
  want_bytes = params.bytes_per_line;
  scanline_count = 0;
  while (res != SANE_STATUS_EOF)
  {
    /* read bytes into buffer from scanner */
    res = sane_read(self->handle, buf + bytes_left, want_bytes, &len);
    
    if (res != SANE_STATUS_GOOD && res != SANE_STATUS_EOF)
    {
      fprintf(stderr, "[scanner_control] Failed to read data from preview scan with reason: %s\n",
              sane_strstatus(res));
      result = 1;
      goto bail_out;
    }

    /* convert and fill scanned pixels into scanline */
    len += bytes_left;
    bytes_left = len;
    while(bytes_left)
    {
      /* GRAY */
      if (params.format == SANE_FRAME_GRAY)
      {
        /* 8bit Grayscale data */
        if (params.depth == 8)
        {
          pixels_processed = _scanline_gray8(&params,
                                             buf + (len - bytes_left),
                                             bytes_left,
                                             scanline + (scanline_fill * 4),
                                             (params.pixels_per_line - scanline_fill));
          bytes_left -= pixels_processed;
        }
        else if (params.depth == 16)
        {
          pixels_processed = _scanline_gray16(&params,
                                              buf + (len - bytes_left),
                                              (bytes_left / 2),
                                              scanline + (scanline_fill * 4),
                                              (params.pixels_per_line - scanline_fill));
          bytes_left -= (pixels_processed * 2);
        }
      }
      /* RGB */
      else if (params.format == SANE_FRAME_RGB)
      {
        /* 8bit RGB data */
        if (params.depth == 8)
        {
          pixels_processed = _scanline_rgb8(&params,
                                            buf + (len - bytes_left),
                                            bytes_left/3,                                            
                                            scanline + (scanline_fill * 4),
                                            (params.pixels_per_line - scanline_fill));
          bytes_left -= (pixels_processed * 3);
        }
        else if (params.depth == 16)
        {
          pixels_processed = _scanline_rgb16(&params,
                                             buf + (len - bytes_left),
                                             (bytes_left / 2 / 3),
                                             scanline + (scanline_fill * 4),
                                             (params.pixels_per_line - scanline_fill));
          bytes_left -= (pixels_processed * 2 * 3);
        }

      }

      /* check if pixels where processed */
      if (pixels_processed == 0)
      {
        uint8_t tmp[4];
        memcpy(tmp, buf + (len - bytes_left), bytes_left);
        memcpy(buf, tmp, bytes_left);
        want_bytes =  params.bytes_per_line - bytes_left;

        break;
      }

      /* if scanline is filled push it to backend */
      scanline_fill += pixels_processed;
      if (scanline_fill == params.pixels_per_line)
      {
        if (backend->scanline)
          backend->scanline(backend, params, scanline_count, scanline);

        scanline_count++;
        scanline_fill = 0;
      }
    }
  }

bail_out:

  /* cleanup backend */
  if (backend->cleanup)
    backend->cleanup(backend, filename);

  g_free(buf);
  g_free(scanline);
  return result;
}

int
dt_scanner_scan_preview(const struct dt_scanner_t *self)
{
  int result;
  dt_scan_backend_t cairo;
 
  _scanner_change_state(self, SCANNER_STATE_BUSY);

  /* set options from configuration for specified scanner */
  _scanner_set_options_from_config(self);

  /* setup scan preview options */
//  _scanner_option_set_bool_value_by_name(self, "preview", TRUE);
//  _scanner_option_set_int_value_by_name(self, "resolution", 200);

  /* setup cairo backend */
  memset(&cairo, 0, sizeof(cairo));
  cairo.init = _backend_cairo_init;
  cairo.scanline = _backend_cairo_scanline;

  result = _scanner_scan_to_backend(self, &cairo, NULL);

  /* reset scan preview options */
//  _scanner_option_set_bool_value_by_name(self, "preview", TRUE);
  _scanner_change_state(self, SCANNER_STATE_READY);
  return result;
}

int
dt_scanner_scan(const struct dt_scanner_t *self, dt_scanner_job_t *job)
{
  int result;
  dt_scan_backend_t tiff;

  _scanner_change_state(self, SCANNER_STATE_BUSY);

  /* set options from configuration for specified scanner */
  _scanner_set_options_from_config(self);

  /* TODO: detect if scanner supports IR channel */

  /* setup tiff backend */
  memset(&tiff, 0, sizeof(tiff));
  tiff.init = _backend_tiff_init;
  tiff.scanline = _backend_tiff_scanline;
  tiff.cleanup = _backend_tiff_cleanup;
  result = _scanner_scan_to_backend(self, &tiff, job->destination_filename);

  _scanner_change_state(self, SCANNER_STATE_READY);
  return result;
}

void
dt_scanner_ref(const struct dt_scanner_t *self)
{
  _scanner_ref((dt_scanner_t *)self);
}

void
dt_scanner_unref(const struct dt_scanner_t *self)
{
  _scanner_unref((dt_scanner_t *)self);
}
