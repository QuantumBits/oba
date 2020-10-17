#include "oba_function.h"
#include "oba_memory.h"
#include "oba_value.h"

ObjFunction* newFunction() {
  ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
  initChunk(&function->chunk);
  function->arity = 0;
  function->name = NULL;
}

void freeFunction(ObjFunction* function) {
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  freeChunk(&function->chunk);
  reallocate(function, sizeof(ObjFunction), 0);
}

ObjClosure* newClosure(ObjFunction* function) {
  ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
  ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

void freeClosure(ObjClosure* closure) {
  FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
  reallocate(closure, sizeof(ObjClosure), 0);
}

ObjUpvalue* newUpvalue(Value* slot) {
  ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
  upvalue->location = slot;
  return upvalue;
}

void freeUpvalue(ObjUpvalue* upvalue) {
  // TODO(kendal): Make a helper macro for this.
  reallocate(upvalue, sizeof(ObjUpvalue), 0);
}
