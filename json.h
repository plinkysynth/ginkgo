// minimal iterative JSON walker; spans point into the input (no copies)
// events: 0=end, '{','}','[',']' for structure, 'd' for any leaf value
// state.key holds the active key-path (keydepth entries), state.value is the last leaf value span
#ifndef MAXDEPTH
#define MAXDEPTH 64
#endif

typedef struct json_state {
  const char *cur, *end;
  int keydepth;
  const char *key[MAXDEPTH][2];
  const char *value[2];
  // minimal extra machinery:
  int depth;                // nesting depth of ctx
  char ctx[MAXDEPTH];       // 'O' object(expect key or '}'), 'K' after key(expect ':'), 'V' object(expect value), 'A' array
  unsigned char key_on[MAXDEPTH]; // key active for object at this depth (0..depth-1)
} json_state;

static void ws(json_state *s){ while(s->cur<s->end && (*s->cur==' '||*s->cur=='\t'||*s->cur=='\n'||*s->cur=='\r')) s->cur++; }
static int parse_string(json_state *s, const char **out0, const char **out1){
  ws(s); if(s->cur>=s->end || *s->cur!='"') return 0;
  const char *p=++s->cur; for(; p<s->end; ++p){
    if(*p=='"'){ *out0=s->cur; *out1=p; s->cur=p+1; return 1; }
    if(*p=='\\'){ if(++p>=s->end) return 0; if(*p=='u'){ for(int i=0;i<4 && p<s->end; ++i) ++p; --p; } }
  } return 0;
}
static int isdelim(char c){ return c==','||c=='}'||c==']'||c==' '||c=='\t'||c=='\n'||c=='\r'; }
static char parse_leaf(json_state *s){
  ws(s);
  if(s->cur>=s->end) return 0;
  if(*s->cur=='"'){ const char *a,*b; if(!parse_string(s,&a,&b)) return 0; s->value[0]=a; s->value[1]=b; return 'd'; }
  const char *a=s->cur;
  if(*s->cur=='-'||(*s->cur>='0'&&*s->cur<='9')){ // number
    ++s->cur; while(s->cur<s->end && ((*s->cur>='0'&&*s->cur<='9')||*s->cur=='.'||*s->cur=='e'||*s->cur=='E'||*s->cur=='+'||*s->cur=='-')) ++s->cur;
    s->value[0]=a; s->value[1]=s->cur; return 'd';
  }
  // true/false/null
  if(s->end-a>=4 && !memcmp(a,"true",4)){ s->cur+=4; s->value[0]=a; s->value[1]=s->cur; return 'd'; }
  if(s->end-a>=5 && !memcmp(a,"false",5)){ s->cur+=5; s->value[0]=a; s->value[1]=s->cur; return 'd'; }
  if(s->end-a>=4 && !memcmp(a,"null",4)){ s->cur+=4; s->value[0]=a; s->value[1]=s->cur; return 'd'; }
  return 0;
}

static void pop_ctx(json_state *s){ if(s->depth>0) --s->depth; }
static void push_ctx(json_state *s, char c){ if(s->depth<MAXDEPTH) s->ctx[s->depth++]=c; }
static char top_ctx(json_state *s){ return s->depth? s->ctx[s->depth-1] : 0; }

// step() mutates state and returns the next event
char json_step(json_state *s){
  for(;;){
    ws(s);
    if(s->cur>=s->end) return 0;

    char t = top_ctx(s);

    // structure closers (handle before anything else)
    if(*s->cur==']'){
      if(t!='A') return 0;
      s->cur++; pop_ctx(s); return ']';
    }
    if(*s->cur=='}'){
      if(t=='O'||t=='V'||t=='K'){        // closing current object level
        // if a key is active for this object, pop it first
        if(s->depth>0 && s->key_on[s->depth-1] && s->keydepth>0){
          s->key_on[s->depth-1]=0;
          if(s->keydepth>0) --s->keydepth;
        }
        s->cur++; pop_ctx(s); return '}';
      }
      return 0;
    }

    // inside object expecting key or '}'  (t=='O')
    if(t=='O'){
      if(*s->cur==','){ s->cur++; ws(s); continue; }
      const char *ka,*kb;
      if(!parse_string(s,&ka,&kb)) return 0;
      ws(s); if(s->cur>=s->end || *s->cur!=':') return 0;
      s->cur++;                   // consume ':'
      if(s->keydepth<MAXDEPTH){ s->key[s->keydepth][0]=ka; s->key[s->keydepth][1]=kb; s->keydepth++; }
      s->key_on[s->depth-1]=1;    // mark active key at this object depth
      s->ctx[s->depth-1]='V';     // now expecting the value
      continue;
    }

    // object expecting value after key (t=='V')
    if(t=='V'){
      ws(s);
      if(*s->cur=='{'){ s->cur++; s->ctx[s->depth-1]='O'; push_ctx(s,'O'); return '{'; }
      if(*s->cur=='['){ s->cur++; s->ctx[s->depth-1]='O'; push_ctx(s,'A'); return '['; }
      // leaf value
      char ev = parse_leaf(s);
      if(!ev) return 0;
      // after a leaf, we'll pop the key on next ',' or '}' at this object depth;
      s->ctx[s->depth-1]='K'; // consumed value; now between value and comma/}
      return ev;
    }

    // between value and comma/} in object (t=='K')
    if(t=='K'){
      if(*s->cur==','){
        // end of this key's value
        if(s->key_on[s->depth-1] && s->keydepth>0){ s->key_on[s->depth-1]=0; --s->keydepth; }
        s->cur++; s->ctx[s->depth-1]='O'; // expect next key
        continue;
      } else if(*s->cur=='}'){
        // handled at top of loop: will pop key then object
        continue;
      } else {
        return 0;
      }
    }

    // array (t=='A')
    if(t=='A'){
      if(*s->cur==','){ s->cur++; continue; }
      if(*s->cur==']'){ s->cur++; pop_ctx(s); return ']'; }
      if(*s->cur=='{'){ s->cur++; push_ctx(s,'O'); return '{'; }
      if(*s->cur=='['){ s->cur++; push_ctx(s,'A'); return '['; }
      char ev = parse_leaf(s);
      if(!ev) return 0;
      return ev;
    }

    // top-level (no ctx)
    if(*s->cur=='{'){ s->cur++; push_ctx(s,'O'); return '{'; }
    if(*s->cur=='['){ s->cur++; push_ctx(s,'A'); return '['; }
    char ev = parse_leaf(s);
    if(!ev) return 0;
    return ev;
  }
}

// convenience init
static inline void json_init(json_state *s, const char *buf, size_t len){
  memset(s,0,sizeof(*s)); s->cur=buf; s->end=buf+len;
}
