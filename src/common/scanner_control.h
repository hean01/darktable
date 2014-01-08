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

#ifndef DT_SCANNER_CONTROL_H
#define DT_SCANNER_CONTROL_H

#include <glib.h>
#include <gtk/gtk.h>

struct dt_scanner_t;
struct dt_scanner_control_t;

typedef enum dt_scanner_state_t
{
  SCANNER_STATE_READY,
  SCANNER_STATE_BUSY
} dt_scanner_state_t;

typedef struct dt_scanner_listener_t
{
  void *opaque;
  void (*on_state_changed)(const struct dt_scanner_t *scanner,
                           dt_scanner_state_t state, void *opaque);
  /** \brief callback when there is new data in the scan preview pixbuf */
  void (*on_scan_preview_update)(const struct dt_scanner_t *scanner, void *opaque);
} dt_scanner_listener_t;

struct dt_scanner_control_t *dt_scanner_control_new();
void dt_scanner_control_destroy(struct dt_scanner_control_t *self);

void dt_scanner_control_find_scanners(struct dt_scanner_control_t *self);

const GList *dt_scanner_control_get_scanners(struct dt_scanner_control_t *self);

/** \brief open a scanner for use. */
int dt_scanner_open(struct dt_scanner_t *self);
/** \brief close the previous opend scanner.
    \note This will remove all registered listeners from the scanner.
*/
void dt_scanner_close(struct dt_scanner_t *self);
/** \brief get scanner model. */
const char *dt_scanner_model(const struct dt_scanner_t *self);
/** \brief get scanner name.
    \remark This is the uniq id string for the specific scanner.
*/
const char *dt_scanner_name(const struct dt_scanner_t *self);
/** \brief helper function to create a option widget. */
gboolean dt_scanner_create_option_widget(const struct dt_scanner_t *self, const char *name,
                                         GtkWidget **label, GtkWidget **control);
/** \brief Add a listener to scanner. */
void dt_scanner_add_listener(const struct dt_scanner_t *self, dt_scanner_listener_t *listener);
/** \brief Remove a listener from scanner. */
void dt_scanner_remove_listener(const struct dt_scanner_t *self, dt_scanner_listener_t *listener);
/** \brief Get scanner state. */
dt_scanner_state_t dt_scanner_state(const struct dt_scanner_t *self);

/** \brief Scan preview. */
void dt_scanner_scan_preview(const struct dt_scanner_t *self);
#endif
