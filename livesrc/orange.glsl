// fov 1
// aperture 0.02
// taa
vec4 pixel(vec2 uv) { 
	vec4 o=vec4(0.,0.,0.,1.);
	for (int sample = 0; sample<8;++sample) {
    	float time = sample/16.;
        vec3 ro,rd;
        eyeray(time, ro, rd);
        float thpt=1.;
        float t = 0.;
        float maxt = aabb_intersect(ro, 1./rd, vec3(-2,-1,-4), vec3(2,1,4));
       if (maxt>16.) maxt=16.;
        for (float t=1.;t<maxt;) {
        	float dt = maxt-t;
            if (dt>1.) dt=1.;
	        vec3 p = ro + t * rd;
            thpt*=0.9;
            o.xyz+=(thpt*0.1) * vec3(0.3,0.1,0.01);
            // random point on box
            vec3 delta = p;
            float dsq=dot(delta,delta);
        	o.xyz+=(dt*thpt/float(dsq))*vec3(0.1,0.2,0.9);
            t+=dt;
        }
        vec3 col = fract(rd*10.5);
        //o += vec4(col, 1./maxt);
    }
    return o*(1./2.);
}
