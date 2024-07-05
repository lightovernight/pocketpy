#include "pocketpy/common/str.h"
#include "pocketpy/common/vector.h"
#include "pocketpy/pocketpy.h"

#include "pocketpy/common/utils.h"
#include "pocketpy/objects/object.h"
#include "pocketpy/interpreter/vm.h"

void py_newint(py_Ref out, int64_t val) {
    out->type = tp_int;
    out->is_ptr = false;
    out->_i64 = val;
}

void py_newfloat(py_Ref out, double val) {
    out->type = tp_float;
    out->is_ptr = false;
    out->_f64 = val;
}

void py_newbool(py_Ref out, bool val) {
    out->type = tp_bool;
    out->is_ptr = false;
    out->_bool = val;
}

void py_newnone(py_Ref out) {
    out->type = tp_none_type;
    out->is_ptr = false;
}

void py_newnotimplemented(py_Ref out) {
    out->type = tp_not_implemented_type;
    out->is_ptr = false;
}

void py_newellipsis(py_Ref out) {
    out->type = tp_ellipsis;
    out->is_ptr = false;
}

void py_newnil(py_Ref out) { out->type = 0; }


void py_newfunction(py_Ref out, py_CFunction f, const char* sig) {
    py_newfunction2(out, f, sig, BindType_FUNCTION, NULL, NULL);
}

void py_newfunction2(py_Ref out,
                     py_CFunction f,
                     const char* sig,
                     enum BindType bt,
                     const char* docstring,
                     const py_Ref upvalue) {}

void py_newnativefunc(py_Ref out, py_CFunction f) {
    out->type = tp_nativefunc;
    out->is_ptr = false;
    out->_cfunc = f;
}

void py_bindmethod(py_Type type, const char *name, py_CFunction f){
    py_bindmethod2(type, name, f, BindType_FUNCTION);
}

void py_bindmethod2(py_Type type, const char *name, py_CFunction f, enum BindType bt){
    py_TValue tmp;
    py_newnativefunc(&tmp, f);
    py_setdict(py_tpobject(type), py_name(name), &tmp);
}

void py_bindnativefunc(py_Ref obj, const char *name, py_CFunction f){
    py_TValue tmp;
    py_newnativefunc(&tmp, f);
    py_setdict(obj, py_name(name), &tmp);
}

void py_newslice(py_Ref out, const py_Ref start, const py_Ref stop, const py_Ref step) {
    py_newobject(out, tp_slice, 3, 0);
    py_setslot(out, 0, start);
    py_setslot(out, 1, stop);
    py_setslot(out, 2, step);
}

void* py_newobject(py_Ref out, py_Type type, int slots, int udsize) {
    pk_ManagedHeap* heap = &pk_current_vm->heap;
    PyObject* obj = pk_ManagedHeap__gcnew(heap, type, slots, udsize);
    out->type = type;
    out->is_ptr = true;
    out->_obj = obj;
    return PyObject__userdata(obj);
}
