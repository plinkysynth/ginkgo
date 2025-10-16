//      _               _           
//  ___| |__   __ _  __| | ___ _ __ 
// / __| '_ \ / _` |/ _` |/ _ \ '__|
// \__ \ | | | (_| | (_| |  __/ |   
// |___/_| |_|\__,_|\__,_|\___|_|   

// sky urban_alley_01
// sky studio_small_09
// sky venice_sunset
// sky poolbeg
// sky sunny_vondelpark
// sky blouberg_sunrise_1
// sky urban_street_01
// sky wasteland_clouds
// sky university_workshop        
// first sky wins

#define PI 3.141592f
const float exposure = 0.5;
const float aperture = 0.06;
        
const float F0 = 0.02;
float schlickF(float cosTheta, float F0){
    float m = 1.0 - cosTheta;
    float m2 = m*m, m5 = m2*m2*m;
    return F0 + (1.0 - F0) * m5;
}
vec3 skycol(vec3 d_norm, float lod) {
    //return vec3(max(0,d_norm.y));
    return textureLod(uSky, vec2(atan(d_norm.x,d_norm.z)*(0.5/PI), 0.5-asin(d_norm.y)*(1./PI)), lod).xyz;
}

vec4 pixel(vec2 uv) { 
    vec3 o=vec3(0.);
    vec4 r4=rnd4();
    float tofs = r4.x;
    float skylod = 0.f;
    float disparity = 0.f;
    vec2 fov2 = vec2(fov * 16./9., fov);
        

    for (int smpl = 0; smpl<4;smpl++) {
        // make the eye ray
        vec4 r4 = rnd4();
        float t= (smpl+tofs)*(1.f/16.f)+0.5f;
        mat4 mat = c_cam2world_old + (c_cam2world - c_cam2world_old) * t;
        vec2 uv = (v_uv - 0.5) * fov2 + (r4.xy-0.5) * 1.f/1080.;
        // choose a point on the focal plane, assuming camera at origin
        float focal_distance = 10.;//c_lookat.w;
        vec3 rt = (mat[2].xyz + uv.x * mat[0].xyz + uv.y * mat[1].xyz) * focal_distance;
        // choose a point on the lens
        vec2 lensuv = rnd_disc(r4.zw)*aperture;
        float anamorphic = 0.66;
        vec3 ro = mat[0].xyz * lensuv.x * anamorphic + mat[1].xyz * lensuv.y;
        vec3 rd = rt-ro;
        ro+=mat[3].xyz; // add in camera pos
        rd=normalize(rd);
        vec3 thpt = vec3(exposure / (1.+2.*dot(uv,uv)));
        // trace the path
        const int max_bounce = 4;
        for (int bounce = 0; bounce < max_bounce; ++bounce) {
            float maxt=1000.;
            vec3 hit_sphere_pos=vec3(0.);
            int hit_sphere_idx = 0;
            for (int i = 0; i < 16; ++i) {
                vec4 sphere_rad = texelFetch(uSpheres, i*3);
                vec2 sph_t = sphere_intersect(ro - sphere_rad.xyz, rd, sphere_rad.w);
                if (sph_t.x > 0. && sph_t.x < maxt) {
                    maxt = sph_t.x;
                    hit_sphere_pos = sphere_rad.xyz;
                    hit_sphere_idx = i;
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
    return vec4(o, disparity) * 1.f/4.f;
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
