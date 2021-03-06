#include "oba_function.h"
#include "oba_common.h"
#include "oba_value.h"

ObjFunction* newFunction(ObaVM* vm, ObjModule* module) {
  ObjFunction* function = ALLOCATE_OBJ(vm, ObjFunction, OBJ_FUNCTION);
  initChunk(&function->chunk);
  function->arity = 0;
  function->module = module;
  function->upvalueCount = 0;
  function->name = NULL;
}

ObjClosure* newClosure(ObaVM* vm, ObjFunction* function) {
  ObjClosure* closure = ALLOCATE_OBJ(vm, ObjClosure, OBJ_CLOSURE);
  ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

ObjUpvalue* newUpvalue(ObaVM* vm, Value* slot) {
  ObjUpvalue* upvalue = ALLOCATE_OBJ(vm, ObjUpvalue, OBJ_UPVALUE);
  upvalue->location = slot;
  upvalue->next = NULL;
  return upvalue;
}
