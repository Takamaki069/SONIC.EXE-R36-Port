/* data.win loader (GameMaker:Studio 1.x, bytecode v13) + PNG decoder + string interning */
#include "gm.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <zlib.h>

GameData D;

#define U32(o) rd32(D.data+(o))
static inline uint32_t rd32(const uint8_t*p){ uint32_t v; memcpy(&v,p,4); return v; }
static inline int32_t  rdi32(const uint8_t*p){ int32_t v; memcpy(&v,p,4); return v; }
static inline float    rdf32(const uint8_t*p){ float v; memcpy(&v,p,4); return v; }
static inline uint16_t rd16(const uint8_t*p){ uint16_t v; memcpy(&v,p,2); return v; }
#define P(o) (D.data+(o))
#define S32(o) rdi32(D.data+(o))

/* ---------- string interning ---------- */
typedef struct SNode { struct SNode *next; char s[1]; } SNode;
static SNode *tab[4096];
const char *str_intern(const char *s){
  uint32_t h=5381; for(const char*p=s;*p;p++) h=h*33+(uint8_t)*p; h&=4095;
  for(SNode*n=tab[h];n;n=n->next) if(!strcmp(n->s,s)) return n->s;
  size_t l=strlen(s); SNode*n=malloc(sizeof(SNode)+l); memcpy(n->s,s,l+1); n->next=tab[h]; tab[h]=n; return n->s;
}
const char *num_to_str(double d){
  char b[64];
  if(d==(double)(long long)d && d>-1e15 && d<1e15) snprintf(b,sizeof b,"%lld",(long long)d);
  else { snprintf(b,sizeof b,"%.2f",d); char*e=b+strlen(b)-1; while(e>b&&*e=='0')*e--=0; if(*e=='.')*e=0; }
  return str_intern(b);
}

/* ---------- chunk table ---------- */
typedef struct { size_t off,size; int found; } Chunk;
static Chunk ch(const char *n){
  Chunk c={0,0,0}; size_t p=8, end=8+U32(4);
  while(p+8<=end && p+8<=D.size){ uint32_t sz=U32(p+4); if(!memcmp(P(p),n,4)){ c.off=p+8; c.size=sz; c.found=1; return c; } p+=8+sz; }
  return c;
}
static const char *cstr(size_t off){ return (const char*)P(off); }

int data_load(const char *path){
  int fd=open(path,O_RDONLY); if(fd<0){ perror(path); return -1; }
  struct stat st; fstat(fd,&st); D.size=st.st_size;
  D.data=mmap(0,D.size,PROT_READ,MAP_PRIVATE,fd,0); close(fd);
  if(D.data==MAP_FAILED||memcmp(D.data,"FORM",4)){ fprintf(stderr,"%s: not a data.win\n",path); return -1; }
  Chunk c;
  /* GEN8: sanity (bytecode version) */
  c=ch("GEN8"); if(!c.found){ fprintf(stderr,"no GEN8\n"); return -1; }
  if(D.data[c.off+1]!=13) fprintf(stderr,"warning: bytecode version %d (runner targets 13)\n",D.data[c.off+1]);
  /* STRG */
  c=ch("STRG"); D.nstr=U32(c.off); D.str=calloc(D.nstr,sizeof(char*));
  for(int i=0;i<D.nstr;i++) D.str[i]=cstr(U32(c.off+4+4*i)+4);
  /* TPAG */
  c=ch("TPAG"); D.ntpag=U32(c.off);
  /* TXTR */
  c=ch("TXTR"); D.ntex=U32(c.off); D.tex=calloc(D.ntex,sizeof(TexInfo));
  for(int i=0;i<D.ntex;i++){ size_t e=U32(c.off+4+4*i); size_t po=U32(e+4); D.tex[i].png=P(po);
    D.tex[i].w=(int)__builtin_bswap32(rd32(P(po+16))); D.tex[i].h=(int)__builtin_bswap32(rd32(P(po+20))); }
  /* AUDO */
  c=ch("AUDO"); if(c.found){ D.naud=U32(c.off); D.aud=calloc(D.naud,sizeof(void*)); D.audlen=calloc(D.naud,4);
    for(int i=0;i<D.naud;i++){ size_t e=U32(c.off+4+4*i); D.audlen[i]=U32(e); D.aud[i]=P(e+4); } }
  /* SPRT */
  c=ch("SPRT"); D.nspr=U32(c.off); D.spr=calloc(D.nspr,sizeof(Sprite));
  for(int i=0;i<D.nspr;i++){ size_t x=U32(c.off+4+4*i); Sprite*s=&D.spr[i];
    s->name=cstr(U32(x)); s->w=S32(x+4); s->h=S32(x+8); s->ml=S32(x+12); s->mr=S32(x+16); s->mb=S32(x+20); s->mt=S32(x+24);
    s->sep=S32(x+44); s->ox=S32(x+48); s->oy=S32(x+52); s->nframes=U32(x+56);
    s->fr=calloc(s->nframes?s->nframes:1,sizeof(TPag*));
    for(int j=0;j<s->nframes;j++) s->fr[j]=(TPag*)P(U32(x+60+4*j));
    size_t m=x+60+4*s->nframes; s->nmasks=U32(m); s->stride=(s->w+7)/8; s->masks=P(m+4);
    if(s->sep!=1) s->nmasks=0;
  }
  /* BGND */
  c=ch("BGND"); D.nbg=U32(c.off); D.bg=calloc(D.nbg,sizeof(Background));
  for(int i=0;i<D.nbg;i++){ size_t x=U32(c.off+4+4*i); D.bg[i].name=cstr(U32(x)); D.bg[i].tp=(TPag*)P(U32(x+16)); }
  /* FONT */
  c=ch("FONT"); D.nfont=U32(c.off); D.font=calloc(D.nfont,sizeof(Font));
  for(int i=0;i<D.nfont;i++){ size_t x=U32(c.off+4+4*i); Font*f=&D.font[i];
    f->name=cstr(U32(x)); f->size=S32(x+8); f->first=rd16(P(x+20)); f->last=S32(x+24); f->tp=(TPag*)P(U32(x+28));
    f->nglyphs=U32(x+40); f->g=calloc(f->nglyphs,sizeof(Glyph)); for(int k=0;k<256;k++) f->map[k]=-1;
    for(int j=0;j<f->nglyphs;j++){ size_t g=U32(x+44+4*j); Glyph*G=&f->g[j];
      G->ch=rd16(P(g)); G->x=rd16(P(g+2)); G->y=rd16(P(g+4)); G->w=rd16(P(g+6)); G->h=rd16(P(g+8));
      G->shift=(int16_t)rd16(P(g+10)); G->offset=(int16_t)rd16(P(g+12)); if(G->ch<256) f->map[G->ch]=j; }
  }
  /* SOND */
  c=ch("SOND"); D.nsnd=U32(c.off); D.snd=calloc(D.nsnd,sizeof(Sound));
  for(int i=0;i<D.nsnd;i++){ size_t x=U32(c.off+4+4*i); Sound*s=&D.snd[i];
    s->name=cstr(U32(x)); s->embedded=U32(x+4)&1; s->file=U32(x+12)?cstr(U32(x+12)):""; s->volume=rdf32(P(x+20)); s->audio_id=S32(x+32); }
  /* CODE (copy: we patch variable/function references in place) */
  c=ch("CODE"); D.ncode=U32(c.off); D.code=calloc(D.ncode,sizeof(Code));
  D.codebuf=malloc(c.size); memcpy(D.codebuf,P(c.off),c.size); size_t cs=c.off, ce=c.off+c.size;
  for(int i=0;i<D.ncode;i++){ size_t x=U32(c.off+4+4*i); D.code[i].name=cstr(U32(x)); D.code[i].len=U32(x+4); D.code[i].bc=D.codebuf+(x+8-cs); }
  /* FUNC / VARI: entries (name, occurrences, first address), chained via operand low 28 bits */
  for(int pass=0;pass<2;pass++){
    c=ch(pass?"VARI":"FUNC"); int n=c.size/12;
    if(pass){ D.nvar=n; D.var=calloc(n,sizeof(VarInfo)); } else { D.nfunc=n; D.func=calloc(n,sizeof(FuncInfo)); }
    for(int k=0;k<n;k++){ size_t e=c.off+12*k; const char*nm=cstr(U32(e)); uint32_t occ=U32(e+4); size_t a=U32(e+8);
      if(pass) D.var[k].name=nm; else D.func[k].name=nm;
      for(uint32_t j=0;j<occ;j++){ if(a+8>ce||a<cs){ fprintf(stderr,"bad ref chain for %s\n",nm); break; }
        uint8_t*w=D.codebuf+(a+4-cs); uint32_t v=rd32(w); uint32_t nx=v&0x0FFFFFFF; uint32_t out=(v&0xFF000000)|(uint32_t)k; memcpy(w,&out,4); a+=nx; }
    }
  }
  /* SCPT */
  c=ch("SCPT"); D.nscript=U32(c.off); D.scriptname=calloc(D.nscript+1,sizeof(char*)); D.scriptcode=calloc(D.nscript+1,sizeof(int));
  for(int i=0;i<D.nscript;i++){ size_t x=U32(c.off+4+4*i); D.scriptname[i]=cstr(U32(x)); D.scriptcode[i]=S32(x+4); }
  /* TMLN */
  c=ch("TMLN"); D.ntl=U32(c.off); D.tl=calloc(D.ntl+1,sizeof(Timeline));
  for(int i=0;i<D.ntl;i++){ size_t x=U32(c.off+4+4*i); Timeline*t=&D.tl[i]; t->name=cstr(U32(x)); t->nmom=U32(x+4);
    t->step=calloc(t->nmom+1,4); t->code=calloc(t->nmom+1,4);
    for(int k=0;k<t->nmom;k++){ t->step[k]=U32(x+8+8*k); size_t ev=U32(x+12+8*k); t->code[k]=-1;
      if(U32(ev)>0){ size_t act=U32(ev+4); t->code[k]=S32(act+32); } } }
  /* OBJT */
  c=ch("OBJT"); D.nobj=U32(c.off); D.obj=calloc(D.nobj,sizeof(Object));
  for(int i=0;i<D.nobj;i++){ size_t x=U32(c.off+4+4*i); Object*o=&D.obj[i];
    o->name=cstr(U32(x)); o->sprite=S32(x+4); o->visible=S32(x+8); o->solid=S32(x+12); o->depth=S32(x+16);
    o->persistent=S32(x+20); o->parent=S32(x+24); o->mask=S32(x+28);
    int pn=S32(x+64); size_t p=x+80+8*pn; int nt=U32(p); if(nt>12) nt=12;
    for(int et=0;et<nt;et++){ size_t lp=U32(p+4+4*et); int n=U32(lp); o->nev[et]=n; o->ev[et]=calloc(n?n:1,sizeof(Event));
      for(int j=0;j<n;j++){ size_t ep=U32(lp+4+4*j); Event*e=&o->ev[et][j]; e->sub=S32(ep); e->n=U32(ep+4); e->codes=calloc(e->n?e->n:1,sizeof(int));
        for(int a=0;a<e->n;a++){ size_t ap=U32(ep+8+4*a); e->codes[a]=S32(ap+32); } } }
  }
  /* ROOM */
  c=ch("ROOM"); D.nroom=U32(c.off); D.room=calloc(D.nroom,sizeof(Room));
  for(int i=0;i<D.nroom;i++){ size_t x=U32(c.off+4+4*i); Room*r=&D.room[i];
    r->name=cstr(U32(x)); r->w=U32(x+8); r->h=U32(x+12); r->speed=U32(x+16); r->persistent=U32(x+20); r->bgcol=U32(x+24);
    r->drawbg=U32(x+28); r->cc=S32(x+32); r->flags=U32(x+36);
    size_t bp=U32(x+40), vp=U32(x+44), op=U32(x+48), tp=U32(x+52);
    for(int j=0;j<MAX_BG&&j<(int)U32(bp);j++){ size_t q=U32(bp+4+4*j); RoomBg*b=&r->bg[j];
      b->enabled=S32(q); b->fg=S32(q+4); b->bg=S32(q+8); b->x=S32(q+12); b->y=S32(q+16); b->tilex=S32(q+20); b->tiley=S32(q+24); b->hspd=S32(q+28); b->vspd=S32(q+32); b->stretch=S32(q+36); }
    for(int j=0;j<MAX_VIEWS&&j<(int)U32(vp);j++){ size_t q=U32(vp+4+4*j); RoomView*v=&r->view[j]; int*f=(int*)v; for(int k=0;k<14;k++) f[k]=S32(q+4*k); }
    r->ninst=U32(op); r->inst=calloc(r->ninst?r->ninst:1,sizeof(RoomInst));
    for(int j=0;j<r->ninst;j++){ size_t q=U32(op+4+4*j); RoomInst*ri=&r->inst[j];
      ri->x=S32(q); ri->y=S32(q+4); ri->obj=S32(q+8); ri->id=U32(q+12); ri->cc=S32(q+16); ri->sx=rdf32(P(q+20)); ri->sy=rdf32(P(q+24)); ri->color=U32(q+28); ri->rot=rdf32(P(q+32)); }
    r->ntile=U32(tp); r->tile=calloc(r->ntile?r->ntile:1,sizeof(Tile));
    for(int j=0;j<r->ntile;j++){ size_t q=U32(tp+4+4*j); Tile*t=&r->tile[j];
      t->x=S32(q); t->y=S32(q+4); t->bg=S32(q+8); t->sx=S32(q+12); t->sy=S32(q+16); t->w=S32(q+20); t->h=S32(q+24); t->depth=S32(q+28);
      t->id=U32(q+32); t->xs=rdf32(P(q+36)); t->ys=rdf32(P(q+40)); t->color=U32(q+44); }
  }
  return 0;
}

/* ---------- PNG (RGBA8, non-interlaced) ---------- */
static inline int paeth(int a,int b,int c){ int p=a+b-c,pa=abs(p-a),pb=abs(p-b),pc=abs(p-c); return (pa<=pb&&pa<=pc)?a:(pb<=pc?b:c); }
uint8_t *png_decode_rgba(const uint8_t *png,size_t len,int *pw,int *ph){
  if(len<33||memcmp(png,"\x89PNG\r\n\x1a\n",8)) return NULL;
  int w=0,h=0; size_t p=8; size_t idatTotal=0;
  /* pass 1: sizes */
  for(;;){ if(p+12>len) return NULL; uint32_t l=__builtin_bswap32(rd32(png+p)); const uint8_t*t=png+p+4;
    if(!memcmp(t,"IHDR",4)){ w=__builtin_bswap32(rd32(png+p+8)); h=__builtin_bswap32(rd32(png+p+12)); if(png[p+16]!=8||png[p+17]!=6||png[p+20]!=0) return NULL; }
    else if(!memcmp(t,"IDAT",4)) idatTotal+=l;
    else if(!memcmp(t,"IEND",4)) break;
    p+=12+l; }
  uint8_t*zin=malloc(idatTotal); size_t zp=0; p=8;
  for(;;){ uint32_t l=__builtin_bswap32(rd32(png+p)); const uint8_t*t=png+p+4;
    if(!memcmp(t,"IDAT",4)){ memcpy(zin+zp,png+p+8,l); zp+=l; } else if(!memcmp(t,"IEND",4)) break; p+=12+l; }
  size_t stride=(size_t)w*4+1, rawsz=stride*h;
  uint8_t*raw=malloc(rawsz); if(!raw){ free(zin); return NULL; }
  z_stream z; memset(&z,0,sizeof z); inflateInit(&z); z.next_in=zin; z.avail_in=zp; z.next_out=raw; z.avail_out=rawsz;
  int rc=inflate(&z,Z_FINISH); inflateEnd(&z); free(zin);
  if(rc!=Z_STREAM_END||z.total_out!=rawsz){ free(raw); return NULL; }
  /* unfilter in place (bpp 4) */
  for(int y=0;y<h;y++){ uint8_t*row=raw+y*stride; int f=row[0]; uint8_t*cur=row+1; uint8_t*up=y?(raw+(y-1)*stride+1):NULL; int n=w*4;
    switch(f){
      case 0: break;
      case 1: for(int i=4;i<n;i++) cur[i]+=cur[i-4]; break;
      case 2: if(up) for(int i=0;i<n;i++) cur[i]+=up[i]; break;
      case 3: for(int i=0;i<n;i++){ int a=i>=4?cur[i-4]:0, b=up?up[i]:0; cur[i]+=(a+b)>>1; } break;
      case 4: for(int i=0;i<n;i++){ int a=i>=4?cur[i-4]:0, b=up?up[i]:0, c=(up&&i>=4)?up[i-4]:0; cur[i]+=paeth(a,b,c); } break;
      default: free(raw); return NULL; } }
  for(int y=0;y<h;y++) memmove(raw+(size_t)y*w*4,raw+y*stride+1,(size_t)w*4);
  *pw=w; *ph=h; return raw;   /* (buffer slightly larger than needed; fine) */
}
