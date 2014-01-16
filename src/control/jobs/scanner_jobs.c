/*
    This file is part of darktable,
    copyright (c) 2014 Henrik Andersson.

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

#include "control/jobs/scanner_jobs.h"
#include "control/jobs/image_jobs.h"

int32_t
dt_scanner_preview_job_run(dt_job_t *job)
{
  int res;
  dt_scanner_preview_job_t *t=(dt_scanner_preview_job_t*)job->param;
  res = dt_scanner_scan_preview(t->scanner);
  if (res != 0)
    dt_control_log(_("Scan preview failed, see console for more information."));
  return 0;
}

void
dt_scanner_preview_job_init(dt_job_t *job, const struct dt_scanner_t *scanner)
{
  dt_control_job_init(job, "scan preview");
  job->execute = &dt_scanner_preview_job_run;
  dt_scanner_preview_job_t *t = (dt_scanner_preview_job_t *)job->param;
  t->scanner = scanner;
}


void
dt_scanner_scan_job_init(dt_job_t *job, const struct dt_scanner_t *scanner,
                         const char *jobcode)
{
  dt_scanner_scan_job_t *t;
  dt_control_job_init(job, "scan");
  job->execute = &dt_scanner_scan_job_run;
  t = (dt_scanner_scan_job_t *)job->param;
  t->scanner = scanner;
  t->session = dt_import_session_new();
  dt_import_session_set_name(t->session, jobcode);
}


int32_t dt_scanner_scan_job_run(dt_job_t *job)
{
  int res;
  dt_job_t j;
  dt_scanner_scan_job_t *t;
  dt_scanner_job_t sj;

  t = (dt_scanner_scan_job_t *)job->param;

  /* make sure that filename used for expansion is used, in this case
     all scans are tiff files. */
  dt_import_session_set_filename(t->session, "scan.tiff");

  /* TODO: for each region execute a scan */

  /* setup scanner job and perform a scan */
  sj.destination_filename = g_build_filename(dt_import_session_path(t->session, FALSE),
                                             dt_import_session_filename(t->session, FALSE), NULL);
  res = dt_scanner_scan(t->scanner, &sj);
  if (res != 0)
    dt_control_log(_("Scan preview failed, see console for more information."));

  /* add import job of scanned image */
  dt_image_import_job_init(&j, dt_import_session_film_id(t->session), sj.destination_filename);
  dt_control_add_job(darktable.control, &j);
  g_free(sj.destination_filename);

  /* cleanup */
  dt_import_session_destroy(t->session);
  return 0;
}
