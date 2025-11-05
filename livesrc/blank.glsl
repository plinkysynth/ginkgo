vec4 pixel(vec2 uv) {
uv.x*=16./9.;
vec3 c= vec3(0.);
float colphase = uv.x*0.1+uv.y*1.3;
for (int i=0;i<6;++i) {
uv.x+=iTime*0.01/(i+1);
c*=0.6;
colphase+=uv.y+1;
uv=vec2(uv.x*.4+uv.y*.3, uv.y*.4-uv.x*.3);
	float d=smoothstep(0.13,0.1,length(fract(uv*5.)-0.5)); 
    c+=d*(1.+vec3(cos(colphase+vec3(1.,1.5,2))));
}
return vec4(tanh(c*1.),0.); 
}
