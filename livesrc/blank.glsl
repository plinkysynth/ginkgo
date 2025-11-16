//taa
#ifdef PATTERNS
/vispat [down pow 10 mul 50]*4
#endif
uniform vec3 vispat;
vec4 pixel(vec2 uv) {
    vec3 c=vec3(0.1,0.05,0.15);
    c=mix(c,vec3(0.4,0.3,0.5), aa(sdCircle(uv, 0.4)));
    uv=rot2(vispat.z)*uv;
    c=mix(c,vec3(0.15,0.05,0.1), aa(sdRect(uv, vec2(0.1,0.3))));
    return vec4(c,1.);
}
