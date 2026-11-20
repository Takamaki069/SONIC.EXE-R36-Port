/* Headless test harness: software renderer -> PPM frames. Usage:
 *   test_runner data.win [-f frames] [-r room] [-k "frame:keycode:1|0,..."] [-d "f1,f2,.."] [-o outprefix] [-v] */
#include "gm.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint8_t *pg[64]; static int pw[64],ph[64];
static uint8_t fb[LOGICAL_W*LOGICAL_H*3]; static int clip_x,clip_y,clip_w,clip_h; static long ndraw,nrect;
static void t_upload(int p,int w,int h,const uint8_t*px){ free(pg[p]); pg[p]=malloc((size_t)w*h*4); memcpy(pg[p],px,(size_t)w*h*4); pw[p]=w; ph[p]=h; }
static void t_free(int p){ free(pg[p]); pg[p]=0; }
static void t_clear(uint8_t r,uint8_t g,uint8_t b){ for(int i=0;i<LOGICAL_W*LOGICAL_H;i++){ fb[i*3]=r; fb[i*3+1]=g; fb[i*3+2]=b; } }
static void t_clip(int x,int y,int w,int h){ if(w<=0){ clip_x=0;clip_y=0;clip_w=LOGICAL_W;clip_h=LOGICAL_H; } else { clip_x=x;clip_y=y;clip_w=w;clip_h=h; } }
static inline void put(int x,int y,int r,int g,int b,float a){
  if(x<clip_x||y<clip_y||x>=clip_x+clip_w||y>=clip_y+clip_h||x<0||y<0||x>=LOGICAL_W||y>=LOGICAL_H) return; uint8_t*d=fb+(y*LOGICAL_W+x)*3;
  d[0]=d[0]+(r-d[0])*a; d[1]=d[1]+(g-d[1])*a; d[2]=d[2]+(b-d[2])*a; }
static void t_draw(int p,int sx,int sy,int sw,int sh,float dx,float dy,float dw,float dh,float px,float py,float ang,uint8_t r,uint8_t g,uint8_t b,float a){
  ndraw++; if(!pg[p]||sw<=0||sh<=0||dw==0||dh==0) return; (void)px;(void)py;(void)ang;
  float adw=dw<0?-dw:dw,adh=dh<0?-dh:dh; float x0=dw<0?dx+dw:dx,y0=dh<0?dy+dh:dy;
  int ya=(int)floorf(y0+0.5f),yb=(int)floorf(y0+adh+0.5f),xa=(int)floorf(x0+0.5f),xb=(int)floorf(x0+adw+0.5f);
  if(ya<clip_y)ya=clip_y; if(yb>clip_y+clip_h)yb=clip_y+clip_h; if(xa<clip_x)xa=clip_x; if(xb>clip_x+clip_w)xb=clip_x+clip_w;
  for(int y=ya;y<yb;y++){ for(int x=xa;x<xb;x++){
      float u=(x+0.5f-x0)/adw, v=(y+0.5f-y0)/adh; if(dw<0)u=1-u; if(dh<0)v=1-v; int tx=sx+(int)(u*sw),ty=sy+(int)(v*sh); if(tx>=sx+sw)tx=sx+sw-1; if(ty>=sy+sh)ty=sy+sh-1;
      const uint8_t*s=pg[p]+((size_t)ty*pw[p]+tx)*4; if(!s[3]) continue; put(x,y,s[0]*r/255,s[1]*g/255,s[2]*b/255,a*s[3]/255.0f); } } }
static void t_rect(float x,float y,float w,float h,uint8_t r,uint8_t g,uint8_t b,float a){ nrect++; for(int j=(int)y;j<(int)(y+h);j++) for(int i=(int)x;i<(int)(x+w);i++) put(i,j,r,g,b,a); }
static void t_sfx(int s,int l){ if(g_verbose) fprintf(stderr,"[snd] sfx %s loop=%d\n",D.snd[s].name,l); }
static void t_sfxs(int s){ (void)s; }
static void t_mus(int s,int l){ fprintf(stderr,"[snd] music %s loop=%d\n",D.snd[s].name,l); }
static void t_muss(void){ if(g_verbose) fprintf(stderr,"[snd] music stop\n"); }
static Platform tp={t_upload,t_free,t_clear,t_clip,t_draw,t_rect,t_sfx,t_sfxs,t_mus,t_muss};

static void dump(const char*prefix,int f){ char n[256]; snprintf(n,sizeof n,"%s_%04d.ppm",prefix,f); FILE*o=fopen(n,"wb"); fprintf(o,"P6\n%d %d\n255\n",LOGICAL_W,LOGICAL_H); fwrite(fb,1,sizeof fb,o); fclose(o); }

int main(int argc,char**argv){
  if(argc<2){ fprintf(stderr,"usage: %s data.win [-g var=val] [-p x:y] [-t N] [-f N] [-r room] [-k f:key:1|0,...] [-d f,f,..] [-o prefix] [-v]\n",argv[0]); return 1; }
  int frames=120,room=-1; const char*gset[16]; int ngset=0; const char*ppos=0; int trace=0; int dumpinst=-1; const char*keys="",*dumps="",*prefix="/tmp/frame";
  for(int i=2;i<argc;i++){ if(!strcmp(argv[i],"-f")) frames=atoi(argv[++i]); else if(!strcmp(argv[i],"-r")) room=atoi(argv[++i]); else if(!strcmp(argv[i],"-k")) keys=argv[++i];
    else if(!strcmp(argv[i],"-d")) dumps=argv[++i]; else if(!strcmp(argv[i],"-o")) prefix=argv[++i]; else if(!strcmp(argv[i],"-v")) g_verbose=1;
    else if(!strcmp(argv[i],"-g")) gset[ngset++]=argv[++i]; else if(!strcmp(argv[i],"-p")) ppos=argv[++i]; else if(!strcmp(argv[i],"-t")) trace=atoi(argv[++i]); else if(!strcmp(argv[i],"-i")) dumpinst=atoi(argv[++i]); }
  if(data_load(argv[1])) return 1; PL=&tp; t_clip(0,0,0,0);
  g_tex_budget_mb=100000;
  if(engine_init(0)) return 1;
  for(int g=0;g<ngset;g++){ char n[64]; double v; if(sscanf(gset[g],"%63[^=]=%lf",n,&v)==2) engine_set_global(n,v); else { char sv[64]; if(sscanf(gset[g],"%63[^=]=%63s",n,sv)==2) engine_set_global_str(n,sv); } }
  if(room>=0) engine_goto_room(room);
  double px_set=-1,py_set=-1; if(ppos) sscanf(ppos,"%lf:%lf",&px_set,&py_set);
  uint8_t cur[256]={0}; char kb[4096]; strncpy(kb,keys,sizeof kb-1); kb[sizeof kb-1]=0; char db[1024]; strncpy(db,dumps,sizeof db-1); db[sizeof db-1]=0;
  int lastroom=engine_room();
  for(int f=0;f<frames;f++){
    char tmp[4096]; strcpy(tmp,kb); for(char*t=strtok(tmp,",");t;t=strtok(0,",")){ int ff,k,d; if(sscanf(t,"%d:%d:%d",&ff,&k,&d)==3&&ff==f) cur[k&255]=d; }
    if(f==2&&px_set>=0) engine_set_pos("obj_playerMain",px_set,py_set);
    engine_frame(cur);
    if(f==dumpinst){ fprintf(stderr,"[instances @ frame %d]\n",f); engine_dump_instances(); }
    if(trace&&f%trace==0){ double x,y,hs,vx,vy; if(engine_obj_info("obj_playerMain",&x,&y,&hs)){ engine_view_pos(&vx,&vy); fprintf(stderr,"[trace] players=%d ",engine_obj_count("obj_playerMain")); fprintf(stderr,"[trace] f=%d player x=%.1f y=%.1f hs=%.2f view=%.1f,%.1f  player-view=%.1f\n",f,x,y,hs,vx,vy,x-vx); } }
    if(engine_room()!=lastroom){ fprintf(stderr,"[test] frame %d: room %d -> %d (%s), %d instances\n",f,lastroom,engine_room(),D.room[engine_room()].name,engine_ninst()); lastroom=engine_room(); }
    char tmp2[1024]; strcpy(tmp2,db); for(char*t=strtok(tmp2,",");t;t=strtok(0,",")) if(atoi(t)==f) dump(prefix,f);
  }
  fprintf(stderr,"[test] done: room %d (%s), %d instances, draws=%ld rects=%ld\n",engine_room(),D.room[engine_room()].name,engine_ninst(),ndraw,nrect);
  return 0; }
