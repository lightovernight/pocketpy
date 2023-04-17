#pragma once

#include "codeobject.h"
#include "common.h"
#include "frame.h"
#include "error.h"
#include "gc.h"
#include "memory.h"
#include "obj.h"
#include "str.h"
#include "tuplelist.h"
#include <tuple>

namespace pkpy{

/* Stack manipulation macros */
// https://github.com/python/cpython/blob/3.9/Python/ceval.c#L1123
#define TOP()             (s_data.top())
#define SECOND()          (s_data.second())
#define THIRD()           (s_data.third())
#define PEEK(n)           (s_data.peek(n))
#define STACK_SHRINK(n)   (s_data.shrink(n))
#define PUSH(v)           (s_data.push(v))
#define POP()             (s_data.pop())
#define POPX()            (s_data.popx())
#define STACK_VIEW(n)     (s_data.view(n))

Str _read_file_cwd(const Str& name, bool* ok);

#define DEF_NATIVE_2(ctype, ptype)                                      \
    template<> inline ctype py_cast<ctype>(VM* vm, PyObject* obj) {     \
        vm->check_type(obj, vm->ptype);                                 \
        return OBJ_GET(ctype, obj);                                     \
    }                                                                   \
    template<> inline ctype _py_cast<ctype>(VM* vm, PyObject* obj) {    \
        return OBJ_GET(ctype, obj);                                     \
    }                                                                   \
    template<> inline ctype& py_cast<ctype&>(VM* vm, PyObject* obj) {   \
        vm->check_type(obj, vm->ptype);                                 \
        return OBJ_GET(ctype, obj);                                     \
    }                                                                   \
    template<> inline ctype& _py_cast<ctype&>(VM* vm, PyObject* obj) {  \
        return OBJ_GET(ctype, obj);                                     \
    }                                                                   \
    inline PyObject* py_var(VM* vm, const ctype& value) { return vm->heap.gcnew(vm->ptype, value);}     \
    inline PyObject* py_var(VM* vm, ctype&& value) { return vm->heap.gcnew(vm->ptype, std::move(value));}


class Generator final: public BaseIter {
    Frame frame;
    int state;      // 0,1,2
    List s_data;    // backup
public:
    Generator(VM* vm, Frame&& frame): BaseIter(vm), frame(std::move(frame)), state(0) {}

    PyObject* next() override;
    void _gc_mark() const override;
};

struct PyTypeInfo{
    PyObject* obj;
    Type base;
    Str name;
};

struct FrameId{
    std::vector<pkpy::Frame>* data;
    int index;
    FrameId(std::vector<pkpy::Frame>* data, int index) : data(data), index(index) {}
    Frame* operator->() const { return &data->operator[](index); }
};

class VM {
    VM* vm;     // self reference for simplify code
public:
    ManagedHeap heap;
    ValueStack s_data;
    stack< Frame > callstack;
    std::vector<PyTypeInfo> _all_types;

    NameDict _modules;                                  // loaded modules
    std::map<StrName, Str> _lazy_modules;               // lazy loaded modules

    PyObject* _py_null;
    PyObject* _py_begin_call;
    PyObject* _py_op_call;
    PyObject* _py_op_yield;
    PyObject* None;
    PyObject* True;
    PyObject* False;
    PyObject* Ellipsis;
    PyObject* builtins;         // builtins module
    PyObject* _main;            // __main__ module

    std::stringstream _stdout_buffer;
    std::stringstream _stderr_buffer;
    std::ostream* _stdout;
    std::ostream* _stderr;

    // for quick access
    Type tp_object, tp_type, tp_int, tp_float, tp_bool, tp_str;
    Type tp_list, tp_tuple;
    Type tp_function, tp_native_function, tp_iterator, tp_bound_method;
    Type tp_slice, tp_range, tp_module;
    Type tp_super, tp_exception;

    VM(bool use_stdio) : heap(this){
        this->vm = this;
        this->_stdout = use_stdio ? &std::cout : &_stdout_buffer;
        this->_stderr = use_stdio ? &std::cerr : &_stderr_buffer;
        callstack.data().reserve(8);
        init_builtin_types();
    }

    bool is_stdio_used() const { return _stdout == &std::cout; }

    FrameId top_frame() {
#if DEBUG_EXTRA_CHECK
        if(callstack.empty()) FATAL_ERROR();
#endif
        return FrameId(&callstack.data(), callstack.size()-1);
    }

    PyObject* asStr(PyObject* obj){
        PyObject* self;
        PyObject* f = get_unbound_method(obj, __str__, &self, false);
        if(self != _py_null) return call_method(self, f);
        return asRepr(obj);
    }

    PyObject* asIter(PyObject* obj){
        if(is_type(obj, tp_iterator)) return obj;
        PyObject* self;
        PyObject* iter_f = get_unbound_method(obj, __iter__, &self, false);
        if(self != _py_null) return call_method(self, iter_f);
        TypeError(OBJ_NAME(_t(obj)).escape() + " object is not iterable");
        return nullptr;
    }

    PyObject* asList(PyObject* it){
        if(is_non_tagged_type(it, tp_list)) return it;
        return call_(_t(tp_list), it);
    }

    PyObject* find_name_in_mro(PyObject* cls, StrName name){
        PyObject* val;
        do{
            val = cls->attr().try_get(name);
            if(val != nullptr) return val;
            Type cls_t = OBJ_GET(Type, cls);
            Type base = _all_types[cls_t].base;
            if(base.index == -1) break;
            cls = _all_types[base].obj;
        }while(true);
        return nullptr;
    }

    bool isinstance(PyObject* obj, Type cls_t){
        Type obj_t = OBJ_GET(Type, _t(obj));
        do{
            if(obj_t == cls_t) return true;
            Type base = _all_types[obj_t].base;
            if(base.index == -1) break;
            obj_t = base;
        }while(true);
        return false;
    }

    PyObject* exec(Str source, Str filename, CompileMode mode, PyObject* _module=nullptr){
        if(_module == nullptr) _module = _main;
        try {
            CodeObject_ code = compile(source, filename, mode);
#if DEBUG_DIS_EXEC
            if(_module == _main) std::cout << disassemble(code) << '\n';
#endif
            return _exec(code, _module);
        }catch (const Exception& e){
            *_stderr << e.summary() << '\n';

        }
#if !DEBUG_FULL_EXCEPTION
        catch (const std::exception& e) {
            *_stderr << "An std::exception occurred! It could be a bug.\n";
            *_stderr << e.what() << '\n';
        }
#endif
        callstack.clear();
        s_data.clear();
        return nullptr;
    }

    template<typename ...Args>
    PyObject* _exec(Args&&... args){
        callstack.emplace(&s_data, s_data._sp, std::forward<Args>(args)...);
        return _run_top_frame();
    }

    void _pop_frame(){
        Frame* frame = &callstack.top();
        s_data.reset(frame->_sp_base);
        callstack.pop();
    }

    void _push_varargs(int n, ...){
        va_list args;
        va_start(args, n);
        for(int i=0; i<n; i++){
            PyObject* obj = va_arg(args, PyObject*);
            PUSH(obj);
        }
        va_end(args);
    }

    template<typename... Args>
    PyObject* call_(PyObject* callable, Args&&... args){
        PUSH(callable);
        PUSH(_py_null);
        int ARGC = sizeof...(args);
        _push_varargs(ARGC, args...);
        return _vectorcall(ARGC);
    }

    template<typename... Args>
    PyObject* call_method(PyObject* self, PyObject* callable, Args&&... args){
        PUSH(callable);
        PUSH(self);
        int ARGC = sizeof...(args);
        _push_varargs(ARGC, args...);
        return _vectorcall(ARGC);
    }

    template<typename... Args>
    PyObject* call_method(PyObject* self, StrName name, Args&&... args){
        PyObject* callable = get_unbound_method(self, name, &self);
        return call_method(self, callable, args...);
    }

    PyObject* property(NativeFuncRaw fget){
        PyObject* p = builtins->attr("property");
        PyObject* method = heap.gcnew(tp_native_function, NativeFunc(fget, 1, false));
        return call_(p, method);
    }

    PyObject* new_type_object(PyObject* mod, StrName name, Type base){
        PyObject* obj = heap._new<Type>(tp_type, _all_types.size());
        PyTypeInfo info{
            obj,
            base,
            (mod!=nullptr && mod!=builtins) ? Str(OBJ_NAME(mod)+"."+name.sv()): name.sv()
        };
        if(mod != nullptr) mod->attr().set(name, obj);
        _all_types.push_back(info);
        return obj;
    }

    Type _new_type_object(StrName name, Type base=0) {
        PyObject* obj = new_type_object(nullptr, name, base);
        return OBJ_GET(Type, obj);
    }

    PyObject* _find_type(const Str& type){
        PyObject* obj = builtins->attr().try_get(type);
        if(obj == nullptr){
            for(auto& t: _all_types) if(t.name == type) return t.obj;
            throw std::runtime_error(fmt("type not found: ", type));
        }
        return obj;
    }

    template<int ARGC>
    void bind_func(Str type, Str name, NativeFuncRaw fn) {
        bind_func<ARGC>(_find_type(type), name, fn);
    }

    template<int ARGC>
    void bind_method(Str type, Str name, NativeFuncRaw fn) {
        bind_method<ARGC>(_find_type(type), name, fn);
    }

    template<int ARGC, typename... Args>
    void bind_static_method(Args&&... args) {
        bind_func<ARGC>(std::forward<Args>(args)...);
    }

    template<int ARGC>
    void _bind_methods(std::vector<Str> types, Str name, NativeFuncRaw fn) {
        for(auto& type: types) bind_method<ARGC>(type, name, fn);
    }

    template<int ARGC>
    void bind_builtin_func(Str name, NativeFuncRaw fn) {
        bind_func<ARGC>(builtins, name, fn);
    }

    int normalized_index(int index, int size){
        if(index < 0) index += size;
        if(index < 0 || index >= size){
            IndexError(std::to_string(index) + " not in [0, " + std::to_string(size) + ")");
        }
        return index;
    }

    template<typename P>
    PyObject* PyIter(P&& value) {
        static_assert(std::is_base_of_v<BaseIter, std::decay_t<P>>);
        return heap.gcnew<P>(tp_iterator, std::forward<P>(value));
    }

    BaseIter* PyIter_AS_C(PyObject* obj)
    {
        check_type(obj, tp_iterator);
        return static_cast<BaseIter*>(obj->value());
    }
    
    /***** Error Reporter *****/
    void _error(StrName name, const Str& msg){
        _error(Exception(name, msg));
    }

    void _raise(){
        bool ok = top_frame()->jump_to_exception_handler();
        if(ok) throw HandledException();
        else throw UnhandledException();
    }

    void RecursionError() { _error("RecursionError", "maximum recursion depth exceeded"); }
    void StackOverflowError() { _error("StackOverflowError", ""); }
    void IOError(const Str& msg) { _error("IOError", msg); }
    void NotImplementedError(){ _error("NotImplementedError", ""); }
    void TypeError(const Str& msg){ _error("TypeError", msg); }
    void ZeroDivisionError(){ _error("ZeroDivisionError", "division by zero"); }
    void IndexError(const Str& msg){ _error("IndexError", msg); }
    void ValueError(const Str& msg){ _error("ValueError", msg); }
    void NameError(StrName name){ _error("NameError", fmt("name ", name.escape() + " is not defined")); }

    void AttributeError(PyObject* obj, StrName name){
        // OBJ_NAME calls getattr, which may lead to a infinite recursion
        _error("AttributeError", fmt("type ", OBJ_NAME(_t(obj)).escape(), " has no attribute ", name.escape()));
    }

    void AttributeError(Str msg){ _error("AttributeError", msg); }

    void check_type(PyObject* obj, Type type){
        if(is_type(obj, type)) return;
        TypeError("expected " + OBJ_NAME(_t(type)).escape() + ", but got " + OBJ_NAME(_t(obj)).escape());
    }

    PyObject* _t(Type t){
        return _all_types[t.index].obj;
    }

    PyObject* _t(PyObject* obj){
        if(is_int(obj)) return _t(tp_int);
        if(is_float(obj)) return _t(tp_float);
        return _all_types[OBJ_GET(Type, _t(obj->type)).index].obj;
    }

    ~VM() {
        callstack.clear();
        _all_types.clear();
        _modules.clear();
        _lazy_modules.clear();
    }

    PyObject* _vectorcall(int ARGC, int KWARGC=0, bool op_call=false);

    CodeObject_ compile(Str source, Str filename, CompileMode mode, bool unknown_global_scope=false);
    PyObject* num_negated(PyObject* obj);
    f64 num_to_float(PyObject* obj);
    bool asBool(PyObject* obj);
    i64 hash(PyObject* obj);
    PyObject* asRepr(PyObject* obj);
    PyObject* new_module(StrName name);
    Str disassemble(CodeObject_ co);
    void init_builtin_types();
    PyObject* _py_call(PyObject** sp_base, PyObject* callable, ArgsView args, ArgsView kwargs);
    PyObject* getattr(PyObject* obj, StrName name, bool throw_err=true);
    PyObject* get_unbound_method(PyObject* obj, StrName name, PyObject** self, bool throw_err=true, bool fallback=false);
    void setattr(PyObject* obj, StrName name, PyObject* value);
    template<int ARGC>
    void bind_method(PyObject*, Str, NativeFuncRaw);
    template<int ARGC>
    void bind_func(PyObject*, Str, NativeFuncRaw);
    void _error(Exception);
    PyObject* _run_top_frame();
    void post_init();
};

inline PyObject* NativeFunc::operator()(VM* vm, ArgsView args) const{
    int args_size = args.size() - (int)method;  // remove self
    if(argc != -1 && args_size != argc) {
        vm->TypeError(fmt("expected ", argc, " arguments, but got ", args_size));
    }
    return f(vm, args);
}

inline void CodeObject::optimize(VM* vm){
    // uint32_t base_n = (uint32_t)(names.size() / kLocalsLoadFactor + 0.5);
    // perfect_locals_capacity = std::max(find_next_capacity(base_n), NameDict::__Capacity);
    // perfect_hash_seed = find_perfect_hash_seed(perfect_locals_capacity, names);
}

DEF_NATIVE_2(Str, tp_str)
DEF_NATIVE_2(List, tp_list)
DEF_NATIVE_2(Tuple, tp_tuple)
DEF_NATIVE_2(Function, tp_function)
DEF_NATIVE_2(NativeFunc, tp_native_function)
DEF_NATIVE_2(BoundMethod, tp_bound_method)
DEF_NATIVE_2(Range, tp_range)
DEF_NATIVE_2(Slice, tp_slice)
DEF_NATIVE_2(Exception, tp_exception)

#define PY_CAST_INT(T)                                  \
template<> inline T py_cast<T>(VM* vm, PyObject* obj){  \
    vm->check_type(obj, vm->tp_int);                    \
    return (T)(BITS(obj) >> 2);                         \
}                                                       \
template<> inline T _py_cast<T>(VM* vm, PyObject* obj){ \
    return (T)(BITS(obj) >> 2);                         \
}

PY_CAST_INT(char)
PY_CAST_INT(short)
PY_CAST_INT(int)
PY_CAST_INT(long)
PY_CAST_INT(long long)
PY_CAST_INT(unsigned char)
PY_CAST_INT(unsigned short)
PY_CAST_INT(unsigned int)
PY_CAST_INT(unsigned long)
PY_CAST_INT(unsigned long long)


template<> inline float py_cast<float>(VM* vm, PyObject* obj){
    vm->check_type(obj, vm->tp_float);
    i64 bits = BITS(obj);
    bits = (bits >> 2) << 2;
    return BitsCvt(bits)._float;
}
template<> inline float _py_cast<float>(VM* vm, PyObject* obj){
    i64 bits = BITS(obj);
    bits = (bits >> 2) << 2;
    return BitsCvt(bits)._float;
}
template<> inline double py_cast<double>(VM* vm, PyObject* obj){
    vm->check_type(obj, vm->tp_float);
    i64 bits = BITS(obj);
    bits = (bits >> 2) << 2;
    return BitsCvt(bits)._float;
}
template<> inline double _py_cast<double>(VM* vm, PyObject* obj){
    i64 bits = BITS(obj);
    bits = (bits >> 2) << 2;
    return BitsCvt(bits)._float;
}


#define PY_VAR_INT(T)                                       \
    inline PyObject* py_var(VM* vm, T _val){                \
        i64 val = static_cast<i64>(_val);                   \
        if(((val << 2) >> 2) != val){                       \
            vm->_error("OverflowError", std::to_string(val) + " is out of range");  \
        }                                                                           \
        val = (val << 2) | 0b01;                                                    \
        return reinterpret_cast<PyObject*>(val);                                    \
    }

PY_VAR_INT(char)
PY_VAR_INT(short)
PY_VAR_INT(int)
PY_VAR_INT(long)
PY_VAR_INT(long long)
PY_VAR_INT(unsigned char)
PY_VAR_INT(unsigned short)
PY_VAR_INT(unsigned int)
PY_VAR_INT(unsigned long)
PY_VAR_INT(unsigned long long)

#define PY_VAR_FLOAT(T)                             \
    inline PyObject* py_var(VM* vm, T _val){        \
        f64 val = static_cast<f64>(_val);           \
        i64 bits = BitsCvt(val)._int;               \
        bits = (bits >> 2) << 2;                    \
        bits |= 0b10;                               \
        return reinterpret_cast<PyObject*>(bits);   \
    }

PY_VAR_FLOAT(float)
PY_VAR_FLOAT(double)

inline PyObject* py_var(VM* vm, bool val){
    return val ? vm->True : vm->False;
}

template<> inline bool py_cast<bool>(VM* vm, PyObject* obj){
    vm->check_type(obj, vm->tp_bool);
    return obj == vm->True;
}
template<> inline bool _py_cast<bool>(VM* vm, PyObject* obj){
    return obj == vm->True;
}

inline PyObject* py_var(VM* vm, const char val[]){
    return VAR(Str(val));
}

inline PyObject* py_var(VM* vm, std::string val){
    return VAR(Str(std::move(val)));
}

inline PyObject* py_var(VM* vm, std::string_view val){
    return VAR(Str(val));
}

template<typename T>
void _check_py_class(VM* vm, PyObject* obj){
    vm->check_type(obj, T::_type(vm));
}

inline PyObject* VM::num_negated(PyObject* obj){
    if (is_int(obj)){
        return VAR(-CAST(i64, obj));
    }else if(is_float(obj)){
        return VAR(-CAST(f64, obj));
    }
    TypeError("expected 'int' or 'float', got " + OBJ_NAME(_t(obj)).escape());
    return nullptr;
}

inline f64 VM::num_to_float(PyObject* obj){
    if(is_float(obj)){
        return CAST(f64, obj);
    } else if (is_int(obj)){
        return (f64)CAST(i64, obj);
    }
    TypeError("expected 'int' or 'float', got " + OBJ_NAME(_t(obj)).escape());
    return 0;
}

inline bool VM::asBool(PyObject* obj){
    if(is_non_tagged_type(obj, tp_bool)) return obj == True;
    if(obj == None) return false;
    if(is_int(obj)) return CAST(i64, obj) != 0;
    if(is_float(obj)) return CAST(f64, obj) != 0.0;
    PyObject* self;
    PyObject* len_f = get_unbound_method(obj, __len__, &self, false);
    if(self != _py_null){
        PUSH(len_f);
        PUSH(self);
        PyObject* ret = _vectorcall(0);
        return CAST(i64, ret) > 0;
    }
    return true;
}

inline i64 VM::hash(PyObject* obj){
    if (is_non_tagged_type(obj, tp_str)) return CAST(Str&, obj).hash();
    if (is_int(obj)) return CAST(i64, obj);
    if (is_non_tagged_type(obj, tp_tuple)) {
        i64 x = 1000003;
        const Tuple& items = CAST(Tuple&, obj);
        for (int i=0; i<items.size(); i++) {
            i64 y = hash(items[i]);
            // recommended by Github Copilot
            x = x ^ (y + 0x9e3779b9 + (x << 6) + (x >> 2));
        }
        return x;
    }
    if (is_non_tagged_type(obj, tp_type)) return BITS(obj);
    if (is_non_tagged_type(obj, tp_bool)) return _CAST(bool, obj) ? 1 : 0;
    if (is_float(obj)){
        f64 val = CAST(f64, obj);
        return (i64)std::hash<f64>()(val);
    }
    TypeError("unhashable type: " +  OBJ_NAME(_t(obj)).escape());
    return 0;
}

inline PyObject* VM::asRepr(PyObject* obj){
    return call_method(obj, __repr__);
}

inline PyObject* VM::new_module(StrName name) {
    PyObject* obj = heap._new<DummyModule>(tp_module, DummyModule());
    obj->attr().set(__name__, VAR(name.sv()));
    // we do not allow override in order to avoid memory leak
    // it is because Module objects are not garbage collected
    if(_modules.contains(name)) FATAL_ERROR();
    _modules.set(name, obj);
    return obj;
}

inline Str VM::disassemble(CodeObject_ co){
    auto pad = [](const Str& s, const int n){
        if(s.length() >= n) return s.substr(0, n);
        return s + std::string(n - s.length(), ' ');
    };

    std::vector<int> jumpTargets;
    for(auto byte : co->codes){
        if(byte.op == OP_JUMP_ABSOLUTE || byte.op == OP_POP_JUMP_IF_FALSE){
            jumpTargets.push_back(byte.arg);
        }
    }
    std::stringstream ss;
    int prev_line = -1;
    for(int i=0; i<co->codes.size(); i++){
        const Bytecode& byte = co->codes[i];
        Str line = std::to_string(co->lines[i]);
        if(co->lines[i] == prev_line) line = "";
        else{
            if(prev_line != -1) ss << "\n";
            prev_line = co->lines[i];
        }

        std::string pointer;
        if(std::find(jumpTargets.begin(), jumpTargets.end(), i) != jumpTargets.end()){
            pointer = "-> ";
        }else{
            pointer = "   ";
        }
        ss << pad(line, 8) << pointer << pad(std::to_string(i), 3);
        ss << " " << pad(OP_NAMES[byte.op], 20) << " ";
        // ss << pad(byte.arg == -1 ? "" : std::to_string(byte.arg), 5);
        std::string argStr = byte.arg == -1 ? "" : std::to_string(byte.arg);
        switch(byte.op){
            case OP_LOAD_CONST:
                argStr += fmt(" (", CAST(Str, asRepr(co->consts[byte.arg])), ")");
                break;
            case OP_LOAD_NAME: case OP_LOAD_GLOBAL: case OP_LOAD_NONLOCAL: case OP_STORE_GLOBAL:
            case OP_LOAD_ATTR: case OP_LOAD_METHOD: case OP_STORE_ATTR: case OP_DELETE_ATTR:
            case OP_IMPORT_NAME: case OP_BEGIN_CLASS:
            case OP_DELETE_GLOBAL:
                argStr += fmt(" (", StrName(byte.arg).sv(), ")");
                break;
            case OP_LOAD_FAST: case OP_STORE_FAST: case OP_DELETE_FAST:
                argStr += fmt(" (", co->varnames[byte.arg].sv(), ")");
                break;
            case OP_BINARY_OP:
                argStr += fmt(" (", BINARY_SPECIAL_METHODS[byte.arg], ")");
                break;
            case OP_LOAD_FUNCTION:
                argStr += fmt(" (", co->func_decls[byte.arg]->code->name, ")");
                break;
        }
        ss << pad(argStr, 40);      // may overflow
        ss << co->blocks[byte.block].type;
        if(i != co->codes.size() - 1) ss << '\n';
    }

    for(auto& decl: co->func_decls){
        ss << "\n\n" << "Disassembly of " << decl->code->name << ":\n";
        ss << disassemble(decl->code);
    }
    ss << "\n";
    return Str(ss.str());
}

inline void VM::init_builtin_types(){
    _all_types.push_back({heap._new<Type>(Type(1), Type(0)), -1, "object"});
    _all_types.push_back({heap._new<Type>(Type(1), Type(1)), 0, "type"});
    tp_object = 0; tp_type = 1;

    tp_int = _new_type_object("int");
    tp_float = _new_type_object("float");
    if(tp_int.index != kTpIntIndex || tp_float.index != kTpFloatIndex) FATAL_ERROR();

    tp_bool = _new_type_object("bool");
    tp_str = _new_type_object("str");
    tp_list = _new_type_object("list");
    tp_tuple = _new_type_object("tuple");
    tp_slice = _new_type_object("slice");
    tp_range = _new_type_object("range");
    tp_module = _new_type_object("module");
    tp_function = _new_type_object("function");
    tp_native_function = _new_type_object("native_function");
    tp_iterator = _new_type_object("iterator");
    tp_bound_method = _new_type_object("bound_method");
    tp_super = _new_type_object("super");
    tp_exception = _new_type_object("Exception");

    this->None = heap._new<Dummy>(_new_type_object("NoneType"), {});
    this->Ellipsis = heap._new<Dummy>(_new_type_object("ellipsis"), {});
    this->True = heap._new<Dummy>(tp_bool, {});
    this->False = heap._new<Dummy>(tp_bool, {});

    Type _internal_type = _new_type_object("_internal");
    this->_py_null = heap._new<Dummy>(_internal_type, {});
    this->_py_begin_call = heap._new<Dummy>(_internal_type, {});
    this->_py_op_call = heap._new<Dummy>(_internal_type, {});
    this->_py_op_yield = heap._new<Dummy>(_internal_type, {});

    this->builtins = new_module("builtins");
    this->_main = new_module("__main__");
    
    // setup public types
    builtins->attr().set("type", _t(tp_type));
    builtins->attr().set("object", _t(tp_object));
    builtins->attr().set("bool", _t(tp_bool));
    builtins->attr().set("int", _t(tp_int));
    builtins->attr().set("float", _t(tp_float));
    builtins->attr().set("str", _t(tp_str));
    builtins->attr().set("list", _t(tp_list));
    builtins->attr().set("tuple", _t(tp_tuple));
    builtins->attr().set("range", _t(tp_range));

    post_init();
    for(int i=0; i<_all_types.size(); i++){
        _all_types[i].obj->attr()._try_perfect_rehash();
    }
    for(auto [k, v]: _modules.items()) v->attr()._try_perfect_rehash();
}

inline PyObject* VM::_vectorcall(int ARGC, int KWARGC, bool op_call){
    bool is_varargs = ARGC == 0xFFFF;
    PyObject** p0;
    PyObject** p1 = s_data._sp - KWARGC*2;
    if(is_varargs){
        p0 = p1 - 1;
        while(*p0 != _py_begin_call) p0--;
        // [BEGIN_CALL, callable, <self>, args..., kwargs...]
        //      ^p0                                ^p1      ^_sp
        ARGC = p1 - (p0 + 3);
    }else{
        p0 = p1 - ARGC - 2 - (int)is_varargs;
        // [callable, <self>, args..., kwargs...]
        //      ^p0                    ^p1      ^_sp
    }
    PyObject* callable = p1[-(ARGC + 2)];
    bool method_call = p1[-(ARGC + 1)] != _py_null;

    ArgsView args(p1 - ARGC - int(method_call), p1);

    // handle boundmethod, do a patch
    if(is_non_tagged_type(callable, tp_bound_method)){
        if(method_call) FATAL_ERROR();
        auto& bm = CAST(BoundMethod&, callable);
        callable = bm.method;      // get unbound method
        p1[-(ARGC + 2)] = bm.method;
        p1[-(ARGC + 1)] = bm.obj;
        // [unbound, self, args..., kwargs...]
    }

    if(is_non_tagged_type(callable, tp_native_function)){
        const auto& f = OBJ_GET(NativeFunc, callable);
        if(KWARGC != 0) TypeError("native_function does not accept keyword arguments");
        PyObject* ret = f(this, args);
        s_data.reset(p0);
        return ret;
    }

    ArgsView kwargs(p1, s_data._sp);

    if(is_non_tagged_type(callable, tp_function)){
        // ret is nullptr or a generator
        PyObject* ret = _py_call(p0, callable, args, kwargs);
        // stack resetting is handled by _py_call
        if(ret != nullptr) return ret;
        if(op_call) return _py_op_call;
        return _run_top_frame();
    }

    if(is_non_tagged_type(callable, tp_type)){
        if(method_call) FATAL_ERROR();
        // [type, NULL, args..., kwargs...]

        // TODO: derived __new__ ?
        PyObject* new_f = callable->attr().try_get(__new__);
        PyObject* obj;
        if(new_f != nullptr){
            PUSH(new_f);
            s_data.dup_top_n(1 + ARGC + KWARGC*2);
            obj = _vectorcall(ARGC, KWARGC, false);
            if(!isinstance(obj, OBJ_GET(Type, callable))) return obj;
        }else{
            obj = heap.gcnew<DummyInstance>(OBJ_GET(Type, callable), {});
        }
        PyObject* self;
        callable = get_unbound_method(obj, __init__, &self, false);
        if (self != _py_null) {
            // replace `NULL` with `self`
            p1[-(ARGC + 2)] = callable;
            p1[-(ARGC + 1)] = self;
            // [init_f, self, args..., kwargs...]
            _vectorcall(ARGC, KWARGC, false);
            // We just discard the return value of `__init__`
            // in cpython it raises a TypeError if the return value is not None
        }else{
            // manually reset the stack
            s_data.reset(p0);
        }
        return obj;
    }

    // handle `__call__` overload
    PyObject* self;
    PyObject* call_f = get_unbound_method(callable, __call__, &self, false);
    if(self != _py_null){
        p1[-(ARGC + 2)] = call_f;
        p1[-(ARGC + 1)] = self;
        // [call_f, self, args..., kwargs...]
        return _vectorcall(ARGC, KWARGC, false);
    }
    TypeError(OBJ_NAME(_t(callable)).escape() + " object is not callable");
    return nullptr;
}

inline PyObject* VM::_py_call(PyObject** sp_base, PyObject* callable, ArgsView args, ArgsView kwargs){
    // callable must be a `function` object
    const Function& fn = CAST(Function&, callable);
    const CodeObject* co = fn.decl->code.get();
    FastLocals locals(co);

    int i = 0;
    if(args.size() < fn.decl->args.size()){
        vm->TypeError(fmt(
            "expected ",
            fn.decl->args.size(),
            " positional arguments, but got ",
            args.size(),
            " (", fn.decl->code->name, ')'
        ));
    }

    // prepare args
    for(int index: fn.decl->args) locals[index] = args[i++];
    // prepare kwdefaults
    for(auto& kv: fn.decl->kwargs) locals[kv.key] = kv.value;
    
    // handle *args
    if(fn.decl->starred_arg != -1){
        List vargs;        // handle *args
        while(i < args.size()) vargs.push_back(args[i++]);
        locals[fn.decl->starred_arg] = VAR(Tuple(std::move(vargs)));
    }else{
        // kwdefaults override
        for(auto& kv: fn.decl->kwargs){
            if(i < args.size()){
                locals[kv.key] = args[i++];
            }else{
                break;
            }
        }
        if(i < args.size()) TypeError(fmt("too many arguments", " (", fn.decl->code->name, ')'));
    }
    
    for(int i=0; i<kwargs.size(); i+=2){
        StrName key = CAST(int, kwargs[i]);
        bool ok = locals._try_set(key, kwargs[i+1]);
        if(!ok) TypeError(fmt(key.escape(), " is an invalid keyword argument for ", co->name, "()"));
    }
    PyObject* _module = fn._module != nullptr ? fn._module : top_frame()->_module;
    if(co->is_generator) return PyIter(Generator(this, Frame(
        &s_data, sp_base, co, _module, std::move(locals), fn._closure
    )));
    callstack.emplace(&s_data, sp_base, co, _module, std::move(locals), fn._closure);
    return nullptr;
}

// https://docs.python.org/3/howto/descriptor.html#invocation-from-an-instance
inline PyObject* VM::getattr(PyObject* obj, StrName name, bool throw_err){
    PyObject* objtype = _t(obj);
    // handle super() proxy
    if(is_type(obj, tp_super)){
        const Super& super = OBJ_GET(Super, obj);
        obj = super.first;
        objtype = _t(super.second);
    }
    PyObject* cls_var = find_name_in_mro(objtype, name);
    if(cls_var != nullptr){
        // handle descriptor
        PyObject* descr_get = _t(cls_var)->attr().try_get(__get__);
        if(descr_get != nullptr) return call_method(cls_var, descr_get, obj);
    }
    // handle instance __dict__
    if(!is_tagged(obj) && obj->is_attr_valid()){
        PyObject* val = obj->attr().try_get(name);
        if(val != nullptr) return val;
    }
    if(cls_var != nullptr){
        // bound method is non-data descriptor
        if(is_type(cls_var, tp_function) || is_type(cls_var, tp_native_function)){
            return VAR(BoundMethod(obj, cls_var));
        }
        return cls_var;
    }
    if(throw_err) AttributeError(obj, name);
    return nullptr;
}

// used by OP_LOAD_METHOD
// try to load a unbound method (fallback to `getattr` if not found)
inline PyObject* VM::get_unbound_method(PyObject* obj, StrName name, PyObject** self, bool throw_err, bool fallback){
    *self = _py_null;
    PyObject* objtype = _t(obj);
    // handle super() proxy
    if(is_type(obj, tp_super)){
        const Super& super = OBJ_GET(Super, obj);
        obj = super.first;
        objtype = _t(super.second);
    }
    PyObject* cls_var = find_name_in_mro(objtype, name);

    if(fallback){
        if(cls_var != nullptr){
            // handle descriptor
            PyObject* descr_get = _t(cls_var)->attr().try_get(__get__);
            if(descr_get != nullptr) return call_method(cls_var, descr_get, obj);
        }
        // handle instance __dict__
        if(!is_tagged(obj) && obj->is_attr_valid()){
            PyObject* val = obj->attr().try_get(name);
            if(val != nullptr) return val;
        }
    }

    if(cls_var != nullptr){
        if(is_type(cls_var, tp_function) || is_type(cls_var, tp_native_function)){
            *self = obj;
        }
        return cls_var;
    }
    if(throw_err) AttributeError(obj, name);
    return nullptr;
}

inline void VM::setattr(PyObject* obj, StrName name, PyObject* value){
    PyObject* objtype = _t(obj);
    // handle super() proxy
    if(is_type(obj, tp_super)){
        Super& super = OBJ_GET(Super, obj);
        obj = super.first;
        objtype = _t(super.second);
    }
    PyObject* cls_var = find_name_in_mro(objtype, name);
    if(cls_var != nullptr){
        // handle descriptor
        PyObject* cls_var_t = _t(cls_var);
        if(cls_var_t->attr().contains(__get__)){
            PyObject* descr_set = cls_var_t->attr().try_get(__set__);
            if(descr_set != nullptr){
                call_method(cls_var, descr_set, obj, value);
            }else{
                TypeError(fmt("readonly attribute: ", name.escape()));
            }
            return;
        }
    }
    // handle instance __dict__
    if(is_tagged(obj) || !obj->is_attr_valid()) TypeError("cannot set attribute");
    obj->attr().set(name, value);
}

template<int ARGC>
void VM::bind_method(PyObject* obj, Str name, NativeFuncRaw fn) {
    check_type(obj, tp_type);
    obj->attr().set(name, VAR(NativeFunc(fn, ARGC, true)));
}

template<int ARGC>
void VM::bind_func(PyObject* obj, Str name, NativeFuncRaw fn) {
    obj->attr().set(name, VAR(NativeFunc(fn, ARGC, false)));
}

inline void VM::_error(Exception e){
    if(callstack.empty()){
        e.is_re = false;
        throw e;
    }
    s_data.push(VAR(e));
    _raise();
}

inline void ManagedHeap::mark() {
    for(PyObject* obj: _no_gc) OBJ_MARK(obj);
    for(auto& frame : vm->callstack.data()) frame._gc_mark();
    for(PyObject* obj: vm->s_data) OBJ_MARK(obj);
}

inline Str obj_type_name(VM *vm, Type type){
    return vm->_all_types[type].name;
}

}   // namespace pkpy