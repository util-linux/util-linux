#ifndef UTIL_LINUX_PYLIBMOUNT_H
#define UTIL_LINUX_PYLIBMOUNT_H

#include <Python.h>
#include <structmember.h>

#include "libmount.h"
#include "mountP.h"

#define NODEL_ATTR	"This attribute cannot be deleted"
#define CONSTRUCT_ERR	"Error during object construction"
#define ARG_ERR		"Invalid number or type of arguments"
#define NOFS_ERR	"No filesystems to mount"
#define MEMORY_ERR	strerror(ENOMEM)
#define CONV_ERR	"Type conversion failed"

/*
 * fs.c
 */
typedef struct {
	PyObject_HEAD
	struct libmnt_fs *fs;
} FsObject;

extern PyTypeObject FsType;

extern PyObject *PyObjectResultFs(struct libmnt_fs *fs);

extern void pymnt_init_fs(PyObject *mod);

/*
 * tab.c
 */
typedef struct {
	PyObject_HEAD

	struct libmnt_table		*tab;
	struct libmnt_iter		*iter;
	PyObject			*errcb;
} TableObject;

extern PyTypeObject TableType;

extern PyObject *PyObjectResultTab(struct libmnt_table *tab);

extern void pymnt_init_table(PyObject *mod);
extern void pymnt_free_table(struct libmnt_table *tab);
extern int pymnt_table_parser_errcb(struct libmnt_table *tb, const char *filename, int line);

/*
 * context.c
 */
typedef struct {
	PyObject_HEAD

	struct libmnt_context		*cxt;
	PyObject			*table_errcb;

} ContextObjext;

extern PyTypeObject ContextType;
extern void pymnt_init_context(PyObject *mod);

/*
 * misc
 */
extern PyObject *LibmountError;
extern PyObject *UL_IncRef(void *killme);
extern void *UL_RaiseExc(int e);

extern PyObject *PyObjectResultInt(int i);
extern PyObject *PyObjectResultStr(const char *s);

extern char *pystos(PyObject *pys);



#endif /* UTIL_LINUX_PYLIBMOUNT */
