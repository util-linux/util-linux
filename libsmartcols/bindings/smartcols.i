%module(docstring="Python API for the util-linux libsmartcols library") smartcols
%{
#include <libsmartcols.h>
%}

%include "exception.i"

%inline %{
    /* Handling default return codes */
    #define HANDLE_RC(rc) if (rc != 0) throw std::runtime_error(strerror(rc))
%}

%exception {
    try {
        $action
    } catch(std::runtime_error &err) {
        SWIG_exception(SWIG_RuntimeError, err.what());
    }
}

%newobject *::__str__;
%typemap(newfree) char * "free($1);";

%nodefaultctor;
%nodefaultdtor;

%include "symbols.i"
%include "column.i"
%include "line.i"
%include "table.i"
