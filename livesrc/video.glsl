//      _               _           
//  ___| |__   __ _  __| | ___ _ __ 
// / __| '_ \ / _` |/ _` |/ _ \ '__|
// \__ \ | | | (_| | (_| |  __/ |   
// |___/_| |_|\__,_|\__,_|\___|_|   
//        
// just set something into vec3 o, or return a color.

o=vec3(0.2,0.2,0.3) * 0.5;
// o=vec3(uv*0.2,0.);

// vec2 c = vec2(uv.x*3.5-2.5, uv.y*2.0-1.0), z = vec2(0.0);
// float it = 200.0;
// for (int i = 0; i < 200; i++) {
//     z = vec2(z.x*z.x - z.y*z.y, 2.0*z.x*z.y) + c;
//     if (dot(z,z) > 4.0) { it = float(i); break; }
// }
// float t = it/5.0+1.;
// o=0.5+0.2*sin(vec3(1.,2.,3.)*t);
