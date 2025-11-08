#ifdef PATTERNS
/vispat [1 0 0.5 0]
#endif

uniform float vispat;

vec4 pixel(vec2 uv) {
uv-=0.5;//vec2(cc[0],cc[1]);
uv.x*=16./9.;
//return vec4(uv.xy*0.1,square(square(1.-fract(t*4.))),1.); 
//return vec4(levels_smooth.xxx,1.);
return vec4((1.-fract(t))*vec3(10000.,5000.,12500.)*exp(-10000.*lengthsq(uv)),1.);
}
