#ifndef BB_BLITZ3D_WEBGPU_H
#define BB_BLITZ3D_WEBGPU_H


#include <bb/blitz3d/blitz3d.h>
#include <bb/blitz3d/graphics.h>
#include <bb/graphics.webgpu/graphics.webgpu.h>

#include <cstdint>


struct BBWebGPUFrameState{
	float proj[16];
	float view[16];
	struct LightData{
		float mat[16];
		float color[4];
		float params[4];
	}lights[8];
	int32_t lights_used;
	int32_t _pad[3];
};
static_assert( sizeof(BBWebGPUFrameState)==912,"BBFrameState layout mismatch" );

struct BBWebGPUEntityState{
	float world[16];
	float normal_mat[16];
	float ambient[4];
	float brush_color[4];
	float fog_color[4];
	struct TexState{
		float mat[16];
		int32_t blend,sphere_map,flags,cube_map;
	}texs[8];
	float fog_range[2];
	int32_t texs_used;
	int32_t use_vertex_color;
	float brush_shininess;
	int32_t fullbright;
	int32_t fog_mode;
	int32_t alpha_test;
};
static_assert( sizeof(BBWebGPUEntityState)==848,"BBEntityState layout mismatch" );

struct BBWebGPUClearState{
	float color[4];
	float z;
	float _pad[3];
};
static_assert( sizeof(BBWebGPUClearState)==32,"BBClearState layout mismatch" );

struct BBWebGPU3DVertex{
	float coords[3];
	float normal[3];
	float color[4];
	float tex_coord0[2];
	float tex_coord1[2];
};
static_assert( sizeof(BBWebGPU3DVertex)==56,"3D vertex layout mismatch" );


class WebGPUB3DGraphics : public B3DGraphics{
public:
	BBScene *createScene( int w,int h,float d,int flags );
};

#endif
