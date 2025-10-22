//      _               _           
//  ___| |__   __ _  __| | ___ _ __ 
// / __| '_ \ / _` |/ _` |/ _ \ '__|
// \__ \ | | | (_| | (_| |  __/ |   
// |___/_| |_|\__,_|\__,_|\___|_|   

// sky venice_sunset
// sky studio_small_09
// sky urban_alley_01
// sky poolbeg
// sky sunny_vondelpark
// sky blouberg_sunrise_1
// sky urban_street_01
// sky wasteland_clouds
// sky university_workshop        
// first sky wins
// taa
#define PI 3.141592f
const float exposure = 0.3f;
        
const float F0 = 0.01;
float schlickF(float cosTheta, float F0){
    float m = 1.0 - cosTheta;
    float m2 = m*m, m5 = m2*m2*m;
    return F0 + (1.0 - F0) * m5;
}
vec3 skycol(vec3 d_norm, float lod) {
    return 10.*smoothstep(0.51,0.52,(d_norm.yyy));
    //return d_norm*0.5+0.5;
    return textureLod(uSky, vec2(atan(d_norm.x,d_norm.z)*(0.5/PI), 0.5-asin(d_norm.y)*(1./PI)), lod).xyz;
    return vec3(max(0,d_norm.y));
    
}

bool bvh_intersect(vec3 ro, vec3 inv_rd, int i, float maxt) {
    vec3 boxmin = texelFetch(uSpheres, i).xyz;
    vec3 boxmax = texelFetch(uSpheres, i+1).xyz;
    float boxhit_t = aabb_intersect(ro, inv_rd, boxmin, boxmax);                
    return boxhit_t >= maxt;
}


vec4 pixel(vec2 uv) { 
    vec3 o=vec3(0.);
    vec4 r4=rnd4();
    float tofs = r4.x;
    float skylod = 0.f;
    float disparity = 0.f;
    for (int smpl = 0; smpl<8;smpl++) {
        float t= (smpl+tofs)*(1.f/16.f)+0.5f;
        vec3 ro,rd;
        eyeray(t, ro, rd);
        //return vec4(rd*0.5+0.5,1.);
        vec3 thpt = vec3(exposure / (1.+2.*dot(uv,uv)));
        // trace the path
        const int max_bounce = 4;
        for (int bounce = 0; bounce < max_bounce; ++bounce) {
            float maxt=1000.;
            vec3 inv_rd = 1.0 / rd;
            vec3 hit_sphere_pos=vec3(0.);
            int hit_sphere_idx = 0;
            const int MAX_SPHERES = 64;
            const int BVH_BASE_0 = MAX_SPHERES*3;
            const int BVH_BASE_1 = MAX_SPHERES*3+(MAX_SPHERES/4)*2;
            const int BVH_BASE_2 = MAX_SPHERES*3+(MAX_SPHERES/4+MAX_SPHERES/16)*2;
            const int BVH_BASE_3 = MAX_SPHERES*3+(MAX_SPHERES/4+MAX_SPHERES/16+MAX_SPHERES/64)*2;
            //for (int m = 0; m<MAX_SPHERES/256; m++)  // top level of bvh - 256 spheres at a time
            { 
                // int idx3 = m*2;
                // if (bvh_intersect(ro, inv_rd, idx3+BVH_BASE_3, maxt)) continue;
                // idx3*=4;
                const int idx3 = 0;
                for (int l = 0; l<4; l++) { // second-from-top level of bvh - 64 spheres at a time
                    int idx2 = idx3 + l*2;
                    if (bvh_intersect(ro, inv_rd,idx2+BVH_BASE_2, maxt)) continue;
                    idx2*=4;
                    for (int k = 0; k<4; k++) { // penultimate level of bvh - 16 spheres at a time
                        int idx1 = idx2 + k*2;
                        if (bvh_intersect(ro, inv_rd, idx1+BVH_BASE_1, maxt)) continue;
                        idx1*=4;
                        for (int j = 0; j<4; j++) { // last level of bvh - 4 spheres at a time
                            int idx0 = idx1 + j*2;
                            if (bvh_intersect(ro, inv_rd, idx0+BVH_BASE_0, maxt)) continue;
                            idx0 *=4/2; // switch from pairs-of-float4s to sphere indices.
                            for (int i = 0; i < 4; ++i) {
                                vec4 sphere_rad_old = texelFetch(uSpheres, (idx0+i)*3+1);
                                vec4 sphere_rad = texelFetch(uSpheres, (idx0+i)*3);
                                sphere_rad = mix(sphere_rad_old, sphere_rad, t);
                                vec2 sph_t = sphere_intersect(ro - sphere_rad.xyz, rd, sphere_rad.w);
                                if (sph_t.x > 0. && sph_t.x < maxt) {
                                    maxt = sph_t.x;
                                    hit_sphere_pos = sphere_rad.xyz;
                                    hit_sphere_idx = idx0+i;
                                }
                            }
                        }
                    }
                }
            }
            if (bounce == 0) disparity += 1./ maxt;
            else if (bounce==max_bounce-1) {
            	thpt = vec3(0.);
                break;
            }
            if (maxt>=1000.0f) 
                break; // escape to the sky!
            r4 = rnd4();            
            vec4 material = texelFetch(uSpheres, hit_sphere_idx*3+2);	
            o.xyz += thpt * material.xyz * material.w;
            vec3 hit = ro+maxt * rd;
            vec3 n = normalize(hit - hit_sphere_pos);
            
            bool isspec = schlickF(-dot(n, rd), F0) > r4.z;
            if (isspec) n=reflect(rd,n)*300.;
            rd = rnd_dir_cos(n,r4.xy);
            if (isspec)
                skylod += 3.f;
            else { 
            	thpt*=material.xyz;
                skylod += 5.f;
            }
            ro=hit+rd*0.0001f;
        }
        vec3 c = skycol(rd,skylod)*thpt;
        //c=sqrt(c); // firefly supression?
        o += c;
    }
    return vec4(o, disparity) * 1.f/8.f;
/*
    uv-=0.5;
    uv.x*=16./9.;
    float vignette = 1./(1.+length(uv));
    uv*=1.+0.12*dot(uv,uv); // bendy
    vec2 reticulexy = abs(fract(uv*10.+0.5)-0.5);
    vec2 reticulexy2 = abs(fract(uv*100.+0.5)-0.5);
    float reticule=min(reticulexy.x,reticulexy.y);
    float reticule2=min(reticulexy2.x,reticulexy2.y);
    reticule2 += 100.*max(0.,min(abs(uv.x), abs(uv.y))-0.005);
    reticule=min(reticule,reticule2*0.1);
    reticule *= reticule;
    reticule = exp2(-reticule*3000.)*0.66+exp2(-reticule*300.)*0.125;

    vec2 sc = scope((uv.x+2.f)*512.f)*0.5;
    float beam = exp(-10000.*square((uv.y*5+2.1)-sc.x));
    beam += exp(-10000.*square((uv.y*5+2.1)-sc.y));

    beam *= 0.3; // beam brigtness

    beam*=exp(fract(uv.x*0.5-iTime*11.)-1.);
    beam+=0.01;
    beam*=vignette;
    beam*=1.-reticule;
    vec3 o.xyz=vec3(2,4,3) * beam;
    return mix(o, texture(uFP,v_uv).xyz, 0.1);
    */
}
