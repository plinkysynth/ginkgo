vec2 rot2d(vec2 uv, float angle) {
	float c=cos(angle), s=sin(angle);
    return vec2(uv.x*c+uv.y*s,uv.y*c-uv.x*s);
}

vec4 pixel(vec2 uv) { 
    uv-=0.5;
    uv.x *= 16./9.;

    vec3 rd = normalize(c_cam2world[2].xyz + uv.x * c_cam2world[0].xyz + uv.y * c_cam2world[1].xyz);
    vec3 ro = c_cam2world[3].xyz;
    float t = -ro.z/rd.z;
    if (t<0.) return vec4(0.5,0.5,0.5,t);
    vec3 hit = ro + rd * t;
    uv = hit.xy;
        

	vec2 pixel = uv*-150.;
    
    
    
    
    
     
//    live coding is time 
// consuming :)
    
    
    
    
    
    
     int row = int(pixel.y/24.0);
    vec3 o = 0.8-get_text_pixel(pixel, ivec2(16.,24.), 1., 0.2, 0.4f).xyz;
	// o.x =(0.3-length(uv))*2.;
    // o.y =(0.3-length(uv+vec2(0.3,0.1)))*2.;
    // o.z =(0.3-length(uv+vec2(-0.3,0.2)))*2.;
    // o +=saturate((0.2-length(uv+vec2(sin(iTime*0.2)*0.2,0.2*cos(iTime*0.3))))*20.)*0.5;
    // o-=0.2;
    /////////////////////////////////////////////////////////////////////
    o=-o;
    vec4 cmyk=vec4(o, min(o.x,min(o.y,o.z)));
    float mask_angle = 0.2;
    float mask_scale = 100.;
    cmyk.x -= length(fract(rot2d(uv, mask_angle + 0.261799)*mask_scale)-0.5)-0.5;
    cmyk.y -= length(fract(rot2d(uv, mask_angle+ 1.308997)*mask_scale)-0.5)-0.5;
    cmyk.z -= length(fract(rot2d(uv, mask_angle)*mask_scale)-0.5)-0.5;
    cmyk.w -= length(fract(rot2d(uv, mask_angle + 0.785398)*mask_scale)-0.5)-0.5;
    //vec2 paperuv = vec2(uv*vec2(0.25*16./9.,0.25));
    //vec3 pdiff = texture(uPaperDiff,paperuv).xyz;
    //float pdisp = texture(uPaperDisp, paperuv).x;
    //cmyk+=pdiff.x*2.2 - pdisp*0.5 - 1.4;
    cmyk = clamp(cmyk*200.,0.,1.);
    //cmyk *= pdisp*1.2;
    vec3 rgb=cmyk.x * (1.-vec3(0.,0.5,0.7)); // C
    rgb+=cmyk.y * (1.-vec3(0.9,0.05,0.5)); // M
    rgb+=cmyk.z * (1.-vec3(0.9,0.65,0.0)); // Y
    rgb+=cmyk.w; // K
       //if ((row&1)!=0) rgb+=vec3(0.15,0.051,0.071);
    o=exp(-rgb*2.75-0.75); // contrast

    
    //o *= smoothstep(0.45,1.1,pdiff);
	return vec4(o,1./t);
}
