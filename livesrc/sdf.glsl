// taa
// sky urban_street_01
// sky venice_sunset
// sky studio_small_09
// sky urban_alley_01
// sky poolbeg
// sky sunny_vondelpark
// sky blouberg_sunrise_1
// sky wasteland_clouds
// sky university_workshop        
// first sky wins
// aperture 0.1
// fov 0.3
// focal_distance 33
#ifdef PATTERNS
/vispat [1 0 0.5 0]
#endif

uniform float vispat;

const float F0 = 0.05;
float schlickF(float cosTheta, float F0){
    float m = 1.0 - cosTheta;
    float m2 = m*m, m5 = m2*m2*m;
    return F0 + (1.0 - F0) * m5;
}

vec3 skycol(vec3 d_norm, float lod) {
    return textureLod(uSky, vec2(atan(d_norm.x,d_norm.z)*(0.5/PI), 0.5-asin(d_norm.y)*(1./PI)), lod).xyz;
}

bool plane_intersect(vec3 ro, vec3 rd, vec4 plane_nd, inout float maxt, inout vec3 n) {
    float denom = dot(rd, plane_nd.xyz);
    if (denom < -1e-6) {
        float t = (plane_nd.w - dot(ro, plane_nd.xyz)) / denom;
        if (t > 0 && t<maxt) {
            maxt=t;
            n=plane_nd.xyz;
            return true;
        }
    }
    return false;
}

vec4 pixel(vec2 uv) {
    vec4 o=vec4(0.);
    vec4 r4=rnd4();
    float tofs = r4.x;
    vec3 c=vec3(0.);
    float disparity=0.;

    for (int smpl = 0; smpl<16;smpl++) {
        float shuttert= (smpl+tofs)*(1.f/16.f)+0.5f;
        vec3 ro,rd;
        eyeray(shuttert, ro, rd);
        float thpt=1.;
        vec4 photocopier=sin(iTime*vec4(0.1,0.125,0.1643,0.235));
        for (int bounce = 0; bounce < 6; ++bounce) {
            float tmax = 1e9;
            vec3 n=vec3(0.);


            plane_intersect(ro,rd,vec4(1,0,0,-8),tmax,n);
            plane_intersect(ro,rd,vec4(0,1,0,-8),tmax,n);
            plane_intersect(ro,rd,vec4(0,0,1,-80),tmax,n);

            plane_intersect(ro,rd,vec4(-1,0,0,-8),tmax,n);
            plane_intersect(ro,rd,vec4(0,-1,0,-8),tmax,n);
            plane_intersect(ro,rd,vec4(0,0,-1,-8),tmax,n);

			bool is_sphere = false;
            vec2 spht = sphere_intersect(ro, rd, 3.);
            if (spht.x>0. && spht.x<tmax) {
                n=safe_normalize(ro+spht.x*rd);
                tmax=spht.x;
                is_sphere=true;
                //c+=n*0.5+0.5;
                //return o;
            }
            r4=rnd4();
            // float freeflight = -log(max(r4.w, 1e-6)) * 50.1;
            // if (tmax > freeflight) {
            // 	ro=ro+rd*freeflight;
            // 	rd=rnd_dir_cos(rd, r4.xy);
            //     continue;
            // }
            

            if (tmax==1e9) {
                //c+=skycol(rd,0.) * thpt;
                break;
            } else {
                ro = ro + rd * tmax;
                //float v = fft(square(ro.x+1.)*2.);
                //if (ro.y>7.9 && abs(ro.z)<2.) c+=50*thpt*fft(abs(ro.x)*100.);
                bool isspec = schlickF(-dot(n, rd), F0) > r4.z;
                float albedo=0.8;
                
                if (is_sphere) {
                	albedo=0.2;
                } else {
                float band=smoothstep(0.51,0.5,abs(dot(ro,photocopier.xyz)-photocopier.w));
                	c+=(1.+sin(vec3(0.51,0.51,0.52)*(ro.y+iTime*2.) + vec3(0,1,2)))*band*thpt;
                }
                if (isspec) n=reflect(rd,n)*300.;
                else thpt*=albedo;
                rd = rnd_dir_cos(n, r4.xy);
                if (bounce ==0) disparity=1./tmax;
            }
        }
    }
    c*=1./16.;
    c*=0.75/(0.75+dot(uv,uv)); // vignette
    o = vec4(c, disparity);
    return o;
}
