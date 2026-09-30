
struct BBRenderState {
  xywh : vec4<f32>,
  res : vec2<f32>,
  texscale : vec2<f32>,
  color : vec3<f32>,
  texenabled : i32,
  scale : vec2<f32>,
};

@group(0) @binding(0) var<uniform> RS : BBRenderState;
@group(0) @binding(1) var u_sampler : sampler;
@group(0) @binding(2) var u_tex : texture_2d<f32>;

struct BBPerVertex {
  @builtin(position) position : vec4<f32>,
  @location(0) color : vec3<f32>,
  @location(1) texcoord : vec2<f32>,
};

@vertex
fn vs_main( @location(0) a_position : vec2<f32>,
            @location(1) a_texcoord : vec2<f32> ) -> BBPerVertex {
  var v : BBPerVertex;

  let xy = RS.xywh.xy;
  let wh = RS.xywh.zw;

  var p = (a_position * wh) + xy;

  p = p * RS.scale;

  p.y = RS.res.y - p.y;
  p = p / RS.res;

  p = p * 2.0 - vec2<f32>( 1.0,1.0 );

  v.position = vec4<f32>( p,0.0,1.0 );
  v.color = RS.color;
  v.texcoord = a_texcoord * RS.texscale;
  return v;
}

@fragment
fn fs_main( v : BBPerVertex ) -> @location(0) vec4<f32> {
  if( RS.texenabled==1 ){
    return textureSample( u_tex,u_sampler,v.texcoord ) * vec4<f32>( v.color,1.0 );
  }
  return vec4<f32>( v.color,1.0 );
}
