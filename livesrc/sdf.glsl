// taa
// sky venice_sunset
#ifdef PATTERNS
/vispat [1 0 0.5 0]
#endif

uniform float vispat;
vec3 skycol(vec3 d_norm, float lod) {
    return textureLod(uSky, vec2(atan(d_norm.x,d_norm.z)*(0.5/PI), 0.5-asin(d_norm.y)*(1./PI)), lod).xyz;
}

vec4 pixel(vec2 uv) {
    vec4 o=vec4(0.);
    vec4 r4=rnd4();
    float tofs = r4.x;
    vec3 c=vec3(0.);
    float disparity=0.;
    for (int smpl = 0; smpl<4;smpl++) {
        float shuttert= (smpl+tofs)*(1.f/16.f)+0.5f;
        vec3 ro,rd;
        eyeray(shuttert, ro, rd);
        float thpt=0.1;
        for (int bounce = 0; bounce < 3; ++bounce) {
            float tplane = -ro.z / rd.z;
            if (tplane > 0.0001) {
                vec2 tbox = aabb_intersect(ro, 1./rd, vec3(-2.), vec3(2.,2.,10.));
                vec3 n=vec3(0,0,-1.);
                if (ro.z>0.) { // inside the box 
                    tplane=tbox.y;
                    ro = ro + rd * tplane;
                    if (ro.z<0.) {
                        c+=skycol(rd,0.) * thpt;
                        break;
                    }
                } else
                if (tbox.x<tplane && tbox.y>tplane) {
                    tplane=tbox.y;
                    ro = ro + rd * tplane;
                    if (ro.x>1.999) n=vec3(-1,0,0); 
                    else if (ro.x< -1.999) n=vec3(1,0,0);
                    else if (ro.y> 1.999) n=vec3(0,-1,0);
                    else if (ro.y< -1.999) n=vec3(0,1,0);
                }
                ro = ro + rd * tplane;
                r4=rnd4();
                rd = rnd_dir_cos(n, r4.xy);
                thpt *= 0.9;
                if (bounce ==0) disparity=1./tplane;
            } else {
                c+=skycol(rd,0.) * thpt;
                break;
            }
        }
    }
    c*=1./2.;
    o = vec4(c, disparity);
    return o;
}
