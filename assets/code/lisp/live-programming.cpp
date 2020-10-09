// vim: set tabstop=2 shiftwidth=2 textwidth=79 expandtab:
// gcc -O2 -g -Wall -Wextra -pedantic -fno-strict-aliasing
//   assets/code/lisp/compiling-if.c

// In general: https://course.ccs.neu.edu/cs4410sp20/#%28part._lectures%29
// https://course.ccs.neu.edu/cs4410sp20/lec_let-and-stack_notes.html#%28part._let._.Growing_the_language__adding_let%29

#define _GNU_SOURCE
#include <assert.h> // for assert
#include <ctype.h>
#include <stdbool.h> // for bool
#include <stddef.h>  // for NULL
#include <stdint.h>  // for int32_t, etc
#include <stdio.h>   // for getline, fprintf
#include <stdlib.h>
#include <string.h>   // for memcpy
#include <sys/mman.h> // for mmap
#undef _GNU_SOURCE

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl.h"
#include "imgui_memory_editor.h"
#include <SDL.h>
#include <stdio.h>

// About Desktop OpenGL function loaders:
//  Modern desktop OpenGL doesn't have a standard portable header file to load
//  OpenGL function pointers. Helper libraries are often used for this purpose!
//  Here we are supporting a few common ones (gl3w, glew, glad). You may use
//  another loader/header of your choice (glext, glLoadGen, etc.), or chose to
//  manually implement your own.
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
#include <GL/gl3w.h> // Initialize with gl3wInit()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
#include <GL/glew.h> // Initialize with glewInit()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
#include <glad/glad.h> // Initialize with gladLoadGL()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD2)
#include <glad/gl.h> // Initialize with gladLoadGL(...) or gladLoaderLoadGL()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING2)
#define GLFW_INCLUDE_NONE // GLFW including OpenGL headers causes ambiguity or
                          // multiple definition errors.
#include <glbinding/Binding.h> // Initialize with glbinding::Binding::initialize()
#include <glbinding/gl/gl.h>
using namespace gl;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING3)
#define GLFW_INCLUDE_NONE // GLFW including OpenGL headers causes ambiguity or
                          // multiple definition errors.
#include <glbinding/gl/gl.h>
#include <glbinding/glbinding.h> // Initialize with glbinding::initialize()
using namespace gl;
#else
#include IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#endif

#define WARN_UNUSED __attribute__((warn_unused_result))

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

static const int kCarOffset = 0;
static const int kCdrOffset = 1;

uword Object_encode_integer(word value) {
  assert(value < kIntegerMax && "too big");
  assert(value > kIntegerMin && "too small");
  return value << kIntegerShift;
}

word Object_decode_integer(uword value) { return (word)value >> kIntegerShift; }

bool Object_is_integer(uword value) {
  return (value & kIntegerTagMask) == kIntegerTag;
}

uword Object_encode_char(char value) {
  return ((uword)value << kCharShift) | kCharTag;
}

char Object_decode_char(uword value) {
  return (value >> kCharShift) & kCharMask;
}

bool Object_is_char(uword value) {
  return (value & kImmediateTagMask) == kCharTag;
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

bool Object_is_pair(uword value) { return (value & kHeapTagMask) == kPairTag; }

uword Object_pair_car(uword value) {
  assert(Object_is_pair(value));
  return ((uword *)Object_address((void *)value))[kCarOffset];
}

uword Object_pair_cdr(uword value) {
  assert(Object_is_pair(value));
  return ((uword *)Object_address((void *)value))[kCdrOffset];
}

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
  word len;
  word capacity;
} Buffer;

byte *Buffer_alloc_writable(word capacity) {
  byte *result = reinterpret_cast<byte *>(mmap(/*addr=*/NULL, capacity,
                                               PROT_READ | PROT_WRITE,
                                               MAP_ANONYMOUS | MAP_PRIVATE,
                                               /*filedes=*/-1, /*off=*/0));
  assert(result != MAP_FAILED);
  return result;
}

void Buffer_init(Buffer *result, word capacity) {
  result->address = Buffer_alloc_writable(capacity);
  assert(result->address != MAP_FAILED);
  result->state = kWritable;
  result->len = 0;
  result->capacity = capacity;
}

word Buffer_len(Buffer *buf) { return buf->len; }

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

byte Buffer_at8(Buffer *buf, word pos) { return buf->address[pos]; }

void Buffer_at_put8(Buffer *buf, word pos, byte b) { buf->address[pos] = b; }

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
  for (uword i = 0; i < sizeof(value); i++) {
    Buffer_write8(buf, (value >> (i * kBitsPerByte)) & 0xff);
  }
}

void Buffer_at_put32(Buffer *buf, word offset, int32_t value) {
  for (uword i = 0; i < sizeof(value); i++) {
    Buffer_at_put8(buf, offset + i, (value >> (i * kBitsPerByte)) & 0xff);
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

// mov dst, [src+disp]
// or
// mov disp(%src), %dst
void Emit_load_reg_indirect(Buffer *buf, Register dst, Indirect src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x8b);
  Buffer_write8(buf, 0x40 + dst * 8 + src.reg);
  Buffer_write8(buf, disp8(src.disp));
}

static uint32_t disp32(int32_t disp) {
  return disp >= 0 ? disp : 0x100000000 + disp;
}

word Emit_jcc(Buffer *buf, Condition cond, int32_t offset) {
  Buffer_write8(buf, 0x0f);
  Buffer_write8(buf, 0x80 + cond);
  word pos = Buffer_len(buf);
  Buffer_write32(buf, disp32(offset));
  return pos;
}

word Emit_jmp(Buffer *buf, int32_t offset) {
  Buffer_write8(buf, 0xe9);
  word pos = Buffer_len(buf);
  Buffer_write32(buf, disp32(offset));
  return pos;
}

void Emit_backpatch_imm32(Buffer *buf, int32_t target_pos) {
  word current_pos = Buffer_len(buf);
  word relative_pos = current_pos - target_pos - sizeof(int32_t);
  Buffer_at_put32(buf, target_pos, disp32(relative_pos));
}

void Emit_mov_reg_reg(Buffer *buf, Register dst, Register src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x89);
  Buffer_write8(buf, 0xc0 + src * 8 + dst);
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
  char *buf = reinterpret_cast<char *>(malloc(size + 1));
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
  case '<':
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
  if (car == AST_error())
    return car;
  ASTNode *cdr = read_list(input, pos);
  if (cdr == AST_error())
    return cdr;
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

// Env

typedef struct Env {
  const char *name;
  word value;
  struct Env *prev;
} Env;

Env Env_bind(const char *name, word value, Env *prev) {
  return (Env){.name = name, .value = value, .prev = prev};
}

bool Env_find(Env *env, const char *key, word *result) {
  if (env == NULL)
    return false;
  if (strcmp(env->name, key) == 0) {
    *result = env->value;
    return true;
  }
  return Env_find(env->prev, key, result);
}

// End Env

// Compile

WARN_UNUSED int Compile_expr(Buffer *buf, ASTNode *node, word stack_index,
                             Env *varenv);

ASTNode *operand1(ASTNode *args) { return AST_pair_car(args); }

ASTNode *operand2(ASTNode *args) { return AST_pair_car(AST_pair_cdr(args)); }

ASTNode *operand3(ASTNode *args) {
  return AST_pair_car(AST_pair_cdr(AST_pair_cdr(args)));
}

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

// This is let, not let*. Therefore we keep track of two environments -- the
// parent environment, for evaluating the bindings, and the body environment,
// which will have all of the bindings in addition to the parent. This makes
// programs like (let ((a 1) (b a)) b) fail.
WARN_UNUSED int Compile_let(Buffer *buf, ASTNode *bindings, ASTNode *body,
                            word stack_index, Env *binding_env, Env *body_env) {
  if (AST_is_nil(bindings)) {
    // Base case: no bindings. Compile the body
    _(Compile_expr(buf, body, stack_index, body_env));
    return 0;
  }
  assert(AST_is_pair(bindings));
  // Get the next binding
  ASTNode *binding = AST_pair_car(bindings);
  ASTNode *name = AST_pair_car(binding);
  assert(AST_is_symbol(name));
  ASTNode *binding_expr = AST_pair_car(AST_pair_cdr(binding));
  // Compile the binding expression
  _(Compile_expr(buf, binding_expr, stack_index, binding_env));
  Emit_store_reg_indirect(buf, /*dst=*/Ind(kRbp, stack_index),
                          /*src=*/kRax);
  // Bind the name
  Env entry = Env_bind(AST_symbol_cstr(name), stack_index, body_env);
  _(Compile_let(buf, AST_pair_cdr(bindings), body, stack_index - kWordSize,
                /*binding_env=*/binding_env, /*body_env=*/&entry));
  return 0;
}

const word kLabelPlaceholder = 0xdeadbeef;

WARN_UNUSED int Compile_if(Buffer *buf, ASTNode *cond, ASTNode *consequent,
                           ASTNode *alternate, word stack_index, Env *varenv) {
  _(Compile_expr(buf, cond, stack_index, varenv));
  Emit_cmp_reg_imm32(buf, kRax, Object_false());
  word alternate_pos = Emit_jcc(buf, kEqual, kLabelPlaceholder); // je alternate
  _(Compile_expr(buf, consequent, stack_index, varenv));
  word end_pos = Emit_jmp(buf, kLabelPlaceholder); // jmp end
  Emit_backpatch_imm32(buf, alternate_pos);        // alternate:
  _(Compile_expr(buf, alternate, stack_index, varenv));
  Emit_backpatch_imm32(buf, end_pos); // end:
  return 0;
}

WARN_UNUSED int Compile_cons(Buffer *buf, ASTNode *car, ASTNode *cdr,
                             int stack_index, Env *varenv) {
  // Compile and store car
  _(Compile_expr(buf, car, stack_index, varenv));
  Emit_store_reg_indirect(buf, /*dst=*/Ind(kRsi, kCarOffset * kWordSize),
                          /*src=*/kRax);
  // Compile and store cdr
  _(Compile_expr(buf, cdr, stack_index, varenv));
  Emit_store_reg_indirect(buf, /*dst=*/Ind(kRsi, kCdrOffset * kWordSize),
                          /*src=*/kRax);
  // Store tagged pointer in rax
  Emit_mov_reg_reg(buf, /*dst=*/kRax, /*src=*/kRsi);
  Emit_or_reg_imm8(buf, /*dst=*/kRax, 1);
  // Bump the heap pointer
  Emit_add_reg_imm32(buf, /*dst=*/kRsi, 2 * kWordSize);
  return 0;
}

WARN_UNUSED int Compile_call(Buffer *buf, ASTNode *callable, ASTNode *args,
                             word stack_index, Env *varenv) {
  if (AST_is_symbol(callable)) {
    if (AST_symbol_matches(callable, "add1")) {
      _(Compile_expr(buf, operand1(args), stack_index, varenv));
      Emit_add_reg_imm32(buf, kRax, Object_encode_integer(1));
      return 0;
    }
    if (AST_symbol_matches(callable, "sub1")) {
      _(Compile_expr(buf, operand1(args), stack_index, varenv));
      Emit_sub_reg_imm32(buf, kRax, Object_encode_integer(1));
      return 0;
    }
    if (AST_symbol_matches(callable, "integer->char")) {
      _(Compile_expr(buf, operand1(args), stack_index, varenv));
      Emit_shl_reg_imm8(buf, kRax, kCharShift - kIntegerShift);
      Emit_or_reg_imm8(buf, kRax, kCharTag);
      return 0;
    }
    if (AST_symbol_matches(callable, "char->integer")) {
      _(Compile_expr(buf, operand1(args), stack_index, varenv));
      Emit_shr_reg_imm8(buf, kRax, kCharShift - kIntegerShift);
      return 0;
    }
    if (AST_symbol_matches(callable, "nil?")) {
      _(Compile_expr(buf, operand1(args), stack_index, varenv));
      Compile_compare_imm32(buf, Object_nil());
      return 0;
    }
    if (AST_symbol_matches(callable, "zero?")) {
      _(Compile_expr(buf, operand1(args), stack_index, varenv));
      Compile_compare_imm32(buf, Object_encode_integer(0));
      return 0;
    }
    if (AST_symbol_matches(callable, "not")) {
      _(Compile_expr(buf, operand1(args), stack_index, varenv));
      // All non #f values are truthy
      // ...this might be a problem if we want to make nil falsey
      Compile_compare_imm32(buf, Object_false());
      return 0;
    }
    if (AST_symbol_matches(callable, "integer?")) {
      _(Compile_expr(buf, operand1(args), stack_index, varenv));
      Emit_and_reg_imm8(buf, kRax, kIntegerTagMask);
      Compile_compare_imm32(buf, kIntegerTag);
      return 0;
    }
    if (AST_symbol_matches(callable, "boolean?")) {
      _(Compile_expr(buf, operand1(args), stack_index, varenv));
      Emit_and_reg_imm8(buf, kRax, kImmediateTagMask);
      Compile_compare_imm32(buf, kBoolTag);
      return 0;
    }
    if (AST_symbol_matches(callable, "+")) {
      _(Compile_expr(buf, operand2(args), stack_index, varenv));
      Emit_store_reg_indirect(buf, /*dst=*/Ind(kRbp, stack_index),
                              /*src=*/kRax);
      _(Compile_expr(buf, operand1(args), stack_index - kWordSize, varenv));
      Emit_add_reg_indirect(buf, /*dst=*/kRax, /*src=*/Ind(kRbp, stack_index));
      return 0;
    }
    if (AST_symbol_matches(callable, "-")) {
      _(Compile_expr(buf, operand2(args), stack_index, varenv));
      Emit_store_reg_indirect(buf, /*dst=*/Ind(kRbp, stack_index),
                              /*src=*/kRax);
      _(Compile_expr(buf, operand1(args), stack_index - kWordSize, varenv));
      Emit_sub_reg_indirect(buf, /*dst=*/kRax, /*src=*/Ind(kRbp, stack_index));
      return 0;
    }
    if (AST_symbol_matches(callable, "*")) {
      _(Compile_expr(buf, operand2(args), stack_index, varenv));
      // Remove the tag so that the result is still only tagged with 0b00
      // instead of 0b0000
      Emit_shr_reg_imm8(buf, kRax, kIntegerShift);
      Emit_store_reg_indirect(buf, /*dst=*/Ind(kRbp, stack_index),
                              /*src=*/kRax);
      _(Compile_expr(buf, operand1(args), stack_index - kWordSize, varenv));
      Emit_mul_reg_indirect(buf, /*src=*/Ind(kRbp, stack_index));
      return 0;
    }
    if (AST_symbol_matches(callable, "=")) {
      _(Compile_expr(buf, operand2(args), stack_index, varenv));
      Emit_store_reg_indirect(buf, /*dst=*/Ind(kRbp, stack_index),
                              /*src=*/kRax);
      _(Compile_expr(buf, operand1(args), stack_index - kWordSize, varenv));
      Emit_cmp_reg_indirect(buf, kRax, Ind(kRbp, stack_index));
      Emit_mov_reg_imm32(buf, kRax, 0);
      Emit_setcc_imm8(buf, kEqual, kAl);
      Emit_shl_reg_imm8(buf, kRax, kBoolShift);
      Emit_or_reg_imm8(buf, kRax, kBoolTag);
      return 0;
    }
    if (AST_symbol_matches(callable, "<")) {
      _(Compile_expr(buf, operand2(args), stack_index, varenv));
      Emit_store_reg_indirect(buf, /*dst=*/Ind(kRbp, stack_index),
                              /*src=*/kRax);
      _(Compile_expr(buf, operand1(args), stack_index - kWordSize, varenv));
      Emit_cmp_reg_indirect(buf, kRax, Ind(kRbp, stack_index));
      Emit_mov_reg_imm32(buf, kRax, 0);
      Emit_setcc_imm8(buf, kLess, kAl);
      Emit_shl_reg_imm8(buf, kRax, kBoolShift);
      Emit_or_reg_imm8(buf, kRax, kBoolTag);
      return 0;
    }
    if (AST_symbol_matches(callable, "let")) {
      return Compile_let(buf, /*bindings=*/operand1(args),
                         /*body=*/operand2(args), stack_index,
                         /*binding_env=*/varenv,
                         /*body_env=*/varenv);
    }
    if (AST_symbol_matches(callable, "if")) {
      return Compile_if(buf, /*condition=*/operand1(args),
                        /*consequent=*/operand2(args),
                        /*alternate=*/operand3(args), stack_index, varenv);
    }
    if (AST_symbol_matches(callable, "cons")) {
      return Compile_cons(buf, /*car=*/operand1(args), /*cdr=*/operand2(args),
                          stack_index, varenv);
    }
    if (AST_symbol_matches(callable, "car")) {
      _(Compile_expr(buf, operand1(args), stack_index, varenv));
      Emit_load_reg_indirect(
          buf, /*dst=*/kRax,
          /*src=*/Ind(kRax, (kCarOffset * kWordSize) - kPairTag));
      return 0;
    }
    if (AST_symbol_matches(callable, "cdr")) {
      _(Compile_expr(buf, operand1(args), stack_index, varenv));
      Emit_load_reg_indirect(
          buf, /*dst=*/kRax,
          /*src=*/Ind(kRax, (kCdrOffset * kWordSize) - kPairTag));
      return 0;
    }
  }
  assert(0 && "unexpected call type");
}

WARN_UNUSED int Compile_expr(Buffer *buf, ASTNode *node, word stack_index,
                             Env *varenv) {
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
                        stack_index, varenv);
  }
  if (AST_is_symbol(node)) {
    const char *symbol = AST_symbol_cstr(node);
    word value;
    if (Env_find(varenv, symbol, &value)) {
      Emit_load_reg_indirect(buf, /*dst=*/kRax, /*src=*/Ind(kRbp, value));
      return 0;
    }
    return -1;
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

WARN_UNUSED int Compile_function(Buffer *buf, ASTNode *node) {
  Buffer_write_arr(buf, kFunctionPrologue, sizeof kFunctionPrologue);
  _(Compile_expr(buf, node, -kWordSize, /*varenv=*/NULL));
  Buffer_write_arr(buf, kFunctionEpilogue, sizeof kFunctionEpilogue);
  return 0;
}

static const byte kEntryPrologue[] = {
    // Save the heap in rsi, our global heap pointer
    // mov rsi, rdi
    0x48,
    0x89,
    0xfe,
};

WARN_UNUSED int Compile_entry(Buffer *buf, ASTNode *node) {
  Buffer_write_arr(buf, kEntryPrologue, sizeof kEntryPrologue);
  return Compile_function(buf, node);
}

// End Compile

typedef uword (*JitFunction)();
typedef uword (*JitEntry)(uword *heap);

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

uword Testing_execute_entry(Buffer *buf, uword *heap) {
  assert(buf != NULL);
  assert(buf->address != NULL);
  assert(buf->state == kExecutable);
  // The pointer-pointer cast is allowed but the underlying
  // data-to-function-pointer back-and-forth is only guaranteed to work on
  // POSIX systems (because of eg dlsym).
  JitEntry function = *(JitEntry *)(&buf->address);
  return function(heap);
}

int live() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) !=
      0) {
    printf("Error: %s\n", SDL_GetError());
    return -1;
  }

  // Decide GL+GLSL versions
#ifdef __APPLE__
  // GL 3.2 Core + GLSL 150
  const char *glsl_version = "#version 150";
  SDL_GL_SetAttribute(
      SDL_GL_CONTEXT_FLAGS,
      SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
  // GL 3.0 + GLSL 130
  const char *glsl_version = "#version 130";
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

  // Create window with graphics context
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_WindowFlags window_flags = (SDL_WindowFlags)(
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Window *window = SDL_CreateWindow(
      "Dear ImGui SDL2+OpenGL3 example", SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
  SDL_GLContext gl_context = SDL_GL_CreateContext(window);
  SDL_GL_MakeCurrent(window, gl_context);
  SDL_GL_SetSwapInterval(1); // Enable vsync

  // Initialize OpenGL loader
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
  bool err = gl3wInit() != 0;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
  bool err = glewInit() != GLEW_OK;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
  bool err = gladLoadGL() == 0;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD2)
  bool err = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress) ==
             0; // glad2 recommend using the windowing library loader instead of
                // the (optionally) bundled one.
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING2)
  bool err = false;
  glbinding::Binding::initialize();
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING3)
  bool err = false;
  glbinding::initialize([](const char *name) {
    return (glbinding::ProcAddress)SDL_GL_GetProcAddress(name);
  });
#else
  bool err = false; // If you use IMGUI_IMPL_OPENGL_LOADER_CUSTOM, your loader
                    // is likely to requires some form of initialization.
#endif
  if (err) {
    fprintf(stderr, "Failed to initialize OpenGL loader!\n");
    return 1;
  }

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable
  // Keyboard Controls io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; //
  // Enable Gamepad Controls

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  // ImGui::StyleColorsClassic();

  // Setup Platform/Renderer bindings
  ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Load Fonts
  // - If no fonts are loaded, dear imgui will use the default font. You can
  // also load multiple fonts and use ImGui::PushFont()/PopFont() to select
  // them.
  // - AddFontFromFileTTF() will return the ImFont* so you can store it if you
  // need to select the font among multiple.
  // - If the file cannot be loaded, the function will return NULL. Please
  // handle those errors in your application (e.g. use an assertion, or display
  // an error and quit).
  // - The fonts will be rasterized at a given size (w/ oversampling) and stored
  // into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which
  // ImGui_ImplXXXX_NewFrame below will call.
  // - Read 'docs/FONTS.md' for more instructions and details.
  // - Remember that in C/C++ if you want to include a backslash \ in a string
  // literal you need to write a double backslash \\ !
  // io.Fonts->AddFontDefault();
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
  // ImFont* font =
  // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f,
  // NULL, io.Fonts->GetGlyphRangesJapanese()); IM_ASSERT(font != NULL);

  // Our state
  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  // Main loop
  bool done = false;
  ASTNode *node = NULL;
  Buffer buf;
  Buffer_init(&buf, 1);
  int compile_result = 0;
  MemoryEditor compiled_code;
  compiled_code.ReadOnly = true;
  bool appearing = true;
  uword *heap = reinterpret_cast<uword*>(malloc(1000 * kWordSize));
  uword execute_result = Object_error();
  while (!done) {
    // Poll and handle events (inputs, window resize, etc.)
    // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to
    // tell if dear imgui wants to use your inputs.
    // - When io.WantCaptureMouse is true, do not dispatch mouse input data to
    // your main application.
    // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input
    // data to your main application. Generally you may always pass all inputs
    // to dear imgui, and hide them from your application based on those two
    // flags.
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT)
        done = true;
      if (event.type == SDL_WINDOWEVENT &&
          event.window.event == SDL_WINDOWEVENT_CLOSE &&
          event.window.windowID == SDL_GetWindowID(window))
        done = true;
    }

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame(window);
    ImGui::NewFrame();

    // 2. Show a simple window that we create ourselves. We use a Begin/End pair
    // to created a named window.
    {
      ImGui::Begin("Live programming environment");

      ImGui::Text("Program");
      static char str0[128] = "(+ 1 2)";
      bool edited = ImGui::InputText("", str0, IM_ARRAYSIZE(str0));
      if (edited || appearing) {
        AST_heap_free(node);
        node = Reader_read(str0);

        Buffer_deinit(&buf);
        Buffer_init(&buf, 1);
        if (!AST_is_error(node)) {
          compile_result = Compile_entry(&buf, node);
          if (compile_result == 0) {
            Buffer_make_executable(&buf);
            execute_result = Testing_execute_entry(&buf, heap);
          }
        }
        appearing = false;
      }

      ImGui::Spacing();

      ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
      if (ImGui::TreeNode("AST")) {
        ImGui::Text("Result: %p", reinterpret_cast<void *>(node));
        ImGui::TreePop();
      }

      ImGui::Spacing();

      ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
      if (ImGui::TreeNode("Executed code")) {
        ImGui::Text("Result: %lld", Object_decode_integer(execute_result));
        ImGui::TreePop();
      }

      ImGui::Spacing();

      ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
      if (ImGui::TreeNode("Compiled code")) {
      ImGui::BeginChild("Compiled code");
      if (AST_is_error(node)) {
        ImGui::Text("Parse error");
      } else if (compile_result != 0) {
        ImGui::Text("Compile error");
      } else {
        compiled_code.DrawContents(buf.address, Buffer_len(&buf));
      }
      ImGui::EndChild();
        ImGui::TreePop();
      }

      // ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
      //             1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
      ImGui::End();
    }

    // {
    //   ImGui::Begin("Compiled code");
    //   if (AST_is_error(node)) {
    //     ImGui::Text("Parse error");
    //   } else if (compile_result != 0) {
    //     ImGui::Text("Compile error");
    //   } else {
    //     compiled_code.DrawContents(buf.address, Buffer_len(&buf));
    //   }
    //   ImGui::End();
    // }

    // Rendering
    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
  }

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_GL_DeleteContext(gl_context);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}

int main() { return live(); }
