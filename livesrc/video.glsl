//      _               _           
//  ___| |__   __ _  __| | ___ _ __ 
// / __| '_ \ / _` |/ _` |/ _ \ '__|
// \__ \ | | | (_| | (_| |  __/ |   
// |___/_| |_|\__,_|\__,_|\___|_|   
//        
// just set something into vec3 o, or return a color.

vec2 xuv = v_uv - 0.5;
xuv.x-=iTime*0.02;
float d = 100000.*exp(-10000.*dot(xuv,xuv));
d=smoothstep(0.00501,0.005,abs(fract(xuv.x*20.)*0.05+abs(xuv.y+0.4)-0.025));
o=vec3(5.,2.,1.)*d*uv.x*uv.x*3.;
o+=(1-uv.y)*vec3(0.1,0.2,0.3)*0.2;
//o.x=1;
//o=vec3(fract(v_uv*4),0.)*0.;

// o=vec3(uv*0.2,0.);

// vec2 c = vec2(uv.x*3.5-2.5, uv.y*2.0-1.0), z = vec2(0.0);
// float it = 200.0;
// for (int i = 0; i < 200; i++) {
//     z = vec2(z.x*z.x - z.y*z.y, 2.0*z.x*z.y) + c;
//     if (dot(z,z) > 4.0) { it = float(i); break; }
// }
// float t = it/5.0+1.;
// o=0.5+0.2*sin(vec3(1.,2.,3.)*t);
