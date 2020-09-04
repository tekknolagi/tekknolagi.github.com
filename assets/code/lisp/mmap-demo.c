#include <assert.h>   /* for assert */
#include <stddef.h>   /* for NULL */
#include <string.h>   /* for memcpy */
#include <sys/mman.h> /* for mmap */

// clang-format off
const unsigned char program[] = {
    // mov eax, 42 (0x2a)
    0xb8, 0x2a, 0x00, 0x00, 0x00,
    // ret
    0xc3,
};
// clang-format on

const int kProgramSize = sizeof program;

typedef int (*JitFunction)();

int main() {
  void *memory = mmap(/*addr=*/NULL, kProgramSize, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE,
                      /*filedes=*/-1, /*off=*/0);
  memcpy(memory, program, kProgramSize);
  int result = mprotect(memory, kProgramSize, PROT_EXEC);
  assert(result == 0 && "mprotect failed");
  JitFunction function = *(JitFunction*)&memory;
  int return_code = function();
  assert(return_code == 42 && "the assembly was wrong");
  result = munmap(memory, kProgramSize);
  assert(result == 0 && "munmap failed");
  // Return 0 so we can run this as a test and not stop Make
  return 0;
}
