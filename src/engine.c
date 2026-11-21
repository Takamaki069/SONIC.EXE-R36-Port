/* VM + engine core. Platform independent. */
#include "gm.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>

const char *g_player_name="player"; Platform *PL; int g_tex_budget_mb=160, g_verbose=0, g_hide_touch=1;

/* ================= builtin variable ids ================= */
enum { BV_NONE=0, BV_X,BV_Y,BV_XPREV,BV_YPREV,BV_HSPEED,BV_VSPEED,BV_SPEED,BV_DIRECTION,BV_FRICTION,BV_GRAVITY,BV_GDIR,
  BV_SPRITE,BV_IMAGE_INDEX,BV_IMAGE_SPEED,BV_XSCALE,BV_YSCALE,BV_ANGLE,BV_ALPHA,BV_BLEND,BV_VISIBLE,BV_DEPTH,BV_SOLID,BV_MASK,
  BV_ID,BV_OBJ,BV_PERSISTENT,BV_ALARM,BV_TL_INDEX,BV_TL_POS,BV_TL_RUNNING,
  BV_ROOM,BV_ROOM_SPEED,BV_OS_TYPE,BV_VIEW_X,BV_VIEW_Y,BV_VIEW_W,BV_VIEW_H,BV_VIEW_OBJ,
  BV_BG_VISIBLE,BV_BG_INDEX,BV_BG_HSPEED,BV_BG_VSPEED,BV_BG_XSCALE,BV_BG_YSCALE,BV_BG_BLEND,BV_BG_ALPHA,BV_BG_X,BV_BG_Y,BV_BG_FG,BV_BG_COLOUR,
  BV_ARG0 };
static const struct { const char*n; int b; } bvt[]={
 {"x",BV_X},{"y",BV_Y},{"xprevious",BV_XPREV},{"yprevious",BV_YPREV},{"hspeed",BV_HSPEED},{"vspeed",BV_VSPEED},{"speed",BV_SPEED},
 {"direction",BV_DIRECTION},{"friction",BV_FRICTION},{"gravity",BV_GRAVITY},{"gravity_direction",BV_GDIR},{"sprite_index",BV_SPRITE},
 {"image_index",BV_IMAGE_INDEX},{"image_speed",BV_IMAGE_SPEED},{"image_xscale",BV_XSCALE},{"image_yscale",BV_YSCALE},{"image_angle",BV_ANGLE},
 {"image_alpha",BV_ALPHA},{"image_blend",BV_BLEND},{"visible",BV_VISIBLE},{"depth",BV_DEPTH},{"solid",BV_SOLID},{"mask_index",BV_MASK},
 {"id",BV_ID},{"object_index",BV_OBJ},{"persistent",BV_PERSISTENT},{"alarm",BV_ALARM},{"timeline_index",BV_TL_INDEX},{"timeline_position",BV_TL_POS},
 {"timeline_running",BV_TL_RUNNING},{"room",BV_ROOM},{"room_speed",BV_ROOM_SPEED},{"os_type",BV_OS_TYPE},{"view_xview",BV_VIEW_X},{"view_yview",BV_VIEW_Y},
 {"view_wview",BV_VIEW_W},{"view_hview",BV_VIEW_H},{"view_object",BV_VIEW_OBJ},{"background_visible",BV_BG_VISIBLE},{"background_index",BV_BG_INDEX},
 {"background_hspeed",BV_BG_HSPEED},{"background_vspeed",BV_BG_VSPEED},{"background_xscale",BV_BG_XSCALE},{"background_yscale",BV_BG_YSCALE},
 {"background_blend",BV_BG_BLEND},{"background_alpha",BV_BG_ALPHA},{"background_x",BV_BG_X},{"background_y",BV_BG_Y},{"background_foreground",BV_BG_FG},
 {"background_colour",BV_BG_COLOUR},{"background_color",BV_BG_COLOUR},{0,0}};
static int *vbi;   /* builtin id per VARI index */

/* ================= instances ================= */
typedef struct Inst {
  int id,obj; int dead; double x,y,xp,yp,hs,vs,spd,dir,fric,grav,gdir;
  int sprite,mask; double ii,isp,xs,ys,ang,alpha; uint32_t blend; int visible,solid,depth,persistent;
  int alarm[12]; int tl,tl_run; double tl_pos; int order; Value *uv;
} Inst;

static Inst **I; static int nI,capI; static int nextid=100000;
static Inst dummy; static Value gv[MAX_VARS]; static int nvars;

static struct {
  int room,next_room,os_type; uint8_t key[256],kp[256],kr[256],prevkey[256];
  double vx[8],vy[8],vw[8],vh[8]; int vobj[8];
  int bgvis[8],bgidx[8],bgfg[8]; double bgx[8],bgy[8],bghs[8],bgvs[8],bgxs[8],bgys[8],bgalpha[8]; uint32_t bgblend[8]; uint32_t bgcolor;
  int relative; uint32_t dcolor; int dfont,dhalign; int guiw,guih;
  /* draw transform */ double tvx,tvy,tsc,tpx,tpy;
  int frame; int touch_obj;
} E;

/* Audio started by a room's Create code is held back until that room's first frame has been presented,
   so the picture and the music appear together even when the textures take a while to load. */
typedef struct { int kind,snd,loop; } AudCmd;
static AudCmd aq[128]; static int naq,audio_hold;
static void aud_exec(int kind,int snd,int loop){ switch(kind){ case 0: PL->sfx_play(snd,loop); break; case 1: PL->sfx_stop(snd); break; case 2: PL->music_play(snd,loop); break; case 3: PL->music_stop(); break; } }
static void aud(int kind,int snd,int loop){ if(audio_hold){ if(naq<128) aq[naq++]=(AudCmd){kind,snd,loop}; return; } aud_exec(kind,snd,loop); }
void engine_present_done(void){ if(!audio_hold) return; audio_hold=0; for(int i=0;i<naq;i++) aud_exec(aq[i].kind,aq[i].snd,aq[i].loop); naq=0; }
static int tex_loaded[64]; static int tex_last[64]; static size_t tex_bytes;

/* ---- background PNG decoding: pages likely needed soon are decoded on a worker thread while the main thread does other work ---- */
enum { T_NONE=0, T_QUEUED, T_DECODING, T_READY, T_FAILED };
static struct { int state; uint8_t *px; int w,h; } tpf[64];
static pthread_mutex_t tmx=PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t tcv=PTHREAD_COND_INITIALIZER;
static int tq[64],tqn; static int tth_started; static size_t pf_bytes;
static void *tex_worker(void*arg){ (void)arg;
  for(;;){ pthread_mutex_lock(&tmx); while(!tqn) pthread_cond_wait(&tcv,&tmx);
    int p=tq[0]; memmove(tq,tq+1,(--tqn)*sizeof(int)); tpf[p].state=T_DECODING; pthread_mutex_unlock(&tmx);
    int w=0,h=0; uint8_t*px=png_decode_rgba(D.tex[p].png,(size_t)-1>>1,&w,&h);
    pthread_mutex_lock(&tmx); tpf[p].px=px; tpf[p].w=w; tpf[p].h=h; tpf[p].state=px?T_READY:T_FAILED; pthread_cond_broadcast(&tcv); pthread_mutex_unlock(&tmx); }
  return NULL; }
static void tex_prefetch(int p){
  if(p<0||p>=D.ntex||p>=64||tex_loaded[p]) return; size_t need=(size_t)D.tex[p].w*D.tex[p].h*4;
  pthread_mutex_lock(&tmx);
  if(tpf[p].state==T_NONE&&pf_bytes+need<=(size_t)128<<20&&tex_bytes+pf_bytes+need<=(size_t)g_tex_budget_mb*1048576){
    if(!tth_started){ pthread_t t; if(pthread_create(&t,NULL,tex_worker,NULL)==0){ pthread_detach(t); tth_started=1; } }
    if(tth_started){ tq[tqn++]=p; tpf[p].state=T_QUEUED; pf_bytes+=need; pthread_cond_signal(&tcv); } }
  pthread_mutex_unlock(&tmx); }
/* returns a ready decoded buffer (waiting for the worker if it is mid-decode), or NULL if the caller must decode itself */
static uint8_t *tex_take_prefetched(int p,int*w,int*h){
  uint8_t*px=NULL; pthread_mutex_lock(&tmx);
  if(tpf[p].state==T_QUEUED){ for(int i=0;i<tqn;i++) if(tq[i]==p){ memmove(tq+i,tq+i+1,(--tqn-i)*sizeof(int)); break; } tpf[p].state=T_NONE; pf_bytes-=(size_t)D.tex[p].w*D.tex[p].h*4; }
  while(tpf[p].state==T_DECODING) pthread_cond_wait(&tcv,&tmx);
  if(tpf[p].state==T_READY){ px=tpf[p].px; *w=tpf[p].w; *h=tpf[p].h; tpf[p].px=NULL; tpf[p].state=T_NONE; pf_bytes-=(size_t)D.tex[p].w*D.tex[p].h*4; }
  else if(tpf[p].state==T_FAILED){ tpf[p].state=T_NONE; pf_bytes-=(size_t)D.tex[p].w*D.tex[p].h*4; }
  pthread_mutex_unlock(&tmx); return px; }
static void tex_discard_unused(const uint8_t*keep){      /* free decoded-but-unused buffers so they cannot pile up */
  pthread_mutex_lock(&tmx);
  for(int p=0;p<D.ntex&&p<64;p++) if(tpf[p].state==T_READY&&!keep[p]){ free(tpf[p].px); tpf[p].px=NULL; tpf[p].state=T_NONE; pf_bytes-=(size_t)D.tex[p].w*D.tex[p].h*4; }
  pthread_mutex_unlock(&tmx); }

static void tex_use(int p){
  if(p<0||p>=D.ntex) return; tex_last[p]=E.frame;
  if(tex_loaded[p]) return;
  size_t need=(size_t)D.tex[p].w*D.tex[p].h*4;
  while(tex_bytes+need>(size_t)g_tex_budget_mb*1048576){ int best=-1,bl=1<<30;
    for(int i=0;i<D.ntex;i++) if(tex_loaded[i]&&tex_last[i]<E.frame&&tex_last[i]<bl){ bl=tex_last[i]; best=i; }
    if(best<0) break; PL->page_free(best); tex_loaded[best]=0; tex_bytes-=(size_t)D.tex[best].w*D.tex[best].h*4;
    if(g_verbose) fprintf(stderr,"[tex] evict page %d\n",best); }
  int w,h; uint8_t*px=tex_take_prefetched(p,&w,&h); int pre=px!=NULL;
  if(!px) px=png_decode_rgba(D.tex[p].png,(size_t)-1>>1,&w,&h);
  if(!px){ fprintf(stderr,"[tex] decode failed for page %d\n",p); tex_loaded[p]=1; return; }
  PL->page_upload(p,w,h,px); free(px); tex_loaded[p]=1; tex_bytes+=need;
  if(g_verbose) fprintf(stderr,"[tex] loaded page %d (%dx%d)%s, resident %zu MB\n",p,w,h,pre?" [prefetched]":"",tex_bytes>>20); }

/* which texture pages does a room use (its instances' sprites, backgrounds and tiles)? */
static void room_pages(int r,uint8_t*used){
  memset(used,0,64); if(r<0||r>=D.nroom) return; Room*rm=&D.room[r];
  for(int k=0;k<rm->ninst;k++){ int o=rm->inst[k].obj; if(o<0||o>=D.nobj) continue; int sp=D.obj[o].sprite; if(sp<0||sp>=D.nspr) continue;
    Sprite*s=&D.spr[sp]; for(int f=0;f<s->nframes;f++) if(s->fr[f]&&s->fr[f]->page<64) used[s->fr[f]->page]=1; }
  for(int b=0;b<MAX_BG;b++) if(rm->bg[b].enabled&&rm->bg[b].bg>=0&&rm->bg[b].bg<D.nbg) used[D.bg[rm->bg[b].bg].tp->page&63]=1;
  for(int k=0;k<rm->ntile;k++){ int b=rm->tile[k].bg; if(b>=0&&b<D.nbg) used[D.bg[b].tp->page&63]=1; } }
static void prefetch_room(int r){ uint8_t u[64]; room_pages(r,u); for(int p=0;p<D.ntex&&p<64;p++) if(u[p]) tex_prefetch(p); }
/* call right after data_load(): starts decoding the title screen's textures while SDL/GL initialise */
void engine_prefetch_start(void){ if(D.nroom>1) prefetch_room(1); }

static inline double num(Value v){ return v.s? atof(v.s):v.d; }
static inline int truth(Value v){ return v.s?0:(v.d>0.5); }
static double ang_norm(double a){ a=fmod(a,360.0); if(a<0)a+=360; return a; }
/* exact lengthdir for multiples of 90 degrees: cos(270deg) is -1.8e-16 in floating point, which made
   gravity leak a tiny horizontal speed and broke exact comparisons like hspeed>=1 in the game code */
static void lendir(double deg,double*cx,double*sy){
  double a=ang_norm(deg);
  if(a==0){ *cx=1; *sy=0; } else if(a==90){ *cx=0; *sy=1; } else if(a==180){ *cx=-1; *sy=0; } else if(a==270){ *cx=0; *sy=-1; }
  else { double r=a*M_PI/180; *cx=cos(r); *sy=sin(r); } }
static void sync_hv(Inst*i){ i->spd=hypot(i->hs,i->vs); if(i->spd>1e-9) i->dir=ang_norm(atan2(-i->vs,i->hs)*180/M_PI); }
static void sync_sd(Inst*i){ double c,sn; lendir(i->dir,&c,&sn); i->hs=i->spd*c; i->vs=-i->spd*sn; }

static Inst *inst_by_id(int id){ for(int k=0;k<nI;k++) if(I[k]->id==id&&!I[k]->dead) return I[k]; return NULL; }
static Inst *inst_first_obj(int o){ for(int k=0;k<nI;k++) if(I[k]->obj==o&&!I[k]->dead) return I[k]; return NULL; }
int engine_room(void){ return E.room; }
int engine_ninst(void){ int n=0; for(int k=0;k<nI;k++) if(!I[k]->dead) n++; return n; }

/* ---- sprite bbox / collision ---- */
static Sprite *mask_sprite(Inst*i){ int s=i->mask>=0?i->mask:i->sprite; return (s>=0&&s<D.nspr)?&D.spr[s]:NULL; }
static int bbox(Inst*i,double x,double y,double*l,double*t,double*r,double*b){
  Sprite*s=mask_sprite(i); if(!s) return 0;
  double a=x+(s->ml-s->ox)*i->xs, c=x+(s->mr+1-s->ox)*i->xs, d=y+(s->mt-s->oy)*i->ys, e=y+(s->mb+1-s->oy)*i->ys;
  *l=a<c?a:c; *r=a<c?c:a; *t=d<e?d:e; *b=d<e?e:d; return 1; }
static int maskbit(Sprite*s,int frame,int lx,int ly){
  if(lx<0||ly<0||lx>=s->w||ly>=s->h) return 0; if(!s->nmasks) return lx>=s->ml&&lx<=s->mr&&ly>=s->mt&&ly<=s->mb;
  int m=frame<s->nmasks?frame:s->nmasks-1; if(m<0)m=0; if(frame<0)m=0;
  return (s->masks[(size_t)m*s->stride*s->h+(size_t)ly*s->stride+(lx>>3)]>>(7-(lx&7)))&1; }
static int collide_at(Inst*a,double ax,double ay,Inst*b){
  double al,at,ar,ab,bl,bt,br,bb;
  if(!bbox(a,ax,ay,&al,&at,&ar,&ab)||!bbox(b,b->x,b->y,&bl,&bt,&br,&bb)) return 0;
  if(ar<=bl||br<=al||ab<=bt||bb<=at) return 0;
  Sprite*sa=mask_sprite(a),*sb=mask_sprite(b);
  if(sa->sep!=1&&sb->sep!=1) return 1;
  int x0=(int)floor(fmax(al,bl)),x1=(int)ceil(fmin(ar,br)),y0=(int)floor(fmax(at,bt)),y1=(int)ceil(fmin(ab,bb));
  int fa=(int)floor(a->ii),fb=(int)floor(b->ii);
  for(int py=y0;py<y1;py++) for(int px=x0;px<x1;px++){
    double cx=px+0.5,cy=py+0.5;
    int ax_=(int)floor((cx-ax)/a->xs+sa->ox), ay_=(int)floor((cy-ay)/a->ys+sa->oy);
    int bx_=(int)floor((cx-b->x)/b->xs+sb->ox), by_=(int)floor((cy-b->y)/b->ys+sb->oy);
    if(maskbit(sa,fa,ax_,ay_)&&maskbit(sb,fb,bx_,by_)) return 1; }
  return 0; }
static int is_solid(Inst*i){ return i->solid; }

/* ================= VM ================= */
typedef struct { Inst *self,*other; Value args[16]; int nargs; int relative; Value loc[MAX_VARS]; } Ctx;
typedef Value (*Fn)(Ctx*,Value*,int);
static Value vm_exec(int code,Inst*self,Inst*other,const Value*args,int nargs);
static void run_code(int code,Inst*self,Inst*other){ if(code>=0&&code<D.ncode) vm_exec(code,self,other,NULL,0); }
static void run_event(Inst*i,int type,int sub){
  Object*o=&D.obj[i->obj];
  for(int k=0;k<o->nev[type];k++){ Event*e=&o->ev[type][k]; if(e->sub!=sub) continue;
    for(int a=0;a<e->n;a++){ if(i->dead&&type!=1) return; run_code(e->codes[a],i,NULL); } } }

static Inst *inst_alloc(int obj,double x,double y,int id){
  Inst*i=calloc(1,sizeof(Inst)); Object*o=&D.obj[obj];
  i->id=id; i->obj=obj; i->x=i->xp=x; i->y=i->yp=y; i->gdir=270; i->sprite=o->sprite; i->mask=o->mask; i->isp=1; i->xs=i->ys=1; i->alpha=1;
  i->blend=0xFFFFFF; i->visible=o->visible; i->solid=o->solid; i->depth=o->depth; i->persistent=o->persistent; for(int k=0;k<12;k++) i->alarm[k]=-1;
  i->tl=-1; i->uv=calloc(nvars,sizeof(Value)); i->order=nextid;
  if(nI==capI){ capI=capI?capI*2:512; I=realloc(I,capI*sizeof(Inst*)); } I[nI++]=i; return i; }
static Inst *inst_create(int obj,double x,double y){
  if(obj<0||obj>=D.nobj) return NULL; if(g_hide_touch&&obj==E.touch_obj) return NULL;
  Inst*i=inst_alloc(obj,x,y,++nextid); run_event(i,0,0); return i; }
static void inst_destroy(Inst*i){ if(!i||i->dead) return; run_event(i,1,0); i->dead=1; }

/* ---- instance specifier resolution ---- */
static int resolve(Ctx*c,int spec,Inst**out,int max){
  int n=0;
  if(spec==-1){ out[n++]=c->self; }
  else if(spec==-2){ if(c->other) out[n++]=c->other; }
  else if(spec==-3){ for(int k=0;k<nI&&n<max;k++) if(!I[k]->dead) out[n++]=I[k]; }
  else if(spec==-4){ }
  else if(spec>=100000){ Inst*i=inst_by_id(spec); if(i) out[n++]=i; }
  else if(spec>=0){ for(int k=0;k<nI&&n<max;k++) if(I[k]->obj==spec&&!I[k]->dead) out[n++]=I[k]; }
  return n; }

static Value get_b(Ctx*c,int b,Inst*i,int idx){
  switch(b){
  case BV_X: return vnum(i->x); case BV_Y: return vnum(i->y); case BV_XPREV: return vnum(i->xp); case BV_YPREV: return vnum(i->yp);
  case BV_HSPEED: return vnum(i->hs); case BV_VSPEED: return vnum(i->vs); case BV_SPEED: return vnum(i->spd); case BV_DIRECTION: return vnum(i->dir);
  case BV_FRICTION: return vnum(i->fric); case BV_GRAVITY: return vnum(i->grav); case BV_GDIR: return vnum(i->gdir);
  case BV_SPRITE: return vnum(i->sprite); case BV_IMAGE_INDEX: return vnum(i->ii); case BV_IMAGE_SPEED: return vnum(i->isp);
  case BV_XSCALE: return vnum(i->xs); case BV_YSCALE: return vnum(i->ys); case BV_ANGLE: return vnum(i->ang); case BV_ALPHA: return vnum(i->alpha);
  case BV_BLEND: return vnum(i->blend); case BV_VISIBLE: return vnum(i->visible); case BV_DEPTH: return vnum(i->depth); case BV_SOLID: return vnum(i->solid);
  case BV_MASK: return vnum(i->mask); case BV_ID: return vnum(i->id); case BV_OBJ: return vnum(i->obj); case BV_PERSISTENT: return vnum(i->persistent);
  case BV_ALARM: return vnum(idx>=0&&idx<12?i->alarm[idx]:-1);
  case BV_TL_INDEX: return vnum(i->tl); case BV_TL_POS: return vnum(i->tl_pos); case BV_TL_RUNNING: return vnum(i->tl_run);
  } return vnum(0); }
static void set_b(int b,Inst*i,int idx,Value v){
  double d=num(v);
  switch(b){
  case BV_X: i->x=d; break; case BV_Y: i->y=d; break; case BV_XPREV: i->xp=d; break; case BV_YPREV: i->yp=d; break;
  case BV_HSPEED: i->hs=d; sync_hv(i); break; case BV_VSPEED: i->vs=d; sync_hv(i); break;
  case BV_SPEED: i->spd=d; sync_sd(i); break; case BV_DIRECTION: i->dir=ang_norm(d); sync_sd(i); break;
  case BV_FRICTION: i->fric=d; break; case BV_GRAVITY: i->grav=d; break; case BV_GDIR: i->gdir=d; break;
  case BV_SPRITE: i->sprite=(int)d; break; case BV_IMAGE_INDEX: i->ii=d; break; case BV_IMAGE_SPEED: i->isp=d; break;
  case BV_XSCALE: i->xs=d; break; case BV_YSCALE: i->ys=d; break; case BV_ANGLE: i->ang=d; break; case BV_ALPHA: i->alpha=d; break;
  case BV_BLEND: i->blend=(uint32_t)(long long)d; break; case BV_VISIBLE: i->visible=d>0.5; break; case BV_DEPTH: i->depth=(int)d; break;
  case BV_SOLID: i->solid=d>0.5; break; case BV_MASK: i->mask=(int)d; break; case BV_PERSISTENT: i->persistent=d>0.5; break;
  case BV_ALARM: if(idx>=0&&idx<12) i->alarm[idx]=(int)d; break;
  case BV_TL_INDEX: i->tl=(int)d; break; case BV_TL_POS: i->tl_pos=d; break; case BV_TL_RUNNING: i->tl_run=d>0.5; break;
  } }

static Value var_get(Ctx*c,int vi,int arr,int spec,int owner,int idx){
  int b=vbi[vi];
  switch(b){
  case BV_ROOM: return vnum(E.room); case BV_ROOM_SPEED: return vnum(D.room[E.room].speed); case BV_OS_TYPE: return vnum(E.os_type);
  case BV_VIEW_X: return vnum(E.vx[idx&7]); case BV_VIEW_Y: return vnum(E.vy[idx&7]); case BV_VIEW_W: return vnum(E.vw[idx&7]); case BV_VIEW_H: return vnum(E.vh[idx&7]);
  case BV_VIEW_OBJ: return vnum(E.vobj[idx&7]);
  case BV_BG_VISIBLE: return vnum(E.bgvis[idx&7]); case BV_BG_INDEX: return vnum(E.bgidx[idx&7]); case BV_BG_HSPEED: return vnum(E.bghs[idx&7]);
  case BV_BG_VSPEED: return vnum(E.bgvs[idx&7]); case BV_BG_XSCALE: return vnum(E.bgxs[idx&7]); case BV_BG_YSCALE: return vnum(E.bgys[idx&7]);
  case BV_BG_BLEND: return vnum(E.bgblend[idx&7]); case BV_BG_ALPHA: return vnum(E.bgalpha[idx&7]); case BV_BG_X: return vnum(E.bgx[idx&7]);
  case BV_BG_Y: return vnum(E.bgy[idx&7]); case BV_BG_FG: return vnum(E.bgfg[idx&7]); case BV_BG_COLOUR: return vnum(E.bgcolor);
  }
  if(b>=BV_ARG0&&b<BV_ARG0+16) return c->args[b-BV_ARG0];
  if(spec==-5&&!arr) return gv[vi];
  if(spec==-7&&!arr) return c->loc[vi];
  Inst*t[1]; int sp=arr?owner:spec; if(!resolve(c,sp,t,1)) return vnum(0);
  if(b) return get_b(c,b,t[0],idx);
  return t[0]->uv[vi]; }

static void var_set(Ctx*c,int vi,int arr,int spec,int owner,int idx,Value v){
  int b=vbi[vi]; double d=num(v);
  switch(b){
  case BV_ROOM: E.next_room=(int)d; return;
  case BV_VIEW_X: E.vx[idx&7]=d; return; case BV_VIEW_Y: E.vy[idx&7]=d; return; case BV_VIEW_W: E.vw[idx&7]=d; return; case BV_VIEW_H: E.vh[idx&7]=d; return;
  case BV_VIEW_OBJ: E.vobj[idx&7]=(int)d; return;
  case BV_BG_VISIBLE: E.bgvis[idx&7]=d>0.5; return; case BV_BG_INDEX: E.bgidx[idx&7]=(int)d; return; case BV_BG_HSPEED: E.bghs[idx&7]=d; return;
  case BV_BG_VSPEED: E.bgvs[idx&7]=d; return; case BV_BG_XSCALE: E.bgxs[idx&7]=d; return; case BV_BG_YSCALE: E.bgys[idx&7]=d; return;
  case BV_BG_BLEND: E.bgblend[idx&7]=(uint32_t)(long long)d; return; case BV_BG_ALPHA: E.bgalpha[idx&7]=d; return; case BV_BG_X: E.bgx[idx&7]=d; return;
  case BV_BG_Y: E.bgy[idx&7]=d; return; case BV_BG_FG: E.bgfg[idx&7]=d>0.5; return; case BV_BG_COLOUR: E.bgcolor=(uint32_t)(long long)d; return;
  case BV_ROOM_SPEED: case BV_OS_TYPE: return;
  }
  if(b>=BV_ARG0&&b<BV_ARG0+16){ c->args[b-BV_ARG0]=v; return; }
  if(spec==-5&&!arr){ gv[vi]=v; return; }
  if(spec==-7&&!arr){ c->loc[vi]=v; return; }
  Inst*t[2048]; int sp=arr?owner:spec; int n=resolve(c,sp,t,2048);
  for(int k=0;k<n;k++){ if(b) set_b(b,t[k],idx,v); else t[k]->uv[vi]=v; } }

/* ---- function table (filled below) ---- */
typedef struct { const char*n; Fn f; } FnEnt;
static Fn *fnmap;           /* per FUNC index */
static int *fnscript;       /* per FUNC index: script code or -1 */
static unsigned char *fnwarn;

static int codeidx_by_name(const char*n){ for(int i=0;i<D.ncode;i++) if(!strcmp(D.code[i].name,n)) return i; return -1; }

typedef struct { Inst **l; int n,idx; Inst *ss,*so; } Env;

static Value vm_exec(int code,Inst*self,Inst*other,const Value*args,int nargs){
  static int depth=0; if(++depth>64){ depth--; fprintf(stderr,"vm: recursion limit\n"); return vnum(0); }
  Ctx *c=calloc(1,sizeof(Ctx)); c->self=self?self:&dummy; c->other=other; c->nargs=nargs; for(int k=0;k<nargs&&k<16;k++) c->args[k]=args[k];
  Value stk[256]; int sp=0; Env env[16]; int ne=0; Value ret=vnum(0);
  const uint8_t*bc=D.code[code].bc; int len=D.code[code].len; int pc=0;
  #define PUSH(v) do{ if(sp>=255){ fprintf(stderr,"vm: stack overflow in %s\n",D.code[code].name); goto done; } stk[sp++]=(v);}while(0)
  #define POP() (sp>0?stk[--sp]:vnum(0))
  while(pc<len){
    uint32_t w; memcpy(&w,bc+pc,4); int op=w>>24,t1=(w>>16)&15,t2=(w>>20)&15; int lo=w&0xFFFF;
    switch(op){
    case 0x03: { Value v=stk[sp-1]; switch(t2){
        case 4: v=vnum(num(v)>0.5); break;
        case 2: case 3: v=vnum(trunc(num(v))); break;
        case 0: case 1: if(v.s) v=vnum(atof(v.s)); break;
        case 6: if(!v.s) v=vstr(num_to_str(v.d)); break; }
        stk[sp-1]=v; pc+=4; break; }
    case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0f: case 0x10: {
        Value r=POP(),l=POP(); double a=num(l),b=num(r),o=0;
        if(op==0x08&&(l.s||r.s)){ char*t=malloc(strlen(l.s?l.s:num_to_str(l.d))+strlen(r.s?r.s:num_to_str(r.d))+1);
          strcpy(t,l.s?l.s:num_to_str(l.d)); strcat(t,r.s?r.s:num_to_str(r.d)); PUSH(vstr(str_intern(t))); free(t); pc+=4; break; }
        switch(op){ case 4:o=a*b;break; case 5:o=b!=0?a/b:0;break; case 6: case 7:o=b!=0?fmod(a,b):0;break; case 8:o=a+b;break; case 9:o=a-b;break;
          case 0xa:o=(double)((long long)a&(long long)b);break; case 0xb:o=(double)((long long)a|(long long)b);break; case 0xc:o=(double)((long long)a^(long long)b);break;
          case 0xf:o=(double)((long long)a<<(int)b);break; case 0x10:o=(double)((long long)a>>(int)b);break; }
        PUSH(vnum(o)); pc+=4; break; }
    case 0x0d: stk[sp-1]=vnum(-num(stk[sp-1])); pc+=4; break;
    case 0x0e: if(t1==4) stk[sp-1]=vnum(!truth(stk[sp-1])); else stk[sp-1]=vnum((double)(~(long long)num(stk[sp-1]))); pc+=4; break;
    case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: {
        Value r=POP(),l=POP(); int cmp;
        if(l.s&&r.s) cmp=strcmp(l.s,r.s); else { double a=num(l),b=num(r); cmp=a<b?-1:(a>b?1:0); if(op==0x13||op==0x14){ if(fabs(a-b)<1e-9) cmp=0; } }
        int res=0; switch(op){ case 0x11:res=cmp<0;break; case 0x12:res=cmp<=0;break; case 0x13:res=cmp==0;break; case 0x14:res=cmp!=0;break; case 0x15:res=cmp>=0;break; case 0x16:res=cmp>0;break; }
        PUSH(vnum(res)); pc+=4; break; }
    case 0xc0: case 0xc1: case 0xc2: case 0xc3: case 0x84: {
        if(op==0x84){ PUSH(vnum((int16_t)lo)); pc+=4; break; }
        switch(t1){
        case 15: PUSH(vnum((int16_t)lo)); pc+=4; break;
        case 2: { int32_t v; memcpy(&v,bc+pc+4,4); PUSH(vnum(v)); pc+=8; break; }
        case 3: { int64_t v; memcpy(&v,bc+pc+4,8); PUSH(vnum((double)v)); pc+=12; break; }
        case 0: { double v; memcpy(&v,bc+pc+4,8); PUSH(vnum(v)); pc+=12; break; }
        case 1: { float v; memcpy(&v,bc+pc+4,4); PUSH(vnum(v)); pc+=8; break; }
        case 4: { uint32_t v; memcpy(&v,bc+pc+4,4); PUSH(vnum(v!=0)); pc+=8; break; }
        case 6: { uint32_t v; memcpy(&v,bc+pc+4,4); PUSH(vstr((int)v<D.nstr?D.str[v]:"")); pc+=8; break; }
        case 5: { uint32_t ref; memcpy(&ref,bc+pc+4,4); int vi=ref&0xFFFFFF; int arr=(ref>>24)==0; int spec=(int16_t)lo; int idx=0,owner=-1;
          if(arr){ idx=(int)num(POP()); owner=(int)num(POP()); }
          PUSH(var_get(c,vi,arr,spec,owner,idx)); pc+=8; break; }
        default: fprintf(stderr,"vm: push type %d unsupported (%s+%x)\n",t1,D.code[code].name,pc); goto done; }
        break; }
    case 0x41: { uint32_t ref; memcpy(&ref,bc+pc+4,4); int vi=ref&0xFFFFFF; int arr=(ref>>24)==0; int spec=(int16_t)lo; Value v; int idx=0,owner=-1;
        if(arr){ if(t2==5&&t1!=5){ v=POP(); idx=(int)num(POP()); owner=(int)num(POP()); } else { idx=(int)num(POP()); owner=(int)num(POP()); v=POP(); } }
        else v=POP();
        var_set(c,vi,arr,spec,owner,idx,v); pc+=8; break; }
    case 0x82: { int n=lo+1; if(sp>=n&&sp+n<256){ for(int k=0;k<n;k++) stk[sp+k]=stk[sp-n+k]; sp+=n; } pc+=4; break; }
    case 0x9f: if(sp>0) sp--; pc+=4; break;
    case 0x9d: ret=POP(); goto done;
    case 0x9e: goto done;
    case 0xb7: case 0xb8: case 0xb9: { int off=w&0x7FFFFF; if(off&0x400000) off-=0x800000;
        if(op==0xb7) pc+=off*4; else { Value v=POP(); int t=truth(v); if((op==0xb8&&t)||(op==0xb9&&!t)) pc+=off*4; else pc+=4; } break; }
    case 0xbb: { int off=w&0x7FFFFF; if(off&0x400000) off-=0x800000; Value tv=POP(); Inst*tmp[2048]; int n=resolve(c,(int)num(tv),tmp,2048);
        if(n==0||ne>=16){ pc+=off*4+4; break; }
        Env*e=&env[ne++]; e->l=malloc(n*sizeof(Inst*)); memcpy(e->l,tmp,n*sizeof(Inst*)); e->n=n; e->idx=0; e->ss=c->self; e->so=c->other;
        c->other=c->self; c->self=e->l[0]; pc+=4; break; }
    case 0xbc: { int off=w&0x7FFFFF; if(off&0x400000) off-=0x800000; if(ne==0){ pc+=4; break; } Env*e=&env[ne-1];
        if(++e->idx<e->n){ c->self=e->l[e->idx]; pc+=off*4; } else { c->self=e->ss; c->other=e->so; free(e->l); ne--; pc+=4; } break; }
    case 0xda: { uint32_t ref; memcpy(&ref,bc+pc+4,4); int fi=ref&0xFFFFFF; Value a[16]; int n=lo<16?lo:16; for(int k=0;k<lo;k++){ Value v=POP(); if(k<16) a[k]=v; }
        Value r=vnum(0);
        if(fnscript[fi]>=0){ r=vm_exec(fnscript[fi],c->self,c->other,a,n); }
        else if(fnmap[fi]) r=fnmap[fi](c,a,n);
        else if(!fnwarn[fi]){ fnwarn[fi]=1; fprintf(stderr,"[gml] unimplemented function: %s\n",D.func[fi].name); }
        PUSH(r); pc+=8; break; }
    case 0xff: pc+=4; break;
    default: fprintf(stderr,"vm: bad opcode %02x in %s+%x\n",op,D.code[code].name,pc); goto done; }
  }
done:
  for(int k=0;k<ne;k++) free(env[k].l);
  free(c); depth--; return ret; }

/* ================= GML function library ================= */
#define ARG(i) ((i)<n?a[i]:vnum(0))
#define NARG(i) num(ARG(i))
#define FN(name) static Value f_##name(Ctx*c,Value*a,int n)
FN(show_debug_message){ fprintf(stderr,"[game] %s\n",ARG(0).s?ARG(0).s:num_to_str(NARG(0))); return vnum(0); }
FN(room_goto){ E.next_room=(int)NARG(0); return vnum(0); }
FN(room_goto_next){ E.next_room=E.room+1<D.nroom?E.room+1:E.room; return vnum(0); }
FN(instance_create){ Inst*i=inst_create((int)NARG(2),NARG(0),NARG(1)); return vnum(i?i->id:-4); }
FN(instance_destroy){ inst_destroy(c->self); return vnum(0); }
FN(instance_exists){ int o=(int)NARG(0); if(o>=100000) return vnum(inst_by_id(o)!=NULL); return vnum(inst_first_obj(o)!=NULL); }
FN(place_free){ Inst*s=c->self; double x=NARG(0),y=NARG(1);
  for(int k=0;k<nI;k++){ Inst*o=I[k]; if(o==s||o->dead||!is_solid(o)) continue; if(collide_at(s,x,y,o)) return vnum(0); } return vnum(1); }
FN(keyboard_check){ return vnum(E.key[(int)NARG(0)&255]); }
FN(keyboard_check_pressed){ return vnum(E.kp[(int)NARG(0)&255]); }
FN(keyboard_check_released){ return vnum(E.kr[(int)NARG(0)&255]); }
static void play_snd(int s,int loop){ if(s<0||s>=D.nsnd) return; if(D.snd[s].embedded) aud(0,s,loop); else aud(2,s,loop); }
FN(audio_play_sound){ play_snd((int)NARG(0),truth(ARG(2))); return vnum(0); }
FN(audio_play_music){ play_snd((int)NARG(0),truth(ARG(1))); return vnum(0); }
FN(audio_stop_music){ aud(3,0,0); return vnum(0); }
FN(audio_stop_sound){ int s=(int)NARG(0); if(s>=0&&s<D.nsnd){ if(D.snd[s].embedded) aud(1,s,0); else aud(3,0,0); } return vnum(0); }
FN(action_sound){ play_snd((int)NARG(0),truth(ARG(1))); return vnum(0); }
FN(action_end_sound){ return f_audio_stop_sound(c,a,n); }
FN(action_set_relative){ c->relative=truth(ARG(0)); return vnum(0); }
FN(action_sprite_set){ Inst*s=c->self; int sp=(int)NARG(0); if(sp>=0) s->sprite=sp; double si=NARG(1); if(si>=0) s->ii=si; s->isp=NARG(2); return vnum(0); }
FN(action_set_alarm){ int k=(int)NARG(1); if(k>=0&&k<12){ if(c->relative) c->self->alarm[k]+=(int)NARG(0); else c->self->alarm[k]=(int)NARG(0); } return vnum(0); }
FN(action_effect){ return vnum(0); }
FN(action_kill_object){ inst_destroy(c->self); return vnum(0); }
FN(action_create_object){ double x=NARG(1),y=NARG(2); if(c->relative){ x+=c->self->x; y+=c->self->y; } inst_create((int)NARG(0),x,y); return vnum(0); }
FN(action_next_room){ return f_room_goto_next(c,a,n); }
FN(action_another_room){ return f_room_goto(c,a,n); }
FN(action_move){ Inst*s=c->self; const char*d=ARG(0).s?ARG(0).s:""; double sp=NARG(1); static const int dirs[9]={225,270,315,180,-1,0,135,90,45};
  int cand[9],nc=0; for(int k=0;k<9&&d[k];k++) if(d[k]=='1') cand[nc++]=k;
  if(nc){ int k=cand[nc>1?rand()%nc:0]; if(c->relative){
      double nh,nv; if(dirs[k]<0){ nh=0; nv=0; } else { { double c,sn; lendir(dirs[k],&c,&sn); nh=sp*c; nv=-sp*sn; } } s->hs+=nh; s->vs+=nv; sync_hv(s); }
    else { if(dirs[k]<0){ s->spd=0; } else { s->dir=dirs[k]; s->spd=sp; } sync_sd(s); } }
  return vnum(0); }
FN(action_timeline_set){ Inst*s=c->self; s->tl=(int)NARG(0); s->tl_pos=NARG(1); s->tl_run=!truth(ARG(2)); return vnum(0); }  /* 0 = start at once (the game passes 0 and relies on it) */
FN(action_if_variable){ double x=NARG(0),y=NARG(1); int op=(int)NARG(2); return vnum(op==0?fabs(x-y)<1e-9:(op==1?x<y:x>y)); }
FN(action_color){ E.dcolor=(uint32_t)(long long)NARG(0); return vnum(0); }
FN(action_font){ E.dfont=(int)NARG(0); E.dhalign=(int)NARG(1); return vnum(0); }
FN(draw_set_color){ E.dcolor=(uint32_t)(long long)NARG(0); return vnum(0); }
FN(draw_set_halign){ E.dhalign=(int)NARG(0); return vnum(0); }
FN(draw_set_font){ E.dfont=(int)NARG(0); return vnum(0); }
FN(display_set_gui_size){ E.guiw=(int)NARG(0); E.guih=(int)NARG(1); return vnum(0); }
FN(noop){ return vnum(0); }
FN(string){ if(ARG(0).s) return ARG(0); return vstr(num_to_str(NARG(0))); }
FN(choose){ if(n<=0) return vnum(0); return a[rand()%n]; }
FN(make_colour_rgb){ return vnum((int)NARG(0)|((int)NARG(1)<<8)|((int)NARG(2)<<16)); }
FN(environment_get_variable){ const char*k=ARG(0).s?ARG(0).s:""; const char*v;
  if(!strcmp(k,"USERNAME")||!strcmp(k,"USER")||!strcmp(k,"LOGNAME")) v=g_player_name; else v=getenv(k);   /* the game greets the player by name */
  return vstr(str_intern(v?v:"")); }

/* ---- drawing ---- */
static inline void col(uint32_t bgr,uint8_t*r,uint8_t*g,uint8_t*b){ *r=bgr&255; *g=(bgr>>8)&255; *b=(bgr>>16)&255; }
static void submit(TPag*t,double dx,double dy,double dw,double dh,double px,double py,double ang,uint32_t blend,double alpha){
  if(alpha<=0) return; tex_use(t->page); double sc=E.tsc;
  dx=floor(dx+0.5); dy=floor(dy+0.5); px=floor(px+0.5); py=floor(py+0.5);
  uint8_t r,g,b; col(blend,&r,&g,&b);
  PL->draw(t->page,t->sx,t->sy,t->sw,t->sh,(float)((dx-E.tvx)*sc+E.tpx),(float)((dy-E.tvy)*sc+E.tpy),(float)(dw*sc),(float)(dh*sc),
           (float)((px-E.tvx)*sc+E.tpx),(float)((py-E.tvy)*sc+E.tpy),(float)ang,r,g,b,(float)alpha); }
static void draw_spr(int si,double frame,double x,double y,double xs,double ys,double ang,uint32_t blend,double alpha){
  if(si<0||si>=D.nspr) return; Sprite*s=&D.spr[si]; if(s->nframes<=0) return;
  int f=((int)floor(frame))%s->nframes; if(f<0) f+=s->nframes; TPag*t=s->fr[f];
  double bx=x-s->ox*xs,by=y-s->oy*ys;
  submit(t,bx+t->tx*xs,by+t->ty*ys,t->sw*xs,t->sh*ys,x,y,ang,blend,alpha); }
FN(draw_sprite){ Inst*s=c->self; double fr=NARG(1); if(fr<0) fr=s->ii; draw_spr((int)NARG(0),fr,NARG(2),NARG(3),1,1,0,0xFFFFFF,1); return vnum(0); }
FN(draw_sprite_ext){ Inst*s=c->self; double fr=NARG(1); if(fr<0) fr=s->ii;
  draw_spr((int)NARG(0),fr,NARG(2),NARG(3),NARG(4),NARG(5),NARG(6),(uint32_t)(long long)NARG(7),NARG(8)); return vnum(0); }
/* ---- text: GameMaker draws one line per '#'/newline; optional word wrapping keeps long strings (e.g. player names) on screen ---- */
int g_wrap_text=1;
static double glyph_adv(Font*f,unsigned char ch){ int gi=f->map[ch]; return gi>=0?f->g[gi].shift:0; }
static void draw_line(Font*f,const char*a,const char*b,double x0,double y){
  double wd=0; for(const char*q=a;q<b;q++) wd+=glyph_adv(f,(unsigned char)*q);
  double x=x0-(E.dhalign==1?wd/2:(E.dhalign==2?wd:0));
  for(const char*q=a;q<b;q++){ int gi=f->map[(unsigned char)*q]; if(gi<0) continue; Glyph*G=&f->g[gi];
    if(G->w&&G->h){ TPag t=*f->tp; t.sx=f->tp->sx+G->x; t.sy=f->tp->sy+G->y; t.sw=G->w; t.sh=G->h; submit(&t,x+G->offset,y,G->w,G->h,x,y,0,E.dcolor,1); }
    x+=G->shift; } }
FN(draw_text){
  if(E.dfont<0||E.dfont>=D.nfont) return vnum(0); Font*f=&D.font[E.dfont]; const char*s=ARG(2).s?ARG(2).s:num_to_str(NARG(2)); double x0=NARG(0),y=NARG(1);
  double maxw=g_wrap_text?(LOGICAL_W-40)/E.tsc:1e12, lh=f->size*1.25; const char*p=s;
  while(*p){
    const char*e=p; while(*e&&*e!='\n'&&!(*e=='#'&&(e==s||e[-1]!='\\'))) e++;       /* explicit line [p,e) */
    const char*a=p;
    for(;;){                                                                          /* wrap [a,e) into visual lines */
      double w=0; const char*q=a,*lastsp=NULL;
      while(q<e){ double adv=glyph_adv(f,(unsigned char)*q); if(w+adv>maxw&&q>a) break; w+=adv; if(*q==' ') lastsp=q; q++; }
      const char*end,*next;
      if(q>=e){ end=e; next=e; } else if(lastsp&&lastsp>a){ end=lastsp; next=lastsp+1; } else { end=q; next=q; }   /* break at a space, else mid-word */
      draw_line(f,a,end,x0,y); y+=lh; a=next; if(a>=e) break; }
    p=*e?e+1:e; }
  return vnum(0); }

static void register_fns(void){
  static const FnEnt t[]={
   {"show_debug_message",f_show_debug_message},{"room_goto",f_room_goto},{"room_goto_next",f_room_goto_next},{"instance_create",f_instance_create},
   {"instance_destroy",f_instance_destroy},{"instance_exists",f_instance_exists},{"place_free",f_place_free},{"keyboard_check",f_keyboard_check},
   {"keyboard_check_pressed",f_keyboard_check_pressed},{"keyboard_check_released",f_keyboard_check_released},{"audio_play_sound",f_audio_play_sound},
   {"audio_play_music",f_audio_play_music},{"audio_stop_music",f_audio_stop_music},{"audio_stop_sound",f_audio_stop_sound},{"action_sound",f_action_sound},
   {"action_end_sound",f_action_end_sound},{"action_set_relative",f_action_set_relative},{"action_sprite_set",f_action_sprite_set},{"action_set_alarm",f_action_set_alarm},
   {"action_effect",f_action_effect},{"action_kill_object",f_action_kill_object},{"action_create_object",f_action_create_object},{"action_next_room",f_action_next_room},
   {"action_another_room",f_action_another_room},{"action_move",f_action_move},{"action_timeline_set",f_action_timeline_set},{"action_if_variable",f_action_if_variable},
   {"action_color",f_action_color},{"action_font",f_action_font},{"draw_set_color",f_draw_set_color},{"draw_set_halign",f_draw_set_halign},{"draw_set_font",f_draw_set_font},
   {"display_set_gui_size",f_display_set_gui_size},{"YoYo_AddVirtualKey",f_noop},{"YoYo_DeleteVirtualKey",f_noop},{"string",f_string},{"choose",f_choose},
   {"make_colour_rgb",f_make_colour_rgb},{"make_color_rgb",f_make_colour_rgb},{"environment_get_variable",f_environment_get_variable},{"draw_sprite",f_draw_sprite},
   {"draw_sprite_ext",f_draw_sprite_ext},{"draw_text",f_draw_text},{0,0}};
  fnmap=calloc(D.nfunc,sizeof(Fn)); fnscript=malloc(D.nfunc*sizeof(int)); fnwarn=calloc(D.nfunc,1);
  for(int i=0;i<D.nfunc;i++){ fnscript[i]=-1; const char*nm=D.func[i].name;
    for(int k=0;t[k].n;k++) if(!strcmp(t[k].n,nm)){ fnmap[i]=t[k].f; break; }
    if(!fnmap[i]){ for(int s=0;s<D.nscript;s++) if(!strcmp(D.scriptname[s],nm)){ fnscript[i]=D.scriptcode[s]; break; }
      if(fnscript[i]<0){ char b[256]; snprintf(b,sizeof b,"gml_Script_%s",nm); int ci=codeidx_by_name(b); if(ci>=0) fnscript[i]=ci; } } }
}

/* ================= rooms / frame ================= */
static void clear_instances(void){ for(int k=0;k<nI;k++){ free(I[k]->uv); free(I[k]); } nI=0; }
static void enter_room(int r){
  if(E.room>=0&&E.room<D.nroom){ for(int k=0;k<nI;k++) if(!I[k]->dead) run_event(I[k],7,5); }
  audio_hold=1; clear_instances(); E.room=r; E.next_room=-1; Room*rm=&D.room[r]; E.relative=0;
  for(int k=0;k<MAX_BG;k++){ RoomBg*b=&rm->bg[k]; E.bgvis[k]=b->enabled; E.bgidx[k]=b->bg; E.bgfg[k]=b->fg; E.bgx[k]=b->x; E.bgy[k]=b->y; E.bghs[k]=b->hspd; E.bgvs[k]=b->vspd;
    E.bgxs[k]=E.bgys[k]=1; E.bgblend[k]=0xFFFFFF; E.bgalpha[k]=1; }
  E.bgcolor=rm->bgcol;
  for(int k=0;k<MAX_VIEWS;k++){ RoomView*v=&rm->view[k]; E.vx[k]=v->vx; E.vy[k]=v->vy; E.vw[k]=v->vw; E.vh[k]=v->vh; E.vobj[k]=v->obj; }
  E.guiw=LOGICAL_W; E.guih=LOGICAL_H;
  int first=nI; int *cc=calloc(rm->ninst+1,sizeof(int)); int ncc=0;
  for(int k=0;k<rm->ninst;k++){ RoomInst*ri=&rm->inst[k]; if(ri->obj<0||ri->obj>=D.nobj) continue; if(g_hide_touch&&ri->obj==E.touch_obj) continue;
    Inst*i=inst_alloc(ri->obj,ri->x,ri->y,ri->id); i->xs=ri->sx; i->ys=ri->sy; i->blend=ri->color&0xFFFFFF; i->alpha=((ri->color>>24)&255)/255.0; if(i->alpha==0) i->alpha=1; i->ang=ri->rot; if(ri->id>(uint32_t)nextid) nextid=ri->id; cc[ncc++]=ri->cc; }
  int last=nI;
  for(int k=first;k<last;k++){ Inst*i=I[k]; run_code(cc[k-first],i,NULL); run_event(i,0,0); }
  free(cc);
  if(rm->cc>=0) run_code(rm->cc,&dummy,NULL);
  { uint8_t keep[64],k2[64]; room_pages(r,keep); room_pages(r+1,k2); for(int p=0;p<64;p++) keep[p]|=k2[p]; tex_discard_unused(keep); prefetch_room(r+1); }
  if(g_verbose) fprintf(stderr,"[room] entered %s (%d), %d instances\n",rm->name,r,engine_ninst());
}
void engine_goto_room(int r){ if(r>=0&&r<D.nroom) enter_room(r); }

static void update_views(int snap){
  Room*rm=&D.room[E.room];
  for(int v=0;v<MAX_VIEWS;v++){ if(!rm->view[v].enabled||E.vobj[v]<0) continue;
    Inst*t=E.vobj[v]>=100000?inst_by_id(E.vobj[v]):inst_first_obj(E.vobj[v]); if(!t) continue;
    double l=t->x,r=t->x,tp=t->y,b=t->y;   /* follow the origin (stable); mask edges change every animation frame */
    double hb=rm->view[v].bh,vb=rm->view[v].bv,nx=E.vx[v],ny=E.vy[v];
    if(hb>E.vw[v]/2) hb=E.vw[v]/2; if(vb>E.vh[v]/2) vb=E.vh[v]/2;   /* overlapping borders made the camera oscillate */
    if(l-hb<nx) nx=l-hb; else if(r+hb>nx+E.vw[v]) nx=r+hb-E.vw[v];
    if(tp-vb<ny) ny=tp-vb; else if(b+vb>ny+E.vh[v]) ny=b+vb-E.vh[v];
    if(nx>rm->w-E.vw[v]) nx=rm->w-E.vw[v]; if(nx<0) nx=0; if(ny>rm->h-E.vh[v]) ny=rm->h-E.vh[v]; if(ny<0) ny=0;
    double hs=rm->view[v].hs,vs=rm->view[v].vs;
    if(hs>0&&fabs(t->hs)+1>hs) hs=fabs(t->hs)+1;     /* never lag further behind than the object's own speed */
    if(vs>0&&fabs(t->vs)+1>vs) vs=fabs(t->vs)+1;
    if(!snap&&hs>=0&&fabs(nx-E.vx[v])>hs) nx=E.vx[v]+(nx>E.vx[v]?hs:-hs);
    if(!snap&&vs>=0&&fabs(ny-E.vy[v])>vs) ny=E.vy[v]+(ny>E.vy[v]?vs:-vs);
    /* safeguard: the speed limit may slow the camera but must never let the followed object leave the screen */
    /* an axis whose view speed is 0 is LOCKED (e.g. the Hide stage never scrolls vertically, even when a jump takes the
       player above the top of the room), so only apply the safeguard on axes that are allowed to move */
    if(rm->view[v].hs!=0){ if(t->x<nx) nx=t->x; if(t->x>nx+E.vw[v]) nx=t->x-E.vw[v]; }
    if(rm->view[v].vs!=0){ if(t->y<ny) ny=t->y; if(t->y>ny+E.vh[v]) ny=t->y-E.vh[v]; }
    /* the view never leaves the room (a room smaller than the view pins it to 0) */
    if(nx>rm->w-E.vw[v]) nx=rm->w-E.vw[v]; if(nx<0) nx=0; if(ny>rm->h-E.vh[v]) ny=rm->h-E.vh[v]; if(ny<0) ny=0;
    E.vx[v]=nx; E.vy[v]=ny; } }

static void step(void){
  int n;
  n=nI; for(int k=0;k<n;k++) if(!I[k]->dead) run_event(I[k],3,1);                        /* begin step */
  n=nI; for(int k=0;k<n;k++){ Inst*i=I[k]; for(int al=0;al<12&&!i->dead;al++) if(i->alarm[al]>0&&--i->alarm[al]==0){ i->alarm[al]=-1; run_event(i,2,al); } }
  n=nI; for(int k=0;k<n;k++){ Inst*i=I[k]; Object*o=&D.obj[i->obj]; if(i->dead) continue;
    for(int e=0;e<o->nev[5]&&!i->dead;e++){ int sub=o->ev[5][e].sub; if(E.key[sub&255]) run_event(i,5,sub); }
    for(int e=0;e<o->nev[9]&&!i->dead;e++){ int sub=o->ev[9][e].sub; if(E.kp[sub&255]) run_event(i,9,sub); }
    for(int e=0;e<o->nev[10]&&!i->dead;e++){ int sub=o->ev[10][e].sub; if(E.kr[sub&255]) run_event(i,10,sub); } }
  n=nI; for(int k=0;k<n;k++){ Inst*i=I[k]; if(i->dead) continue;                            /* timelines */
    if(i->tl>=0&&i->tl<D.ntl&&i->tl_run){ Timeline*t=&D.tl[i->tl]; for(int m=0;m<t->nmom;m++) if(t->step[m]==(int)i->tl_pos) run_code(t->code[m],i,NULL); i->tl_pos+=1; } }
  n=nI; for(int k=0;k<n;k++) if(!I[k]->dead) run_event(I[k],3,0);                        /* step */
  n=nI; for(int k=0;k<n;k++){ Inst*i=I[k]; if(i->dead) continue;                           /* motion + animation */
    i->xp=i->x; i->yp=i->y;
    if(i->fric!=0&&i->spd!=0){ double s=i->spd-i->fric; if(s<0)s=0; i->spd=s; sync_sd(i); }
    if(i->grav!=0){ double c,sn; lendir(i->gdir,&c,&sn); i->hs+=i->grav*c; i->vs-=i->grav*sn; sync_hv(i); }
    i->x+=i->hs; i->y+=i->vs;
    Sprite*s=(i->sprite>=0&&i->sprite<D.nspr)?&D.spr[i->sprite]:NULL;
    if(s&&s->nframes>0&&i->isp!=0){ i->ii+=i->isp; int wrapped=0; if(i->ii>=s->nframes){ i->ii=fmod(i->ii,s->nframes); wrapped=1; } else if(i->ii<0){ i->ii=fmod(i->ii,s->nframes)+s->nframes; wrapped=1; }
      if(wrapped) run_event(i,7,7); } }
  n=nI; for(int k=0;k<n;k++){ Inst*a=I[k]; if(a->dead) continue; Object*o=&D.obj[a->obj];                 /* collisions */
    for(int e=0;e<o->nev[4]&&!a->dead;e++){ int ob=o->ev[4][e].sub;
      for(int m=0;m<nI&&!a->dead;m++){ Inst*b=I[m]; if(b->dead||b==a||b->obj!=ob) continue;
        if(collide_at(a,a->x,a->y,b)){ if(is_solid(b)){ a->x=a->xp; a->y=a->yp; }
          Inst*sv=NULL; (void)sv; for(int q=0;q<o->ev[4][e].n;q++){ if(a->dead) break; run_code(o->ev[4][e].codes[q],a,b); } } } } }
  n=nI; for(int k=0;k<n;k++) if(!I[k]->dead) run_event(I[k],3,2);                        /* end step */
  for(int b=0;b<MAX_BG;b++){ E.bgx[b]+=E.bghs[b]; E.bgy[b]+=E.bgvs[b]; }
  update_views(0);
  int w=0; for(int k=0;k<nI;k++){ if(I[k]->dead){ free(I[k]->uv); free(I[k]); } else I[w++]=I[k]; } nI=w;
}

typedef struct { int depth,order,kind; void*p; } Item;
static int cmp_item(const void*a,const void*b){ const Item*x=a,*y=b; if(x->depth!=y->depth) return y->depth-x->depth; return x->order-y->order; }

static void draw_bg(int k,double vx,double vy,double vw,double vh){
  int bi=E.bgidx[k]; if(!E.bgvis[k]||bi<0||bi>=D.nbg) return; TPag*t=D.bg[bi].tp; RoomBg*rb=&D.room[E.room].bg[k];
  double xs=E.bgxs[k],ys=E.bgys[k],pw=t->bw*xs,ph=t->bh*ys; if(pw<=0||ph<=0) return; uint32_t bl=E.bgblend[k];
  double x0=E.bgx[k],y0=E.bgy[k]; int tx=rb->tilex,ty=rb->tiley;
  if(tx){ x0=fmod(x0-vx,pw); if(x0>0)x0-=pw; x0+=vx; } if(ty){ y0=fmod(y0-vy,ph); if(y0>0)y0-=ph; y0+=vy; }
  for(double y=y0; ; y+=ph){ for(double x=x0; ; x+=pw){
      submit(t,x+t->tx*xs,y+t->ty*ys,t->sw*xs,t->sh*ys,x,y,0,bl,E.bgalpha[k]); if(!tx||x+pw>=vx+vw) break; }
    if(!ty||y+ph>=vy+vh) break; } }
static void draw_tile(Tile*tl){
  if(tl->bg<0||tl->bg>=D.nbg) return; TPag*t=D.bg[tl->bg].tp;
  int u0=tl->sx>t->tx?tl->sx:t->tx, u1=(tl->sx+tl->w)<(t->tx+t->sw)?(tl->sx+tl->w):(t->tx+t->sw);
  int v0=tl->sy>t->ty?tl->sy:t->ty, v1=(tl->sy+tl->h)<(t->ty+t->sh)?(tl->sy+tl->h):(t->ty+t->sh); if(u1<=u0||v1<=v0) return;
  TPag q=*t; q.sx=t->sx+(u0-t->tx); q.sy=t->sy+(v0-t->ty); q.sw=u1-u0; q.sh=v1-v0;
  submit(&q,tl->x+(u0-tl->sx)*tl->xs,tl->y+(v0-tl->sy)*tl->ys,q.sw*tl->xs,q.sh*tl->ys,tl->x,tl->y,0,tl->color&0xFFFFFF,((tl->color>>24)&255)/255.0); }

static void draw_world(double vx,double vy,double vw,double vh,double px,double py,double pw,double ph){
  Room*rm=&D.room[E.room]; E.tvx=floor(vx+0.5); E.tvy=floor(vy+0.5); E.tsc=pw/vw; E.tpx=px; E.tpy=py;
  PL->clip((int)px,(int)py,(int)pw,(int)ph);
  if(rm->drawbg){ uint8_t r,g,b; col(E.bgcolor,&r,&g,&b); PL->rect((float)px,(float)py,(float)pw,(float)ph,r,g,b,1); }
  for(int k=0;k<MAX_BG;k++) if(!E.bgfg[k]) draw_bg(k,vx,vy,vw,vh);
  int cap=nI+rm->ntile; Item*it=malloc((cap?cap:1)*sizeof(Item)); int ni=0;
  for(int k=0;k<nI;k++) if(!I[k]->dead&&(I[k]->visible||D.obj[I[k]->obj].nev[8])){ it[ni++]=(Item){I[k]->depth,k,0,I[k]}; }
  for(int k=0;k<rm->ntile;k++) it[ni++]=(Item){rm->tile[k].depth,nI+k,1,&rm->tile[k]};
  qsort(it,ni,sizeof(Item),cmp_item);
  for(int k=0;k<ni;k++){
    if(it[k].kind){ draw_tile(it[k].p); continue; }
    Inst*i=it[k].p; if(i->dead) continue; Object*o=&D.obj[i->obj]; int has=0;
    for(int e=0;e<o->nev[8];e++) if(o->ev[8][e].sub==0) has=1;
    if(has){ if(i->visible) run_event(i,8,0); }
    else draw_spr(i->sprite,i->ii,i->x,i->y,i->xs,i->ys,i->ang,i->blend,i->alpha); }
  free(it);
  for(int k=0;k<MAX_BG;k++) if(E.bgfg[k]) draw_bg(k,vx,vy,vw,vh);
  PL->clip(0,0,0,0); }

static void draw(void){
  Room*rm=&D.room[E.room]; PL->clear(0,0,0); int any=0;
  if(rm->flags&1){ for(int v=0;v<MAX_VIEWS;v++){ if(!rm->view[v].enabled) continue; any=1; RoomView*rv=&rm->view[v];
      draw_world(E.vx[v],E.vy[v],E.vw[v],E.vh[v],rv->px,rv->py,rv->pw,rv->ph); } }
  if(!any) draw_world(0,0,LOGICAL_W,LOGICAL_H,0,0,LOGICAL_W,LOGICAL_H);
  /* Draw GUI */
  E.tvx=E.tvy=0; E.tsc=(double)LOGICAL_W/E.guiw; E.tpx=E.tpy=0;
  for(int k=0;k<nI;k++){ Inst*i=I[k]; if(!i->dead&&i->visible) run_event(i,8,64); } }

int engine_init(int os_type){
  memset(&E,0,sizeof E); E.room=-1; E.next_room=-1; E.os_type=os_type; E.dfont=-1; E.dcolor=0xFFFFFF; E.touch_obj=-1;
  nvars=D.nvar; if(nvars>MAX_VARS){ fprintf(stderr,"too many variables (%d)\n",nvars); return -1; }
  vbi=calloc(D.nvar,sizeof(int));
  for(int i=0;i<D.nvar;i++){ const char*nm=D.var[i].name; for(int k=0;bvt[k].n;k++) if(!strcmp(bvt[k].n,nm)) vbi[i]=bvt[k].b;
    if(!strncmp(nm,"argument",8)&&nm[8]>='0'&&nm[8]<='9'&&strlen(nm)<=10) vbi[i]=BV_ARG0+atoi(nm+8); }
  for(int o=0;o<D.nobj;o++) if(!strcmp(D.obj[o].name,"obj_touchControl")) E.touch_obj=o;
  dummy.xs=dummy.ys=1; dummy.alpha=1; dummy.uv=calloc(nvars,sizeof(Value)); dummy.id=-1; dummy.tl=-1; dummy.sprite=-1; dummy.mask=-1; for(int k=0;k<12;k++) dummy.alarm[k]=-1;
  register_fns(); srand((unsigned)time(NULL));
  for(int i=0;i<D.nfunc;i++) if(!fnmap[i]&&fnscript[i]<0) fprintf(stderr,"[init] note: function '%s' not implemented\n",D.func[i].name);
  enter_room(0); return 0; }

void engine_frame(const uint8_t keys[256]){
  for(int k=0;k<256;k++){ E.kp[k]=keys[k]&&!E.prevkey[k]; E.kr[k]=!keys[k]&&E.prevkey[k]; E.key[k]=keys[k]; E.prevkey[k]=keys[k]; }
  step();
  if(E.next_room>=0&&E.next_room<D.nroom){ enter_room(E.next_room); update_views(1); }
  draw(); E.frame++; }

/* ---- debug/test hooks ---- */
int engine_set_global(const char*name,double v){ for(int i=0;i<D.nvar;i++) if(!strcmp(D.var[i].name,name)){ gv[i]=vnum(v); return 0; } return -1; }
static int obj_by_name(const char*n){ for(int o=0;o<D.nobj;o++) if(!strcmp(D.obj[o].name,n)) return o; return -1; }
int engine_obj_info(const char*name,double*x,double*y,double*hs){ int o=obj_by_name(name); if(o<0) return 0; Inst*i=inst_first_obj(o); if(!i) return 0; *x=i->x; *y=i->y; *hs=i->hs; return 1; }
void engine_set_pos(const char*name,double x,double y){ int o=obj_by_name(name); if(o<0) return; Inst*i=inst_first_obj(o); if(i){ i->x=x; i->y=y; } }
void engine_view_pos(double*x,double*y){ *x=E.vx[0]; *y=E.vy[0]; }
int engine_obj_count(const char*name){ int o=obj_by_name(name),n=0; if(o<0) return 0; for(int k=0;k<nI;k++) if(I[k]->obj==o&&!I[k]->dead) n++; return n; }
int engine_set_global_str(const char*name,const char*v){ for(int i=0;i<D.nvar;i++) if(!strcmp(D.var[i].name,name)){ gv[i]=vstr(str_intern(v)); return 0; } return -1; }
void engine_dump_instances(void){ for(int k=0;k<nI;k++){ Inst*i=I[k]; if(i->dead) continue; Object*o=&D.obj[i->obj]; 
  fprintf(stderr,"  inst %-26s x=%7.1f y=%6.1f spr=%-26s idx=%.1f vis=%d depth=%d alpha=%.2f blend=%06x xs=%.1f ys=%.1f\n",o->name,i->x,i->y,(i->sprite>=0&&i->sprite<D.nspr)?D.spr[i->sprite].name:"-",i->ii,i->visible,i->depth,i->alpha,i->blend,i->xs,i->ys); } }
double engine_obj_var(const char*obj,const char*var,int*isstr){ int o=obj_by_name(obj); Inst*i=o>=0?inst_first_obj(o):NULL; if(!i) return -9999;
  for(int k=0;k<D.nvar;k++) if(!strcmp(D.var[k].name,var)){ if(vbi[k]) return -9998; if(isstr) *isstr=i->uv[k].s!=NULL; return i->uv[k].s?0:i->uv[k].d; } return -9997; }
