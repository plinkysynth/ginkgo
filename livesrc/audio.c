
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

#endif


STATE_VERSION(1,  )

void init_state(void) {
}

stereo do_sample(stereo inp) {
    F chord1 = sawo(P_C3) + sawo(P_E4) + sawo(P_G4);
    F chord2 = sawo(P_C3) + sawo(P_F4) + sawo(P_A4);
    F chord3 = sawo(P_G3) + sawo(P_D3) + sawo(P_B3);
    chord1 = chord1 * vol(S0) + 
        chord2 * vol(S1) +
        chord3 * vol(S0);
   float cutoff = vol(S4);
   float reso = vol(S5);
   F out = lpf(chord1, cutoff, reso);
   out=lpf(out, cutoff, reso);

    //out = squareo(P_E4) * 0.5;
    
    // float t= exp2f(-0.0002f * (G->sampleidx&65535));
    // out += sino(P_A5) * t;
    
    // wave_t *wave=get_wave_by_name("harp:0");
    // out=0.f;
    // if (wave && wave->frames && wave->num_frames) out=wave->frames[(G->sampleidx) % (wave->num_frames/20)] * 10.;
    
   	stereo dry=STEREO(out,out);
    return dry;
    return stadd(dry,reverb(dry));
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

