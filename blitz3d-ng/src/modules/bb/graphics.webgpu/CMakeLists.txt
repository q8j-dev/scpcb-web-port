if(BB_EMSCRIPTEN AND TARGET SDL2)
  bin2h(SOURCE_FILE default.wgsl HEADER_FILE default.wgsl.h VARIABLE_NAME DEFAULT_WGSL)

  bb_start_module(graphics.webgpu)
  set(DEPENDS_ON bb.graphics)
  set(SOURCES graphics.webgpu.cpp graphics.webgpu.h canvas.webgpu.cpp canvas.webgpu.h default.wgsl.h)
  set(LIBS SDL2)
  bb_end_module()

  target_compile_options(bb.graphics.webgpu PUBLIC "--use-port=emdawnwebgpu")
endif()
