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

typedef int64_t word;
typedef uint64_t uword;
typedef unsigned char byte;

const int kWordSize = sizeof(word); // bytes

typedef struct ASTNode ASTNode;
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

extern "C" word Object_decode_integer(uword value);
extern "C" word Object_error();
extern "C" void Buffer_init(Buffer *result, word capacity);
extern "C" void Buffer_deinit(Buffer *buf);
extern "C" word Buffer_len(Buffer *buf);
extern "C" int Buffer_make_executable(Buffer *buf);
extern "C" bool AST_is_error(ASTNode *node);
extern "C" void AST_heap_free(ASTNode *node);
extern "C" ASTNode *Reader_read(char *input);
extern "C" WARN_UNUSED int Compile_entry(Buffer *buf, ASTNode *node);
extern "C" uword Testing_execute_entry(Buffer *buf, uword *heap);

int main() {
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
  uword *heap = reinterpret_cast<uword *>(malloc(1000 * kWordSize));
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
        ImGui::Text("Result: %ld", Object_decode_integer(execute_result));
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
      //             1000.0f / ImGui::GetIO().Framerate,
      //             ImGui::GetIO().Framerate);
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
