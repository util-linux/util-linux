/*
 * Python bindings for lastlog2.
 *
 * Copyright (C) 2024 Georg Pfuetzenreuter <mail+linux@georg-pfuetzenreuter.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "pylastlog2.h"

static char *lastlog2_path = LL2_DEFAULT_DATABASE;

static PyObject *
pylastlog2_query(__attribute__((unused)) PyObject *self, PyObject *args, PyObject *keywds)
{
  const char *user = NULL;
  struct ll2_context *db_context = NULL;

  static char *kwlist[] = {"user", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, keywds, "s", kwlist, &user))
    return NULL;

  int64_t time = 0;
  char *tty = NULL;
  char *rhost = NULL;
  char *service = NULL;

  db_context = ll2_new_context(lastlog2_path);
  ll2_read_entry(db_context, user, &time, &tty, &rhost, &service, NULL);

  return Py_BuildValue("{s:s,s:l,s:s,s:s,s:s}",
    "user", user,
    "time", time,
    "tty", tty,
    "rhost", rhost,
    "service", service
  );
};

static PyMethodDef pylastlog2_methods[] = {
  {"query", (PyCFunction)(void(*)(void))pylastlog2_query, METH_VARARGS | METH_KEYWORDS, "Query the lastlog2 database."},
  {NULL, NULL}
};

static struct PyModuleDef moduledef = {
  PyModuleDef_HEAD_INIT,
  "pylastlog2",
  NULL,
  -1,
  pylastlog2_methods
};

PyMODINIT_FUNC PyInit_pylastlog2(void);
PyMODINIT_FUNC
PyInit_pylastlog2(void)
{
  return PyModule_Create(&moduledef);
};
