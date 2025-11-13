//taa
#ifdef PATTERNS
/vispat [1 0 0.5 0]
#endif

uniform float vispat;
float map(vec3 p) {
	float b = -sdBox(p, vec3(5,7,20));
    return min(b,sdSphere(p, 1.));
}
vec3 safe_normalize(vec3 p) {
	float l =length(p);
    if (l!=0) p*=1./l;
    return p;
}
vec4 pixel(vec2 uv) {
    vec4 o=vec4(0.);
    vec4 r4=rnd4();
    float tofs = r4.x;
    for (int smpl = 0; smpl<8;smpl++) {
        float shuttert= (smpl+tofs)*(1.f/16.f)+0.5f;
        vec3 ro,rd;
        eyeray(shuttert, ro, rd);
        float rayt=0,d=0,prevd=0;
        for (int iter =0 ;iter<50;++iter,prevd=d) {
        	vec3 p = ro + rayt * rd;
			d = map(p);
            if (d<=0) break;
            rayt += d+0.1;
        }
        if (prevd!=d) rayt += (d/(prevd-d)) * (prevd+0.1);
    	vec3 p = ro + rayt * rd;
        vec2 eps=vec2(0.01,0.);
        vec3 n=vec3(map(p+eps.xyy), map(p+eps.yxy), map(p+eps.yyx))-map(p);
        o = vec4(safe_normalize(n)*0.5+0.5, 1.);//1./rayt);
        //o= vec4(fract(p),1.);
        break;
    }
    return o;
}
