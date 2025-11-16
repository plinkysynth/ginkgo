//taa
#ifdef PATTERNS
/vispat [1 0 0.5 0]
#endif

uniform float vispat;
float map(vec3 p) {
	float b = p.y+1.;
    b=min(b,1.-p.x);
    b=min(b,sdBox(p-vec3(-2.,7.,5.), vec3(1.,0.1,1.)));
    p.yz=fract((p.yz+1.)*0.5)*2.-1.;
    return min(b,sdSphere(p, 0.9));
}
vec4 pixel(vec2 uv) {
    vec4 o=vec4(0.);
    vec4 r4=rnd4();
    float tofs = r4.x;
    vec3 c=vec3(0.);
    for (int smpl = 0; smpl<2;smpl++) {
        float shuttert= (smpl+tofs)*(1.f/16.f)+0.5f;
        vec3 ro,rd;
        eyeray(shuttert, ro, rd);
        vec3 thpt=vec3(1.);
        for (int bounce = 0;bounce<5;++bounce) {
            float rayt=0.02,d=0,prevd=0;
            for (int iter =0 ;iter<50;++iter,prevd=d) {
                vec3 p = ro + rayt * rd;
                d = map(p);
                if (d<=0) break;
                rayt += d+0.03;
            }
            if (prevd!=d) rayt += (d/(prevd-d)) * (prevd+0.03);
            vec3 p = ro + rayt * rd;
            vec3 albedo = (p.y> -0.9) ? vec3(0.6,0.8,1.) : vec3(0.9);
            if (p.x>0.99) albedo=vec3(1.,0.6,0.5);
            vec2 eps=vec2(0.01,0.);
            vec3 n=safe_normalize(vec3(map(p+eps.xyy), map(p+eps.yxy), map(p+eps.yyx))-map(p));
            r4 = rnd4();
            vec3 lightpos = vec3(r4.x*2.-1-2., 6.8, r4.y*2.-1.+5.);
            vec3 l = lightpos-p;
            float ldist = length(l);
            if (ldist<0.001f) {
                break;
            }
            l/=ldist;
            // shadow ray
            float st = 0.02f;
            float geom = max(0.,dot(l,n)) / (ldist*ldist);
            for (int iter = 0; iter<100 && st<ldist;++iter) {
                float d=map(p + st*l);
                if (d<=0.) { geom=0.f; break; }
                st += d + 0.02f;
            }
            if (bounce == 0 && abs(p.y-7.)<0.1 && abs(p.x+2.)<1. && abs(p.z-5.)<1.) {
                c+=vec3(1.)*2.;
                break;
            }
            thpt *= albedo;
            c += vec3(1.) * thpt * geom;
            ro = p;
            rd = rnd_dir_cos(n, r4.zw);
        }
    }
    o = vec4(c, 1.);//1./rayt);
    return o;
}
