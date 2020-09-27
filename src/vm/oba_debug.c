#include <stdio.h>

#include "oba_debug.h"
#include "oba_value.h"

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t constant = chunk->code[offset + 1];
  printf("%-16s %4d '", name, constant);
  printValue(chunk->constants.values[constant]);
  printf("'\n");
  return offset + 2;
}

static int simpleInstruction(const char* name, Chunk* chunk, int offset) {
  printf("%s\n", name);
  return offset + 1;
}

int disassemble(Chunk* chunk, const char* name) {
  printf("== %s ==\n", name);
  for (int offset = 0; offset < chunk->count;) {
    offset = disassembleInstruction(chunk, offset);
  }
}

int disassembleInstruction(Chunk* chunk, int offset) {
  printf("%04d ", offset);

  uint8_t instr = chunk->code[offset];
  switch (instr) {
  case OP_CONSTANT:
    return constantInstruction("OP_CONSTANT", chunk, offset);
  case OP_ADD:
    return simpleInstruction("OP_ADD", chunk, offset);
  default:
    printf("Unknown opcode %d\n", instr);
    return offset + 1;
  }
}
