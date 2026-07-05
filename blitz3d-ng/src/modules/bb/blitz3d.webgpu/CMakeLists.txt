if(BB_EMSCRIPTEN AND TARGET SDL2)
  bin2h(SOURCE_FILE default3d.wgsl HEADER_FILE default3d.wgsl.h VARIABLE_NAME DEFAULT3D_WGSL)

  bb_start_module(blitz3d.webgpu)
  set(DEPENDS_ON bb.blitz3d bb.graphics.webgpu)
  set(SOURCES blitz3d.webgpu.cpp blitz3d.webgpu.h default3d.wgsl.h)
  set(LIBS SDL2)
  bb_end_module()

  target_compile_options(bb.blitz3d.webgpu PRIVATE "--use-port=emdawnwebgpu")
endif()
