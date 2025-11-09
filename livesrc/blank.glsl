#ifdef PATTERNS
/vispat [1 0 0.5 0]
#endif

uniform float vispat;

vec4 pixel(vec2 uv) {
uv-=vec2(cc[0],cc[1]);
uv.x*=16./9.;
//return vec4(uv.xy*0.1,square(square(1.-fract(t*4.))),1.); 
//return vec4(levels_smooth.xxx,1.);
return max(vec4(0.),vec4(0*uv.xy,0,1)+vec4(vec3(1000.,500.,1250.)*exp(-100000.*lengthsq(uv)),1.));
}
