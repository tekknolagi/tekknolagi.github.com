// vim: set tabstop=2 shiftwidth=2 textwidth=79 expandtab:
// gcc -O2 -g -Wall -Wextra -pedantic -fno-strict-aliasing
//   assets/code/lisp/compiling-reader.c

#define _GNU_SOURCE
#include <assert.h>   // for assert
#include <stdbool.h>  // for bool
#include <stddef.h>   // for NULL
#include <stdint.h>   // for int32_t, etc
#include <stdio.h>    // for getline, fprintf
#include <string.h>   // for memcpy
#include <sys/mman.h> // for mmap
#undef _GNU_SOURCE

#include "greatest.h"

// Objects

typedef int64_t word;
typedef uint64_t uword;

const int kBitsPerByte = 8;                        // bits
const int kWordSize = sizeof(word);                // bytes
const int kBitsPerWord = kWordSize * kBitsPerByte; // bits

const unsigned int kIntegerTag = 0x0;     // 0b00
const unsigned int kIntegerTagMask = 0x3; // 0b11
const unsigned int kIntegerShift = 2;
const unsigned int kIntegerBits = kBitsPerWord - kIntegerShift;
const word kIntegerMax = (1LL << (kIntegerBits - 1)) - 1;
const word kIntegerMin = -(1LL << (kIntegerBits - 1));

const unsigned int kImmediateTagMask = 0x3f;

const unsigned int kCharTag = 0xf;   // 0b00001111
const unsigned int kCharMask = 0xff; // 0b11111111
const unsigned int kCharShift = 8;

const unsigned int kBoolTag = 0x1f;  // 0b0011111
const unsigned int kBoolMask = 0x80; // 0b10000000
const unsigned int kBoolShift = 7;

const unsigned int kNilTag = 0x2f; // 0b101111

const unsigned int kErrorTag = 0x3f; // 0b111111

const unsigned int kPairTag = 0x1;        // 0b001
const unsigned int kSymbolTag = 0x5;      // 0b101
const uword kHeapTagMask = ((uword)0x7);  // 0b000...111
const uword kHeapPtrMask = ~kHeapTagMask; // 0b1111...1000

uword Object_encode_integer(word value) {
  assert(value < kIntegerMax && "too big");
  assert(value > kIntegerMin && "too small");
  return value << kIntegerShift;
}

word Object_decode_integer(uword value) { return (word)value >> kIntegerShift; }

uword Object_encode_char(char value) {
  return ((uword)value << kCharShift) | kCharTag;
}

char Object_decode_char(uword value) {
  return (value >> kCharShift) & kCharMask;
}

uword Object_encode_bool(bool value) {
  return ((uword)value << kBoolShift) | kBoolTag;
}

bool Object_decode_bool(uword value) { return value & kBoolMask; }

uword Object_true() { return Object_encode_bool(true); }

uword Object_false() { return Object_encode_bool(false); }

uword Object_nil() { return kNilTag; }

uword Object_error() { return kErrorTag; }

uword Object_address(void *obj) { return (uword)obj & kHeapPtrMask; }

// End Objects

// Buffer

typedef unsigned char byte;

typedef enum {
  kWritable,
  kExecutable,
} BufferState;

typedef struct {
  byte *address;
  BufferState state;
  size_t len;
  size_t capacity;
} Buffer;

byte *Buffer_alloc_writable(size_t capacity) {
  byte *result = mmap(/*addr=*/NULL, capacity, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE,
                      /*filedes=*/-1, /*off=*/0);
  assert(result != MAP_FAILED);
  return result;
}

void Buffer_init(Buffer *result, size_t capacity) {
  result->address = Buffer_alloc_writable(capacity);
  assert(result->address != MAP_FAILED);
  result->state = kWritable;
  result->len = 0;
  result->capacity = capacity;
}

void Buffer_deinit(Buffer *buf) {
  munmap(buf->address, buf->capacity);
  buf->address = NULL;
  buf->len = 0;
  buf->capacity = 0;
}

int Buffer_make_executable(Buffer *buf) {
  int result = mprotect(buf->address, buf->len, PROT_EXEC);
  buf->state = kExecutable;
  return result;
}

byte Buffer_at8(Buffer *buf, size_t pos) { return buf->address[pos]; }

void Buffer_at_put8(Buffer *buf, size_t pos, byte b) { buf->address[pos] = b; }

word max(word left, word right) { return left > right ? left : right; }

void Buffer_ensure_capacity(Buffer *buf, word additional_capacity) {
  if (buf->len + additional_capacity <= buf->capacity) {
    return;
  }
  word new_capacity =
      max(buf->capacity * 2, buf->capacity + additional_capacity);
  byte *address = Buffer_alloc_writable(new_capacity);
  memcpy(address, buf->address, buf->len);
  int result = munmap(buf->address, buf->capacity);
  assert(result == 0 && "munmap failed");
  buf->address = address;
  buf->capacity = new_capacity;
}

void Buffer_write8(Buffer *buf, byte b) {
  Buffer_ensure_capacity(buf, sizeof b);
  Buffer_at_put8(buf, buf->len++, b);
}

void Buffer_write32(Buffer *buf, int32_t value) {
  for (size_t i = 0; i < 4; i++) {
    Buffer_write8(buf, (value >> (i * kBitsPerByte)) & 0xff);
  }
}

void Buffer_write_arr(Buffer *buf, const byte *arr, word arr_size) {
  Buffer_ensure_capacity(buf, arr_size);
  for (word i = 0; i < arr_size; i++) {
    Buffer_write8(buf, arr[i]);
  }
}

// End Buffer

// Emit

typedef enum {
  kRax = 0,
  kRcx,
  kRdx,
  kRbx,
  kRsp,
  kRbp,
  kRsi,
  kRdi,
} Register;

typedef enum {
  kAl = 0,
  kCl,
  kDl,
  kBl,
  kAh,
  kCh,
  kDh,
  kBh,
} PartialRegister;

typedef enum {
  kOverflow = 0,
  kNotOverflow,
  kBelow,
  kCarry = kBelow,
  kNotAboveOrEqual = kBelow,
  kAboveOrEqual,
  kNotBelow = kAboveOrEqual,
  kNotCarry = kAboveOrEqual,
  kEqual,
  kZero = kEqual,
  kLess = 0xc,
  kNotGreaterOrEqual = kLess,
  // TODO(max): Add more
} Condition;

typedef struct Indirect {
  Register reg;
  int8_t disp;
} Indirect;

Indirect Ind(Register reg, int8_t disp) {
  return (Indirect){.reg = reg, .disp = disp};
}

static const byte kRexPrefix = 0x48;

void Emit_mov_reg_imm32(Buffer *buf, Register dst, int32_t src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0xc7);
  Buffer_write8(buf, 0xc0 + dst);
  Buffer_write32(buf, src);
}

void Emit_ret(Buffer *buf) { Buffer_write8(buf, 0xc3); }

void Emit_add_reg_imm32(Buffer *buf, Register dst, int32_t src) {
  Buffer_write8(buf, kRexPrefix);
  if (dst == kRax) {
    // Optimization: add eax, {imm32} can either be encoded as 05 {imm32} or 81
    // c0 {imm32}.
    Buffer_write8(buf, 0x05);
  } else {
    Buffer_write8(buf, 0x81);
    Buffer_write8(buf, 0xc0 + dst);
  }
  Buffer_write32(buf, src);
}

void Emit_sub_reg_imm32(Buffer *buf, Register dst, int32_t src) {
  Buffer_write8(buf, kRexPrefix);
  if (dst == kRax) {
    // Optimization: sub eax, {imm32} can either be encoded as 2d {imm32} or 81
    // e8 {imm32}.
    Buffer_write8(buf, 0x2d);
  } else {
    Buffer_write8(buf, 0x81);
    Buffer_write8(buf, 0xe8 + dst);
  }
  Buffer_write32(buf, src);
}

void Emit_shl_reg_imm8(Buffer *buf, Register dst, int8_t bits) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0xc1);
  Buffer_write8(buf, 0xe0 + dst);
  Buffer_write8(buf, bits);
}

void Emit_shr_reg_imm8(Buffer *buf, Register dst, int8_t bits) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0xc1);
  Buffer_write8(buf, 0xe8 + dst);
  Buffer_write8(buf, bits);
}

void Emit_or_reg_imm8(Buffer *buf, Register dst, uint8_t tag) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x83);
  Buffer_write8(buf, 0xc8 + dst);
  Buffer_write8(buf, tag);
}

void Emit_and_reg_imm8(Buffer *buf, Register dst, uint8_t tag) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x83);
  Buffer_write8(buf, 0xe0 + dst);
  Buffer_write8(buf, tag);
}

void Emit_cmp_reg_imm32(Buffer *buf, Register left, int32_t right) {
  Buffer_write8(buf, kRexPrefix);
  if (left == kRax) {
    // Optimization: cmp rax, {imm32} can either be encoded as 3d {imm32} or 81
    // f8 {imm32}.
    Buffer_write8(buf, 0x3d);
  } else {
    Buffer_write8(buf, 0x81);
    Buffer_write8(buf, 0xf8 + left);
  }
  Buffer_write32(buf, right);
}

void Emit_setcc_imm8(Buffer *buf, Condition cond, PartialRegister dst) {
  Buffer_write8(buf, 0x0f);
  Buffer_write8(buf, 0x90 + cond);
  Buffer_write8(buf, 0xc0 + dst);
}

uint8_t disp8(int8_t disp) { return disp >= 0 ? disp : 0x100 + disp; }

// mov [dst+disp], src
// or
// mov %src, disp(%dst)
void Emit_store_reg_indirect(Buffer *buf, Indirect dst, Register src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x89);
  Buffer_write8(buf, 0x40 + src * 8 + dst.reg);
  Buffer_write8(buf, disp8(dst.disp));
}

// add dst, [src+disp]
// or
// add disp(%src), %dst
void Emit_add_reg_indirect(Buffer *buf, Register dst, Indirect src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x03);
  Buffer_write8(buf, 0x40 + dst * 8 + src.reg);
  Buffer_write8(buf, disp8(src.disp));
}

// sub dst, [src+disp]
// or
// sub disp(%src), %dst
void Emit_sub_reg_indirect(Buffer *buf, Register dst, Indirect src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x2b);
  Buffer_write8(buf, 0x40 + dst * 8 + src.reg);
  Buffer_write8(buf, disp8(src.disp));
}

// mul rax, [src+disp]
// or
// mul disp(%src), %rax
void Emit_mul_reg_indirect(Buffer *buf, Indirect src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0xf7);
  Buffer_write8(buf, 0x60 + src.reg);
  Buffer_write8(buf, disp8(src.disp));
}

// cmp left, [right+disp]
// or
// cmp disp(%right), %left
void Emit_cmp_reg_indirect(Buffer *buf, Register left, Indirect right) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x3b);
  Buffer_write8(buf, 0x40 + left * 8 + right.reg);
  Buffer_write8(buf, disp8(right.disp));
}

// End Emit

// AST

typedef struct ASTNode ASTNode;

typedef struct Pair {
  ASTNode *car;
  ASTNode *cdr;
} Pair;

typedef struct Symbol {
  word length;
  char cstr[];
} Symbol;

bool AST_is_integer(ASTNode *node) {
  return ((uword)node & kIntegerTagMask) == kIntegerTag;
}

word AST_get_integer(ASTNode *node) {
  return Object_decode_integer((uword)node);
}

ASTNode *AST_new_integer(word value) {
  return (ASTNode *)Object_encode_integer(value);
}

bool AST_is_char(ASTNode *node) {
  return ((uword)node & kImmediateTagMask) == kCharTag;
}

char AST_get_char(ASTNode *node) { return Object_decode_char((uword)node); }

ASTNode *AST_new_char(char value) {
  return (ASTNode *)Object_encode_char(value);
}

bool AST_is_bool(ASTNode *node) {
  return ((uword)node & kImmediateTagMask) == kBoolTag;
}

bool AST_get_bool(ASTNode *node) { return Object_decode_bool((uword)node); }

ASTNode *AST_new_bool(bool value) {
  return (ASTNode *)Object_encode_bool(value);
}

bool AST_is_nil(ASTNode *node) { return (uword)node == Object_nil(); }

ASTNode *AST_nil() { return (ASTNode *)Object_nil(); }

bool AST_is_error(ASTNode *node) { return (uword)node == Object_error(); }

ASTNode *AST_error() { return (ASTNode *)Object_error(); }

ASTNode *AST_heap_alloc(unsigned char tag, uword size) {
  // Initialize to 0
  uword address = (uword)calloc(size, 1);
  return (ASTNode *)(address | tag);
}

bool AST_is_heap_object(ASTNode *node) {
  // For some reason masking out the tag first and then doing the comparison
  // makes this branchless
  unsigned char tag = (uword)node & kHeapTagMask;
  // Heap object tags are between 0b001 and 0b110 except for 0b100 (which is an
  // integer)
  return (tag & kIntegerTagMask) > 0 && (tag & kImmediateTagMask) != 0x7;
}

void AST_pair_set_car(ASTNode *node, ASTNode *car);
void AST_pair_set_cdr(ASTNode *node, ASTNode *cdr);

ASTNode *AST_new_pair(ASTNode *car, ASTNode *cdr) {
  ASTNode *node = AST_heap_alloc(kPairTag, sizeof(Pair));
  AST_pair_set_car(node, car);
  AST_pair_set_cdr(node, cdr);
  return node;
}

bool AST_is_pair(ASTNode *node) {
  return ((uword)node & kHeapTagMask) == kPairTag;
}

Pair *AST_as_pair(ASTNode *node) {
  assert(AST_is_pair(node));
  return (Pair *)Object_address(node);
}

ASTNode *AST_pair_car(ASTNode *node) { return AST_as_pair(node)->car; }

void AST_pair_set_car(ASTNode *node, ASTNode *car) {
  AST_as_pair(node)->car = car;
}

ASTNode *AST_pair_cdr(ASTNode *node) { return AST_as_pair(node)->cdr; }

void AST_pair_set_cdr(ASTNode *node, ASTNode *cdr) {
  AST_as_pair(node)->cdr = cdr;
}

void AST_heap_free(ASTNode *node) {
  if (!AST_is_heap_object(node)) {
    return;
  }
  if (AST_is_pair(node)) {
    AST_heap_free(AST_pair_car(node));
    AST_heap_free(AST_pair_cdr(node));
  }
  free((void *)Object_address(node));
}

Symbol *AST_as_symbol(ASTNode *node);

ASTNode *AST_new_symbol(const char *str) {
  word data_length = strlen(str) + 1; // for NUL
  ASTNode *node = AST_heap_alloc(kSymbolTag, sizeof(Symbol) + data_length);
  Symbol *s = AST_as_symbol(node);
  s->length = data_length;
  memcpy(s->cstr, str, data_length);
  return node;
}

bool AST_is_symbol(ASTNode *node) {
  return ((uword)node & kHeapTagMask) == kSymbolTag;
}

Symbol *AST_as_symbol(ASTNode *node) {
  assert(AST_is_symbol(node));
  return (Symbol *)Object_address(node);
}

const char *AST_symbol_cstr(ASTNode *node) {
  return (const char *)AST_as_symbol(node)->cstr;
}

bool AST_symbol_matches(ASTNode *node, const char *cstr) {
  return strcmp(AST_symbol_cstr(node), cstr) == 0;
}

int node_to_str(ASTNode *node, char *buf, word size);

int list_to_str(ASTNode *node, char *buf, word size) {
  if (AST_is_pair(node)) {
    word result = 0;
    result += snprintf(buf + result, size, " ");
    result += node_to_str(AST_pair_car(node), buf + result, size);
    result += list_to_str(AST_pair_cdr(node), buf + result, size);
    return result;
  }
  if (AST_is_nil(node)) {
    return snprintf(buf, size, ")");
  }
  word result = 0;
  result += snprintf(buf + result, size, " . ");
  result += node_to_str(node, buf + result, size);
  result += snprintf(buf + result, size, ")");
  return result;
}

int node_to_str(ASTNode *node, char *buf, word size) {
  if (AST_is_integer(node)) {
    return snprintf(buf, size, "%ld", AST_get_integer(node));
  }
  if (AST_is_char(node)) {
    return snprintf(buf, size, "'%c'", AST_get_char(node));
  }
  if (AST_is_bool(node)) {
    return snprintf(buf, size, "%s", AST_get_bool(node) ? "true" : "false");
  }
  if (AST_is_nil(node)) {
    return snprintf(buf, size, "nil");
  }
  if (AST_is_pair(node)) {
    word result = 0;
    result += snprintf(buf + result, size, "(");
    result += node_to_str(AST_pair_car(node), buf + result, size);
    result += list_to_str(AST_pair_cdr(node), buf + result, size);
    return result;
  }
  if (AST_is_symbol(node)) {
    return snprintf(buf, size, "%s", AST_symbol_cstr(node));
  }
  assert(0 && "unknown ast");
}

char *AST_to_cstr(ASTNode *node) {
  int size = node_to_str(node, NULL, 0);
  char *buf = malloc(size + 1);
  assert(buf != NULL);
  node_to_str(node, buf, size + 1);
  buf[size] = '\0';
  return buf;
}

// End AST

// Reader

void advance(word *pos) { ++*pos; }

char next(char *input, word *pos) {
  advance(pos);
  return input[*pos];
}

ASTNode *read_integer(char *input, word *pos, int sign) {
  word result = 0;
  for (char c = input[*pos]; isdigit(c); c = next(input, pos)) {
    result *= 10;
    result += c - '0';
  }
  return AST_new_integer(sign * result);
}

bool starts_symbol(char c) {
  switch (c) {
  case '+':
  case '-':
  case '*':
  case '>':
  case '=':
  case '?':
    return true;
  default:
    return isalpha(c);
  }
}

bool is_symbol_char(char c) { return starts_symbol(c) || isdigit(c); }

const word ATOM_MAX = 32;

ASTNode *read_symbol(char *input, word *pos) {
  char buf[ATOM_MAX + 1]; // +1 for NUL
  word length = 0;
  for (length = 0; length < ATOM_MAX && is_symbol_char(input[*pos]); length++) {
    buf[length] = input[*pos];
    advance(pos);
  }
  buf[length] = '\0';
  return AST_new_symbol(buf);
}

ASTNode *read_char(char *input, word *pos) {
  char c = input[*pos];
  if (c == '\'') {
    return AST_error();
  }
  advance(pos);
  if (input[*pos] != '\'') {
    return AST_error();
  }
  advance(pos);
  return AST_new_char(c);
}

char skip_whitespace(char *input, word *pos) {
  char c = '\0';
  for (c = input[*pos]; isspace(c); c = next(input, pos)) {
    ;
  }
  return c;
}

ASTNode *read_rec(char *input, word *pos);

ASTNode *read_list(char *input, word *pos) {
  char c = skip_whitespace(input, pos);
  if (c == ')') {
    advance(pos);
    return AST_nil();
  }
  ASTNode *car = read_rec(input, pos);
  assert(car != AST_error());
  ASTNode *cdr = read_list(input, pos);
  assert(cdr != AST_error());
  return AST_new_pair(car, cdr);
}

ASTNode *read_rec(char *input, word *pos) {
  char c = skip_whitespace(input, pos);
  if (isdigit(c)) {
    return read_integer(input, pos, /*sign=*/1);
  }
  if (c == '-' && isdigit(input[*pos + 1])) {
    advance(pos);
    return read_integer(input, pos, /*sign=*/-1);
  }
  if (c == '+' && isdigit(input[*pos + 1])) {
    advance(pos);
    return read_integer(input, pos, /*sign=*/1);
  }
  if (starts_symbol(c)) {
    return read_symbol(input, pos);
  }
  if (c == '\'') {
    advance(pos); // skip '\''
    return read_char(input, pos);
  }
  if (c == '#' && input[*pos + 1] == 't') {
    advance(pos); // skip '#'
    advance(pos); // skip 't'
    return AST_new_bool(true);
  }
  if (c == '#' && input[*pos + 1] == 'f') {
    advance(pos); // skip '#'
    advance(pos); // skip 'f'
    return AST_new_bool(false);
  }
  if (c == '(') {
    advance(pos); // skip '('
    return read_list(input, pos);
  }
  return AST_error();
}

ASTNode *Reader_read(char *input) {
  word pos = 0;
  return read_rec(input, &pos);
}

// End Reader

// Compile

int Compile_expr(Buffer *buf, ASTNode *node, word stack_index);

ASTNode *operand1(ASTNode *args) { return AST_pair_car(args); }

ASTNode *operand2(ASTNode *args) { return AST_pair_car(AST_pair_cdr(args)); }

#define _(exp)                                                                 \
  do {                                                                         \
    int result = exp;                                                          \
    if (result != 0)                                                           \
      return result;                                                           \
  } while (0)

void Compile_compare_imm32(Buffer *buf, int32_t value) {
  Emit_cmp_reg_imm32(buf, kRax, value);
  Emit_mov_reg_imm32(buf, kRax, 0);
  Emit_setcc_imm8(buf, kEqual, kAl);
  Emit_shl_reg_imm8(buf, kRax, kBoolShift);
  Emit_or_reg_imm8(buf, kRax, kBoolTag);
}

int Compile_call(Buffer *buf, ASTNode *callable, ASTNode *args,
                 word stack_index) {
  if (AST_is_symbol(callable)) {
    if (AST_symbol_matches(callable, "add1")) {
      _(Compile_expr(buf, operand1(args), stack_index));
      Emit_add_reg_imm32(buf, kRax, Object_encode_integer(1));
      return 0;
    }
    if (AST_symbol_matches(callable, "sub1")) {
      _(Compile_expr(buf, operand1(args), stack_index));
      Emit_sub_reg_imm32(buf, kRax, Object_encode_integer(1));
      return 0;
    }
    if (AST_symbol_matches(callable, "integer->char")) {
      _(Compile_expr(buf, operand1(args), stack_index));
      Emit_shl_reg_imm8(buf, kRax, kCharShift - kIntegerShift);
      Emit_or_reg_imm8(buf, kRax, kCharTag);
      return 0;
    }
    if (AST_symbol_matches(callable, "char->integer")) {
      _(Compile_expr(buf, operand1(args), stack_index));
      Emit_shr_reg_imm8(buf, kRax, kCharShift - kIntegerShift);
      return 0;
    }
    if (AST_symbol_matches(callable, "nil?")) {
      _(Compile_expr(buf, operand1(args), stack_index));
      Compile_compare_imm32(buf, Object_nil());
      return 0;
    }
    if (AST_symbol_matches(callable, "zero?")) {
      _(Compile_expr(buf, operand1(args), stack_index));
      Compile_compare_imm32(buf, Object_encode_integer(0));
      return 0;
    }
    if (AST_symbol_matches(callable, "not")) {
      _(Compile_expr(buf, operand1(args), stack_index));
      // All non #f values are truthy
      // ...this might be a problem if we want to make nil falsey
      Compile_compare_imm32(buf, Object_false());
      return 0;
    }
    if (AST_symbol_matches(callable, "integer?")) {
      _(Compile_expr(buf, operand1(args), stack_index));
      Emit_and_reg_imm8(buf, kRax, kIntegerTagMask);
      Compile_compare_imm32(buf, kIntegerTag);
      return 0;
    }
    if (AST_symbol_matches(callable, "boolean?")) {
      _(Compile_expr(buf, operand1(args), stack_index));
      Emit_and_reg_imm8(buf, kRax, kImmediateTagMask);
      Compile_compare_imm32(buf, kBoolTag);
      return 0;
    }
    if (AST_symbol_matches(callable, "+")) {
      _(Compile_expr(buf, operand2(args), stack_index));
      Emit_store_reg_indirect(buf, /*dst=*/Ind(kRbp, stack_index),
                              /*src=*/kRax);
      _(Compile_expr(buf, operand1(args), stack_index - kWordSize));
      Emit_add_reg_indirect(buf, /*dst=*/kRax, /*src=*/Ind(kRbp, stack_index));
      return 0;
    }
    if (AST_symbol_matches(callable, "-")) {
      _(Compile_expr(buf, operand2(args), stack_index));
      Emit_store_reg_indirect(buf, /*dst=*/Ind(kRbp, stack_index),
                              /*src=*/kRax);
      _(Compile_expr(buf, operand1(args), stack_index - kWordSize));
      Emit_sub_reg_indirect(buf, /*dst=*/kRax, /*src=*/Ind(kRbp, stack_index));
      return 0;
    }
    if (AST_symbol_matches(callable, "*")) {
      _(Compile_expr(buf, operand2(args), stack_index));
      // Remove the tag so that the result is still only tagged with 0b00
      // instead of 0b0000
      Emit_shr_reg_imm8(buf, kRax, kIntegerShift);
      Emit_store_reg_indirect(buf, /*dst=*/Ind(kRbp, stack_index),
                              /*src=*/kRax);
      _(Compile_expr(buf, operand1(args), stack_index - kWordSize));
      Emit_mul_reg_indirect(buf, /*src=*/Ind(kRbp, stack_index));
      return 0;
    }
    if (AST_symbol_matches(callable, "=")) {
      _(Compile_expr(buf, operand2(args), stack_index));
      Emit_store_reg_indirect(buf, /*dst=*/Ind(kRbp, stack_index),
                              /*src=*/kRax);
      _(Compile_expr(buf, operand1(args), stack_index - kWordSize));
      Emit_cmp_reg_indirect(buf, kRax, Ind(kRbp, stack_index));
      Emit_mov_reg_imm32(buf, kRax, 0);
      Emit_setcc_imm8(buf, kEqual, kAl);
      Emit_shl_reg_imm8(buf, kRax, kBoolShift);
      Emit_or_reg_imm8(buf, kRax, kBoolTag);
      return 0;
    }
    if (AST_symbol_matches(callable, "<")) {
      _(Compile_expr(buf, operand2(args), stack_index));
      Emit_store_reg_indirect(buf, /*dst=*/Ind(kRbp, stack_index),
                              /*src=*/kRax);
      _(Compile_expr(buf, operand1(args), stack_index - kWordSize));
      Emit_cmp_reg_indirect(buf, kRax, Ind(kRbp, stack_index));
      Emit_mov_reg_imm32(buf, kRax, 0);
      Emit_setcc_imm8(buf, kLess, kAl);
      Emit_shl_reg_imm8(buf, kRax, kBoolShift);
      Emit_or_reg_imm8(buf, kRax, kBoolTag);
      return 0;
    }
  }
  assert(0 && "unexpected call type");
}

int Compile_expr(Buffer *buf, ASTNode *node, word stack_index) {
  if (AST_is_integer(node)) {
    word value = AST_get_integer(node);
    Emit_mov_reg_imm32(buf, kRax, Object_encode_integer(value));
    return 0;
  }
  if (AST_is_char(node)) {
    char value = AST_get_char(node);
    Emit_mov_reg_imm32(buf, kRax, Object_encode_char(value));
    return 0;
  }
  if (AST_is_bool(node)) {
    bool value = AST_get_bool(node);
    Emit_mov_reg_imm32(buf, kRax, Object_encode_bool(value));
    return 0;
  }
  if (AST_is_nil(node)) {
    Emit_mov_reg_imm32(buf, kRax, Object_nil());
    return 0;
  }
  if (AST_is_pair(node)) {
    return Compile_call(buf, AST_pair_car(node), AST_pair_cdr(node),
                        stack_index);
  }
  assert(0 && "unexpected node type");
}

static const byte kFunctionPrologue[] = {
    // push rbp
    0x55,
    // mov rbp, rsp
    kRexPrefix,
    0x89,
    0xe5,
};

static const byte kFunctionEpilogue[] = {
    // pop rbp
    0x5d,
    // ret
    0xc3,
};

int Compile_function(Buffer *buf, ASTNode *node) {
  Buffer_write_arr(buf, kFunctionPrologue, sizeof kFunctionPrologue);
  _(Compile_expr(buf, node, -kWordSize));
  Buffer_write_arr(buf, kFunctionEpilogue, sizeof kFunctionEpilogue);
  return 0;
}

// End Compile

typedef int (*JitFunction)();

// Testing

uword Testing_execute_expr(Buffer *buf) {
  assert(buf != NULL);
  assert(buf->address != NULL);
  assert(buf->state == kExecutable);
  // The pointer-pointer cast is allowed but the underlying
  // data-to-function-pointer back-and-forth is only guaranteed to work on
  // POSIX systems (because of eg dlsym).
  JitFunction function = *(JitFunction *)(&buf->address);
  return function();
}

TEST Testing_expect_function_has_contents(Buffer *buf, byte *arr,
                                          size_t arr_size) {
  size_t total_size =
      sizeof kFunctionPrologue + arr_size + sizeof kFunctionEpilogue;
  ASSERT_EQ(total_size, buf->len);

  byte *ptr = buf->address;
  ASSERT_MEM_EQ(kFunctionPrologue, ptr, sizeof kFunctionPrologue);
  ptr += sizeof kFunctionPrologue;
  ASSERT_MEM_EQ(arr, ptr, arr_size);
  ptr += arr_size;
  ASSERT_MEM_EQ(kFunctionEpilogue, ptr, sizeof kFunctionEpilogue);
  ptr += sizeof kFunctionEpilogue;
  PASS();
}

#define EXPECT_EQUALS_BYTES(buf, arr)                                          \
  ASSERT_MEM_EQ(arr, (buf)->address, sizeof arr)

#define EXPECT_FUNCTION_CONTAINS_CODE(buf, arr)                                \
  CHECK_CALL(Testing_expect_function_has_contents(buf, arr, sizeof arr))

#define RUN_BUFFER_TEST(test_name)                                             \
  do {                                                                         \
    Buffer buf;                                                                \
    Buffer_init(&buf, 1);                                                      \
    GREATEST_RUN_TEST1(test_name, &buf);                                       \
    Buffer_deinit(&buf);                                                       \
  } while (0)

ASTNode *list1(ASTNode *item0) { return AST_new_pair(item0, AST_nil()); }

ASTNode *list2(ASTNode *item0, ASTNode *item1) {
  return AST_new_pair(item0, list1(item1));
}

ASTNode *list3(ASTNode *item0, ASTNode *item1, ASTNode *item2) {
  return AST_new_pair(item0, list2(item1, item2));
}

ASTNode *new_unary_call(const char *name, ASTNode *arg) {
  return list2(AST_new_symbol(name), arg);
}

ASTNode *new_binary_call(const char *name, ASTNode *arg0, ASTNode *arg1) {
  return list3(AST_new_symbol(name), arg0, arg1);
}

// End Testing

// Tests

TEST encode_positive_integer(void) {
  ASSERT_EQ(Object_encode_integer(0), 0x0);
  ASSERT_EQ(Object_encode_integer(1), 0x4);
  ASSERT_EQ(Object_encode_integer(10), 0x28);
  PASS();
}

TEST encode_negative_integer(void) {
  ASSERT_EQ(Object_encode_integer(0), 0x0);
  ASSERT_EQ(Object_encode_integer(-1), 0xfffffffffffffffc);
  ASSERT_EQ(Object_encode_integer(-10), 0xffffffffffffffd8);
  PASS();
}

TEST encode_char(void) {
  ASSERT_EQ(Object_encode_char('\0'), 0xf);
  ASSERT_EQ(Object_encode_char('a'), 0x610f);
  PASS();
}

TEST decode_char(void) {
  ASSERT_EQ(Object_decode_char(0xf), '\0');
  ASSERT_EQ(Object_decode_char(0x610f), 'a');
  PASS();
}

TEST encode_bool(void) {
  ASSERT_EQ(Object_encode_bool(true), 0x9f);
  ASSERT_EQ(Object_encode_bool(false), 0x1f);
  ASSERT_EQ(Object_true(), 0x9f);
  ASSERT_EQ(Object_false(), 0x1f);
  PASS();
}

TEST decode_bool(void) {
  ASSERT_EQ(Object_decode_bool(0x9f), true);
  ASSERT_EQ(Object_decode_bool(0x1f), false);
  PASS();
}

TEST address(void) {
  ASSERT_EQ(Object_address((void *)0xFF01), 0xFF00);
  PASS();
}

TEST ast_new_pair(void) {
  ASTNode *node = AST_new_pair(NULL, NULL);
  ASSERT(AST_is_pair(node));
  AST_heap_free(node);
  PASS();
}

TEST ast_pair_car_returns_car(void) {
  ASTNode *node = AST_new_pair(AST_new_integer(123), NULL);
  ASTNode *car = AST_pair_car(node);
  ASSERT(AST_is_integer(car));
  ASSERT_EQ(Object_decode_integer((uword)car), 123);
  AST_heap_free(node);
  PASS();
}

TEST ast_pair_cdr_returns_cdr(void) {
  ASTNode *node = AST_new_pair(NULL, AST_new_integer(123));
  ASTNode *cdr = AST_pair_cdr(node);
  ASSERT(AST_is_integer(cdr));
  ASSERT_EQ(Object_decode_integer((uword)cdr), 123);
  AST_heap_free(node);
  PASS();
}

TEST ast_new_symbol(void) {
  const char *value = "my symbol";
  ASTNode *node = AST_new_symbol(value);
  ASSERT(AST_is_symbol(node));
  ASSERT_STR_EQ(AST_symbol_cstr(node), value);
  AST_heap_free(node);
  PASS();
}

#define ASSERT_IS_CHAR_EQ(node, c)                                             \
  do {                                                                         \
    ASTNode *__tmp = node;                                                     \
    if (AST_is_error(__tmp)) {                                                 \
      fprintf(stderr, "Expected a char but got an error.\n");                  \
    }                                                                          \
    ASSERT(AST_is_char(__tmp));                                                \
    ASSERT_EQ(AST_get_char(__tmp), c);                                         \
  } while (0);

#define ASSERT_IS_INT_EQ(node, val)                                            \
  do {                                                                         \
    ASTNode *__tmp = node;                                                     \
    if (AST_is_error(__tmp)) {                                                 \
      fprintf(stderr, "Expected an int but got an error.\n");                  \
    }                                                                          \
    ASSERT(AST_is_integer(__tmp));                                             \
    ASSERT_EQ(AST_get_integer(__tmp), val);                                    \
  } while (0);

#define ASSERT_IS_SYM_EQ(node, cstr)                                           \
  do {                                                                         \
    ASTNode *__tmp = node;                                                     \
    if (AST_is_error(__tmp)) {                                                 \
      fprintf(stderr, "Expected a symbol but got an error.\n");                \
    }                                                                          \
    ASSERT(AST_is_symbol(__tmp));                                              \
    ASSERT_STR_EQ(AST_symbol_cstr(__tmp), cstr);                               \
  } while (0);

TEST read_with_integer_returns_integer(void) {
  char *input = "1234";
  ASTNode *node = Reader_read(input);
  ASSERT_IS_INT_EQ(node, 1234);
  AST_heap_free(node);
  PASS();
}

TEST read_with_negative_integer_returns_integer(void) {
  char *input = "-1234";
  ASTNode *node = Reader_read(input);
  ASSERT_IS_INT_EQ(node, -1234);
  AST_heap_free(node);
  PASS();
}

TEST read_with_positive_integer_returns_integer(void) {
  char *input = "+1234";
  ASTNode *node = Reader_read(input);
  ASSERT_IS_INT_EQ(node, 1234);
  AST_heap_free(node);
  PASS();
}

TEST read_with_leading_whitespace_ignores_whitespace(void) {
  char *input = "   \t   \n  1234";
  ASTNode *node = Reader_read(input);
  ASSERT_IS_INT_EQ(node, 1234);
  AST_heap_free(node);
  PASS();
}

TEST read_with_symbol_returns_symbol(void) {
  char *input = "hello?+-*=>";
  ASTNode *node = Reader_read(input);
  ASSERT_IS_SYM_EQ(node, "hello?+-*=>");
  AST_heap_free(node);
  PASS();
}

TEST read_with_symbol_with_trailing_digits(void) {
  char *input = "add1 1";
  ASTNode *node = Reader_read(input);
  ASSERT_IS_SYM_EQ(node, "add1");
  AST_heap_free(node);
  PASS();
}

TEST read_with_char_returns_char(void) {
  char *input = "'a'";
  ASTNode *node = Reader_read(input);
  ASSERT_IS_CHAR_EQ(node, 'a');
  ASSERT(AST_is_error(Reader_read("''")));
  ASSERT(AST_is_error(Reader_read("'aa'")));
  ASSERT(AST_is_error(Reader_read("'aa")));
  AST_heap_free(node);
  PASS();
}

TEST read_with_bool_returns_bool(void) {
  ASSERT_EQ(Reader_read("#t"), AST_new_bool(true));
  ASSERT_EQ(Reader_read("#f"), AST_new_bool(false));
  ASSERT(AST_is_error(Reader_read("#")));
  ASSERT(AST_is_error(Reader_read("#x")));
  ASSERT(AST_is_error(Reader_read("##")));
  PASS();
}

TEST read_with_nil_returns_nil(void) {
  char *input = "()";
  ASTNode *node = Reader_read(input);
  ASSERT(AST_is_nil(node));
  AST_heap_free(node);
  PASS();
}

TEST read_with_list_returns_list(void) {
  char *input = "( 1 2 0 )";
  ASTNode *node = Reader_read(input);
  ASSERT(AST_is_pair(node));
  ASSERT_IS_INT_EQ(AST_pair_car(node), 1);
  ASSERT_IS_INT_EQ(AST_pair_car(AST_pair_cdr(node)), 2);
  ASSERT_IS_INT_EQ(AST_pair_car(AST_pair_cdr(AST_pair_cdr(node))), 0);
  ASSERT(AST_is_nil(AST_pair_cdr(AST_pair_cdr(AST_pair_cdr(node)))));
  AST_heap_free(node);
  PASS();
}

TEST read_with_nested_list_returns_list(void) {
  char *input = "((hello world) (foo bar))";
  ASTNode *node = Reader_read(input);
  ASSERT(AST_is_pair(node));
  ASTNode *first = AST_pair_car(node);
  ASSERT(AST_is_pair(first));
  ASSERT_IS_SYM_EQ(AST_pair_car(first), "hello");
  ASSERT_IS_SYM_EQ(AST_pair_car(AST_pair_cdr(first)), "world");
  ASSERT(AST_is_nil(AST_pair_cdr(AST_pair_cdr(first))));
  ASTNode *second = AST_pair_car(AST_pair_cdr(node));
  ASSERT(AST_is_pair(second));
  ASSERT_IS_SYM_EQ(AST_pair_car(second), "foo");
  ASSERT_IS_SYM_EQ(AST_pair_car(AST_pair_cdr(second)), "bar");
  ASSERT(AST_is_nil(AST_pair_cdr(AST_pair_cdr(second))));
  AST_heap_free(node);
  PASS();
}

TEST buffer_write8_increases_length(Buffer *buf) {
  ASSERT_EQ(buf->len, 0);
  Buffer_write8(buf, 0xdb);
  ASSERT_EQ(Buffer_at8(buf, 0), 0xdb);
  ASSERT_EQ(buf->len, 1);
  PASS();
}

TEST buffer_write8_expands_buffer(void) {
  Buffer buf;
  Buffer_init(&buf, 1);
  ASSERT_EQ(buf.capacity, 1);
  ASSERT_EQ(buf.len, 0);
  Buffer_write8(&buf, 0xdb);
  Buffer_write8(&buf, 0xef);
  ASSERT(buf.capacity > 1);
  ASSERT_EQ(buf.len, 2);
  Buffer_deinit(&buf);
  PASS();
}

TEST buffer_write32_expands_buffer(void) {
  Buffer buf;
  Buffer_init(&buf, 1);
  ASSERT_EQ(buf.capacity, 1);
  ASSERT_EQ(buf.len, 0);
  Buffer_write32(&buf, 0xdeadbeef);
  ASSERT(buf.capacity > 1);
  ASSERT_EQ(buf.len, 4);
  Buffer_deinit(&buf);
  PASS();
}

TEST buffer_write32_writes_little_endian(Buffer *buf) {
  Buffer_write32(buf, 0xdeadbeef);
  ASSERT_EQ(Buffer_at8(buf, 0), 0xef);
  ASSERT_EQ(Buffer_at8(buf, 1), 0xbe);
  ASSERT_EQ(Buffer_at8(buf, 2), 0xad);
  ASSERT_EQ(Buffer_at8(buf, 3), 0xde);
  PASS();
}

TEST compile_positive_integer(Buffer *buf) {
  word value = 123;
  ASTNode *node = AST_new_integer(value);
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov eax, imm(123)
  byte expected[] = {0x48, 0xc7, 0xc0, 0xec, 0x01, 0x00, 0x00};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(value));
  PASS();
}

TEST compile_negative_integer(Buffer *buf) {
  word value = -123;
  ASTNode *node = AST_new_integer(value);
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov eax, imm(-123)
  byte expected[] = {0x48, 0xc7, 0xc0, 0x14, 0xfe, 0xff, 0xff};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(value));
  PASS();
}

TEST compile_char(Buffer *buf) {
  char value = 'a';
  ASTNode *node = AST_new_char(value);
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov eax, imm('a')
  byte expected[] = {0x48, 0xc7, 0xc0, 0x0f, 0x61, 0x00, 0x00};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_char(value));
  PASS();
}

TEST compile_true(Buffer *buf) {
  ASTNode *node = AST_new_bool(true);
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov eax, imm(true)
  byte expected[] = {0x48, 0xc7, 0xc0, 0x9f, 0x0, 0x0, 0x0};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_true());
  PASS();
}

TEST compile_false(Buffer *buf) {
  ASTNode *node = AST_new_bool(false);
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov eax, imm(false)
  byte expected[] = {0x48, 0xc7, 0xc0, 0x1f, 0x00, 0x00, 0x00};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_false());
  PASS();
}

TEST compile_nil(Buffer *buf) {
  ASTNode *node = AST_nil();
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov eax, imm(nil)
  byte expected[] = {0x48, 0xc7, 0xc0, 0x2f, 0x00, 0x00, 0x00};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_nil());
  PASS();
}

TEST compile_unary_add1(Buffer *buf) {
  ASTNode *node = new_unary_call("add1", AST_new_integer(123));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov rax, imm(123); add rax, imm(1)
  byte expected[] = {0x48, 0xc7, 0xc0, 0xec, 0x01, 0x00, 0x00,
                     0x48, 0x05, 0x04, 0x00, 0x00, 0x00};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(124));
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_add1_nested(Buffer *buf) {
  ASTNode *node =
      new_unary_call("add1", new_unary_call("add1", AST_new_integer(123)));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov rax, imm(123); add rax, imm(1); add rax, imm(1)
  byte expected[] = {0x48, 0xc7, 0xc0, 0xec, 0x01, 0x00, 0x00, 0x48, 0x05, 0x04,
                     0x00, 0x00, 0x00, 0x48, 0x05, 0x04, 0x00, 0x00, 0x00};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(125));
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_sub1(Buffer *buf) {
  ASTNode *node = new_unary_call("sub1", AST_new_integer(123));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov rax, imm(123); sub rax, imm(1)
  byte expected[] = {0x48, 0xc7, 0xc0, 0xec, 0x01, 0x00, 0x00,
                     0x48, 0x2d, 0x04, 0x00, 0x00, 0x00};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(122));
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_integer_to_char(Buffer *buf) {
  ASTNode *node = new_unary_call("integer->char", AST_new_integer(97));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov rax, imm(97); shl rax, 6; or rax, 0xf
  byte expected[] = {0x48, 0xc7, 0xc0, 0x84, 0x01, 0x00, 0x00, 0x48,
                     0xc1, 0xe0, 0x06, 0x48, 0x83, 0xc8, 0x0f};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_char('a'));
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_char_to_integer(Buffer *buf) {
  ASTNode *node = new_unary_call("char->integer", AST_new_char('a'));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov rax, imm('a'); shr rax, 6
  byte expected[] = {0x48, 0xc7, 0xc0, 0x0f, 0x61, 0x00,
                     0x00, 0x48, 0xc1, 0xe8, 0x06};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(97));
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_nilp_with_nil_returns_true(Buffer *buf) {
  ASTNode *node = new_unary_call("nil?", AST_nil());
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // 0:  48 c7 c0 2f 00 00 00    mov    rax,0x2f
  // 7:  48 3d 2f 00 00 00       cmp    rax,0x0000002f
  // d:  48 c7 c0 00 00 00 00    mov    rax,0x0
  // 14: 0f 94 c0                sete   al
  // 17: 48 c1 e0 07             shl    rax,0x7
  // 1b: 48 83 c8 1f             or     rax,0x1f
  byte expected[] = {0x48, 0xc7, 0xc0, 0x2f, 0x00, 0x00, 0x00, 0x48,
                     0x3d, 0x2f, 0x00, 0x00, 0x00, 0x48, 0xc7, 0xc0,
                     0x00, 0x00, 0x00, 0x00, 0x0f, 0x94, 0xc0, 0x48,
                     0xc1, 0xe0, 0x07, 0x48, 0x83, 0xc8, 0x1f};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_true());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_nilp_with_non_nil_returns_false(Buffer *buf) {
  ASTNode *node = new_unary_call("nil?", AST_new_integer(5));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // 0:  48 c7 c0 14 00 00 00    mov    rax,0x14
  // 7:  48 3d 2f 00 00 00       cmp    rax,0x0000002f
  // d:  48 c7 c0 00 00 00 00    mov    rax,0x0
  // 14: 0f 94 c0                sete   al
  // 17: 48 c1 e0 07             shl    rax,0x7
  // 1b: 48 83 c8 1f             or     rax,0x1f
  byte expected[] = {0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00, 0x48,
                     0x3d, 0x2f, 0x00, 0x00, 0x00, 0x48, 0xc7, 0xc0,
                     0x00, 0x00, 0x00, 0x00, 0x0f, 0x94, 0xc0, 0x48,
                     0xc1, 0xe0, 0x07, 0x48, 0x83, 0xc8, 0x1f};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_false());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_zerop_with_zero_returns_true(Buffer *buf) {
  ASTNode *node = new_unary_call("zero?", AST_new_integer(0));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // 0:  48 c7 c0 00 00 00 00    mov    rax,0x0
  // 7:  48 3d 00 00 00 00       cmp    rax,0x00000000
  // d:  48 c7 c0 00 00 00 00    mov    rax,0x0
  // 14: 0f 94 c0                sete   al
  // 17: 48 c1 e0 07             shl    rax,0x7
  // 1b: 48 83 c8 1f             or     rax,0x1f
  byte expected[] = {0x48, 0xc7, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x48,
                     0x3d, 0x00, 0x00, 0x00, 0x00, 0x48, 0xc7, 0xc0,
                     0x00, 0x00, 0x00, 0x00, 0x0f, 0x94, 0xc0, 0x48,
                     0xc1, 0xe0, 0x07, 0x48, 0x83, 0xc8, 0x1f};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_true());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_zerop_with_non_zero_returns_false(Buffer *buf) {
  ASTNode *node = new_unary_call("zero?", AST_new_integer(5));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // 0:  48 c7 c0 14 00 00 00    mov    rax,0x14
  // 7:  48 3d 00 00 00 00       cmp    rax,0x00000000
  // d:  48 c7 c0 00 00 00 00    mov    rax,0x0
  // 14: 0f 94 c0                sete   al
  // 17: 48 c1 e0 07             shl    rax,0x7
  // 1b: 48 83 c8 1f             or     rax,0x1f
  byte expected[] = {0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00, 0x48,
                     0x3d, 0x00, 0x00, 0x00, 0x00, 0x48, 0xc7, 0xc0,
                     0x00, 0x00, 0x00, 0x00, 0x0f, 0x94, 0xc0, 0x48,
                     0xc1, 0xe0, 0x07, 0x48, 0x83, 0xc8, 0x1f};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_false());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_not_with_false_returns_true(Buffer *buf) {
  ASTNode *node = new_unary_call("not", AST_new_bool(false));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // 0:  48 c7 c0 1f 00 00 00    mov    rax,0x1f
  // 7:  48 3d 1f 00 00 00       cmp    rax,0x0000001f
  // d:  48 c7 c0 00 00 00 00    mov    rax,0x0
  // 14: 0f 94 c0                sete   al
  // 17: 48 c1 e0 07             shl    rax,0x7
  // 1b: 48 83 c8 1f             or     rax,0x1f
  byte expected[] = {0x48, 0xc7, 0xc0, 0x1f, 0x00, 0x00, 0x00, 0x48,
                     0x3d, 0x1f, 0x00, 0x00, 0x00, 0x48, 0xc7, 0xc0,
                     0x00, 0x00, 0x00, 0x00, 0x0f, 0x94, 0xc0, 0x48,
                     0xc1, 0xe0, 0x07, 0x48, 0x83, 0xc8, 0x1f};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_true());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_not_with_non_false_returns_false(Buffer *buf) {
  ASTNode *node = new_unary_call("not", AST_new_integer(5));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // 0:  48 c7 c0 14 00 00 00    mov    rax,0x14
  // 7:  48 3d 1f 00 00 00       cmp    rax,0x0000001f
  // d:  48 c7 c0 00 00 00 00    mov    rax,0x0
  // 14: 0f 94 c0                sete   al
  // 17: 48 c1 e0 07             shl    rax,0x7
  // 1b: 48 83 c8 1f             or     rax,0x1f
  byte expected[] = {0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00, 0x48,
                     0x3d, 0x1f, 0x00, 0x00, 0x00, 0x48, 0xc7, 0xc0,
                     0x00, 0x00, 0x00, 0x00, 0x0f, 0x94, 0xc0, 0x48,
                     0xc1, 0xe0, 0x07, 0x48, 0x83, 0xc8, 0x1f};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_false());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_integerp_with_integer_returns_true(Buffer *buf) {
  ASTNode *node = new_unary_call("integer?", AST_new_integer(5));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // 0:  48 c7 c0 14 00 00 00    mov    rax,0x14
  // 7:  48 83 e0 03             and    rax,0x3
  // b:  48 3d 00 00 00 00       cmp    rax,0x00000000
  // 11: 48 c7 c0 00 00 00 00    mov    rax,0x0
  // 18: 0f 94 c0                sete   al
  // 1b: 48 c1 e0 07             shl    rax,0x7
  // 1f: 48 83 c8 1f             or     rax,0x1f
  byte expected[] = {0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00, 0x48, 0x83,
                     0xe0, 0x03, 0x48, 0x3d, 0x00, 0x00, 0x00, 0x00, 0x48,
                     0xc7, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x94, 0xc0,
                     0x48, 0xc1, 0xe0, 0x07, 0x48, 0x83, 0xc8, 0x1f};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_true());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_integerp_with_non_integer_returns_false(Buffer *buf) {
  ASTNode *node = new_unary_call("integer?", AST_nil());
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // 0:  48 c7 c0 2f 00 00 00    mov    rax,0x2f
  // 7:  48 83 e0 03             and    rax,0x3
  // b:  48 3d 00 00 00 00       cmp    rax,0x00000000
  // 11: 48 c7 c0 00 00 00 00    mov    rax,0x0
  // 18: 0f 94 c0                sete   al
  // 1b: 48 c1 e0 07             shl    rax,0x7
  // 1f: 48 83 c8 1f             or     rax,0x1f
  byte expected[] = {0x48, 0xc7, 0xc0, 0x2f, 0x00, 0x00, 0x00, 0x48, 0x83,
                     0xe0, 0x03, 0x48, 0x3d, 0x00, 0x00, 0x00, 0x00, 0x48,
                     0xc7, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x94, 0xc0,
                     0x48, 0xc1, 0xe0, 0x07, 0x48, 0x83, 0xc8, 0x1f};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_false());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_booleanp_with_boolean_returns_true(Buffer *buf) {
  ASTNode *node = new_unary_call("boolean?", AST_new_bool(true));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // 0:  48 c7 c0 9f 00 00 00    mov    rax,0x9f
  // 7:  48 83 e0 3f             and    rax,0x3f
  // b:  48 3d 1f 00 00 00       cmp    rax,0x0000001f
  // 11: 48 c7 c0 00 00 00 00    mov    rax,0x0
  // 18: 0f 94 c0                sete   al
  // 1b: 48 c1 e0 07             shl    rax,0x7
  // 1f: 48 83 c8 1f             or     rax,0x1f
  byte expected[] = {0x48, 0xc7, 0xc0, 0x9f, 0x00, 0x00, 0x00, 0x48, 0x83,
                     0xe0, 0x3f, 0x48, 0x3d, 0x1f, 0x00, 0x00, 0x00, 0x48,
                     0xc7, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x94, 0xc0,
                     0x48, 0xc1, 0xe0, 0x07, 0x48, 0x83, 0xc8, 0x1f};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_true());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_booleanp_with_non_boolean_returns_false(Buffer *buf) {
  ASTNode *node = new_unary_call("boolean?", AST_new_integer(5));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // 0:  48 c7 c0 14 00 00 00    mov    rax,0x14
  // 7:  48 83 e0 3f             and    rax,0x3f
  // b:  48 3d 1f 00 00 00       cmp    rax,0x0000001f
  // 11: 48 c7 c0 00 00 00 00    mov    rax,0x0
  // 18: 0f 94 c0                sete   al
  // 1b: 48 c1 e0 07             shl    rax,0x7
  // 1f: 48 83 c8 1f             or     rax,0x1f
  byte expected[] = {0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00, 0x48, 0x83,
                     0xe0, 0x3f, 0x48, 0x3d, 0x1f, 0x00, 0x00, 0x00, 0x48,
                     0xc7, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x94, 0xc0,
                     0x48, 0xc1, 0xe0, 0x07, 0x48, 0x83, 0xc8, 0x1f};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_false());
  AST_heap_free(node);
  PASS();
}

TEST compile_binary_plus(Buffer *buf) {
  ASTNode *node = new_binary_call("+", AST_new_integer(5), AST_new_integer(8));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  byte expected[] = {
      // 0:  48 c7 c0 20 00 00 00    mov    rax,0x20
      0x48, 0xc7, 0xc0, 0x20, 0x00, 0x00, 0x00,
      // 7:  48 89 45 f8             mov    QWORD PTR [rbp-0x8],rax
      0x48, 0x89, 0x45, 0xf8,
      // b:  48 c7 c0 14 00 00 00    mov    rax,0x14
      0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00,
      // 12: 48 03 45 f8             add    rax,QWORD PTR [rbp-0x8]
      0x48, 0x03, 0x45, 0xf8};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(13));
  AST_heap_free(node);
  PASS();
}

TEST compile_binary_plus_nested(Buffer *buf) {
  ASTNode *node = new_binary_call(
      "+", new_binary_call("+", AST_new_integer(1), AST_new_integer(2)),
      new_binary_call("+", AST_new_integer(3), AST_new_integer(4)));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  byte expected[] = {
      // 4:  48 c7 c0 10 00 00 00    mov    rax,0x10
      0x48, 0xc7, 0xc0, 0x10, 0x00, 0x00, 0x00,
      // b:  48 89 45 f8             mov    QWORD PTR [rbp-0x8],rax
      0x48, 0x89, 0x45, 0xf8,
      // f:  48 c7 c0 0c 00 00 00    mov    rax,0xc
      0x48, 0xc7, 0xc0, 0x0c, 0x00, 0x00, 0x00,
      // 16: 48 03 45 f8             add    rax,QWORD PTR [rbp-0x8]
      0x48, 0x03, 0x45, 0xf8,
      // 1a: 48 89 45 f8             mov    QWORD PTR [rbp-0x8],rax
      0x48, 0x89, 0x45, 0xf8,
      // 1e: 48 c7 c0 08 00 00 00    mov    rax,0x8
      0x48, 0xc7, 0xc0, 0x08, 0x00, 0x00, 0x00,
      // 25: 48 89 45 f0             mov    QWORD PTR [rbp-0x10],rax
      0x48, 0x89, 0x45, 0xf0,
      // 29: 48 c7 c0 04 00 00 00    mov    rax,0x4
      0x48, 0xc7, 0xc0, 0x04, 0x00, 0x00, 0x00,
      // 30: 48 03 45 f0             add    rax,QWORD PTR [rbp-0x10]
      0x48, 0x03, 0x45, 0xf0,
      // 34: 48 03 45 f8             add    rax,QWORD PTR [rbp-0x8]
      0x48, 0x03, 0x45, 0xf8};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(10));
  AST_heap_free(node);
  PASS();
}

TEST compile_binary_minus(Buffer *buf) {
  ASTNode *node = new_binary_call("-", AST_new_integer(5), AST_new_integer(8));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  byte expected[] = {
      // 0:  48 c7 c0 20 00 00 00    mov    rax,0x20
      0x48, 0xc7, 0xc0, 0x20, 0x00, 0x00, 0x00,
      // 7:  48 89 45 f8             mov    QWORD PTR [rbp-0x8],rax
      0x48, 0x89, 0x45, 0xf8,
      // b:  48 c7 c0 14 00 00 00    mov    rax,0x14
      0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00,
      // 12: 48 2b 45 f8             add    rax,QWORD PTR [rbp-0x8]
      0x48, 0x2b, 0x45, 0xf8};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(-3));
  AST_heap_free(node);
  PASS();
}

TEST compile_binary_minus_nested(Buffer *buf) {
  ASTNode *node = new_binary_call(
      "-", new_binary_call("-", AST_new_integer(5), AST_new_integer(1)),
      new_binary_call("-", AST_new_integer(4), AST_new_integer(3)));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  byte expected[] = {
      // 4:  48 c7 c0 0c 00 00 00    mov    rax,0xc
      0x48, 0xc7, 0xc0, 0x0c, 0x00, 0x00, 0x00,
      // b:  48 89 45 f8             mov    QWORD PTR [rbp-0x8],rax
      0x48, 0x89, 0x45, 0xf8,
      // f:  48 c7 c0 10 00 00 00    mov    rax,0x10
      0x48, 0xc7, 0xc0, 0x10, 0x00, 0x00, 0x00,
      // 16: 48 2b 45 f8             add    rax,QWORD PTR [rbp-0x8]
      0x48, 0x2b, 0x45, 0xf8,
      // 1a: 48 89 45 f8             mov    QWORD PTR [rbp-0x8],rax
      0x48, 0x89, 0x45, 0xf8,
      // 1e: 48 c7 c0 04 00 00 00    mov    rax,0x4
      0x48, 0xc7, 0xc0, 0x04, 0x00, 0x00, 0x00,
      // 25: 48 89 45 f0             mov    QWORD PTR [rbp-0x10],rax
      0x48, 0x89, 0x45, 0xf0,
      // 29: 48 c7 c0 14 00 00 00    mov    rax,0x14
      0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00,
      // 30: 48 2b 45 f0             add    rax,QWORD PTR [rbp-0x10]
      0x48, 0x2b, 0x45, 0xf0,
      // 34: 48 2b 45 f8             add    rax,QWORD PTR [rbp-0x8]
      0x48, 0x2b, 0x45, 0xf8};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(3));
  AST_heap_free(node);
  PASS();
}

TEST compile_binary_mul(Buffer *buf) {
  ASTNode *node = new_binary_call("*", AST_new_integer(5), AST_new_integer(8));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ_FMT(Object_encode_integer(40), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}

TEST compile_binary_mul_nested(Buffer *buf) {
  ASTNode *node = new_binary_call(
      "*", new_binary_call("*", AST_new_integer(1), AST_new_integer(2)),
      new_binary_call("*", AST_new_integer(3), AST_new_integer(4)));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ_FMT(Object_encode_integer(24), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}

TEST compile_binary_eq_with_same_address_returns_true(Buffer *buf) {
  ASTNode *node = new_binary_call("=", AST_new_integer(5), AST_new_integer(5));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ_FMT(Object_true(), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}

TEST compile_binary_eq_with_different_address_returns_false(Buffer *buf) {
  ASTNode *node = new_binary_call("=", AST_new_integer(5), AST_new_integer(4));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ_FMT(Object_false(), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}

TEST compile_binary_lt_with_left_less_than_right_returns_true(Buffer *buf) {
  ASTNode *node = new_binary_call("<", AST_new_integer(-5), AST_new_integer(5));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ_FMT(Object_true(), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}

TEST compile_binary_lt_with_left_equal_to_right_returns_false(Buffer *buf) {
  ASTNode *node = new_binary_call("<", AST_new_integer(5), AST_new_integer(5));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ_FMT(Object_false(), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}

TEST compile_binary_lt_with_left_greater_than_right_returns_false(Buffer *buf) {
  ASTNode *node = new_binary_call("<", AST_new_integer(6), AST_new_integer(5));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ_FMT(Object_false(), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}

SUITE(object_tests) {
  RUN_TEST(encode_positive_integer);
  RUN_TEST(encode_negative_integer);
  RUN_TEST(encode_char);
  RUN_TEST(decode_char);
  RUN_TEST(encode_bool);
  RUN_TEST(decode_bool);
  RUN_TEST(address);
}

SUITE(ast_tests) {
  RUN_TEST(ast_new_pair);
  RUN_TEST(ast_pair_car_returns_car);
  RUN_TEST(ast_pair_cdr_returns_cdr);
  RUN_TEST(ast_new_symbol);
}

SUITE(reader_tests) {
  RUN_TEST(read_with_integer_returns_integer);
  RUN_TEST(read_with_negative_integer_returns_integer);
  RUN_TEST(read_with_positive_integer_returns_integer);
  RUN_TEST(read_with_leading_whitespace_ignores_whitespace);
  RUN_TEST(read_with_symbol_returns_symbol);
  RUN_TEST(read_with_symbol_with_trailing_digits);
  RUN_TEST(read_with_nil_returns_nil);
  RUN_TEST(read_with_list_returns_list);
  RUN_TEST(read_with_nested_list_returns_list);
  RUN_TEST(read_with_char_returns_char);
  RUN_TEST(read_with_bool_returns_bool);
}

SUITE(buffer_tests) {
  RUN_BUFFER_TEST(buffer_write8_increases_length);
  RUN_TEST(buffer_write8_expands_buffer);
  RUN_TEST(buffer_write32_expands_buffer);
  RUN_BUFFER_TEST(buffer_write32_writes_little_endian);
}

SUITE(compiler_tests) {
  RUN_BUFFER_TEST(compile_positive_integer);
  RUN_BUFFER_TEST(compile_negative_integer);
  RUN_BUFFER_TEST(compile_char);
  RUN_BUFFER_TEST(compile_true);
  RUN_BUFFER_TEST(compile_false);
  RUN_BUFFER_TEST(compile_nil);
  RUN_BUFFER_TEST(compile_unary_add1);
  RUN_BUFFER_TEST(compile_unary_add1_nested);
  RUN_BUFFER_TEST(compile_unary_sub1);
  RUN_BUFFER_TEST(compile_unary_integer_to_char);
  RUN_BUFFER_TEST(compile_unary_char_to_integer);
  RUN_BUFFER_TEST(compile_unary_nilp_with_nil_returns_true);
  RUN_BUFFER_TEST(compile_unary_nilp_with_non_nil_returns_false);
  RUN_BUFFER_TEST(compile_unary_zerop_with_zero_returns_true);
  RUN_BUFFER_TEST(compile_unary_zerop_with_non_zero_returns_false);
  RUN_BUFFER_TEST(compile_unary_not_with_false_returns_true);
  RUN_BUFFER_TEST(compile_unary_not_with_non_false_returns_false);
  RUN_BUFFER_TEST(compile_unary_integerp_with_integer_returns_true);
  RUN_BUFFER_TEST(compile_unary_integerp_with_non_integer_returns_false);
  RUN_BUFFER_TEST(compile_unary_booleanp_with_boolean_returns_true);
  RUN_BUFFER_TEST(compile_unary_booleanp_with_non_boolean_returns_false);
  RUN_BUFFER_TEST(compile_binary_plus);
  RUN_BUFFER_TEST(compile_binary_plus_nested);
  RUN_BUFFER_TEST(compile_binary_minus);
  RUN_BUFFER_TEST(compile_binary_minus_nested);
  RUN_BUFFER_TEST(compile_binary_mul);
  RUN_BUFFER_TEST(compile_binary_mul_nested);
  RUN_BUFFER_TEST(compile_binary_eq_with_same_address_returns_true);
  RUN_BUFFER_TEST(compile_binary_eq_with_different_address_returns_false);
  RUN_BUFFER_TEST(compile_binary_lt_with_left_less_than_right_returns_true);
  RUN_BUFFER_TEST(compile_binary_lt_with_left_equal_to_right_returns_false);
  RUN_BUFFER_TEST(compile_binary_lt_with_left_greater_than_right_returns_false);
}

// End Tests

typedef void (*REPL_Callback)(char *);

void print_assembly(char *line) {
  // Parse the line
  ASTNode *node = Reader_read(line);
  if (AST_is_error(node)) {
    fprintf(stderr, "Parse error.\n");
    return;
  }

  // Compile the line
  Buffer buf;
  Buffer_init(&buf, 1);
  int result = Compile_expr(&buf, node, /*stack_index=*/-kWordSize);
  AST_heap_free(node);
  if (result < 0) {
    fprintf(stderr, "Compile error.\n");
    Buffer_deinit(&buf);
    return;
  }

  // Print the assembled code
  for (size_t i = 0; i < buf.len; i++) {
    fprintf(stderr, "%.02x ", buf.address[i]);
  }
  fprintf(stderr, "\n");

  // Clean up
  Buffer_deinit(&buf);
}

int repl(REPL_Callback callback) {
  do {
    // Read a line
    fprintf(stdout, "lisp> ");
    char *line = NULL;
    size_t size = 0;
    ssize_t nchars = getline(&line, &size, stdin);
    if (nchars < 0) {
      fprintf(stderr, "Goodbye.\n");
      free(line);
      break;
    }

    callback(line);
    free(line);
  } while (true);
  return 0;
}

GREATEST_MAIN_DEFS();

int run_tests(int argc, char **argv) {
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(object_tests);
  RUN_SUITE(ast_tests);
  RUN_SUITE(reader_tests);
  RUN_SUITE(buffer_tests);
  RUN_SUITE(compiler_tests);
  GREATEST_MAIN_END();
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--repl-assembly") == 0) {
    return repl(print_assembly);
  }
  return run_tests(argc, argv);
}
