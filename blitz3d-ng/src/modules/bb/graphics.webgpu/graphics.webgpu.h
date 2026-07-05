#ifndef BB_GRAPHICS_WEBGPU_H
#define BB_GRAPHICS_WEBGPU_H

#include <bb/graphics/graphics.h>

#include <webgpu/webgpu.h>

#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include <emscripten.h>
#include <emscripten/html5.h>

#include <SDL.h>

class WebGPUCanvas;

struct WebGPUCanvasListener{
	void *ctx;
	void (*view_gone)( void *ctx,WGPUTextureView view );
	void (*canvas_gone)( void *ctx,WebGPUCanvas *canvas );
};

inline WGPUStringView bbStrView( const char *s ){
	WGPUStringView v;
	v.data=s;
	v.length=s?strlen( s ):0;
	return v;
}

struct BBWebGPURenderState{
	float xywh[4];
	float res[2];
	float texscale[2];
	float color[3];
	int32_t texenabled;
	float scale[2];
	float _pad[2];
};
static_assert( sizeof(BBWebGPURenderState)==64,"BBRenderState layout mismatch" );

struct BBWebGPUVertex{
	float x,y;
	float u,v;
};
static_assert( sizeof(BBWebGPUVertex)==16,"vertex layout mismatch" );

struct WebGPUContextResources{
	WGPUInstance instance=0;
	WGPUAdapter adapter=0;
	WGPUDevice device=0;
	WGPUQueue queue=0;
	WGPUSurface surface=0;
	WGPUTextureFormat surface_format=WGPUTextureFormat_BGRA8Unorm;
	int surface_width=0,surface_height=0;

	WGPUTexture surface_texture=0;
	WGPUTextureView surface_view=0;

	WGPUShaderModule shader=0;
	WGPUBindGroupLayout bind_layout=0;
	WGPUPipelineLayout pipeline_layout=0;
	std::map<uint32_t,WGPURenderPipeline> pipelines;

	WGPUSampler sampler_linear=0,sampler_nearest=0;

	WGPUTexture white_tex=0;
	WGPUTextureView white_view=0;

	WGPUBuffer vertex_buffer=0;
	size_t vertex_capacity=0,vertex_used=0;
	WGPUBuffer uniform_buffer=0;
	size_t uniform_capacity=0,uniform_used=0;

	WGPUCommandEncoder encoder=0;
	WGPURenderPassEncoder pass=0;
	WebGPUCanvas *pass_target=0;

	std::map<std::pair<WGPUTextureView,WGPUSampler>,WGPUBindGroup> bind_groups;

	std::vector<WebGPUCanvasListener> listeners;

	struct FontTexture{
		WGPUTexture tex=0;
		WGPUTextureView view=0;
		int width=0,height=0;
	};
	std::map<BBImageFont*,FontTexture> font_textures;

	bool initDevice( const char *canvas_selector,int width,int height );
	void configureSurface( int width,int height );
	void ensurePipelineObjects();

	WGPUTextureView acquireSurfaceView();
	void releaseSurfaceTexture();

	WGPUCommandEncoder ensureEncoder();
	WGPURenderPassEncoder ensurePass( WebGPUCanvas *target );
	void endPass();
	void flush();

	WGPURenderPipeline getPipeline( WGPUPrimitiveTopology topology,bool blend,WGPUTextureFormat format );
	WGPUBindGroup getBindGroup( WGPUTextureView view,WGPUSampler sampler );
	uint32_t pushVertices( const BBWebGPUVertex *verts,int count );
	uint32_t pushUniforms( const BBWebGPURenderState &state );

	void invalidateBindGroupsFor( WGPUTextureView view );

	void addListener( const WebGPUCanvasListener &l );
	void removeListener( void *ctx );
	void notifyCanvasDestroyed( WebGPUCanvas *canvas );

	void destroy();
};

void bbWebGPUWait( WebGPUContextResources *res,volatile bool *done );

class WebGPUGraphics : public BBGraphics{
protected:
	BBImageFont *def_font;

	WebGPUCanvas *fb;

	int window_width,window_height,drawable_width,drawable_height;

	unsigned short gamma_red[256],gamma_green[256],gamma_blue[256];

	static void onAppChange( void *data,void *context );
public:
	WebGPUContextResources res;

	SDL_Window *wnd;

	WebGPUGraphics( SDL_Window *wnd );
	~WebGPUGraphics();

	void resize();

	bool init();

	void present();

	BBFont *getDefaultFont()const;

	void backup();
	bool restore();

	void vwait();

	void copy( BBCanvas *dest,int dx,int dy,int dw,int dh,BBCanvas *src,int sx,int sy,int sw,int sh );

	void setGamma( int r,int g,int b,float dr,float dg,float db );
	void getGamma( int r,int g,int b,float *dr,float *dg,float *db );
	void updateGamma( bool calibrate );

	int getWidth()const;
	int getHeight()const;
	int getLogicalWidth()const;
	int getLogicalHeight()const;
	int getDepth()const;
	float getDensity()const;
	int getScanLine()const;
	int getAvailVidmem()const;
	int getTotalVidmem()const;

	void moveMouse( int x,int y );

	BBCanvas *createCanvas( int width,int height,int flags );
	BBCanvas *loadCanvas( const std::string &file,int flags );

	BBMovie *openMovie( const std::string &file,int flags );
};

class WebGPUContextDriver : public BBContextDriver{
public:
	WebGPUContextDriver();

	int numGraphicsDrivers();
	void graphicsDriverInfo( int driver,std::string *name,int *c );
	int numGraphicsModes( int driver );
	void graphicsModeInfo( int driver,int mode,int *w,int *h,int *d,int *c );
	void windowedModeInfo( int *c );

	BBGraphics *openGraphics( int w,int h,int d,int driver,int flags );
	void closeGraphics( BBGraphics *graphics );
	bool graphicsLost();

	void flip( bool vwait );
};

#endif
