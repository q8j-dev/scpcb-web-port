#include "../stdutil/stdutil.h"
#include "runtime.sdl.h"
#include <bb/pixmap/pixmap.h>
#include <bb/event/event.h>
#include <bb/system/system.h>
#include <bb/input/input.h>
#include <bb/hook/hook.h>
#ifdef BB_WEBGPU
#include <bb/graphics.webgpu/graphics.webgpu.h>
#endif

#include "scancodes.cpp"

static int em_virt_x=0,em_virt_y=0;

class SDLInputDriver : public BBInputDriver{
public:
	~SDLInputDriver(){}

	BBDevice *getJoystick( int port )const{ return 0; }
	int getJoystickType( int port )const{ return 0; }
	int numJoysticks()const{ return 0; }

	int toAscii( int code )const{
		return code&0xff;
	}
};

class SDLJoystick : public BBDevice{
private:
	SDL_Joystick *js;
public:
	SDLJoystick( SDL_Joystick *js ):js(js){
		memset( axis_states,0,sizeof(axis_states) );
		memset( down_state,0,sizeof(down_state) );

		SDL_JoystickGetGUIDString( SDL_JoystickGetGUID(js),id,sizeof(id) );
		snprintf( name,sizeof(name),"%s",SDL_JoystickName( js ) );
	}

	void update(){
		int ax_count=SDL_JoystickNumAxes( js );
		if( ax_count>32 ) ax_count=32;

		for( int i=0;i<ax_count;i++ ){
			axis_states[i]=(float)SDL_JoystickGetAxis( js,i )/SHRT_MAX;
		}

		int btn_count=SDL_JoystickNumButtons( js );
		for( int i=0;i<btn_count;i++ ){
			setDownState( i,SDL_JoystickGetButton( js,i ) );
		}
	}
};

BBRuntime *bbCreateOpenGLRuntime(){
	return d_new SDLRuntime();
}

SDLRuntime::SDLRuntime(){
}

SDLRuntime::~SDLRuntime(){
	SDL_Quit();
}

void SDLRuntime::afterCreate(){
	SDL_InitSubSystem( SDL_INIT_JOYSTICK );

	gx_input=d_new SDLInputDriver();

	for( int i=0;i<SDL_NumJoysticks();i++ ){
		SDL_Joystick *js=SDL_JoystickOpen( i );
		if( js ){
			SDLJoystick *j=d_new SDLJoystick( js );
			bbJoysticks.push_back( j );
		}
	}

#ifdef BB_WEBGPU
	if( !BBContextDriver::change( "webgpu" ) ){
		LOGD( "%s","[webgpu] context driver missing" );
	}
#else
	BBContextDriver::change( "sdl" );
#endif
}

void SDLRuntime::asyncStop(){
}

void SDLRuntime::asyncRun(){
}

void SDLRuntime::asyncEnd(){
}

bool SDLRuntime::idle(){
	SDL_Event event;
	while( SDL_PollEvent(&event) ){
		if( event.type == SDL_QUIT ){
			RTEX( 0 );
		}else if( event.type==SDL_MOUSEMOTION ){
			if( SDL_GetRelativeMouseMode()==SDL_TRUE ){
				em_virt_x+=event.motion.xrel;
				em_virt_y+=event.motion.yrel;
				if( SDL_Window *w=SDL_GetWindowFromID( event.motion.windowID ) ){
					int ww=0,wh=0;
					SDL_GetWindowSize( w,&ww,&wh );
					if( em_virt_x<0 ) em_virt_x=0; else if( ww>0 && em_virt_x>=ww ) em_virt_x=ww-1;
					if( em_virt_y<0 ) em_virt_y=0; else if( wh>0 && em_virt_y>=wh ) em_virt_y=wh-1;
				}
				BBEvent ev( BBEVENT_MOUSEMOVE,0,em_virt_x,em_virt_y );
				bbOnEvent.run( &ev );
			}else{
				em_virt_x=event.motion.x;em_virt_y=event.motion.y;
				BBEvent ev( BBEVENT_MOUSEMOVE,0,event.motion.x,event.motion.y );
				bbOnEvent.run( &ev );
			}
		}else if( event.type==SDL_MOUSEWHEEL ){
			int notches=event.wheel.y;
#if SDL_VERSION_ATLEAST(2,0,4)
			if( event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED ) notches=-notches;
#endif
			if( notches ){
				BBEvent ev( BBEVENT_MOUSEWHEEL,notches*120 );
				bbOnEvent.run( &ev );
			}
		}else if( event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP ){
			int button=0;
			switch( event.button.button ){
			case SDL_BUTTON_LEFT:button=1;break;
			case SDL_BUTTON_MIDDLE:button=3;break;
			case SDL_BUTTON_RIGHT:button=2;break;
			}

			if( button ){
				BBEvent ev( event.type==SDL_MOUSEBUTTONDOWN?BBEVENT_MOUSEDOWN:BBEVENT_MOUSEUP,button );
				bbOnEvent.run( &ev );
			}
		}else if( (event.type==SDL_KEYDOWN||event.type==SDL_KEYUP) && event.key.repeat==0 ){
			int code=event.key.keysym.scancode;
			if( code>=MAX_SDL_SCANCODES ) continue;

			int key=SDL_SCANCODE_MAP[code];
			if( !key ){
				LOGD( "unmapped key code: %i",code );
				continue;
			}

			BBEvent ev;
			switch( event.type ){
			case SDL_KEYDOWN:
				ev=BBEvent( BBEVENT_KEYDOWN,key );
				break;
			case SDL_KEYUP:
				ev=BBEvent( BBEVENT_KEYUP,key );
				break;
			default:
				continue;
			}
			bbOnEvent.run( &ev );

			if( event.type==SDL_KEYDOWN ){
				BBEvent ev=BBEvent( BBEVENT_CHAR,0 );
				switch( code ){
				case SDL_SCANCODE_BACKSPACE:
					ev.data='\b';
					break;
				case SDL_SCANCODE_RETURN:case SDL_SCANCODE_RETURN2:case SDL_SCANCODE_KP_ENTER:
					ev.data='\n';
					break;
				case SDL_SCANCODE_UP:
					ev.data=BBInputDriver::ASC_UP;
					break;
				case SDL_SCANCODE_DOWN:
					ev.data=BBInputDriver::ASC_DOWN;
					break;
				case SDL_SCANCODE_LEFT:
					ev.data=BBInputDriver::ASC_LEFT;
					break;
				case SDL_SCANCODE_RIGHT:
					ev.data=BBInputDriver::ASC_RIGHT;
					break;
				}
				if( ev.data ) bbOnEvent.run( &ev );
			}
		}else if( event.type==SDL_TEXTINPUT||event.type==SDL_TEXTEDITING ){
			char *c=event.text.text;
			while( *c ){
				BBEvent ev=BBEvent( BBEVENT_CHAR,*(c++) );
				bbOnEvent.run( &ev );
			}
		}
	}

	return true;
}

void *SDLRuntime::window(){
	return 0;
}

void SDLRuntime::moveMouse( int x,int y ){
	BBGraphics *graphics=bbContextDriver?bbContextDriver->getGraphics():0;
	if( !graphics ) return;
	em_virt_x=x;em_virt_y=y;
#ifdef BB_WEBGPU
	((WebGPUGraphics*)graphics)->moveMouse( x,y );
#else
	((SDLGraphics*)graphics)->moveMouse( x,y );
#endif
}

void SDLRuntime::setPointerVisible( bool vis ){
	SDL_bool rel=vis?SDL_FALSE:SDL_TRUE;
	if( SDL_GetRelativeMouseMode()!=rel ) SDL_SetRelativeMouseMode( rel );
	SDL_ShowCursor( vis?SDL_ENABLE:SDL_DISABLE );
}

void SDLRuntime::_refreshTitle( void *data,void *context ){
	((SDLRuntime*)context)->setTitle( ((BBApp*)data)->title.c_str() );
}

void SDLRuntime::setTitle( const char *title ){
}

BBMODULE_CREATE( runtime_sdl ){
	return true;
}

BBMODULE_DESTROY( runtime_sdl ){
	return true;
}
