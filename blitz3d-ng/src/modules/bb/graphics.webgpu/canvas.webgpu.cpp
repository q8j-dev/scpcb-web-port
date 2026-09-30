#include "../../../stdutil/stdutil.h"
#include <bb/graphics/font.h>
#include "canvas.webgpu.h"
#include <utf8.h>

#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>


WebGPUCanvas::WebGPUCanvas( WebGPUContextResources *res,int w,int h,int f ):
	res(res),width(w),height(h),pixels(0),lock_count(0),font(0),
	scale_x(1.0f),scale_y(1.0f),origin_x(0),origin_y(0),handle_x(0),handle_y(0),
	mask(0),pixmap(0),dirty(false),pixmap_stale(false),clear_pending(false),
	gpu_written(false),is_surface(false),cube_face(0),cube_mode(0),
	texture(0),twidth(0),theight(0),texture_view(0){
	flags=f;

	color[0]=color[1]=color[2]=1.0f;
	cls_color[0]=cls_color[1]=cls_color[2]=0.0f;
	cls_argb=0;

	vx=vy=0;
	vw=w;vh=h;
}

WebGPUCanvas::WebGPUCanvas( WebGPUContextResources *res,int f ):WebGPUCanvas( res,0,0,f ){
}

WebGPUCanvas::~WebGPUCanvas(){
	if( res->pass_target==this ) res->endPass();
	res->notifyCanvasDestroyed( this );
	if( texture_view ){
		res->invalidateBindGroupsFor( texture_view );
		wgpuTextureViewRelease( texture_view );
		texture_view=0;
	}
	if( texture ){
		wgpuTextureRelease( texture );
		texture=0;
	}
	if( pixels ){
		delete[] pixels;pixels=0;
	}
	if( pixmap ){
		delete pixmap;pixmap=0;
	}
}

void WebGPUCanvas::resize( int w,int h,float d ){
	width=w;height=h;
	vx=vy=0;vw=w;vh=h;
}


WGPUTextureFormat WebGPUCanvas::format()const{
	return is_surface?res->surface_format:WGPUTextureFormat_RGBA8Unorm;
}

void WebGPUCanvas::ensureTexture(){
	if( is_surface ) return;

	int w=width>0?width:1,h=height>0?height:1;
	if( texture && twidth==w && theight==h ) return;

	if( texture_view ){
		res->invalidateBindGroupsFor( texture_view );
		wgpuTextureViewRelease( texture_view );
		texture_view=0;
	}
	if( texture ){
		wgpuTextureRelease( texture );
		texture=0;
	}

	WGPUTextureDescriptor desc={};
	desc.label=bbStrView( "bb.canvas" );
	desc.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_RenderAttachment|
	           WGPUTextureUsage_CopySrc|WGPUTextureUsage_CopyDst;
	desc.dimension=WGPUTextureDimension_2D;
	desc.size={ (uint32_t)w,(uint32_t)h,1 };
	desc.format=WGPUTextureFormat_RGBA8Unorm;
	desc.mipLevelCount=1;
	desc.sampleCount=1;
	texture=wgpuDeviceCreateTexture( res->device,&desc );
	texture_view=wgpuTextureCreateView( texture,0 );
	twidth=w;theight=h;
}

WGPUTextureView WebGPUCanvas::targetView(){
	if( is_surface ) return res->acquireSurfaceView();
	if( dirty ) uploadData();
	ensureTexture();
	return texture_view;
}

WGPUTextureView WebGPUCanvas::sampleView(){
	if( is_surface ) return res->acquireSurfaceView();
	if( dirty ) uploadData();
	ensureTexture();
	return texture_view;
}

bool WebGPUCanvas::consumeClearPending(){
	bool c=clear_pending;
	clear_pending=false;
	return c;
}

void WebGPUCanvas::getClsColorf( float col[4] )const{
	col[0]=cls_color[0];col[1]=cls_color[1];col[2]=cls_color[2];col[3]=1.0f;
}


void WebGPUCanvas::uploadData(){
	if( !res->device ) return;

	BBPixmap *pm=0;
	const void *data=0;

	if( pixels ){
		data=pixels;
	}else if( pixmap && pixmap->bits ){
		pm=d_new BBPixmap;
		pm->format=pixmap->format;
		pm->width=pixmap->width;
		pm->height=pixmap->height;
		pm->depth=pixmap->depth;
		pm->pitch=pixmap->pitch;
		pm->bpp=pixmap->bpp;
		pm->trans=pixmap->trans;

		int size=pm->width*pm->bpp*pm->height;
		pm->bits=new unsigned char[size];
		memcpy( pm->bits,pixmap->bits,size );

		if( flags&CANVAS_TEX_MASK ){
			pm->mask( (mask>>16)&255,(mask>>8)&255,mask&255 );
		}

		data=pm->bits;
	}

	dirty=false;
	if( !data ){
		ensureTexture();
		delete pm;
		return;
	}

	res->flush();

	WGPUTexture tex;
	WGPUTextureFormat fmt=format();
	if( is_surface ){
		if( !res->acquireSurfaceView() ){ delete pm;return; }
		tex=res->surface_texture;
	}else{
		ensureTexture();
		tex=texture;
	}

	const unsigned char *src=(const unsigned char*)data;
	std::vector<unsigned char> swapped;
	if( fmt==WGPUTextureFormat_BGRA8Unorm ){
		swapped.resize( (size_t)width*height*4 );
		for( size_t i=0;i<(size_t)width*height;i++ ){
			swapped[i*4+0]=src[i*4+2];
			swapped[i*4+1]=src[i*4+1];
			swapped[i*4+2]=src[i*4+0];
			swapped[i*4+3]=src[i*4+3];
		}
		src=swapped.data();
	}

	WGPUTexelCopyTextureInfo dst={};
	dst.texture=tex;
	dst.aspect=WGPUTextureAspect_All;
	WGPUTexelCopyBufferLayout layout={};
	layout.offset=0;
	layout.bytesPerRow=(uint32_t)width*4;
	layout.rowsPerImage=(uint32_t)height;
	WGPUExtent3D extent={ (uint32_t)width,(uint32_t)height,1 };
	wgpuQueueWriteTexture( res->queue,&dst,src,(size_t)width*height*4,&layout,&extent );

	pixmap_stale=false;
	delete pm;
}

struct BBMapRequest{
	volatile bool done=false;
	bool ok=false;
};

static void bbOnMapped( WGPUMapAsyncStatus status,WGPUStringView message,void *userdata1,void *userdata2 ){
	BBMapRequest *req=(BBMapRequest*)userdata1;
	req->ok=( status==WGPUMapAsyncStatus_Success );
	if( !req->ok ){
		LOGD( "[webgpu] mapAsync failed: %.*s",(int)message.length,message.data?message.data:"" );
	}
	req->done=true;
}

bool WebGPUCanvas::readbackInto( unsigned char *dst ){
	if( !dst || !res->device || width<=0 || height<=0 ) return false;

	WGPUTexture tex;
	WGPUTextureFormat fmt=format();
	if( is_surface ){
		if( !res->acquireSurfaceView() ) return false;
		tex=res->surface_texture;
	}else{
		if( dirty ) uploadData();
		ensureTexture();
		tex=texture;
	}

	res->flush();

	size_t bpr=((size_t)width*4+255)&~(size_t)255;
	size_t size=bpr*height;

	WGPUBufferDescriptor bdesc={};
	bdesc.label=bbStrView( "bb.readback" );
	bdesc.usage=WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst;
	bdesc.size=size;
	WGPUBuffer buf=wgpuDeviceCreateBuffer( res->device,&bdesc );
	if( !buf ) return false;

	WGPUCommandEncoderDescriptor edesc={};
	edesc.label=bbStrView( "bb.readback.encoder" );
	WGPUCommandEncoder enc=wgpuDeviceCreateCommandEncoder( res->device,&edesc );

	WGPUTexelCopyTextureInfo srci={};
	srci.texture=tex;
	srci.aspect=WGPUTextureAspect_All;
	WGPUTexelCopyBufferInfo dsti={};
	dsti.layout.offset=0;
	dsti.layout.bytesPerRow=(uint32_t)bpr;
	dsti.layout.rowsPerImage=(uint32_t)height;
	dsti.buffer=buf;
	WGPUExtent3D extent={ (uint32_t)width,(uint32_t)height,1 };
	wgpuCommandEncoderCopyTextureToBuffer( enc,&srci,&dsti,&extent );

	WGPUCommandBuffer commands=wgpuCommandEncoderFinish( enc,0 );
	wgpuCommandEncoderRelease( enc );
	wgpuQueueSubmit( res->queue,1,&commands );
	wgpuCommandBufferRelease( commands );

	BBMapRequest req;
	WGPUBufferMapCallbackInfo cb={};
	cb.mode=WGPUCallbackMode_AllowSpontaneous;
	cb.callback=bbOnMapped;
	cb.userdata1=&req;
	wgpuBufferMapAsync( buf,WGPUMapMode_Read,0,size,cb );
	bbWebGPUWait( res,&req.done );

	if( !req.ok ){
		wgpuBufferRelease( buf );
		return false;
	}

	const unsigned char *mapped=(const unsigned char*)wgpuBufferGetConstMappedRange( buf,0,size );
	if( !mapped ){
		wgpuBufferUnmap( buf );
		wgpuBufferRelease( buf );
		return false;
	}

	bool bgra=( fmt==WGPUTextureFormat_BGRA8Unorm );
	for( int y=0;y<height;y++ ){
		const unsigned char *in=mapped+(size_t)y*bpr;
		unsigned char *out=dst+(size_t)y*width*4;
		if( bgra ){
			for( int x=0;x<width;x++ ){
				out[x*4+0]=in[x*4+2];
				out[x*4+1]=in[x*4+1];
				out[x*4+2]=in[x*4+0];
				out[x*4+3]=in[x*4+3];
			}
		}else{
			memcpy( out,in,(size_t)width*4 );
		}
	}

	wgpuBufferUnmap( buf );
	wgpuBufferRelease( buf );
	return true;
}

void WebGPUCanvas::syncPixmapFromTexture(){
	if( !pixmap || !pixmap->bits || !pixmap_stale ) return;
	if( pixmap->bpp==4 && pixmap->width==width && pixmap->height==height ){
		if( readbackInto( pixmap->bits ) ) pixmap_stale=false;
	}
}

void WebGPUCanvas::downloadData(){
	if( pixels && pixmap && pixmap->bits && !pixmap_stale &&
	    pixmap->bpp==4 && pixmap->width==width && pixmap->height==height ){
		memcpy( pixels,pixmap->bits,(size_t)width*height*4 );
		return;
	}

	unsigned char *bits=pixels?pixels:(pixmap?pixmap->bits:0);
	if( bits ){
		readbackInto( bits );
		if( pixmap && bits==pixmap->bits ) pixmap_stale=false;
	}
}

void WebGPUCanvas::setPixmap( BBPixmap *pm ){
	if( pixmap==pm ) return;

	dirty=true;

	if( !pm ){
		if( pixmap ){
			delete pixmap;
		}
		pixmap=0;
		return;
	}

	if( pixmap ) delete pixmap;
	pixmap=pm;
	pixmap_stale=false;

	width=pm->width;
	height=pm->height;
	vx=vy=0;vw=width;vh=height;
	uploadData();
}


void WebGPUCanvas::draw2d( WGPUPrimitiveTopology topology,bool blend,WGPUTextureView tex,
                           WGPUSampler sampler,const float xywh[4],const float col[3],
                           const float scale[2],const BBWebGPUVertex *verts,int nverts ){
	if( !res->device || width<=0 || height<=0 || nverts<=0 ) return;

	int cx=vx,cy=vy,cw=vw,ch=vh;
	if( cw<=0 || ch<=0 ){ cx=0;cy=0;cw=width;ch=height; }
	if( cx<0 ){ cw+=cx;cx=0; }
	if( cy<0 ){ ch+=cy;cy=0; }
	if( cx+cw>width ) cw=width-cx;
	if( cy+ch>height ) ch=height-cy;
	if( cw<=0 || ch<=0 ) return;

	bool texenabled=( tex!=0 );
	if( !tex ) tex=res->white_view;
	if( !sampler ) sampler=res->sampler_linear;

	WGPUTextureView view=targetView();
	if( !view ) return;

	size_t vbytes=(size_t)nverts*sizeof( BBWebGPUVertex );
	size_t voff_est=(res->vertex_used+15)&~(size_t)15;
	size_t uoff_est=(res->uniform_used+255)&~(size_t)255;
	if( voff_est+vbytes>res->vertex_capacity || uoff_est+256>res->uniform_capacity ){
		res->flush();
	}

	uint32_t voffset=res->pushVertices( verts,nverts );
	BBWebGPURenderState state={};
	state.xywh[0]=xywh[0];state.xywh[1]=xywh[1];state.xywh[2]=xywh[2];state.xywh[3]=xywh[3];
	state.res[0]=(float)width;state.res[1]=(float)height;
	state.texscale[0]=1.0f;state.texscale[1]=1.0f;
	state.color[0]=col[0];state.color[1]=col[1];state.color[2]=col[2];
	state.texenabled=texenabled?1:0;
	state.scale[0]=scale[0];state.scale[1]=scale[1];
	uint32_t uoffset=res->pushUniforms( state );

	WGPURenderPassEncoder pass=res->ensurePass( this );
	if( !pass ) return;

	wgpuRenderPassEncoderSetScissorRect( pass,(uint32_t)cx,(uint32_t)cy,(uint32_t)cw,(uint32_t)ch );
	wgpuRenderPassEncoderSetPipeline( pass,res->getPipeline( topology,blend,format() ) );
	wgpuRenderPassEncoderSetBindGroup( pass,0,res->getBindGroup( tex,sampler ),1,&uoffset );
	wgpuRenderPassEncoderSetVertexBuffer( pass,0,res->vertex_buffer,voffset,vbytes );
	wgpuRenderPassEncoderDraw( pass,(uint32_t)nverts,1,0,0 );

	pixmap_stale=true;
	gpu_written=true;
}

void WebGPUCanvas::quad( int x,int y,int w,int h,bool solid,bool blend,
                         WGPUTextureView tex,float col[3] ){
	float xywh[4]={ (float)x,(float)y,(float)w,(float)h };
	float scale[2]={ scale_x,scale_y };

	if( solid ){
		static const BBWebGPUVertex verts[6]={
			{ 0.f,0.f,0.f,0.f },
			{ 0.f,1.f,0.f,1.f },
			{ 1.f,0.f,1.f,0.f },
			{ 1.f,0.f,1.f,0.f },
			{ 0.f,1.f,0.f,1.f },
			{ 1.f,1.f,1.f,1.f }
		};
		draw2d( WGPUPrimitiveTopology_TriangleList,blend,tex,res->sampler_linear,xywh,col,scale,verts,6 );
	}else{
		static const BBWebGPUVertex verts[8]={
			{ 0.f,0.f,0.f,0.f },{ 0.f,1.f,0.f,1.f },
			{ 0.f,1.f,0.f,1.f },{ 1.f,1.f,1.f,1.f },
			{ 1.f,1.f,1.f,1.f },{ 1.f,0.f,1.f,0.f },
			{ 1.f,0.f,1.f,0.f },{ 0.f,0.f,0.f,0.f }
		};
		draw2d( WGPUPrimitiveTopology_LineList,blend,tex,res->sampler_linear,xywh,col,scale,verts,8 );
	}
}

void WebGPUCanvas::cls(){
	bool full=( vx<=0 && vy<=0 && vx+vw>=width && vy+vh>=height );
	if( full ){
		if( res->pass_target==this ) res->endPass();
		clear_pending=true;
		res->ensurePass( this );
		pixmap_stale=true;
		gpu_written=true;
	}else{
		float col[3]={ cls_color[0],cls_color[1],cls_color[2] };
		float xywh[4]={ (float)vx,(float)vy,(float)vw,(float)vh };
		float scale[2]={ 1.0f,1.0f };
		static const BBWebGPUVertex verts[6]={
			{ 0.f,0.f,0.f,0.f },
			{ 0.f,1.f,0.f,1.f },
			{ 1.f,0.f,1.f,0.f },
			{ 1.f,0.f,1.f,0.f },
			{ 0.f,1.f,0.f,1.f },
			{ 1.f,1.f,1.f,1.f }
		};
		draw2d( WGPUPrimitiveTopology_TriangleList,false,0,0,xywh,col,scale,verts,6 );
	}
}

void WebGPUCanvas::plot( int x,int y ){
	quad( x,y,1,1,true,false,0,color );
}

void WebGPUCanvas::line( int x,int y,int x2,int y2 ){
	const BBWebGPUVertex verts[2]={
		{ (float)x,(float)y,0.f,0.f },
		{ (float)x2,(float)y2,0.f,0.f }
	};
	float xywh[4]={ 0.f,0.f,1.f,1.f };
	float scale[2]={ scale_x,scale_y };
	draw2d( WGPUPrimitiveTopology_LineList,false,0,0,xywh,color,scale,verts,2 );
}

void WebGPUCanvas::rect( int x,int y,int w,int h,bool solid ){
	quad( x,y,w,h,solid,false,0,color );
}

void WebGPUCanvas::oval( int x,int y,int w,int h,bool solid ){
	int rx=w/2,ry=h/2;
	int segs=abs( rx )+abs( ry );
	if( segs<3 ) segs=3;

	float theta=2*3.1415926f/float( segs );
	float c=cosf( theta ),s=sinf( theta ),t;

	x+=rx;y+=ry;

	float ux=1.0f,uy=0.0f;

	std::vector<BBWebGPUVertex> ring;
	for( int i=0;i<segs;i++ ){
		BBWebGPUVertex v={ ux*rx+x,uy*ry+y,0.f,0.f };
		ring.push_back( v );

		t=ux;
		ux=c*ux-s*uy;
		uy=s*t+c*uy;
	}

	std::vector<BBWebGPUVertex> verts;
	if( solid ){
		for( int i=1;i+1<(int)ring.size();i++ ){
			verts.push_back( ring[0] );
			verts.push_back( ring[i] );
			verts.push_back( ring[i+1] );
		}
	}else{
		for( int i=0;i<(int)ring.size();i++ ){
			verts.push_back( ring[i] );
			verts.push_back( ring[(i+1)%ring.size()] );
		}
	}

	float xywh[4]={ 0.f,0.f,1.f,1.f };
	float scale[2]={ scale_x,scale_y };
	draw2d( solid?WGPUPrimitiveTopology_TriangleList:WGPUPrimitiveTopology_LineList,
	        false,0,0,xywh,color,scale,verts.data(),(int)verts.size() );
}

void WebGPUCanvas::text( int x,int y,const std::string &t ){
	if( !font || t.size()==0 || !res->device ) return;

	bool changed=font->loadChars( t );
	if( changed ) font->rebuildAtlas();
	if( !font->atlas ) return;

	WebGPUContextResources::FontTexture &ft=res->font_textures[font];

	if( changed || !ft.tex || ft.width!=font->atlas->width || ft.height!=font->atlas->height ){
		if( ft.tex && ft.width==font->atlas->width && ft.height==font->atlas->height ) res->flush();

		if( ft.tex && (ft.width!=font->atlas->width || ft.height!=font->atlas->height) ){
			if( ft.view ){
				res->invalidateBindGroupsFor( ft.view );
				wgpuTextureViewRelease( ft.view );
				ft.view=0;
			}
			wgpuTextureRelease( ft.tex );
			ft.tex=0;
		}

		if( !ft.tex ){
			WGPUTextureDescriptor desc={};
			desc.label=bbStrView( "bb.font.atlas" );
			desc.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst;
			desc.dimension=WGPUTextureDimension_2D;
			desc.size={ (uint32_t)font->atlas->width,(uint32_t)font->atlas->height,1 };
			desc.format=WGPUTextureFormat_RGBA8Unorm;
			desc.mipLevelCount=1;
			desc.sampleCount=1;
			ft.tex=wgpuDeviceCreateTexture( res->device,&desc );
			ft.view=wgpuTextureCreateView( ft.tex,0 );
			ft.width=font->atlas->width;
			ft.height=font->atlas->height;
		}

		int size=font->atlas->width*font->atlas->height;
		std::vector<unsigned char> bmp( (size_t)size*4 );
		for( int i=0;i<size;i++ ){
			bmp[i*4+0]=bmp[i*4+1]=bmp[i*4+2]=255;
			bmp[i*4+3]=font->atlas->bits[i];
		}

		WGPUTexelCopyTextureInfo dst={};
		dst.texture=ft.tex;
		dst.aspect=WGPUTextureAspect_All;
		WGPUTexelCopyBufferLayout layout={};
		layout.offset=0;
		layout.bytesPerRow=(uint32_t)font->atlas->width*4;
		layout.rowsPerImage=(uint32_t)font->atlas->height;
		WGPUExtent3D extent={ (uint32_t)font->atlas->width,(uint32_t)font->atlas->height,1 };
		wgpuQueueWriteTexture( res->queue,&dst,bmp.data(),bmp.size(),&layout,&extent );
	}

	std::vector<BBWebGPUVertex> verts;

	float fy=(float)y+font->baseline*font->density;
	float fx=(float)x;

	float awidth=(float)font->atlas->width,aheight=(float)font->atlas->height;

	const char *cstr=t.c_str();
	utf8_int32_t curr=0,prev=0;
	while( *cstr ){
		prev=curr;
		cstr=utf8codepoint( cstr,&curr );
		BBImageFont::Char ch=font->getChar( curr );

		float cx=fx+ch.bearing_x*font->density,cy=fy-ch.bearing_y*font->density;

		if( prev>0 ){
			cx+=font->getKerning( prev,curr );
		}

		float l=ch.x/awidth;
		float r=(ch.x+ch.width)/awidth;
		float tt=ch.y/aheight;
		float b=(ch.y+ch.height)/aheight;

		BBWebGPUVertex lb={ cx,cy+ch.height*font->density,l,b },
		               rb={ cx+ch.width*font->density,cy+ch.height*font->density,r,b },
		               rt={ cx+ch.width*font->density,cy,r,tt },
		               lt={ cx,cy,l,tt };

		verts.push_back( lb );verts.push_back( rb );verts.push_back( rt );
		verts.push_back( lb );verts.push_back( rt );verts.push_back( lt );

		fx+=ch.advance*font->density;
	}

	if( verts.empty() ) return;

	float xywh[4]={ 0.f,0.f,1.f,1.f };
	float scale[2]={ scale_x,scale_y };
	draw2d( WGPUPrimitiveTopology_TriangleList,true,ft.view,res->sampler_linear,
	        xywh,color,scale,verts.data(),(int)verts.size() );
}

void WebGPUCanvas::blit( int x,int y,BBCanvas *s,int src_x,int src_y,int src_w,int src_h,bool solid ){
	if( !s || width<=0 || height<=0 || s->getWidth()<=0 || s->getHeight()<=0 || src_w<=0 || src_h<=0 ) return;

	WebGPUCanvas *src=(WebGPUCanvas*)s;

	float sx,sy,dx,dy;
	src->getScale( &sx,&sy );
	getScale( &dx,&dy );

	float sx0=src_x*sx,sy0=src_y*sy;
	float sw=src_w*sx,sh=src_h*sy;
	float dx0=x*dx,dy0=y*dy;
	float dw=src_w*dx,dh=src_h*dy;

	float srcW=(float)src->getWidth(),srcH=(float)src->getHeight();

	WGPUTextureView view=0;
	WGPUTexture temp_tex=0;
	WGPUTextureView temp_view=0;

	if( src==this ){
		if( is_surface ){
			if( !res->acquireSurfaceView() ) return;
		}else{
			if( dirty ) uploadData();
			ensureTexture();
		}
		res->endPass();
		res->ensureEncoder();

		int cw=(int)srcW,ch=(int)srcH;
		WGPUTextureDescriptor tdesc={};
		tdesc.label=bbStrView( "bb.blit.temp" );
		tdesc.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst;
		tdesc.dimension=WGPUTextureDimension_2D;
		tdesc.size={ (uint32_t)cw,(uint32_t)ch,1 };
		tdesc.format=format();
		tdesc.mipLevelCount=1;
		tdesc.sampleCount=1;
		temp_tex=wgpuDeviceCreateTexture( res->device,&tdesc );
		temp_view=wgpuTextureCreateView( temp_tex,0 );

		WGPUTexelCopyTextureInfo from={};
		from.texture=is_surface?res->surface_texture:texture;
		from.aspect=WGPUTextureAspect_All;
		WGPUTexelCopyTextureInfo to={};
		to.texture=temp_tex;
		to.aspect=WGPUTextureAspect_All;
		WGPUExtent3D extent={ (uint32_t)cw,(uint32_t)ch,1 };
		wgpuCommandEncoderCopyTextureToTexture( res->encoder,&from,&to,&extent );

		view=temp_view;
	}else{
		view=src->sampleView();
	}
	if( !view ){
		if( temp_view ) wgpuTextureViewRelease( temp_view );
		if( temp_tex ) wgpuTextureRelease( temp_tex );
		return;
	}

	float u0=sx0/srcW,v0=sy0/srcH;
	float u1=(sx0+sw)/srcW,v1=(sy0+sh)/srcH;

	const BBWebGPUVertex verts[6]={
		{ 0.f,0.f,u0,v0 },
		{ 0.f,1.f,u0,v1 },
		{ 1.f,0.f,u1,v0 },
		{ 1.f,0.f,u1,v0 },
		{ 0.f,1.f,u0,v1 },
		{ 1.f,1.f,u1,v1 }
	};

	float white[3]={ 1.f,1.f,1.f };
	float xywh[4]={ dx0,dy0,dw,dh };
	float scale[2]={ 1.f,1.f };
	draw2d( WGPUPrimitiveTopology_TriangleList,false,view,res->sampler_nearest,
	        xywh,white,scale,verts,6 );

	if( temp_view ){
		res->invalidateBindGroupsFor( temp_view );
		wgpuTextureViewRelease( temp_view );
	}
	if( temp_tex ) wgpuTextureRelease( temp_tex );

	pixmap_stale=true;
}

void WebGPUCanvas::blitScaled( int x,int y,int w,int h,BBCanvas *s,int src_x,int src_y,int src_w,int src_h,bool filter ){
	if( !s || width<=0 || height<=0 || s->getWidth()<=0 || s->getHeight()<=0 ||
	    src_w<=0 || src_h<=0 || w<=0 || h<=0 ) return;

	WebGPUCanvas *src=(WebGPUCanvas*)s;

	float dx,dy;
	getScale( &dx,&dy );

	float dx0=x*dx,dy0=y*dy;
	float dw=w*dx,dh=h*dy;

	float srcW=(float)src->getWidth(),srcH=(float)src->getHeight();

	WGPUTextureView view=src->sampleView();
	if( !view ) return;

	float u0=(float)src_x/srcW,v0=(float)src_y/srcH;
	float u1=(float)(src_x+src_w)/srcW,v1=(float)(src_y+src_h)/srcH;

	const BBWebGPUVertex verts[6]={
		{ 0.f,0.f,u0,v0 },
		{ 0.f,1.f,u0,v1 },
		{ 1.f,0.f,u1,v0 },
		{ 1.f,0.f,u1,v0 },
		{ 0.f,1.f,u0,v1 },
		{ 1.f,1.f,u1,v1 }
	};

	float white[3]={ 1.f,1.f,1.f };
	float xywh[4]={ dx0,dy0,dw,dh };
	float scale[2]={ 1.f,1.f };
	draw2d( WGPUPrimitiveTopology_TriangleList,false,view,
	        filter?res->sampler_linear:res->sampler_nearest,xywh,white,scale,verts,6 );

	pixmap_stale=true;
}

bool WebGPUCanvas::stretchTo( BBCanvas *c,bool filter ){
	WebGPUCanvas *dest=(WebGPUCanvas*)c;
	if( !res->device || width<=0 || height<=0 || dest->width<=0 || dest->height<=0 ) return false;

	WGPUTextureView view=sampleView();
	if( !view ) return false;

	static const BBWebGPUVertex verts[6]={
		{ 0.f,0.f,0.f,0.f },
		{ 0.f,1.f,0.f,1.f },
		{ 1.f,0.f,1.f,0.f },
		{ 1.f,0.f,1.f,0.f },
		{ 0.f,1.f,0.f,1.f },
		{ 1.f,1.f,1.f,1.f }
	};
	float white[3]={ 1.f,1.f,1.f };
	float xywh[4]={ 0.f,0.f,(float)dest->width,(float)dest->height };
	float scale[2]={ 1.f,1.f };

	int ovx=dest->vx,ovy=dest->vy,ovw=dest->vw,ovh=dest->vh;
	dest->vx=0;dest->vy=0;dest->vw=dest->width;dest->vh=dest->height;
	dest->draw2d( WGPUPrimitiveTopology_TriangleList,false,view,
	              filter?res->sampler_linear:res->sampler_nearest,xywh,white,scale,verts,6 );
	dest->vx=ovx;dest->vy=ovy;dest->vw=ovw;dest->vh=ovh;

	dest->pixmap_stale=true;
	return true;
}

void WebGPUCanvas::image( BBCanvas *c,int x,int y,bool solid ){
	WebGPUCanvas *src=(WebGPUCanvas*)c;

	WGPUTextureView view=src->sampleView();
	if( !view ) return;

	float white[3]={ 1.f,1.f,1.f };
	quad( x-src->handle_x,y-src->handle_y,src->getWidth(),src->getHeight(),true,true,view,white );
}

bool WebGPUCanvas::collide( int x,int y,const BBCanvas *src,int src_x,int src_y,bool solid ){
	RTEX( "WebGPUCanvas::collide not implemented" );
	return false;
}

bool WebGPUCanvas::rect_collide( int x,int y,int rect_x,int rect_y,int rect_w,int rect_h,bool solid ){
	RTEX( "WebGPUCanvas::rect_collide not implemented" );
	return false;
}


bool WebGPUCanvas::lock(){
	if( lock_count++ ) return true;
	pixels=new unsigned char[(size_t)width*height*4];
	downloadData();
	return true;
}

void WebGPUCanvas::setPixel( int x,int y,unsigned argb ){
	lock();
	setPixelFast( x,y,argb );
	unlock();
}

#define UC(c) static_cast<unsigned char>(c)

void WebGPUCanvas::setPixelFast( int x,int y,unsigned argb ){
	if( !pixels || x<0 || y<0 || x>=width || y>=height ) return;
	unsigned char *p=pixels+((size_t)y*width+x)*4;
	p[0]=UC( (argb>>16)&0xff );
	p[1]=UC( (argb>>8)&0xff );
	p[2]=UC( argb&0xff );
	p[3]=UC( (argb>>24)&0xff );
}

void WebGPUCanvas::copyPixel( int x,int y,BBCanvas *src,int src_x,int src_y ){
	RTEX( "WebGPUCanvas::copyPixel not implemented" );
}

void WebGPUCanvas::copyPixelFast( int x,int y,BBCanvas *src,int src_x,int src_y ){
	RTEX( "WebGPUCanvas::copyPixelFast not implemented" );
}

unsigned WebGPUCanvas::getPixel( int x,int y ){
	lock();
	unsigned rgb=getPixelFast( x,y );
	unlock();
	return rgb;
}

unsigned WebGPUCanvas::getPixelFast( int x,int y ){
	if( !pixels || x<0 || y<0 || x>=width || y>=height ) return 0;
	unsigned char *p=pixels+((size_t)y*width+x)*4;
	return (UC(p[3])<<24)|(UC(p[0])<<16)|(UC(p[1])<<8)|UC(p[2]);
}

void WebGPUCanvas::unlock(){
	if( !pixels ) return;
	if( --lock_count>0 ) return;
	if( lock_count<0 ) lock_count=0;
	uploadData();
	delete[] pixels;pixels=0;
}


void WebGPUCanvas::set(){
}

void WebGPUCanvas::unset(){
	if( res->pass_target==this ) res->endPass();
}

void WebGPUCanvas::setFont( BBFont *f ){
	font=reinterpret_cast<BBImageFont*>( f );
}

void WebGPUCanvas::setMask( unsigned argb ){
	mask=argb;
	flags|=CANVAS_TEX_MASK;
	syncPixmapFromTexture();
	dirty=true;
}

void WebGPUCanvas::setColor( unsigned argb ){
	color[0]=((argb>>16)&255)/255.0f;
	color[1]=((argb>>8)&255)/255.0f;
	color[2]=(argb&255)/255.0f;
}

void WebGPUCanvas::setClsColor( unsigned argb ){
	cls_argb=argb;
	cls_color[0]=((argb>>16)&255)/255.0f;
	cls_color[1]=((argb>>8)&255)/255.0f;
	cls_color[2]=(argb&255)/255.0f;
}

void WebGPUCanvas::setOrigin( int x,int y ){
	origin_x=x;
	origin_y=y;
}

void WebGPUCanvas::setScale( float x,float y ){
	scale_x=x;
	scale_y=y;
}

void WebGPUCanvas::setHandle( int x,int y ){
	handle_x=x;
	handle_y=y;
}

void WebGPUCanvas::setViewport( int x,int y,int w,int h ){
	vx=x;vy=y;vw=w;vh=h;
}

void WebGPUCanvas::setCubeMode( int mode ){
	RTEX( "WebGPUCanvas::setCubeMode not implemented" );
}

void WebGPUCanvas::setCubeFace( int face ){
	cube_face=face;
}


int WebGPUCanvas::getWidth()const{
	return width;
}

int WebGPUCanvas::getHeight()const{
	return height;
}

int WebGPUCanvas::getDepth()const{
	return 8;
}

int WebGPUCanvas::cubeMode()const{
	return cube_mode;
}

void WebGPUCanvas::getOrigin( int *x,int *y )const{
	*x=origin_x;*y=origin_y;
}

void WebGPUCanvas::getScale( float *x,float *y )const{
	*x=scale_x;*y=scale_y;
}

void WebGPUCanvas::getHandle( int *x,int *y )const{
	*x=handle_x;*y=handle_y;
}

void WebGPUCanvas::getViewport( int *x,int *y,int *w,int *h )const{
	*x=vx;*y=vy;*w=vw;*h=vh;
}

unsigned WebGPUCanvas::getMask()const{
	return mask;
}

unsigned WebGPUCanvas::getColor()const{
	unsigned r=(unsigned)(color[0]*255.0f+0.5f);
	unsigned g=(unsigned)(color[1]*255.0f+0.5f);
	unsigned b=(unsigned)(color[2]*255.0f+0.5f);
	return (r<<16)|(g<<8)|b;
}

unsigned WebGPUCanvas::getClsColor()const{
	return cls_argb;
}
