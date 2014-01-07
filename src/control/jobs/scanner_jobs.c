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

int32_t
dt_scanner_preview_job_run(dt_job_t *job)
{
  dt_scanner_preview_job_t *t=(dt_scanner_preview_job_t*)job->param;
  dt_scanner_scan_preview(t->scanner);
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
