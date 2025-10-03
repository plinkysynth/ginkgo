
#ifdef PATTERNS

// any block comment beginning "#ifdef PATTERNS" will be considered pattern area.
// this is the pattern area. it is parsed and 'compiled' by gingko, not the c compiler.
// we create named mini-notation patterns and curves.

// this is a graph - any string inside single quotes becomes a value you can edit as both text and visually.
// the graph appears when you hover it.

'.aWVVXdq67qdcef'     


/fancy_pattern [[- rim], bd(3,8), hh*8]

/kick44 [bd*4]

/euclid sd(3,8) '.  a      z . ' 

kicklinn
Breaks_Let_There_Break
#endif


STATE_VERSION(1,  )

void init_state(void) {
}

stereo do_sample(stereo inp) {
    F chord1 = sawo(P_C3) + sawo(P_Ds4) + sawo(P_C4) + pwmo(P_C1,0.25);
    F chord2 = sawo(P_Gs2) + sawo(P_F4) + sawo(P_C4) + pwmo(P_F1,0.25);
    F chord3 = sawo(P_D3) + sawo(P_D3) + sawo(P_B3) + pwmo(P_G1,0.25);
    chord1 = chord1 * vol(slew(S0(0.5),1e-4,1e-6)) + 
        chord2 * vol(S1(0.)) +
        chord3 * vol(S0(0.));
   float cutoff = vol(S4(0.75));
   F out = lpf(chord1, cutoff, 0.f);
   //printf("%f\n", out);
   //out=chord1;

    //out = squareo(P_E4) * 0.5;
    
    // float t= exp2f(-0.0002f * (G->sampleidx&65535));
    // out += sino(P_A5) * t;
    
    wave_t *wave=get_wave_by_name((char*)"break_sesame");
    F drums = 0.;
    if (wave && wave->frames && wave->num_frames) drums = wave->frames[(G->sampleidx/2) % (wave->num_frames)];
    //drums=soft(drums*10.)/10.;
    //out+=rndt() * 0.1f;
    
   	stereo dry=stereo{out*0.5f,out*0.5f};
    stereo wet=reverb(dry*0.125f);
    wet = wet+dry+drums*0.5f;
    return wet;
}

/*                           :-:.                              
                     +%=:-+#*.  :#%*+#*:                       
                    =+     .-     -.   +:                      
                    ==     :-    --   -= :*==*-     ginkgo           
                    :*     :-   :-   :+.-+    *-               
                     =+    ::   -:  :+:==    --=*=             
                     .*:   -.  --  .**+-   --    =+            
                      -+  .=  :-   +#-  .-:      -*            
                      :*  :- :=   ::  .=:     :=- =*.          
                      .+: -..=.  -: :=:   .+=       #-         
                       +::=.=. := -=   =*:          :*         
                      .+---=. -==- .+=       .-+*+=-#=         
                      .*-*=.:#*:-=-   -+=-..        %.         
                      -#**:=+-====-::.              +=         
                     .**+-+***-:..::----------:.     #:        
                     +%=+#+==+++=:              .----%:        
                    -#==+*#+=--:.:-===-:.           *-         
                   -*:+%*=:..:=*#*=.     :==.       %:         
                  +*#+             :#*.      .+-    %.         
                .#**                  -#-        +*%:          
               :#*=                     :*-      -+            
              :#*-                        -*-   -*.            
             :#+=                           :-==:              
            .#=*                                               
            *-*                                                
           -*-+                                                
           *:+:                                                
          .# *                                                 
          .%-%                                                 
            :.                                                 */

