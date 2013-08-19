/*
 * Python bindings for the libmount library.
 *
 * Copyright (C) 2013, Red Hat, Inc. All rights reserved.
 * Written by Ondrej Oprala and Karel Zak
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "pylibmount.h"

static PyMemberDef Cxt_members[] = {
	{NULL}
};

static PyObject *Cxt_set_tables_errcb(CxtObject *self, PyObject *func, void *closure __attribute__((unused)))
{
	if (!func) {
		PyErr_SetString(PyExc_TypeError, NODEL_ATTR);
		return NULL;
	}
	if (!PyCallable_Check(func))
		return NULL;
	else {
		PyObject *tmp = self->table_errcb;
		Py_INCREF(func);
		self->table_errcb = func;
		Py_XDECREF(tmp);
	}
	return UL_IncRef(self);
}

static void Cxt_dealloc(CxtObject *self)
{
	if (!self->cxt) /* if init fails */
		return;

	if (!(self->cxt->flags & MNT_FL_EXTERN_FS)) {
		if (self->cxt->fs && self->cxt->fs->userdata)
			Py_DECREF(self->cxt->fs->userdata);
		else {
			mnt_free_fs(self->cxt->fs);
		}
			self->cxt->fs = NULL;
	}

	if (self->cxt->fstab && !(self->cxt->flags & MNT_FL_EXTERN_FSTAB)) {
		if (self->cxt->fstab->userdata)
			Py_DECREF(self->cxt->fstab->userdata);
		else {
			pymnt_free_table(self->cxt->fstab);
		}
			self->cxt->fstab = NULL;
	}
	if (self->cxt->mtab) {
		if (self->cxt->mtab->userdata)
			Py_DECREF(self->cxt->mtab->userdata);
		else {
			pymnt_free_table(self->cxt->mtab);
		}
			self->cxt->mtab = NULL;
	}
	mnt_free_context(self->cxt);
	self->ob_type->tp_free((PyObject*)self);
}

static PyObject *Cxt_new(PyTypeObject *type, PyObject *args __attribute__((unused)),
	       	PyObject *kwds __attribute__((unused)))
{
	CxtObject *self = (CxtObject*)type->tp_alloc(type, 0);
	if (self) {
		self->cxt = NULL;
		self->table_errcb = NULL;
	}

	return (PyObject *)self;
}
/* Note there is no pointer to encapsulating object needed here, since Cxt is on top of the Context(Table(Filesystem)) hierarchy */
#define Cxt_HELP "Cxt(source=None, target=None, fstype=None, options=None, mflags=0, fstype_pattern=None, options_pattern=None, fs=None, fstab=None, optsmode=0, syscall_status=1)"
static int Cxt_init(CxtObject *self, PyObject *args, PyObject *kwds)
{
	char *source = NULL, *target = NULL, *fstype = NULL;
	char *options = NULL, *fstype_pattern = NULL, *options_pattern = NULL;
	unsigned long mflags = 0;
	int optsmode = 0, syscall_status = 1;
	FsObject *fs = NULL;
	TabObject *fstab = NULL;
	int rc = 0;
	char *kwlist[] = {"source", "target", "fstype", "options", "mflags", "fstype_pattern",
		"options_pattern", "fs", "fstab", "optsmode", "syscall_status"};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|sssskssO!O!ii", kwlist, &source, &target, &fstype, &options, &mflags, &fstype_pattern, &options_pattern, &FsType, &fs, &TabType, &fstab, &optsmode, &syscall_status)) {
			PyErr_SetString(PyExc_TypeError, ARG_ERR);
			return -1;
			}
	if (self->cxt)
		mnt_free_context(self->cxt);

	if ((self->cxt = mnt_new_context())) {
		if (source) {
			if ((rc = mnt_context_set_source(self->cxt, source))) {
				UL_RaiseExc(-rc);
				return -1;
			}
		}
		if (target) {
			if ((rc = mnt_context_set_target(self->cxt, target))) {
				UL_RaiseExc(-rc);
				return -1;
			}
		}
		if (fstype) {
			if ((rc = mnt_context_set_fstype(self->cxt, fstype))) {
				UL_RaiseExc(-rc);
				return -1;
			}
		}
		if (options) {
			if ((rc = mnt_context_set_options(self->cxt, options))) {
				UL_RaiseExc(-rc);
				return -1;
			}
		}
		if (fstype_pattern) {
			if ((rc = mnt_context_set_fstype_pattern(self->cxt, fstype_pattern))) {
				UL_RaiseExc(-rc);
				return -1;
			}
		}
		if (options_pattern) {
			if ((rc = mnt_context_set_options_pattern(self->cxt, options_pattern))) {
				UL_RaiseExc(-rc);
				return -1;
			}
		}
		if (fs) {
			if ((rc = mnt_context_set_fs(self->cxt, fs->fs))) {
				UL_RaiseExc(-rc);
				return -1;
			}
		}
		if (fstab) {
			if ((rc = mnt_context_set_fstab(self->cxt, fstab->tab))) {
				UL_RaiseExc(-rc);
				return -1;
			}
		}
		if (optsmode) {
			if ((rc = mnt_context_set_optsmode(self->cxt, optsmode))) {
				UL_RaiseExc(-rc);
				return -1;
			}
		}
		if (syscall_status) {
			if ((rc = mnt_context_set_syscall_status(self->cxt, syscall_status))) {
				UL_RaiseExc(-rc);
				return -1;
			}
		}
		mnt_context_set_mflags(self->cxt, mflags);
		mnt_context_set_optsmode(self->cxt, optsmode);
		mnt_context_set_syscall_status(self->cxt, syscall_status);
	}
	else {
		PyErr_SetString(PyExc_MemoryError, MEMORY_ERR);
		return -1;
	}
	self->cxt->table_errcb = pymnt_table_parser_errcb;
	return 0;
}

#define Cxt_enable_fake_HELP "enable_fake(enable)\n\n\
Enable/disable fake mounting (see mount(8) man page, option -f).\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_enable_fake(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	int enable;
	char *kwlist[] = {"enable", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist, &enable)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_enable_fake(self->cxt, enable)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_enable_force_HELP "enable_force(enable)\n\n\
Enable/disable force umounting (see umount(8) man page, option -f).\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_enable_force(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	int enable;
	char *kwlist[] = {"enable", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist, &enable)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_enable_force(self->cxt, enable)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_enable_lazy_HELP "enable_lazy(enable)\n\n\
Enable/disable lazy umount (see umount(8) man page, option -l).\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_enable_lazy(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	int enable;
	char *kwlist[] = {"enable", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist, &enable)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_enable_lazy(self->cxt, enable)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_enable_loopdel_HELP "enable_loopdel(enable)\n\n\
Enable/disable loop delete (destroy) after umount (see umount(8), option -d)\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_enable_loopdel(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	int enable;
	char *kwlist[] = {"enable", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist, &enable)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_enable_loopdel(self->cxt, enable)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_enable_rdonly_umount_HELP "enable_rdonly_umount(enable)\n\n\
Enable/disable read-only remount on failed umount(2)\n\
(see umount(8) man page, option -r).\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_enable_rdonly_umount(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	int enable;
	char *kwlist[] = {"enable", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist, &enable)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_enable_rdonly_umount(self->cxt, enable)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_enable_sloppy_HELP "enable_sloppy(enable)\n\n\
Set/unset sloppy mounting (see mount(8) man page, option -s).\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_enable_sloppy(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	int enable;
	char *kwlist[] = {"enable", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist, &enable)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_enable_sloppy(self->cxt, enable)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_enable_verbose_HELP "enable_verbose(enable)\n\n\
Enable/disable verbose output (TODO: not implemented yet)\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_enable_verbose(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	int enable;
	char *kwlist[] = {"enable", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist, &enable)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_enable_verbose(self->cxt, enable)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_enable_fork_HELP "enable_fork(enable)\n\n\
Enable/disable fork(2) call in Cxt.next_mount()(not yet implemented) (see mount(8) man\n\
page, option -F).\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_enable_fork(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	int enable;
	char *kwlist[] = {"enable", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist, &enable)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_enable_fork(self->cxt, enable)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_disable_canonicalize_HELP "disable_canonicalize(disable)\n\n\
Enable/disable paths canonicalization and tags evaluation. The libmount context\n\
canonicalies paths when search in fstab and when prepare source and target paths\n\
for mount(2) syscall.\n\
\n\
This fuction has effect to the private (within context) fstab instance only\n\
(see Cxt.fstab).\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_disable_canonicalize(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	int disable;
	char *kwlist[] = {"disable", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist, &disable)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_disable_canonicalize(self->cxt, disable)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_disable_helpers_HELP "disable_helpers(disable)\n\n\
Enable/disable /sbin/[u]mount.* helpers (see mount(8) man page, option -i).\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_disable_helpers(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	int disable;
	char *kwlist[] = {"disable", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist, &disable)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_disable_helpers(self->cxt, disable)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_disable_mtab_HELP "disable_mtab(disable)\n\n\
Disable/enable mtab update (see mount(8) man page, option -n).\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_disable_mtab(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	int disable;
	char *kwlist[] = {"disable", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist, &disable)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_disable_mtab(self->cxt, disable)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_disable_swapmatch_HELP "disable_swapmatch(disable)\n\n\
Disable/enable swap between source and target for mount(8) if only one path\n\
is specified.\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_disable_swapmatch(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	int disable;
	char *kwlist[] = {"disable", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "i", kwlist, &disable)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_disable_swapmatch(self->cxt, disable)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

static int Cxt_set_source(CxtObject *self, PyObject *value, void *closure __attribute__((unused)))
{
	char *source;
	int rc = 0;
	if (!value) {
		PyErr_SetString(PyExc_TypeError, NODEL_ATTR);
		return -1;
	}
	if (!(source = pystos(value)))
		return -1;

	rc = mnt_context_set_source(self->cxt, source);
	if (rc) {
		UL_RaiseExc(-rc);
		return -1;
	}
	return 0;
}

static int Cxt_set_mountdata(CxtObject *self, PyObject *value, void *closure __attribute__((unused)))
{
	char *mountdata;
	int rc = 0;
	if (!value) {
		PyErr_SetString(PyExc_TypeError, NODEL_ATTR);
		return -1;
	}
	if (!(mountdata = pystos(value)))
		return -1;

	rc = mnt_context_set_mountdata(self->cxt, mountdata);
	if (rc) {
		UL_RaiseExc(-rc);
		return -1;
	}
	return 0;
}

static int Cxt_set_target(CxtObject *self, PyObject *value, void *closure __attribute__((unused)))
{
	char * target;
	int rc = 0;
	if (!value) {
		PyErr_SetString(PyExc_TypeError, NODEL_ATTR);
		return -1;
	}
	if (!(target = pystos(value)))
		return -1;

	rc = mnt_context_set_target(self->cxt, target);
	if (rc) {
		UL_RaiseExc(-rc);
		return -1;
	}
	return 0;
}

static int Cxt_set_fstype(CxtObject *self, PyObject *value, void *closure __attribute__((unused)))
{
	char * fstype;
	int rc = 0;
	if (!value) {
		PyErr_SetString(PyExc_TypeError, NODEL_ATTR);
		return -1;
	}
	if (!(fstype = pystos(value)))
		return -1;

	rc = mnt_context_set_fstype(self->cxt, fstype);
	if (rc) {
		UL_RaiseExc(-rc);
		return -1;
	}
	return 0;
}

static int Cxt_set_options(CxtObject *self, PyObject *value, void *closure __attribute__((unused)))
{
	char * options;
	int rc = 0;
	if (!value) {
		PyErr_SetString(PyExc_TypeError, NODEL_ATTR);
		return -1;
	}
	if (!(options = pystos(value)))
		return -1;

	rc = mnt_context_set_options(self->cxt, options);
	if (rc) {
		UL_RaiseExc(-rc);
		return -1;
	}
	return 0;
}

static int Cxt_set_fstype_pattern(CxtObject *self, PyObject *value, void *closure __attribute__((unused)))
{
	char * fstype_pattern;
	int rc = 0;
	if (!value) {
		PyErr_SetString(PyExc_TypeError, NODEL_ATTR);
		return -1;
	}
	if (!(fstype_pattern = pystos(value)))
		return -1;

	rc = mnt_context_set_fstype_pattern(self->cxt, fstype_pattern);
	if (rc) {
		UL_RaiseExc(-rc);
		return -1;
	}
	return 0;
}

static int Cxt_set_options_pattern(CxtObject *self, PyObject *value, void *closure __attribute__((unused)))
{
	char * options_pattern;
	int rc = 0;
	if (!value) {
		PyErr_SetString(PyExc_TypeError, NODEL_ATTR);
		return -1;
	}
	if (!(options_pattern = pystos(value)))
		return -1;

	rc = mnt_context_set_options_pattern(self->cxt, options_pattern);
	if (rc) {
		UL_RaiseExc(-rc);
		return -1;
	}
	return 0;
}

static int Cxt_set_fs(CxtObject *self, PyObject *value, void *closure __attribute__((unused)))
{
	FsObject *fs;
	if (!value) {
		PyErr_SetString(PyExc_TypeError, NODEL_ATTR);
		return -1;
	}
	if (!PyArg_Parse(value, "O!", &FsType, &fs)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return -1;
	}
	Py_INCREF(fs);
	if (self->cxt->fs)
		Py_XDECREF(self->cxt->fs->userdata);
	return mnt_context_set_fs(self->cxt, fs->fs);
}

static int Cxt_set_fstab(CxtObject *self, PyObject *value, void *closure __attribute__((unused)))
{
	TabObject *fstab;
	if (!value) {
		PyErr_SetString(PyExc_TypeError, NODEL_ATTR);
		return -1;
	}
	if (!PyArg_Parse(value, "O!", &TabType, &fstab)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return -1;
	}
	Py_INCREF(fstab);
	if (self->cxt->fstab)
		Py_XDECREF(self->cxt->fstab->userdata);
	return mnt_context_set_fstab(self->cxt, fstab->tab);
}

static int Cxt_set_optsmode(CxtObject *self, PyObject *value, void *closure __attribute__((unused)))
{
	int optsmode;
	if (!value) {
		PyErr_SetString(PyExc_TypeError, NODEL_ATTR);
		return -1;
	}
	else if (!PyInt_Check(value)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return -1;
	}
	optsmode = PyInt_AsLong(value);
	return mnt_context_set_optsmode(self->cxt, optsmode);
}

static int Cxt_set_syscall_status(CxtObject *self, PyObject *value, void *closure __attribute__((unused)))
{
	int syscall_status;
	if (!value) {
		PyErr_SetString(PyExc_TypeError, NODEL_ATTR);
		return -1;
	}
	else if (!PyInt_Check(value)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return -1;
	}
	syscall_status = PyInt_AsLong(value);
	return mnt_context_set_syscall_status(self->cxt, syscall_status);
}

static int Cxt_set_user_mflags(CxtObject *self, PyObject *value, void *closure __attribute__((unused)))
{
	unsigned long flags;
	if (!value) {
		PyErr_SetString(PyExc_TypeError, NODEL_ATTR);
		return -1;
	}
	else if (!PyLong_Check(value)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return -1;
	}
	flags = PyLong_AsUnsignedLong(value);
	return mnt_context_set_mflags(self->cxt, flags);

}

static int Cxt_set_mflags(CxtObject *self, PyObject *value, void *closure __attribute__((unused)))
{
	unsigned long flags;
	if (!value) {
		PyErr_SetString(PyExc_TypeError, NODEL_ATTR);
		return -1;
	}
	else if (!PyLong_Check(value)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return -1;
	}
	flags = PyLong_AsUnsignedLong(value);
	return mnt_context_set_mflags(self->cxt, flags);
}
/* returns a flags integer (behaviour differs from C API) */
static PyObject *Cxt_get_mflags(CxtObject *self)
{
	unsigned long flags;
	PyObject *result;
	mnt_context_get_mflags(self->cxt, &flags);
	result = Py_BuildValue("k", flags);

	if (!result)
		PyErr_SetString(PyExc_RuntimeError, CONSTRUCT_ERR);
	return result;

}
/* returns a flags integer (behaviour differs from C API) */
static PyObject *Cxt_get_user_mflags(CxtObject *self)
{
	unsigned long flags;
	PyObject *result;
	mnt_context_get_user_mflags(self->cxt, &flags);
	result = Py_BuildValue("k", flags);

	if (!result)
		PyErr_SetString(PyExc_RuntimeError, CONSTRUCT_ERR);
	return result;

}
#define Cxt_reset_status_HELP "reset_status()\n\n\
Resets mount(2) and mount.type statuses, so Cxt.do_mount() or\n\
Cxt.do_umount() could be again called with the same settings.\n\
\n\
BE CAREFUL -- after this soft reset the libmount will NOT parse mount\n\
options, evaluate permissions or apply stuff from fstab.\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_reset_status(CxtObject *self)
{
	int rc;
	return (rc = mnt_context_reset_status(self->cxt)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_is_fake_HELP "is_fake()\n\n\
Returns True if fake flag is enabled or False"
static PyObject *Cxt_is_fake(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_is_fake(self->cxt));
}

#define Cxt_is_force_HELP "is_force()\n\n\
Returns True if force umounting flag is enabled or False"
static PyObject *Cxt_is_force(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_is_force(self->cxt));
}

#define Cxt_is_lazy_HELP "is_lazy()\n\n\
Returns True if lazy umount is enabled or False"
static PyObject *Cxt_is_lazy(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_is_lazy(self->cxt));
}

#define Cxt_is_nomtab_HELP "is_nomtab()\n\n\
Returns True if no-mtab is enabled or False"
static PyObject *Cxt_is_nomtab(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_is_nomtab(self->cxt));
}

#define Cxt_is_rdonly_umount_HELP "is_rdonly_umount()\n\n\
Enable/disable read-only remount on failed umount(2)\n\
(see umount(8) man page, option -r).\n\
\n\
Returns self on success, raises an exception in case of error."
static PyObject *Cxt_is_rdonly_umount(CxtObject *self)
{
	int rc;
	return (rc = mnt_context_is_rdonly_umount(self->cxt)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_is_restricted_HELP "is_restricted()\n\n\
Returns False for unrestricted mount (user is root), or True for non-root mounts"
static PyObject *Cxt_is_restricted(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_is_restricted(self->cxt));
}

#define Cxt_is_sloppy_HELP "is_sloppy()\n\n\
Returns True if sloppy flag is enabled or False"
static PyObject *Cxt_is_sloppy(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_is_sloppy(self->cxt));
}

#define Cxt_is_verbose_HELP "is_verbose()\n\n\
Returns True if verbose flag is enabled or False"
static PyObject *Cxt_is_verbose(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_is_verbose(self->cxt));
}
#define Cxt_is_fs_mounted_HELP "is_fs_mounted(fs, mounted)\n\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_is_fs_mounted(CxtObject *self, PyObject *args, PyObject *kwds)
{
	char *kwlist[] = {"fs", "mounted", NULL};
	FsObject *fs;
	int mounted;
	if (PyArg_ParseTupleAndKeywords(args, kwds, "O!i", kwlist, &FsType, &fs, &mounted)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return PyBool_FromLong(mnt_context_is_fs_mounted(self->cxt, fs->fs, &mounted));
}

#define Cxt_is_child_HELP "is_child()\n\n\
Returns True if mount -F enabled and the current context is child, or False"
static PyObject *Cxt_is_child(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_is_child(self->cxt));
}

#define Cxt_is_fork_HELP "is_fork()\n\n\
Returns True if fork (mount -F) is enabled or False"
static PyObject *Cxt_is_fork(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_is_fork(self->cxt));
}

#define Cxt_is_parent_HELP "is_parent()\n\n\
Returns True if mount -F enabled and the current context is parent, or False"
static PyObject *Cxt_is_parent(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_is_parent(self->cxt));
}

#define Cxt_is_loopdel_HELP "is_loopdel()\n\n\
Returns True if loop device should be deleted after umount (umount -d) or False."
static PyObject *Cxt_is_loopdel(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_is_loopdel(self->cxt));
}

#define Cxt_is_nocanonicalize_HELP "is_nocanonicalize()\n\n\
Returns True if no-canonicalize mode enabled or False."
static PyObject *Cxt_is_nocanonicalize(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_is_nocanonicalize(self->cxt));
}

#define Cxt_is_nohelpers_HELP "is_nohelpers()\n\n\
Returns True if helpers are disabled (mount -i) or False."
static PyObject *Cxt_is_nohelpers(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_is_nohelpers(self->cxt));
}

#define Cxt_syscall_called_HELP "syscall_called()\n\n\
Returns True if mount(2) syscall has been called, or False."
static PyObject *Cxt_syscall_called(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_syscall_called(self->cxt));
}

#define Cxt_is_swapmatch_HELP "is_swapmatch()\n\n\
Returns True if swap between source and target is allowed (default is True) or False."
static PyObject *Cxt_is_swapmatch(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_is_swapmatch(self->cxt));
}

#define Cxt_tab_applied_HELP "tab_applied()\n\n\
Returns True if fstab (or mtab) has been applied to the context, False otherwise."
static PyObject *Cxt_tab_applied(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_tab_applied(self->cxt));
}

#define Cxt_apply_fstab_HELP "apply_fstab()\n\n\
This function is optional.\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_apply_fstab(CxtObject *self)
{
	int rc;
	if (!self->cxt->fs) {
		PyErr_SetString(PyExc_AssertionError, NOFS_ERR);
		return NULL;
	}
	return (rc = mnt_context_apply_fstab(self->cxt)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_helper_executed_HELP "helper_executed()\n\n\
Returns True if mount.type helper has been executed, or False."
static PyObject *Cxt_helper_executed(CxtObject *self)
{
	return PyBool_FromLong(mnt_context_helper_executed(self->cxt));
}

static PyObject *Cxt_get_source(CxtObject *self)
{
	return PyObjectResultStr(mnt_context_get_source(self->cxt));
}

static PyObject *Cxt_get_target(CxtObject *self)
{
	return PyObjectResultStr(mnt_context_get_target(self->cxt));
}

static PyObject *Cxt_get_options(CxtObject *self)
{
	return PyObjectResultStr(mnt_context_get_options(self->cxt));
}

static PyObject *Cxt_get_fstype(CxtObject *self)
{
	return PyObjectResultStr(mnt_context_get_fstype(self->cxt));
}

static PyObject *Cxt_get_fs(CxtObject *self)
{
	return PyObjectResultFs(mnt_context_get_fs(self->cxt));
}

static PyObject *Cxt_get_fstab(CxtObject *self)
{
	struct libmnt_table *tab = NULL;
	mnt_context_get_fstab(self->cxt, &tab);
	if (!tab)
		return NULL;
	return PyObjectResultTab(tab);
}

static PyObject *Cxt_get_mtab(CxtObject *self)
{
	struct libmnt_table *tab = NULL;
	mnt_context_get_mtab(self->cxt, &tab);
	return PyObjectResultTab(tab);
}
#define Cxt_get_table_HELP "get_table(filename)\n\n\
This function allocates a new table and parses the file. The parser error\n\
callback and cache for tags and paths is set according to the cxt setting.\n\
See also Tab.parse_file().\n\
\n\
It's strongly recommended to use Cxt.mtab and\n\
Cxt.fstab for mtab and fstab files. These setters\n\
do not care about LIBMOUNT_* env.variables and do not merge userspace\n\
options.\n\
\n\
The getters return a new reference to the result.\n\
\n\
Returns self or raises an exception in case of an error."
/* output differs from the C API */
static PyObject *Cxt_get_table(CxtObject *self, PyObject *args, PyObject *kwds)
{
	char *filename;
	struct libmnt_table *tab = NULL;
	char *kwlist[] = {"filename", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &filename)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	mnt_context_get_table(self->cxt, filename, &tab);
	return PyObjectResultTab(tab);
}

static PyObject *Cxt_get_optsmode(CxtObject *self)
{
	return PyObjectResultInt(mnt_context_get_optsmode(self->cxt));
}

static PyObject *Cxt_get_status(CxtObject *self)
{
	return PyObjectResultInt(mnt_context_get_status(self->cxt));
}

static PyObject *Cxt_get_syscall_errno(CxtObject *self)
{
	return PyObjectResultInt(mnt_context_get_syscall_errno(self->cxt));
}

#define Cxt_do_mount_HELP "do_mount()\n\n\
Call mount(2) or mount.type helper. Unnecessary for Cxt.mount().\n\
\n\
Note that this function could be called only once. If you want to mount\n\
another source or target than you have to call Cxt.reset_context().\n\
\n\
If you want to call mount(2) for the same source and target with a different\n\
mount flags or fstype then call Cxt.reset_status() and then try\n\
again Cxt.do_mount().\n\
\n\
WARNING: non-zero return code does not mean that mount(2) syscall or\n\
mount.type helper wasn't successfully called.\n\
\n\
Check Cxt.status after error!\n\
\n\
Returns self on success\n\
or an exception in case of other errors."
static PyObject *Cxt_do_mount(CxtObject *self)
{
	int rc;
	if (!self->cxt->fs) {
		PyErr_SetString(PyExc_AssertionError, NOFS_ERR);
		return NULL;
	}
	return (rc = mnt_context_do_mount(self->cxt)) ? UL_RaiseExc(rc < 0 ? -rc : rc) : UL_IncRef(self);
}

#define Cxt_do_umount_HELP "do_umount()\n\n\
Umount filesystem by umount(2) or fork()+exec(/sbin/umount.type).\n\
Unnecessary for Cxt.umount().\n\
\n\
See also Cxt.disable_helpers().\n\
\n\
WARNING: non-zero return code does not mean that umount(2) syscall or\n\
umount.type helper wasn't successfully called.\n\
\n\
Check Cxt.status after error!\n\
\n\
Returns self on success\n\
or an exception in case of other errors."
static PyObject *Cxt_do_umount(CxtObject *self)
{
	int rc;
	return (rc = mnt_context_do_umount(self->cxt)) ? UL_RaiseExc(rc < 0 ? -rc : rc) : UL_IncRef(self);
}

#define Cxt_mount_HELP "mount()\n\n\
High-level, mounts filesystem by mount(2) or fork()+exec(/sbin/mount.type).\n\
\n\
This is similar to:\n\
\n\
Cxt.prepare_mount();\n\
Cxt.do_mount();\n\
Cxt.finalize_mount();\n\
\n\
See also Cxt.disable_helper().\n\
\n\
Note that this function could be called only once. If you want to mount with\n\
different setting than you have to call Cxt.reset_context(). It's NOT enough\n\
to call Cxt.reset_status() if you want call this function more than\n\
once, whole context has to be reset.\n\
\n\
WARNING: non-zero return code does not mean that mount(2) syscall or\n\
mount.type helper wasn't successfully called.\n\
\n\
Check Cxt.status after error!\n\
\n\
Returns self on success\n\
or an exception in case of other errors."
static PyObject *Cxt_mount(CxtObject *self)
{
	int rc;
	if (!self->cxt->fs) {
		PyErr_SetString(PyExc_AssertionError, NOFS_ERR);
		return NULL;
	}
	return (rc = mnt_context_mount(self->cxt)) ? UL_RaiseExc(rc < 0 ? -rc : rc) : UL_IncRef(self);
}

#define Cxt_umount_HELP "umount()\n\n\
High-level, umounts filesystem by umount(2) or fork()+exec(/sbin/umount.type).\n\
\n\
This is similar to:\n\
\n\
Cxt.prepare_umount();\n\
Cxt.do_umount();\n\
Cxt.finalize_umount();\n\
\n\
See also Cxt.disable_helpers().\n\
\n\
WARNING: non-zero return code does not mean that umount(2) syscall or\n\
umount.type helper wasn't successfully called.\n\
\n\
Check Cxt.status after error!\n\
\n\
Returns self on success\n\
or an exception in case of other errors."
static PyObject *Cxt_umount(CxtObject *self)
{
	int rc;
	return (rc = mnt_context_umount(self->cxt)) ? UL_RaiseExc(rc < 0 ? -rc : rc) : UL_IncRef(self);
}

#define Cxt_finalize_mount_HELP "finalize_mount()\n\n\
Mtab update, etc. Unnecessary for Cxt.mount(), but should be called\n\
after Cxt.do_mount(). See also Cxt.syscall_status.\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_finalize_mount(CxtObject *self)
{
	int rc;
	if (!self->cxt->fs) {
		PyErr_SetString(PyExc_AssertionError, NOFS_ERR);
		return NULL;
	}
	return (rc = mnt_context_finalize_mount(self->cxt)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_prepare_umount_HELP "prepare_umount()\n\n\
Prepare context for umounting, unnecessary for Cxt.umount().\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_prepare_umount(CxtObject *self)
{
	int rc;
	return (rc = mnt_context_prepare_umount(self->cxt)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_prepare_mount_HELP "prepare_mount()\n\n\
Prepare context for mounting, unnecessary for Cxt.mount().\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_prepare_mount(CxtObject *self)
{
	int rc;
	if (!self->cxt->fs) {
		PyErr_SetString(PyExc_AssertionError, NOFS_ERR);
		return NULL;
	}
	return (rc = mnt_context_prepare_mount(self->cxt)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_finalize_umount_HELP "finalize_umount()\n\n\
Mtab update, etc. Unnecessary for Cxt.umount(), but should be called\n\
after Cxt.do_umount(). See also Cxt.syscall_status.\n\
\n\
Returns self on success, raises LibmountError if target filesystem not found, or other exception on error."
static PyObject *Cxt_finalize_umount(CxtObject *self)
{
	int rc;
	return (rc = mnt_context_finalize_umount(self->cxt)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_find_umount_fs_HELP "find_umount_fs(tgt, pfs)\n\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_find_umount_fs(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	char *kwlist[] = {"tgt", "pfs", NULL};
	char *tgt = NULL;
	FsObject *fs;
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "sO!", kwlist, &tgt, &FsType, &fs)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_find_umount_fs(self->cxt, tgt, &fs->fs)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_append_options_HELP "append_options(optstr)\n\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_append_options(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	char *kwlist[] = {"optstr", NULL};
	char *optstr = NULL;
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &optstr)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_append_options(self->cxt, optstr)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_helper_setopt_HELP "helper_setopt(c, arg)\n\n\
This function applies [u]mount.type command line option (for example parsed\n\
by getopt or getopt_long) to cxt. All unknown options are ignored and\n\
then ValueError is raised.\n\
\n\
Returns self on success, raises ValueError if c is unknown or other exception in case of an error."
static PyObject *Cxt_helper_setopt(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	int c;
	char *arg;
	char *kwlist[] = {"c", "arg", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "is", kwlist, &c, &arg)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_helper_setopt(self->cxt, c, arg)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

#define Cxt_init_helper_HELP "init_helper(action, flags)\n\n\
This function infors libmount that used from [u]mount.type helper.\n\
\n\
The function also calls Cxt.disable_helpers() to avoid recursive\n\
mount.type helpers calling. It you really want to call another\n\
mount.type helper from your helper than you have to explicitly enable this\n\
feature by:\n\
\n\
Cxt.disable_helpers(False);\n\
\n\
Returns self or raises an exception in case of an error."
static PyObject *Cxt_init_helper(CxtObject *self, PyObject *args, PyObject *kwds)
{
	int rc;
	int action, flags;
	char *kwlist[] = {"action", "flags", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii", kwlist, &action, &flags)) {
		PyErr_SetString(PyExc_TypeError, ARG_ERR);
		return NULL;
	}
	return (rc = mnt_context_init_helper(self->cxt, action, flags)) ? UL_RaiseExc(-rc) : UL_IncRef(self);
}

static PyGetSetDef Cxt_getseters[] = {
	{"tables_errcb",	NULL, (setter)Cxt_set_tables_errcb, "error callback function", NULL},
	{"status",		(getter)Cxt_get_status, NULL, "status", NULL},
	{"source",		(getter)Cxt_get_source, (setter)Cxt_set_source, "source", NULL},
	{"target",		(getter)Cxt_get_target, (setter)Cxt_set_target, "target", NULL},
	{"fstype",		(getter)Cxt_get_fstype, (setter)Cxt_set_fstype, "fstype", NULL},
	{"options",		(getter)Cxt_get_options, (setter)Cxt_set_options, "options", NULL},
	{"mflags",		(getter)Cxt_get_mflags, (setter)Cxt_set_mflags, "mflags", NULL},
	{"mountdata",		NULL, (setter)Cxt_set_mountdata, "mountdata", NULL},
	{"fstype_pattern",	NULL, (setter)Cxt_set_fstype_pattern, "fstype_pattern", NULL},
	{"options_pattern",	NULL, (setter)Cxt_set_options_pattern, "options_pattern", NULL},
	{"fs",			(getter)Cxt_get_fs, (setter)Cxt_set_fs, "filesystem description (type, mountpoint, device, ...)", NULL},
	{"mtab",		(getter)Cxt_get_mtab, NULL, "mtab entries", NULL},
	{"fstab",		(getter)Cxt_get_fstab, (setter)Cxt_set_fstab, "fstab (or mtab for some remounts)", NULL},
	{"optsmode",		(getter)Cxt_get_optsmode, (setter)Cxt_set_optsmode, "fstab optstr mode MNT_OPTSMODE_{AUTO,FORCE,IGNORE}", NULL},
	{"syscall_errno",	(getter)Cxt_get_syscall_errno, (setter)Cxt_set_syscall_status, "1: not_called yet, 0: success, <0: -errno", NULL},
	{"user_mflags",		(getter)Cxt_get_user_mflags, (setter)Cxt_set_user_mflags, "user mflags", NULL},
	{NULL}
};
static PyMethodDef Cxt_methods[] = {
	{"get_table",	(PyCFunction)Cxt_get_table, METH_VARARGS|METH_KEYWORDS, Cxt_get_table_HELP},
	{"find_umount_fs",	(PyCFunction)Cxt_find_umount_fs, METH_VARARGS|METH_KEYWORDS, Cxt_find_umount_fs_HELP},
	{"reset_status",	(PyCFunction)Cxt_reset_status, METH_NOARGS, Cxt_reset_status_HELP},
	{"helper_executed",	(PyCFunction)Cxt_helper_executed, METH_NOARGS, Cxt_helper_executed_HELP},
	{"init_helper",	(PyCFunction)Cxt_init_helper, METH_VARARGS|METH_KEYWORDS, Cxt_init_helper_HELP},
	{"helper_setopt",	(PyCFunction)Cxt_helper_setopt, METH_VARARGS|METH_KEYWORDS, Cxt_helper_setopt_HELP},
	{"append_options",	(PyCFunction)Cxt_append_options, METH_VARARGS|METH_KEYWORDS, Cxt_append_options_HELP},
	{"apply_fstab",	(PyCFunction)Cxt_apply_fstab, METH_NOARGS, Cxt_apply_fstab_HELP},
	{"disable_canonicalize",	(PyCFunction)Cxt_disable_canonicalize, METH_VARARGS|METH_KEYWORDS, Cxt_disable_canonicalize_HELP},
	{"disable_helpers",	(PyCFunction)Cxt_disable_helpers, METH_VARARGS|METH_KEYWORDS, Cxt_disable_helpers_HELP},
	{"disable_mtab",	(PyCFunction)Cxt_disable_mtab, METH_VARARGS|METH_KEYWORDS, Cxt_disable_mtab_HELP},
	{"do_mount",	(PyCFunction)Cxt_do_mount, METH_NOARGS, Cxt_do_mount_HELP},
	{"do_umount",	(PyCFunction)Cxt_do_umount, METH_NOARGS , Cxt_do_umount_HELP},
	{"enable_fake",	(PyCFunction)Cxt_enable_fake, METH_VARARGS|METH_KEYWORDS, Cxt_enable_fake_HELP},
	{"enable_force",	(PyCFunction)Cxt_enable_force, METH_VARARGS|METH_KEYWORDS, Cxt_enable_force_HELP},
	{"enable_lazy",	(PyCFunction)Cxt_enable_lazy, METH_VARARGS|METH_KEYWORDS, Cxt_enable_lazy_HELP},
	{"enable_loopdel",	(PyCFunction)Cxt_enable_loopdel, METH_VARARGS|METH_KEYWORDS, Cxt_enable_loopdel_HELP},
	{"enable_rdonly_umount",	(PyCFunction)Cxt_enable_rdonly_umount, METH_VARARGS|METH_KEYWORDS, Cxt_enable_rdonly_umount_HELP},
	{"enable_sloppy",	(PyCFunction)Cxt_enable_sloppy, METH_VARARGS|METH_KEYWORDS, Cxt_enable_sloppy_HELP},
	{"enable_verbose",	(PyCFunction)Cxt_enable_verbose, METH_VARARGS|METH_KEYWORDS, Cxt_enable_verbose_HELP},
	{"enable_fork",	(PyCFunction)Cxt_enable_fork, METH_VARARGS|METH_KEYWORDS, Cxt_enable_fork_HELP},
	{"finalize_mount",	(PyCFunction)Cxt_finalize_mount, METH_NOARGS, Cxt_finalize_mount_HELP},
	{"finalize_umount",	(PyCFunction)Cxt_finalize_umount, METH_NOARGS, Cxt_finalize_umount_HELP},
	{"is_fake",	(PyCFunction)Cxt_is_fake, METH_NOARGS, Cxt_is_fake_HELP},
	{"is_force",	(PyCFunction)Cxt_is_force, METH_NOARGS, Cxt_is_force_HELP},
	{"is_fork",	(PyCFunction)Cxt_is_fork, METH_NOARGS, Cxt_is_fork_HELP},
	{"is_fs_mounted",	(PyCFunction)Cxt_is_fs_mounted, METH_VARARGS|METH_KEYWORDS, Cxt_is_fs_mounted_HELP},
	{"is_lazy",	(PyCFunction)Cxt_is_lazy, METH_NOARGS, Cxt_is_lazy_HELP},
	{"is_nomtab",	(PyCFunction)Cxt_is_nomtab, METH_NOARGS, Cxt_is_nomtab_HELP},
	{"is_rdonly_umount",	(PyCFunction)Cxt_is_rdonly_umount, METH_NOARGS, Cxt_is_rdonly_umount_HELP},
	{"is_restricted",	(PyCFunction)Cxt_is_restricted, METH_NOARGS, Cxt_is_restricted_HELP},
	{"is_sloppy",	(PyCFunction)Cxt_is_sloppy, METH_NOARGS, Cxt_is_sloppy_HELP},
	{"is_verbose",	(PyCFunction)Cxt_is_verbose, METH_NOARGS, Cxt_is_verbose_HELP},
	{"is_child",	(PyCFunction)Cxt_is_child, METH_NOARGS, Cxt_is_child_HELP},
	{"is_parent",	(PyCFunction)Cxt_is_parent, METH_NOARGS, Cxt_is_parent_HELP},
	{"is_loopdel",	(PyCFunction)Cxt_is_loopdel, METH_NOARGS, Cxt_is_loopdel_HELP},
	{"is_nocanonicalize",	(PyCFunction)Cxt_is_nocanonicalize, METH_NOARGS, Cxt_is_nocanonicalize_HELP},
	{"is_nohelpers",	(PyCFunction)Cxt_is_nohelpers, METH_NOARGS, Cxt_is_nohelpers_HELP},
	{"is_swapmatch",	(PyCFunction)Cxt_is_swapmatch, METH_NOARGS, Cxt_is_swapmatch_HELP},
	{"mount",	(PyCFunction)Cxt_mount, METH_NOARGS, Cxt_mount_HELP},
	{"prepare_mount",	(PyCFunction)Cxt_prepare_mount, METH_NOARGS, Cxt_prepare_mount_HELP},
	{"prepare_umount",	(PyCFunction)Cxt_prepare_umount, METH_NOARGS, Cxt_prepare_umount_HELP},
	{"umount",	(PyCFunction)Cxt_umount, METH_NOARGS, Cxt_umount_HELP},
	{"syscall_called",	(PyCFunction)Cxt_syscall_called, METH_NOARGS, Cxt_syscall_called_HELP},
	{"disable_swapmatch",	(PyCFunction)Cxt_disable_swapmatch, METH_VARARGS|METH_KEYWORDS, Cxt_disable_swapmatch_HELP},
	{"tab_applied",	(PyCFunction)Cxt_tab_applied, METH_NOARGS, Cxt_tab_applied_HELP},
	{NULL}
};

static PyObject *Context_repr(CxtObject *self)
{
	return PyString_FromFormat("<libmount.Context object at %p, mtab_path=%s, utab_path=%s, restricted=%s>",
		       	self,
			self->cxt->mtab_path ? self->cxt->mtab_path : "None",
			self->cxt->utab_path ? self->cxt->utab_path : "None",
		       	self->cxt->restricted ? "True" : "False");
}

PyTypeObject CxtType = {
	PyObject_HEAD_INIT(NULL)
	0, /*ob_size*/
	"libmount.Cxt", /*tp_name*/
	sizeof(CxtObject), /*tp_basicsize*/
	0, /*tp_itemsize*/
	(destructor)Cxt_dealloc, /*tp_dealloc*/
	0, /*tp_print*/
	0, /*tp_getattr*/
	0, /*tp_setattr*/
	0, /*tp_compare*/
	(reprfunc) Context_repr,
	0, /*tp_as_number*/
	0, /*tp_as_sequence*/
	0, /*tp_as_mapping*/
	0, /*tp_hash */
	0, /*tp_call*/
	0, /*tp_str*/
	0, /*tp_getattro*/
	0, /*tp_setattro*/
	0, /*tp_as_buffer*/
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
	Cxt_HELP, /* tp_doc */
	0, /* tp_traverse */
	0, /* tp_clear */
	0, /* tp_richcompare */
	0, /* tp_weaklistoffset */
	0, /* tp_iter */
	0, /* tp_iternext */
	Cxt_methods, /* tp_methods */
	Cxt_members, /* tp_members */
	Cxt_getseters, /* tp_getset */
	0, /* tp_base */
	0, /* tp_dict */
	0, /* tp_descr_get */
	0, /* tp_descr_set */
	0, /* tp_dictoffset */
	(initproc)Cxt_init, /* tp_init */
	0, /* tp_alloc */
	Cxt_new, /* tp_new */
};

void pymnt_init_context(PyObject *mod)
{
	if (PyType_Ready(&CxtType) < 0)
		return;

	Py_INCREF(&CxtType);
	PyModule_AddObject(mod, "Cxt", (PyObject *)&CxtType);
}


