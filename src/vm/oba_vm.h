#ifndef oba_vm_h
#define oba_vm_h

#include "oba_compiler.h"
#include "oba_function.h"
#include "oba_token.h"
#include "oba_value.h"

// The maximum size of the VM stack in bytes.
// TODO(kendal): Support dynamically resizing the stack.
#define STACK_MAX 256
#define FRAMES_MAX 256

struct ObaVM {
  CallFrame frames[FRAMES_MAX];
  CallFrame* frame;

  Value stack[STACK_MAX];
  Value* stackTop;

  // Global values available to all modules.
  //
  // Builtins are defined here. When searching for a global, the VM first checks
  // the current module, then this table.
  Table* globals;

  Table* modules;
  ObjUpvalue* openUpvalues;
  Obj* objects;
};

typedef enum {
#define OPCODE(name) OP_##name,
#include "oba_opcodes.h"
#undef OPCODE
} OpCode;

#endif
