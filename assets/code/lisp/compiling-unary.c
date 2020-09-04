// vim: set tabstop=2 shiftwidth=2 textwidth=79 expandtab:
// gcc -O2 -g -Wall -Wextra -pedantic -fno-strict-aliasing
//   assets/code/lisp/compiling-unary.c

#include <assert.h>   // for assert
#include <stdbool.h>  // for bool
#include <stddef.h>   // for NULL
#include <stdint.h>   // for int32_t, etc
#include <string.h>   // for memcpy
#include <sys/mman.h> // for mmap

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

const unsigned int kPairTag = 0x1;        // 0b001
const unsigned int kSymbolTag = 0x5;      // 0b101
const uword kHeapTagMask = ((uword)0x7);  // 0b000...001
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

uword Object_nil() { return 0x2f; }

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
  munmap(buf->address, buf->len);
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
  int result = munmap(buf->address, buf->len);
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
  // TODO(max): Add more
} Condition;

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

// End AST

// Compile

int Compile_expr(Buffer *buf, ASTNode *node);

ASTNode *operand1(ASTNode *args) { return AST_pair_car(args); }

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

int Compile_call(Buffer *buf, ASTNode *callable, ASTNode *args) {
  assert(AST_pair_cdr(args) == AST_nil() &&
         "only unary function calls supported");
  if (AST_is_symbol(callable)) {
    if (AST_symbol_matches(callable, "add1")) {
      _(Compile_expr(buf, operand1(args)));
      Emit_add_reg_imm32(buf, kRax, Object_encode_integer(1));
      return 0;
    }
    if (AST_symbol_matches(callable, "sub1")) {
      _(Compile_expr(buf, operand1(args)));
      Emit_sub_reg_imm32(buf, kRax, Object_encode_integer(1));
      return 0;
    }
    if (AST_symbol_matches(callable, "integer->char")) {
      _(Compile_expr(buf, operand1(args)));
      Emit_shl_reg_imm8(buf, kRax, kCharShift - kIntegerShift);
      Emit_or_reg_imm8(buf, kRax, kCharTag);
      return 0;
    }
    if (AST_symbol_matches(callable, "char->integer")) {
      _(Compile_expr(buf, operand1(args)));
      Emit_shr_reg_imm8(buf, kRax, kCharShift - kIntegerShift);
      return 0;
    }
    if (AST_symbol_matches(callable, "nil?")) {
      _(Compile_expr(buf, operand1(args)));
      Compile_compare_imm32(buf, Object_nil());
      return 0;
    }
    if (AST_symbol_matches(callable, "zero?")) {
      _(Compile_expr(buf, operand1(args)));
      Compile_compare_imm32(buf, Object_encode_integer(0));
      return 0;
    }
    if (AST_symbol_matches(callable, "not")) {
      _(Compile_expr(buf, operand1(args)));
      // All non #f values are truthy
      // ...this might be a problem if we want to make nil falsey
      Compile_compare_imm32(buf, Object_false());
      return 0;
    }
    if (AST_symbol_matches(callable, "integer?")) {
      _(Compile_expr(buf, operand1(args)));
      Emit_and_reg_imm8(buf, kRax, kIntegerTagMask);
      Compile_compare_imm32(buf, kIntegerTag);
      return 0;
    }
    if (AST_symbol_matches(callable, "boolean?")) {
      _(Compile_expr(buf, operand1(args)));
      Emit_and_reg_imm8(buf, kRax, kImmediateTagMask);
      Compile_compare_imm32(buf, kBoolTag);
      return 0;
    }
  }
  assert(0 && "unexpected call type");
}

int Compile_expr(Buffer *buf, ASTNode *node) {
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
    return Compile_call(buf, AST_pair_car(node), AST_pair_cdr(node));
  }
  assert(0 && "unexpected node type");
}

int Compile_function(Buffer *buf, ASTNode *node) {
  _(Compile_expr(buf, node));
  Emit_ret(buf);
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

void Testing_print_hex_array(FILE *fp, byte *arr, size_t arr_size) {
  for (size_t i = 0; i < arr_size; i++) {
    fprintf(fp, "%.2x ", arr[i]);
  }
}

#define EXPECT_EQUALS_BYTES(buf, arr)                                          \
  ASSERT_MEM_EQ(arr, (buf)->address, sizeof arr)

#define RUN_BUFFER_TEST(test_name)                                             \
  do {                                                                         \
    Buffer buf;                                                                \
    Buffer_init(&buf, 1);                                                      \
    GREATEST_RUN_TEST1(test_name, &buf);                                       \
    Buffer_deinit(&buf);                                                       \
  } while (0)

ASTNode *Testing_list1(ASTNode *item0) {
  return AST_new_pair(item0, AST_nil());
}

ASTNode *Testing_list2(ASTNode *item0, ASTNode *item1) {
  return AST_new_pair(item0, Testing_list1(item1));
}

ASTNode *Testing_unary_call(const char *name, ASTNode *arg) {
  return Testing_list2(AST_new_symbol(name), arg);
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
  // mov eax, imm(123); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0xec, 0x01, 0x00, 0x00, 0xc3};
  EXPECT_EQUALS_BYTES(buf, expected);
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
  // mov eax, imm(-123); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0x14, 0xfe, 0xff, 0xff, 0xc3};
  EXPECT_EQUALS_BYTES(buf, expected);
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
  // mov eax, imm('a'); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0x0f, 0x61, 0x00, 0x00, 0xc3};
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_char(value));
  PASS();
}

TEST compile_true(Buffer *buf) {
  ASTNode *node = AST_new_bool(true);
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov eax, imm(true); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0x9f, 0x0, 0x0, 0x0, 0xc3};
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_true());
  PASS();
}

TEST compile_false(Buffer *buf) {
  ASTNode *node = AST_new_bool(false);
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov eax, imm(false); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0x1f, 0x00, 0x00, 0x00, 0xc3};
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_false());
  PASS();
}

TEST compile_nil(Buffer *buf) {
  ASTNode *node = AST_nil();
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov eax, imm(nil); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0x2f, 0x00, 0x00, 0x00, 0xc3};
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_nil());
  PASS();
}

TEST compile_unary_add1(Buffer *buf) {
  ASTNode *node = Testing_unary_call("add1", AST_new_integer(123));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov rax, imm(123); add rax, imm(1); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0xec, 0x01, 0x00, 0x00,
                     0x48, 0x05, 0x04, 0x00, 0x00, 0x00, 0xc3};
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(124));
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_sub1(Buffer *buf) {
  ASTNode *node = Testing_unary_call("sub1", AST_new_integer(123));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov rax, imm(123); sub rax, imm(1); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0xec, 0x01, 0x00, 0x00,
                     0x48, 0x2d, 0x04, 0x00, 0x00, 0x00, 0xc3};
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(122));
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_integer_to_char(Buffer *buf) {
  ASTNode *node = Testing_unary_call("integer->char", AST_new_integer(97));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov rax, imm(97); shl rax, 6; or rax, 0xf; ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0x84, 0x01, 0x00, 0x00, 0x48,
                     0xc1, 0xe0, 0x06, 0x48, 0x83, 0xc8, 0xf,  0xc3};
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_char('a'));
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_char_to_integer(Buffer *buf) {
  ASTNode *node = Testing_unary_call("char->integer", AST_new_char('a'));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov rax, imm('a'); shr rax, 6; ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0x0f, 0x61, 0x00,
                     0x00, 0x48, 0xc1, 0xe8, 0x06, 0xc3};
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(97));
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_nilp_with_nil_returns_true(Buffer *buf) {
  ASTNode *node = Testing_unary_call("nil?", AST_nil());
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
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_true());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_nilp_with_non_nil_returns_false(Buffer *buf) {
  ASTNode *node = Testing_unary_call("nil?", AST_new_integer(5));
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
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_false());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_zerop_with_zero_returns_true(Buffer *buf) {
  ASTNode *node = Testing_unary_call("zero?", AST_new_integer(0));
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
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_true());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_zerop_with_non_zero_returns_false(Buffer *buf) {
  ASTNode *node = Testing_unary_call("zero?", AST_new_integer(5));
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
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_false());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_not_with_false_returns_true(Buffer *buf) {
  ASTNode *node = Testing_unary_call("not", AST_new_bool(false));
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
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_true());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_not_with_non_false_returns_false(Buffer *buf) {
  ASTNode *node = Testing_unary_call("not", AST_new_integer(5));
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
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_false());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_integerp_with_integer_returns_true(Buffer *buf) {
  ASTNode *node = Testing_unary_call("integer?", AST_new_integer(5));
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
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_true());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_integerp_with_non_integer_returns_false(Buffer *buf) {
  ASTNode *node = Testing_unary_call("integer?", AST_nil());
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
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_false());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_booleanp_with_boolean_returns_true(Buffer *buf) {
  ASTNode *node = Testing_unary_call("boolean?", AST_new_bool(true));
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
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_true());
  AST_heap_free(node);
  PASS();
}

TEST compile_unary_booleanp_with_non_boolean_returns_false(Buffer *buf) {
  ASTNode *node = Testing_unary_call("boolean?", AST_new_integer(5));
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
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_false());
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
}

// End Tests

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(object_tests);
  RUN_SUITE(ast_tests);
  RUN_SUITE(buffer_tests);
  RUN_SUITE(compiler_tests);
  GREATEST_MAIN_END();
}
