#ifdef PATTERNS
/vispat [down pow 2 mul 10]*4
/xx [rand smooth 1]*4
/yy [rand smooth 1]*4
/scal [rand smooth 1]*4
#endif
uniform vec4 vispat;
uniform vec4 xx,yy,scal;
vec4 pixel(vec2 _uv) {
	vec3 o = vec3(0.);
    for (float t = 0.; t<0.5; t+=0.01) {
    	vec2 uv=_uv;
        uv=rot2(0.5*mix(vispat.w, vispat.z, t))*uv;
        uv*=exp(mix(scal.x,scal.y,t)*2-1.);
        uv.x+=mix(xx.x,xx.y,t)*10.4-0.2+0.5;
        uv.y+=mix(yy.x,yy.y,t)*10.2-0.1+0.5;
        uv*=4.;
        uv=fract(uv*0.5)*2-0.5;
        vec3 c=vec3(0.1,0.05,0.15);
        c=mix(c,vec3(0.4,0.3,0.5), aa(sdCircle(uv, 0.4)));
        uv=rot2(mix(vispat.w, vispat.z, t))*uv;
        c=mix(c,vec3(0.15,0.05,0.1), aa(sdRect(uv, vec2(0.1,0.3))));
        o+=c;
    }
    return vec4(o/50.,1.);
}
