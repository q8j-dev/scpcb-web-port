#include "../../../stdutil/stdutil.h"
#include "graphics.webgpu.h"
#include "canvas.webgpu.h"

#include <cstring>
#include <vector>
#include <fstream>

#include "default.wgsl.h"

EM_JS( int,bbgpu_movie_open,(const uint8_t *data,int len),{
	if( !Module.bbMovies ) Module.bbMovies={};
	if( !Module.bbMovieNextId ) Module.bbMovieNextId=1;
	var id=Module.bbMovieNextId++;
	var bytes=HEAPU8.slice( data,data+len );
	var blob=new Blob( [bytes],{ type:"video/webm" } );
	var url=URL.createObjectURL( blob );
	var video=document.createElement( "video" );
	video.muted=true;
	video.playsInline=true;
	video.preload="auto";
	video.src=url;
	Module.bbMovies[id]={ video:video,url:url,canvas:null,ctx:null };
	return id;
});

EM_JS( int,bbgpu_movie_ready,(int id),{
	var m=Module.bbMovies[id];
	if( !m ) return -1;
	if( m.video.error ) return -1;
	return m.video.readyState>=2 ? 1 : 0;
});

EM_JS( int,bbgpu_movie_width,(int id),{
	var m=Module.bbMovies[id];
	return m ? (m.video.videoWidth|0) : 0;
});

EM_JS( int,bbgpu_movie_height,(int id),{
	var m=Module.bbMovies[id];
	return m ? (m.video.videoHeight|0) : 0;
});

EM_JS( void,bbgpu_movie_play,(int id),{
	var m=Module.bbMovies[id];
	if( m ) m.video.play().catch( function(){} );
});

EM_JS( int,bbgpu_movie_ended,(int id),{
	var m=Module.bbMovies[id];
	if( !m ) return 1;
	return ( m.video.ended || m.video.error ) ? 1 : 0;
});

EM_JS( int,bbgpu_movie_grab,(int id,uint8_t *dst,int w,int h),{
	var m=Module.bbMovies[id];
	if( !m ) return 0;
	if( !m.canvas ){
		m.canvas=document.createElement( "canvas" );
		m.canvas.width=w;m.canvas.height=h;
		m.ctx=m.canvas.getContext( "2d",{ willReadFrequently:true } );
	}
	try{
		m.ctx.drawImage( m.video,0,0,w,h );
	}catch( e ){
		return 0;
	}
	var img=m.ctx.getImageData( 0,0,w,h ).data;
	HEAPU8.set( img,dst );
	return 1;
});

EM_JS( void,bbgpu_movie_close,(int id),{
	var m=Module.bbMovies[id];
	if( !m ) return;
	m.video.pause();
	m.video.src="";
	URL.revokeObjectURL( m.url );
	delete Module.bbMovies[id];
});

class WebGPUMovie : public BBMovie{
public:
	WebGPUMovie( WebGPUGraphics *g,int handle ):
	gfx(g),handle(handle),vid_w(0),vid_h(0),failed(false),frame_canvas(0){
	}

	~WebGPUMovie(){
		bbgpu_movie_close( handle );
		if( frame_canvas ) gfx->freeCanvas( frame_canvas );
	}

	bool draw( BBCanvas *dest,int x,int y,int w,int h )override{
		if( failed ) return false;

		if( !frame_canvas ){
			int ready=bbgpu_movie_ready( handle );
			if( ready<0 ){ failed=true;return false; }
			if( ready==0 ) return true; // still buffering metadata

			vid_w=bbgpu_movie_width( handle );
			vid_h=bbgpu_movie_height( handle );
			if( vid_w<=0 || vid_h<=0 ){ failed=true;return false; }

			frame_canvas=(WebGPUCanvas*)gfx->createCanvas( vid_w,vid_h,0 );
			bbgpu_movie_play( handle );
		}

		std::vector<unsigned char> buf( (size_t)vid_w*vid_h*4 );
		if( bbgpu_movie_grab( handle,buf.data(),vid_w,vid_h ) ){
			BBPixmap *pm=d_new BBPixmap;
			pm->format=PF_RGBA;
			pm->width=vid_w;pm->height=vid_h;pm->depth=32;pm->bpp=4;pm->pitch=vid_w*4;
			pm->trans=false;
			pm->bits=new unsigned char[ (size_t)vid_w*vid_h*4 ];
			memcpy( pm->bits,buf.data(),buf.size() );
			frame_canvas->setPixmap( pm );

			if( w<0 ) w=vid_w;
			if( h<0 ) h=vid_h;
			((WebGPUCanvas*)dest)->blitScaled( x,y,w,h,frame_canvas,0,0,vid_w,vid_h,true );
		}

		return bbgpu_movie_ended( handle )==0;
	}

	bool isPlaying()const override{
		if( failed ) return false;
		if( !frame_canvas ) return true; // still buffering metadata
		return bbgpu_movie_ended( handle )==0;
	}
	int getWidth()const override{ return vid_w; }
	int getHeight()const override{ return vid_h; }

private:
	WebGPUGraphics *gfx;
	int handle;
	int vid_w,vid_h;
	bool failed;
	WebGPUCanvas *frame_canvas;
};


void bbWebGPUWait( WebGPUContextResources *res,volatile bool *done ){
	if( res && res->device ){
		res->flush();
		res->releaseSurfaceTexture();
	}
	while( !*done ){
		emscripten_sleep( 1 );
	}
}

EM_ASYNC_JS( void,bbWebGPURafYield,(void),{
	await new Promise( function( r ){ requestAnimationFrame( r ); } );
} );

static void bbUncapturedError( WGPUDevice const *device,WGPUErrorType type,WGPUStringView message,void *userdata1,void *userdata2 ){
	LOGD( "[webgpu] uncaptured error (%d): %.*s",(int)type,(int)message.length,message.data?message.data:"" );
}

static void bbDeviceLost( WGPUDevice const *device,WGPUDeviceLostReason reason,WGPUStringView message,void *userdata1,void *userdata2 ){
	LOGD( "[webgpu] device lost (%d): %.*s",(int)reason,(int)message.length,message.data?message.data:"" );
}


struct BBAdapterRequest{
	WGPUAdapter adapter=0;
	volatile bool done=false;
};

static void bbOnAdapter( WGPURequestAdapterStatus status,WGPUAdapter adapter,WGPUStringView message,void *userdata1,void *userdata2 ){
	BBAdapterRequest *req=(BBAdapterRequest*)userdata1;
	if( status==WGPURequestAdapterStatus_Success ){
		req->adapter=adapter;
	}else{
		LOGD( "[webgpu] requestAdapter failed: %.*s",(int)message.length,message.data?message.data:"" );
	}
	req->done=true;
}

struct BBDeviceRequest{
	WGPUDevice device=0;
	volatile bool done=false;
};

static void bbOnDevice( WGPURequestDeviceStatus status,WGPUDevice device,WGPUStringView message,void *userdata1,void *userdata2 ){
	BBDeviceRequest *req=(BBDeviceRequest*)userdata1;
	if( status==WGPURequestDeviceStatus_Success ){
		req->device=device;
	}else{
		LOGD( "[webgpu] requestDevice failed: %.*s",(int)message.length,message.data?message.data:"" );
	}
	req->done=true;
}

bool WebGPUContextResources::initDevice( const char *canvas_selector,int width,int height ){
	if( device ) return true;

	instance=wgpuCreateInstance( 0 );
	if( !instance ){
		LOGD( "%s","[webgpu] wgpuCreateInstance failed" );
		return false;
	}

	WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas_src={};
	canvas_src.chain.next=0;
	canvas_src.chain.sType=WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
	canvas_src.selector=bbStrView( canvas_selector );

	WGPUSurfaceDescriptor surf_desc={};
	surf_desc.nextInChain=&canvas_src.chain;
	surf_desc.label=bbStrView( "bb.surface" );

	surface=wgpuInstanceCreateSurface( instance,&surf_desc );
	if( !surface ){
		LOGD( "%s","[webgpu] wgpuInstanceCreateSurface failed" );
		return false;
	}

	BBAdapterRequest areq;
	WGPURequestAdapterOptions aopts={};
	aopts.compatibleSurface=surface;
	WGPURequestAdapterCallbackInfo acb={};
	acb.mode=WGPUCallbackMode_AllowSpontaneous;
	acb.callback=bbOnAdapter;
	acb.userdata1=&areq;
	wgpuInstanceRequestAdapter( instance,&aopts,acb );
	bbWebGPUWait( this,&areq.done );
	if( !areq.adapter ) return false;
	adapter=areq.adapter;

	BBDeviceRequest dreq;
	WGPUDeviceDescriptor ddesc={};
	ddesc.label=bbStrView( "bb.device" );
	ddesc.defaultQueue.label=bbStrView( "bb.queue" );
	ddesc.deviceLostCallbackInfo.mode=WGPUCallbackMode_AllowSpontaneous;
	ddesc.deviceLostCallbackInfo.callback=bbDeviceLost;
	ddesc.uncapturedErrorCallbackInfo.callback=bbUncapturedError;
	WGPURequestDeviceCallbackInfo dcb={};
	dcb.mode=WGPUCallbackMode_AllowSpontaneous;
	dcb.callback=bbOnDevice;
	dcb.userdata1=&dreq;
	wgpuAdapterRequestDevice( adapter,&ddesc,dcb );
	bbWebGPUWait( this,&dreq.done );
	if( !dreq.device ) return false;
	device=dreq.device;

	queue=wgpuDeviceGetQueue( device );

	WGPUSurfaceCapabilities caps={};
	if( wgpuSurfaceGetCapabilities( surface,adapter,&caps )==WGPUStatus_Success && caps.formatCount>0 ){
		surface_format=caps.formats[0];
		wgpuSurfaceCapabilitiesFreeMembers( caps );
	}else{
		surface_format=WGPUTextureFormat_BGRA8Unorm;
	}

	configureSurface( width,height );
	if( surface_width<=0 || surface_height<=0 ){
		LOGD( "[webgpu] refusing unconfigured %dx%d surface",width,height );
		return false;
	}
	ensurePipelineObjects();

	LOGD( "[webgpu] device ready, surface %dx%d format=%d",width,height,(int)surface_format );

	return true;
}

void WebGPUContextResources::configureSurface( int width,int height ){
	if( !device || !surface || width<=0 || height<=0 ) return;

	WGPUSurfaceConfiguration config={};
	config.device=device;
	config.format=surface_format;
	config.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopySrc|WGPUTextureUsage_CopyDst;
	config.width=(uint32_t)width;
	config.height=(uint32_t)height;
	config.alphaMode=WGPUCompositeAlphaMode_Opaque;
	config.presentMode=WGPUPresentMode_Fifo;

	wgpuSurfaceConfigure( surface,&config );

	surface_width=width;
	surface_height=height;
}

void WebGPUContextResources::ensurePipelineObjects(){
	if( shader ) return;

	std::string src( (const char*)DEFAULT_WGSL,DEFAULT_WGSL_SIZE );
	WGPUShaderSourceWGSL wgsl={};
	wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;
	wgsl.code.data=src.data();
	wgsl.code.length=src.size();
	WGPUShaderModuleDescriptor smdesc={};
	smdesc.nextInChain=&wgsl.chain;
	smdesc.label=bbStrView( "default.wgsl" );
	shader=wgpuDeviceCreateShaderModule( device,&smdesc );

	WGPUBindGroupLayoutEntry entries[3]={};
	entries[0].binding=0;
	entries[0].visibility=WGPUShaderStage_Vertex|WGPUShaderStage_Fragment;
	entries[0].buffer.type=WGPUBufferBindingType_Uniform;
	entries[0].buffer.hasDynamicOffset=1;
	entries[0].buffer.minBindingSize=sizeof( BBWebGPURenderState );
	entries[1].binding=1;
	entries[1].visibility=WGPUShaderStage_Fragment;
	entries[1].sampler.type=WGPUSamplerBindingType_Filtering;
	entries[2].binding=2;
	entries[2].visibility=WGPUShaderStage_Fragment;
	entries[2].texture.sampleType=WGPUTextureSampleType_Float;
	entries[2].texture.viewDimension=WGPUTextureViewDimension_2D;

	WGPUBindGroupLayoutDescriptor bgldesc={};
	bgldesc.label=bbStrView( "bb.2d.bgl" );
	bgldesc.entryCount=3;
	bgldesc.entries=entries;
	bind_layout=wgpuDeviceCreateBindGroupLayout( device,&bgldesc );

	WGPUPipelineLayoutDescriptor pldesc={};
	pldesc.label=bbStrView( "bb.2d.layout" );
	pldesc.bindGroupLayoutCount=1;
	pldesc.bindGroupLayouts=&bind_layout;
	pipeline_layout=wgpuDeviceCreatePipelineLayout( device,&pldesc );

	WGPUSamplerDescriptor sdesc={};
	sdesc.label=bbStrView( "bb.linear" );
	sdesc.addressModeU=WGPUAddressMode_ClampToEdge;
	sdesc.addressModeV=WGPUAddressMode_ClampToEdge;
	sdesc.addressModeW=WGPUAddressMode_ClampToEdge;
	sdesc.magFilter=WGPUFilterMode_Linear;
	sdesc.minFilter=WGPUFilterMode_Linear;
	sdesc.mipmapFilter=WGPUMipmapFilterMode_Nearest;
	sdesc.lodMinClamp=0.f;
	sdesc.lodMaxClamp=32.f;
	sdesc.maxAnisotropy=1;
	sampler_linear=wgpuDeviceCreateSampler( device,&sdesc );

	sdesc.label=bbStrView( "bb.nearest" );
	sdesc.magFilter=WGPUFilterMode_Nearest;
	sdesc.minFilter=WGPUFilterMode_Nearest;
	sampler_nearest=wgpuDeviceCreateSampler( device,&sdesc );

	WGPUTextureDescriptor tdesc={};
	tdesc.label=bbStrView( "bb.white" );
	tdesc.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst;
	tdesc.dimension=WGPUTextureDimension_2D;
	tdesc.size={ 1,1,1 };
	tdesc.format=WGPUTextureFormat_RGBA8Unorm;
	tdesc.mipLevelCount=1;
	tdesc.sampleCount=1;
	white_tex=wgpuDeviceCreateTexture( device,&tdesc );
	white_view=wgpuTextureCreateView( white_tex,0 );

	const uint32_t white=0xffffffff;
	WGPUTexelCopyTextureInfo dst={};
	dst.texture=white_tex;
	dst.aspect=WGPUTextureAspect_All;
	WGPUTexelCopyBufferLayout layout={};
	layout.offset=0;
	layout.bytesPerRow=4;
	layout.rowsPerImage=1;
	WGPUExtent3D extent={ 1,1,1 };
	wgpuQueueWriteTexture( queue,&dst,&white,4,&layout,&extent );

	vertex_capacity=1024*1024;
	WGPUBufferDescriptor vbdesc={};
	vbdesc.label=bbStrView( "bb.vertex.arena" );
	vbdesc.usage=WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst;
	vbdesc.size=vertex_capacity;
	vertex_buffer=wgpuDeviceCreateBuffer( device,&vbdesc );
	vertex_used=0;

	uniform_capacity=256*4096;
	WGPUBufferDescriptor ubdesc={};
	ubdesc.label=bbStrView( "bb.uniform.arena" );
	ubdesc.usage=WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst;
	ubdesc.size=uniform_capacity;
	uniform_buffer=wgpuDeviceCreateBuffer( device,&ubdesc );
	uniform_used=0;
}


WGPUTextureView WebGPUContextResources::acquireSurfaceView(){
	if( surface_view ) return surface_view;
	if( !surface ) return 0;

	WGPUSurfaceTexture st={};
	wgpuSurfaceGetCurrentTexture( surface,&st );
	if( st.status!=WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
	    st.status!=WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal ){
		LOGD( "[webgpu] getCurrentTexture failed: %d",(int)st.status );
		if( st.texture ) wgpuTextureRelease( st.texture );
		configureSurface( surface_width,surface_height );
		wgpuSurfaceGetCurrentTexture( surface,&st );
		if( st.status!=WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
		    st.status!=WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal ){
			if( st.texture ) wgpuTextureRelease( st.texture );
			return 0;
		}
	}
	surface_texture=st.texture;
	surface_view=wgpuTextureCreateView( surface_texture,0 );
	return surface_view;
}

void WebGPUContextResources::releaseSurfaceTexture(){
	if( surface_view ){
		invalidateBindGroupsFor( surface_view );
		wgpuTextureViewRelease( surface_view );
		surface_view=0;
	}
	if( surface_texture ){
		wgpuTextureRelease( surface_texture );
		surface_texture=0;
	}
}


WGPUCommandEncoder WebGPUContextResources::ensureEncoder(){
	if( !encoder ){
		WGPUCommandEncoderDescriptor desc={};
		desc.label=bbStrView( "bb.encoder" );
		encoder=wgpuDeviceCreateCommandEncoder( device,&desc );
	}
	return encoder;
}

WGPURenderPassEncoder WebGPUContextResources::ensurePass( WebGPUCanvas *target ){
	if( pass && pass_target==target ) return pass;

	endPass();

	WGPUTextureView view=target->targetView();
	if( !view ) return 0;

	ensureEncoder();

	WGPURenderPassColorAttachment att={};
	att.view=view;
	att.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;
	att.resolveTarget=0;
	bool clear=target->consumeClearPending();
	att.loadOp=clear?WGPULoadOp_Clear:WGPULoadOp_Load;
	att.storeOp=WGPUStoreOp_Store;
	float cc[4];
	target->getClsColorf( cc );
	att.clearValue={ cc[0],cc[1],cc[2],cc[3] };

	WGPURenderPassDescriptor desc={};
	desc.label=bbStrView( "bb.2d.pass" );
	desc.colorAttachmentCount=1;
	desc.colorAttachments=&att;

	pass=wgpuCommandEncoderBeginRenderPass( encoder,&desc );
	pass_target=target;
	return pass;
}

void WebGPUContextResources::endPass(){
	if( pass ){
		wgpuRenderPassEncoderEnd( pass );
		wgpuRenderPassEncoderRelease( pass );
		pass=0;
	}
	pass_target=0;
}

void WebGPUContextResources::flush(){
	endPass();
	if( encoder ){
		WGPUCommandBufferDescriptor desc={};
		desc.label=bbStrView( "bb.commands" );
		WGPUCommandBuffer commands=wgpuCommandEncoderFinish( encoder,&desc );
		wgpuCommandEncoderRelease( encoder );
		encoder=0;
		if( commands ){
			wgpuQueueSubmit( queue,1,&commands );
			wgpuCommandBufferRelease( commands );
		}
	}
	vertex_used=0;
	uniform_used=0;
}


WGPURenderPipeline WebGPUContextResources::getPipeline( WGPUPrimitiveTopology topology,bool blend,WGPUTextureFormat format ){
	uint32_t key=(uint32_t)topology|((blend?1u:0u)<<8)|((uint32_t)format<<16);
	std::map<uint32_t,WGPURenderPipeline>::iterator it=pipelines.find( key );
	if( it!=pipelines.end() ) return it->second;

	WGPUVertexAttribute attrs[2]={};
	attrs[0].format=WGPUVertexFormat_Float32x2;
	attrs[0].offset=0;
	attrs[0].shaderLocation=0;
	attrs[1].format=WGPUVertexFormat_Float32x2;
	attrs[1].offset=8;
	attrs[1].shaderLocation=1;

	WGPUVertexBufferLayout vbl={};
	vbl.stepMode=WGPUVertexStepMode_Vertex;
	vbl.arrayStride=sizeof( BBWebGPUVertex );
	vbl.attributeCount=2;
	vbl.attributes=attrs;

	WGPUBlendState blend_state={};
	blend_state.color.operation=WGPUBlendOperation_Add;
	blend_state.color.srcFactor=WGPUBlendFactor_SrcAlpha;
	blend_state.color.dstFactor=WGPUBlendFactor_OneMinusSrcAlpha;
	blend_state.alpha.operation=WGPUBlendOperation_Add;
	blend_state.alpha.srcFactor=WGPUBlendFactor_One;
	blend_state.alpha.dstFactor=WGPUBlendFactor_OneMinusSrcAlpha;

	WGPUColorTargetState target={};
	target.format=format;
	target.blend=blend?&blend_state:0;
	target.writeMask=WGPUColorWriteMask_All;

	WGPUFragmentState frag={};
	frag.module=shader;
	frag.entryPoint=bbStrView( "fs_main" );
	frag.targetCount=1;
	frag.targets=&target;

	WGPURenderPipelineDescriptor desc={};
	desc.label=bbStrView( "bb.2d.pipeline" );
	desc.layout=pipeline_layout;
	desc.vertex.module=shader;
	desc.vertex.entryPoint=bbStrView( "vs_main" );
	desc.vertex.bufferCount=1;
	desc.vertex.buffers=&vbl;
	desc.primitive.topology=topology;
	desc.primitive.stripIndexFormat=WGPUIndexFormat_Undefined;
	desc.primitive.frontFace=WGPUFrontFace_CCW;
	desc.primitive.cullMode=WGPUCullMode_None;
	desc.multisample.count=1;
	desc.multisample.mask=0xffffffff;
	desc.fragment=&frag;

	WGPURenderPipeline pipeline=wgpuDeviceCreateRenderPipeline( device,&desc );
	pipelines[key]=pipeline;
	return pipeline;
}

WGPUBindGroup WebGPUContextResources::getBindGroup( WGPUTextureView view,WGPUSampler sampler ){
	std::pair<WGPUTextureView,WGPUSampler> key( view,sampler );
	std::map<std::pair<WGPUTextureView,WGPUSampler>,WGPUBindGroup>::iterator it=bind_groups.find( key );
	if( it!=bind_groups.end() ) return it->second;

	WGPUBindGroupEntry entries[3]={};
	entries[0].binding=0;
	entries[0].buffer=uniform_buffer;
	entries[0].offset=0;
	entries[0].size=sizeof( BBWebGPURenderState );
	entries[1].binding=1;
	entries[1].sampler=sampler;
	entries[2].binding=2;
	entries[2].textureView=view;

	WGPUBindGroupDescriptor desc={};
	desc.label=bbStrView( "bb.2d.bindgroup" );
	desc.layout=bind_layout;
	desc.entryCount=3;
	desc.entries=entries;

	WGPUBindGroup group=wgpuDeviceCreateBindGroup( device,&desc );
	bind_groups[key]=group;
	return group;
}

void WebGPUContextResources::invalidateBindGroupsFor( WGPUTextureView view ){
	std::map<std::pair<WGPUTextureView,WGPUSampler>,WGPUBindGroup>::iterator it=bind_groups.begin();
	while( it!=bind_groups.end() ){
		if( it->first.first==view ){
			wgpuBindGroupRelease( it->second );
			it=bind_groups.erase( it );
		}else{
			++it;
		}
	}
	for( size_t i=0;i<listeners.size();++i ){
		if( listeners[i].view_gone ) listeners[i].view_gone( listeners[i].ctx,view );
	}
}

void WebGPUContextResources::addListener( const WebGPUCanvasListener &l ){
	listeners.push_back( l );
}

void WebGPUContextResources::removeListener( void *ctx ){
	size_t i=0;
	while( i<listeners.size() ){
		if( listeners[i].ctx==ctx ){
			listeners.erase( listeners.begin()+i );
		}else{
			++i;
		}
	}
}

void WebGPUContextResources::notifyCanvasDestroyed( WebGPUCanvas *canvas ){
	for( size_t i=0;i<listeners.size();++i ){
		if( listeners[i].canvas_gone ) listeners[i].canvas_gone( listeners[i].ctx,canvas );
	}
}

uint32_t WebGPUContextResources::pushVertices( const BBWebGPUVertex *verts,int count ){
	size_t bytes=(size_t)count*sizeof( BBWebGPUVertex );
	size_t offset=(vertex_used+15)&~(size_t)15;

	if( offset+bytes>vertex_capacity ){
		flush();
		offset=0;
		if( bytes>vertex_capacity ){
			size_t cap=vertex_capacity;
			while( cap<bytes ) cap*=2;
			wgpuBufferRelease( vertex_buffer );
			WGPUBufferDescriptor desc={};
			desc.label=bbStrView( "bb.vertex.arena" );
			desc.usage=WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst;
			desc.size=cap;
			vertex_buffer=wgpuDeviceCreateBuffer( device,&desc );
			vertex_capacity=cap;
		}
	}

	wgpuQueueWriteBuffer( queue,vertex_buffer,offset,verts,bytes );
	vertex_used=offset+bytes;
	return (uint32_t)offset;
}

uint32_t WebGPUContextResources::pushUniforms( const BBWebGPURenderState &state ){
	size_t offset=(uniform_used+255)&~(size_t)255;
	if( offset+256>uniform_capacity ){
		flush();
		offset=0;
	}
	wgpuQueueWriteBuffer( queue,uniform_buffer,offset,&state,sizeof( state ) );
	uniform_used=offset+256;
	return (uint32_t)offset;
}

void WebGPUContextResources::destroy(){
	endPass();
	if( encoder ){ wgpuCommandEncoderRelease( encoder );encoder=0; }
	releaseSurfaceTexture();

	for( std::map<std::pair<WGPUTextureView,WGPUSampler>,WGPUBindGroup>::iterator it=bind_groups.begin();it!=bind_groups.end();++it ){
		wgpuBindGroupRelease( it->second );
	}
	bind_groups.clear();
	for( std::map<uint32_t,WGPURenderPipeline>::iterator it=pipelines.begin();it!=pipelines.end();++it ){
		wgpuRenderPipelineRelease( it->second );
	}
	pipelines.clear();
	for( std::map<BBImageFont*,FontTexture>::iterator it=font_textures.begin();it!=font_textures.end();++it ){
		if( it->second.view ) wgpuTextureViewRelease( it->second.view );
		if( it->second.tex ) wgpuTextureRelease( it->second.tex );
	}
	font_textures.clear();

	if( vertex_buffer ){ wgpuBufferRelease( vertex_buffer );vertex_buffer=0; }
	if( uniform_buffer ){ wgpuBufferRelease( uniform_buffer );uniform_buffer=0; }
	if( white_view ){ wgpuTextureViewRelease( white_view );white_view=0; }
	if( white_tex ){ wgpuTextureRelease( white_tex );white_tex=0; }
	if( sampler_linear ){ wgpuSamplerRelease( sampler_linear );sampler_linear=0; }
	if( sampler_nearest ){ wgpuSamplerRelease( sampler_nearest );sampler_nearest=0; }
	if( pipeline_layout ){ wgpuPipelineLayoutRelease( pipeline_layout );pipeline_layout=0; }
	if( bind_layout ){ wgpuBindGroupLayoutRelease( bind_layout );bind_layout=0; }
	if( shader ){ wgpuShaderModuleRelease( shader );shader=0; }
	if( surface ){ wgpuSurfaceUnconfigure( surface );wgpuSurfaceRelease( surface );surface=0; }
	if( queue ){ wgpuQueueRelease( queue );queue=0; }
	if( device ){ wgpuDeviceRelease( device );device=0; }
	if( adapter ){ wgpuAdapterRelease( adapter );adapter=0; }
	if( instance ){ wgpuInstanceRelease( instance );instance=0; }
}


void WebGPUGraphics::onAppChange( void *data,void *context ){
	WebGPUGraphics *graphics=(WebGPUGraphics*)context;
	SDL_SetWindowTitle( graphics->wnd,bbApp().title.c_str() );
}

WebGPUGraphics::WebGPUGraphics( SDL_Window *wnd ):wnd(wnd),def_font(0),fb(0){
	for( int k=0;k<256;++k ) gamma_red[k]=gamma_green[k]=gamma_blue[k]=k;

	bbAppOnChange.add( onAppChange,this );

	fb=d_new WebGPUCanvas( &res,BBCanvas::CANVAS_TEX_VIDMEM );

	front_canvas=fb;
	back_canvas=fb;

	resize();
}

WebGPUGraphics::~WebGPUGraphics(){
	bbAppOnChange.remove( onAppChange,this );

	delete fb;fb=0;

	res.destroy();

	SDL_DestroyWindow( wnd );wnd=0;
}

void WebGPUGraphics::resize(){
	SDL_GetWindowSize( wnd,&window_width,&window_height );
	drawable_width=window_width;
	drawable_height=window_height;

	float sx=(float)drawable_width/(window_width?window_width:1);
	float sy=(float)drawable_height/(window_height?window_height:1);
	fb->setScale( sx,sy );

	fb->resize( drawable_width,drawable_height,getDensity() );

	if( res.device && (drawable_width!=res.surface_width||drawable_height!=res.surface_height) ){
		res.flush();
		res.releaseSurfaceTexture();
		res.configureSurface( drawable_width,drawable_height );
	}
}

bool WebGPUGraphics::init(){
	if( !res.initDevice( "#canvas",drawable_width,drawable_height ) ) return false;

	def_font=(BBImageFont*)loadFont( "GFX/font/cour/Courier New.ttf",12,0 );
	if( def_font==0 ) def_font=(BBImageFont*)loadFont( "/GFX/cour.ttf",12,0 );
	if( def_font==0 ) def_font=(BBImageFont*)loadFont( "GFX/cour.ttf",12,0 );
	if( def_font==0 ) fprintf( stderr,"[webgpu] warning: no default font found, continuing without one\n" );
	return true;
}

void WebGPUGraphics::present(){
	if( !res.device || !fb ) return;

	res.flush();

	WGPUTextureView src=fb->sampleView();
	if( !src ) return;

	WGPUTextureView dst=res.acquireSurfaceView();
	if( !dst ) return;

	BBWebGPURenderState state={};
	state.xywh[2]=(float)res.surface_width;
	state.xywh[3]=(float)res.surface_height;
	state.res[0]=(float)res.surface_width;
	state.res[1]=(float)res.surface_height;
	state.texscale[0]=1.0f;state.texscale[1]=1.0f;
	state.color[0]=state.color[1]=state.color[2]=1.0f;
	state.texenabled=1;
	state.scale[0]=1.0f;state.scale[1]=1.0f;
	uint32_t uoffset=res.pushUniforms( state );

	static const BBWebGPUVertex verts[6]={
		{ 0.f,0.f,0.f,0.f },
		{ 0.f,1.f,0.f,1.f },
		{ 1.f,0.f,1.f,0.f },
		{ 1.f,0.f,1.f,0.f },
		{ 0.f,1.f,0.f,1.f },
		{ 1.f,1.f,1.f,1.f }
	};
	uint32_t voffset=res.pushVertices( verts,6 );

	res.ensureEncoder();

	WGPURenderPassColorAttachment att={};
	att.view=dst;
	att.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;
	att.loadOp=WGPULoadOp_Clear;
	att.storeOp=WGPUStoreOp_Store;
	att.clearValue={ 0.0,0.0,0.0,1.0 };

	WGPURenderPassDescriptor desc={};
	desc.label=bbStrView( "bb.present" );
	desc.colorAttachmentCount=1;
	desc.colorAttachments=&att;

	WGPURenderPassEncoder p=wgpuCommandEncoderBeginRenderPass( res.encoder,&desc );
	wgpuRenderPassEncoderSetPipeline( p,res.getPipeline( WGPUPrimitiveTopology_TriangleList,false,res.surface_format ) );
	wgpuRenderPassEncoderSetBindGroup( p,0,res.getBindGroup( src,res.sampler_nearest ),1,&uoffset );
	wgpuRenderPassEncoderSetVertexBuffer( p,0,res.vertex_buffer,voffset,6*sizeof( BBWebGPUVertex ) );
	wgpuRenderPassEncoderDraw( p,6,1,0,0 );
	wgpuRenderPassEncoderEnd( p );
	wgpuRenderPassEncoderRelease( p );

	res.flush();
	res.releaseSurfaceTexture();
}

BBFont *WebGPUGraphics::getDefaultFont()const{
	return def_font;
}

void WebGPUGraphics::backup(){
}

bool WebGPUGraphics::restore(){
	return true;
}

void WebGPUGraphics::vwait(){
}

void WebGPUGraphics::copy( BBCanvas *dest,int dx,int dy,int dw,int dh,BBCanvas *src,int sx,int sy,int sw,int sh ){
	dest->blit( dx,dy,src,sx,sy,sw,sh,false );
}

void WebGPUGraphics::setGamma( int r,int g,int b,float dr,float dg,float db ){
	gamma_red[r&255]=dr*257.0f;
	gamma_green[g&255]=dg*257.0f;
	gamma_blue[b&255]=db*257.0f;
}

void WebGPUGraphics::getGamma( int r,int g,int b,float *dr,float *dg,float *db ){
	*dr=gamma_red[r&255];*dg=gamma_green[g&255];*db=gamma_blue[b&255];
}

void WebGPUGraphics::updateGamma( bool calibrate ){
}

int WebGPUGraphics::getWidth()const{ return drawable_width; }
int WebGPUGraphics::getHeight()const{ return drawable_height; }
int WebGPUGraphics::getLogicalWidth()const{ return window_width; }
int WebGPUGraphics::getLogicalHeight()const{ return window_height; }
int WebGPUGraphics::getDepth()const{ return 0; }
float WebGPUGraphics::getDensity()const{
	if( window_width<=0 || drawable_width<=0 ) return 1.0f;
	return (float)drawable_width/window_width;
}
int WebGPUGraphics::getScanLine()const{ return 0; }
int WebGPUGraphics::getAvailVidmem()const{ return 0; }
int WebGPUGraphics::getTotalVidmem()const{ return 0; }

void WebGPUGraphics::moveMouse( int x,int y ){
	SDL_WarpMouseInWindow( wnd,x,y );
}

BBCanvas *WebGPUGraphics::createCanvas( int width,int height,int flags ){
	WebGPUCanvas *canvas=d_new WebGPUCanvas( &res,width,height,flags );
	canvas_set.insert( canvas );
	return canvas;
}

BBCanvas *WebGPUGraphics::loadCanvas( const std::string &file,int flags ){
	BBPixmap *pixmap=bbLoadPixmap( file );
	if( !pixmap ) return 0;

	pixmap->flipVertically();
	pixmap->swapBytes0and2();

	if( (flags&BBCanvas::CANVAS_TEX_ALPHA) && !(flags&BBCanvas::CANVAS_TEX_MASK) && !pixmap->trans ){
		pixmap->buildAlpha( !(flags&BBCanvas::CANVAS_TEX_RGB) );
	}

	WebGPUCanvas *canvas=d_new WebGPUCanvas( &res,flags );
	canvas->setPixmap( pixmap );
	canvas_set.insert( canvas );

	return canvas;
}

BBMovie *WebGPUGraphics::openMovie( const std::string &file,int flags ){
	std::ifstream f( canonicalpath(file).c_str(),std::ios::binary );
	if( !f ) return 0;
	std::vector<unsigned char> data( (std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>() );
	if( data.empty() ) return 0;

	int id=bbgpu_movie_open( data.data(),(int)data.size() );
	if( id<=0 ) return 0;

	return d_new WebGPUMovie( this,id );
}


WebGPUContextDriver::WebGPUContextDriver(){
}

static const int kWebGPUModes[][2]={
	{1280,720},{1920,1080},{2560,1440},{3840,2160},{1600,900},{1366,768},{1024,768},{800,600},{640,480}
};

int WebGPUContextDriver::numGraphicsDrivers(){
	return 1;
}

void WebGPUContextDriver::graphicsDriverInfo( int driver,std::string *name,int *c ){
	if( name ) *name="WebGPU";
	if( c ) *c=GFXMODECAPS_3D;
}

int WebGPUContextDriver::numGraphicsModes( int driver ){
	return (int)(sizeof(kWebGPUModes)/sizeof(kWebGPUModes[0]));
}

void WebGPUContextDriver::graphicsModeInfo( int driver,int mode,int *w,int *h,int *d,int *c ){
	int n=(int)(sizeof(kWebGPUModes)/sizeof(kWebGPUModes[0]));
	if( mode<0 ) mode=0;
	if( mode>=n ) mode=n-1;
	*w=kWebGPUModes[mode][0];*h=kWebGPUModes[mode][1];*d=32;*c=GFXMODECAPS_3D;
}

void WebGPUContextDriver::windowedModeInfo( int *c ){
	*c=GFXMODECAPS_3D;
}

BBGraphics *WebGPUContextDriver::openGraphics( int w,int h,int d,int driver,int flags ){
	LOGD( "[webgpu.openGraphics] requested %dx%d d=%d flags=%d existing=%d",w,h,d,flags,graphics?1:0 );
	if( graphics ) return 0;
	if( w<=0 || h<=0 ){ w=1280;h=720; }

	if( SDL_Init( SDL_INIT_VIDEO )<0 ){
		LOGD( "%s","failed to init sdl" );
		return 0;
	}

	SDL_Window *wnd=SDL_CreateWindow( bbApp().title.c_str(),SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,1,1,0 );
	if( wnd==NULL ){
		LOGD( "%s","failed to create window" );
		return 0;
	}

	SDL_SetWindowSize( wnd,w,h );
	SDL_ShowWindow( wnd );

	WebGPUGraphics *g=d_new WebGPUGraphics( wnd );
	if( g->init() ){
		SDL_RaiseWindow( wnd );
		graphics=g;
		return graphics;
	}
	delete g;
	return 0;
}

void WebGPUContextDriver::closeGraphics( BBGraphics *g ){
	if( graphics!=g || !g ) return;
	delete graphics;graphics=0;
}

bool WebGPUContextDriver::graphicsLost(){
	return false;
}

void WebGPUContextDriver::flip( bool vwait ){
	WebGPUGraphics *g=(WebGPUGraphics*)graphics;
	if( !g ) return;

	g->present();

	if( vwait ){
		bbWebGPURafYield();
	}else{
		emscripten_sleep( 0 );
	}
}

static BBContextDriver *createWebGPUContext( const std::string &name ){
	if( name.find( "webgpu" )==std::string::npos ){
		return 0;
	}

	return d_new WebGPUContextDriver();
}

BBMODULE_CREATE( graphics_webgpu ){
	bbContextDrivers.insert( bbContextDrivers.begin(),createWebGPUContext );

	return true;
}

BBMODULE_DESTROY( graphics_webgpu ){
	return true;
}
