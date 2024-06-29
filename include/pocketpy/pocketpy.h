#pragma once

#include <stdint.h>
#include <stdbool.h>

/************* Public Types *************/
typedef struct py_TValue py_TValue;
typedef struct pk_VM pk_VM;
typedef uint16_t py_Name;
typedef int16_t py_Type;
typedef py_TValue* py_Ref;
typedef struct py_Str py_Str;
typedef int (*py_CFunction)(int argc, py_Ref argv);

typedef struct py_Error{
    py_Type type;
} py_Error;

typedef enum BindType {
    BindType_FUNCTION,
    BindType_STATICMETHOD,
    BindType_CLASSMETHOD,
} BindType;

extern pk_VM* pk_current_vm;

/************* Global VMs *************/
void py_initialize();
void py_finalize();

/// Run a simple source string. Do not change the stack.
int py_exec(const char*);
/// Eval a simple expression. If succeed, the result will be pushed onto the stack.
int py_eval(const char*);

/************* Values Creation *************/
void py_newint(py_Ref, int64_t);
void py_newfloat(py_Ref, double);
void py_newbool(py_Ref, bool);
void py_newstr(py_Ref, const char*);
void py_newstrn(py_Ref, const char*, int);
// void py_newfstr(py_Ref, const char*, ...);
// void py_newbytes(py_Ref, const uint8_t*, int);
void py_newnone(py_Ref);
void py_newnull(py_Ref);

void py_newtuple(py_Ref, int);
// void py_newlist(py_Ref);

// new style decl-based function
void py_newfunction(py_Ref out, py_CFunction, const char* sig);
void py_newfunction2(py_Ref out, py_CFunction, const char* sig, BindType bt, const char* docstring, const py_Ref upvalue);
// old style argc-based function
void py_newnativefunc(py_Ref out, py_CFunction, int argc);
void py_newnativefunc2(py_Ref out, py_CFunction, int argc, BindType bt, const char* docstring, const py_Ref upvalue);

/// Create a new object.
/// @param out output reference.
/// @param type type of the object.
/// @param slots number of slots. Use -1 to create a `__dict__`.
/// @param udsize size of your userdata. You can use `py_touserdata()` to get the pointer to it.
void py_newobject(py_Ref out, py_Type type, int slots, int udsize);
/************* Stack Values Creation *************/
void py_pushint(int64_t);
void py_pushfloat(double);
void py_pushbool(bool);
void py_pushstr(const char*);
void py_pushstrn(const char*, int);

void py_pushnone();
void py_pushnull();
void py_push_notimplemented();

/************* Type Cast *************/
int64_t py_toint(const py_Ref);
double py_tofloat(const py_Ref);
bool py_castfloat(const py_Ref, double* out);
bool py_tobool(const py_Ref);
const char* py_tostr(const py_Ref);
const char* py_tostrn(const py_Ref, int* out);

void* py_touserdata(const py_Ref);

#define py_isint(self) py_istype(self, tp_int)
#define py_isfloat(self) py_istype(self, tp_float)
#define py_isbool(self) py_istype(self, tp_bool)
#define py_isstr(self) py_istype(self, tp_str)

bool py_istype(const py_Ref, py_Type);
// bool py_isinstance(const py_Ref obj, py_Type type);
// bool py_issubclass(py_Type derived, py_Type base);

/************* References *************/
py_Ref py_getdict(const py_Ref self, py_Name name);
void py_setdict(py_Ref self, py_Name name, const py_Ref val);

py_Ref py_getslot(const py_Ref self, int i);
void py_setslot(py_Ref self, int i, const py_Ref val);

py_Ref py_getupvalue(py_Ref self);
void py_setupvalue(py_Ref self, const py_Ref val);

/// Gets the attribute of the object.
int py_getattr(const py_Ref self, py_Name name, py_Ref out);
/// Sets the attribute of the object.
int py_setattr(py_Ref self, py_Name name, const py_Ref val);

/// Equivalent to `*dst = *src`.
void py_assign(py_Ref dst, const py_Ref src);

/************* Stack Operations *************/
py_Ref py_gettop();
void py_settop(const py_Ref);
py_Ref py_getsecond();
void py_setsecond(const py_Ref);
void py_duptop();
void py_dupsecond();
/// Returns a reference to the i-th object from the top of the stack.
/// i should be negative, e.g. (-1) means TOS.
py_Ref py_peek(int i);
/// Pops an object from the stack.
void py_pop();
void py_shrink(int n);

/// Pushes the object to the stack.
void py_push(const py_Ref src);

/// Get a temporary variable from the stack and returns the reference to it.
py_Ref py_pushtmp();
/// Free n temporary variable.
void py_poptmp(int n);

/************* Modules *************/
py_Ref py_newmodule(const char* name, const char* package);
py_Ref py_getmodule(const char* name);

/// Import a module.
int py_import(const char* name, py_Ref out);

/************* Errors *************/
py_Error* py_getlasterror();
void py_Error__print(py_Error*);

/************* Operators *************/
int py_eq(const py_Ref, const py_Ref);
int py_le(const py_Ref, const py_Ref);
int py_hash(const py_Ref, int64_t* out);

#define py_isnull(self) ((self)->type == 0)

/* tuple */

// unchecked functions, if self is not a tuple, the behavior is undefined
py_Ref py_tuple__getitem(const py_Ref self, int i);
void py_tuple__setitem(py_Ref self, int i, const py_Ref val);
int py_tuple__len(const py_Ref self);
bool py_tuple__contains(const py_Ref self, const py_Ref val);

// unchecked functions, if self is not a list, the behavior is undefined
py_Ref py_list__getitem(const py_Ref self, int i);
void py_list__setitem(py_Ref self, int i, const py_Ref val);
void py_list__delitem(py_Ref self, int i);
int py_list__len(const py_Ref self);
bool py_list__contains(const py_Ref self, const py_Ref val);
void py_list__append(py_Ref self, const py_Ref val);
void py_list__extend(py_Ref self, const py_Ref begin, const py_Ref end);
void py_list__clear(py_Ref self);
void py_list__insert(py_Ref self, int i, const py_Ref val);

// unchecked functions, if self is not a dict, the behavior is undefined
int py_dict__len(const py_Ref self);
bool py_dict__contains(const py_Ref self, const py_Ref key);
py_Ref py_dict__getitem(const py_Ref self, const py_Ref key);
void py_dict__setitem(py_Ref self, const py_Ref key, const py_Ref val);
void py_dict__delitem(py_Ref self, const py_Ref key);
void py_dict__clear(py_Ref self);

int py_str(const py_Ref, py_Str* out);
int py_repr(const py_Ref, py_Str* out);

#ifdef __cplusplus
}
#endif
