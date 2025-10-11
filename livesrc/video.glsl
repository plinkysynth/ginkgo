//      _               _           
//  ___| |__   __ _  __| | ___ _ __ 
// / __| '_ \ / _` |/ _` |/ _ \ '__|
// \__ \ | | | (_| | (_| |  __/ |   
// |___/_| |_|\__,_|\__,_|\___|_|   
//        
#define PI 3.141592f
vec3 skycol(vec3 d_norm) {
    return texture(uSky, vec2(atan(d_norm.x,d_norm.z)*(0.5/PI)+iTime*0.01, 0.5-asin(d_norm.y)*(1./PI))).xyz;
}

vec3 pixel(vec2 uv) { 
    vec3 rd = c_fwd + (v_uv.x-0.5)*(16./9.)*c_across + (v_uv.y-0.5)*c_up;
    rd=normalize(rd);
    return skycol(rd)*0.1;
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
