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

const unsigned int kIntegerTag = 0x0;
const unsigned int kIntegerMask = 0x3;
const unsigned int kIntegerShift = 2;
const unsigned int kIntegerBits = kBitsPerWord - kIntegerShift;
const word kIntegerMax = (1LL << (kIntegerBits - 1)) - 1;
const word kIntegerMin = -(1LL << (kIntegerBits - 1));

word Object_encode_integer(word value) {
  assert(value < kIntegerMax && "too big");
  assert(value > kIntegerMin && "too small");
  return value << kIntegerShift;
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

static const byte kRexPrefix = 0x48;

void Emit_mov_reg_imm32(Buffer *buf, Register dst, int32_t src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0xc7);
  Buffer_write8(buf, 0xc0 + dst);
  Buffer_write32(buf, src);
}

void Emit_ret(Buffer *buf) { Buffer_write8(buf, 0xc3); }

// End Emit

// AST

typedef enum {
  kInteger,
} ASTNodeType;

struct ASTNode;
typedef struct ASTNode ASTNode;

ASTNodeType AST_type_of(ASTNode *node) {
  uint64_t address = (uint64_t)node;
  if ((address & kIntegerMask) == kIntegerTag) {
    return kInteger;
  }
  assert(0 && "unexpected node type");
}

bool AST_is_integer(ASTNode *node) { return AST_type_of(node) == kInteger; }

word AST_get_integer(ASTNode *node) { return (word)node >> kIntegerShift; }

ASTNode *AST_new_integer(word value) {
  return (ASTNode *)Object_encode_integer(value);
}

// End AST

// Compile

int Compile_expr(Buffer *buf, ASTNode *node) {
  if (AST_is_integer(node)) {
    word value = AST_get_integer(node);
    Emit_mov_reg_imm32(buf, kRax, Object_encode_integer(value));
    return 0;
  }
  assert(0 && "unexpected node type");
}

int Compile_function(Buffer *buf, ASTNode *node) {
  int result = Compile_expr(buf, node);
  if (result != 0)
    return result;
  Emit_ret(buf);
  return 0;
}

// End Compile

typedef int (*JitFunction)();

// Testing

word Testing_execute_expr(Buffer *buf) {
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

void Testing_expect_buffer_equals_bytes(Buffer *buf, byte *arr,
                                        size_t arr_size) {
  if (buf->len < arr_size || buf->len > arr_size) {
    fprintf(
        stderr,
        "NOT EQUAL. Expected array of size %ld but found array of size %ld.\n",
        arr_size, buf->len);
    return;
  }
  int result = memcmp(buf->address, arr, arr_size);
  if (result == 0) {
    return;
  }
  fprintf(stderr, "NOT EQUAL. Expected: ");
  Testing_print_hex_array(stderr, arr, arr_size);
  fprintf(stderr, "\n           Found:    ");
  Testing_print_hex_array(stderr, buf->address, buf->len);
  fprintf(stderr, "\n");
}

#define EXPECT_EQUALS_BYTES(buf, arr)                                          \
  Testing_expect_buffer_equals_bytes((buf), (arr), sizeof arr)

// End Testing

// Tests

TEST encode_positive_integer(void) {
  ASSERT_EQ(Object_encode_integer(0), 0x0);
  ASSERT_EQ(Object_encode_integer(1), 0x4);
  ASSERT_EQ(Object_encode_integer(10), 0x28);
  PASS();
}

TEST encode_negative_integer(void) {
  ASSERT_EQ(0x0, Object_encode_integer(0));
  ASSERT_EQ(Object_encode_integer(-1), (word)0xfffffffffffffffc);
  ASSERT_EQ(Object_encode_integer(-10), (word)0xffffffffffffffd8);
  PASS();
}

TEST buffer_write8_increases_length(void) {
  Buffer buf;
  Buffer_init(&buf, 5);
  ASSERT_EQ(buf.len, 0);
  Buffer_write8(&buf, 0xdb);
  ASSERT_EQ(Buffer_at8(&buf, 0), 0xdb);
  ASSERT_EQ(buf.len, 1);
  Buffer_deinit(&buf);
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

TEST buffer_write32_writes_little_endian(void) {
  Buffer buf;
  Buffer_init(&buf, 4);
  Buffer_write32(&buf, 0xdeadbeef);
  ASSERT_EQ(Buffer_at8(&buf, 0), 0xef);
  ASSERT_EQ(Buffer_at8(&buf, 1), 0xbe);
  ASSERT_EQ(Buffer_at8(&buf, 2), 0xad);
  ASSERT_EQ(Buffer_at8(&buf, 3), 0xde);
  Buffer_deinit(&buf);
  PASS();
}

TEST compile_positive_integer(void) {
  word value = 123;
  ASTNode *node = AST_new_integer(value);
  Buffer buf;
  Buffer_init(&buf, 1);
  int compile_result = Compile_function(&buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov eax, imm(123); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0xec, 0x01, 0x00, 0x00, 0xc3};
  EXPECT_EQUALS_BYTES(&buf, expected);
  Buffer_make_executable(&buf);
  word result = Testing_execute_expr(&buf);
  ASSERT_EQ(result, Object_encode_integer(value));
  PASS();
}

TEST compile_negative_integer(void) {
  word value = -123;
  ASTNode *node = AST_new_integer(value);
  Buffer buf;
  Buffer_init(&buf, 1);
  int compile_result = Compile_function(&buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov eax, imm(-123); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0x14, 0xfe, 0xff, 0xff, 0xc3};
  EXPECT_EQUALS_BYTES(&buf, expected);
  Buffer_make_executable(&buf);
  word result = Testing_execute_expr(&buf);
  ASSERT_EQ(result, Object_encode_integer(value));
  PASS();
}

SUITE(object_tests) {
  RUN_TEST(encode_positive_integer);
  RUN_TEST(encode_negative_integer);
}

SUITE(buffer_tests) {
  RUN_TEST(buffer_write8_increases_length);
  RUN_TEST(buffer_write8_expands_buffer);
  RUN_TEST(buffer_write32_expands_buffer);
  RUN_TEST(buffer_write32_writes_little_endian);
}

SUITE(compiler_tests) {
  RUN_TEST(compile_positive_integer);
  RUN_TEST(compile_negative_integer);
}

// End Tests

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(object_tests);
  RUN_SUITE(buffer_tests);
  RUN_SUITE(compiler_tests);
  GREATEST_MAIN_END();
}
