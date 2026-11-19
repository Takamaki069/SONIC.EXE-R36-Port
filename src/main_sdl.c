/* SDL2 backend for sonicexe-runner (video, input, audio via SDL2_mixer) */
#include "gm.h"
#include <SDL2/SDL.h>
#ifndef NO_AUDIO
#include <SDL2/SDL_mixer.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>

static SDL_Window *win; static SDL_Renderer *ren; static SDL_Texture *tex[64]; static const char *gamedir=".";
enum { PA,PB,PX,PY,PL1,PR1,PSTART,PSELECT,PL2,PR2,PCOUNT };
static int padmap[PCOUNT]={32,88,90,67,65,83,13,27,81,69};      /* A=space B=X X=Z Y=C L1=A R1=S start=enter select=esc L2=Q R2=E */
static int joydebug=0, cfg_rawpad=0;
static int rawmap[PCOUNT]={0,1,2,3,4,5,7,6,8,9};      /* raw joystick button index for A,B,X,Y,L1,R1,START,SELECT,L2,R2 */
static int rawdpad[4]={-1,-1,-1,-1};                   /* raw button indices for dpad up,down,left,right (-1 = use hat/axes) */
static int inlog=0, joy_cfg_touched=0, cfg_deadzone=14000;
static int lastsign[16];

/* ---------------- video ---------------- */
static void s_upload(int p,int w,int h,const uint8_t *rgba){
  if(tex[p]) SDL_DestroyTexture(tex[p]);
  tex[p]=SDL_CreateTexture(ren,SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_STATIC,w,h);
  if(!tex[p]){ fprintf(stderr,"texture %d (%dx%d): %s\n",p,w,h,SDL_GetError()); return; }
  SDL_UpdateTexture(tex[p],NULL,rgba,w*4); SDL_SetTextureBlendMode(tex[p],SDL_BLENDMODE_BLEND); }
static void s_free(int p){ if(tex[p]){ SDL_DestroyTexture(tex[p]); tex[p]=NULL; } }
static void s_clear(uint8_t r,uint8_t g,uint8_t b){ SDL_SetRenderDrawColor(ren,r,g,b,255); SDL_RenderClear(ren); }
static void s_clip(int x,int y,int w,int h){ if(w<=0){ SDL_RenderSetClipRect(ren,NULL); return; } SDL_Rect r={x,y,w,h}; SDL_RenderSetClipRect(ren,&r); }
static void s_draw(int p,int sx,int sy,int sw,int sh,float dx,float dy,float dw,float dh,float px,float py,float ang,uint8_t r,uint8_t g,uint8_t b,float a){
  if(!tex[p]) return; int flip=SDL_FLIP_NONE; if(dw<0){ dx+=dw; dw=-dw; flip|=SDL_FLIP_HORIZONTAL; } if(dh<0){ dy+=dh; dh=-dh; flip|=SDL_FLIP_VERTICAL; }
  SDL_Rect s={sx,sy,sw,sh}; int x0=(int)floorf(dx+0.5f),y0=(int)floorf(dy+0.5f); SDL_Rect d={x0,y0,(int)floorf(dx+dw+0.5f)-x0,(int)floorf(dy+dh+0.5f)-y0};
  if(d.w<=0||d.h<=0) return;
  SDL_SetTextureColorMod(tex[p],r,g,b); SDL_SetTextureAlphaMod(tex[p],(uint8_t)(a<0?0:(a>1?255:a*255+0.5f)));
  if(ang!=0.0f||flip){ SDL_Point c={(int)(px-x0),(int)(py-y0)}; SDL_RenderCopyEx(ren,tex[p],&s,&d,-ang,&c,(SDL_RendererFlip)flip); }
  else SDL_RenderCopy(ren,tex[p],&s,&d); }
static void s_rect(float x,float y,float w,float h,uint8_t r,uint8_t g,uint8_t b,float a){
  SDL_SetRenderDrawBlendMode(ren,a<1?SDL_BLENDMODE_BLEND:SDL_BLENDMODE_NONE); SDL_SetRenderDrawColor(ren,r,g,b,(uint8_t)(a*255)); SDL_Rect q={(int)x,(int)y,(int)w,(int)h}; SDL_RenderFillRect(ren,&q); }

/* ---------------- audio ---------------- */
#ifndef NO_AUDIO
static Mix_Chunk *chunk[1024]; static int chan[1024]; static Mix_Music *music; static int music_snd=-1; static Mix_Chunk *jingle[1024];
static Mix_Chunk *load_embedded(int s){ if(s<0||s>=D.nsnd||s>=1024) return NULL; if(chunk[s]) return chunk[s]; int id=D.snd[s].audio_id; if(id<0||id>=D.naud) return NULL;
  SDL_RWops*rw=SDL_RWFromConstMem(D.aud[id],D.audlen[id]); chunk[s]=Mix_LoadWAV_RW(rw,1); if(!chunk[s]) fprintf(stderr,"audio %s: %s\n",D.snd[s].name,Mix_GetError());
  else Mix_VolumeChunk(chunk[s],(int)(D.snd[s].volume*MIX_MAX_VOLUME)); return chunk[s]; }
static void a_sfx(int s,int loop){ Mix_Chunk*c=load_embedded(s); if(!c) return; int ch=Mix_PlayChannel(-1,c,loop?-1:0); if(s<1024) chan[s]=ch; }
static void a_sfx_stop(int s){ if(s<0||s>=1024||!chunk[s]) return; for(int c=0;c<16;c++) if(Mix_GetChunk(c)==chunk[s]) Mix_HaltChannel(c); }
static void a_music(int s,int loop){
  char path[1024]; snprintf(path,sizeof path,"%s/%s",gamedir,D.snd[s].file);
  if(Mix_PlayingMusic()&&!loop&&s<1024){ /* short jingle over running music: play as chunk */
    if(!jingle[s]) jingle[s]=Mix_LoadWAV(path); if(jingle[s]){ Mix_PlayChannel(-1,jingle[s],0); return; } }
  if(music){ Mix_HaltMusic(); Mix_FreeMusic(music); music=NULL; }
  music=Mix_LoadMUS(path); if(!music){ fprintf(stderr,"music %s: %s\n",path,Mix_GetError()); return; }
  Mix_VolumeMusic((int)(D.snd[s].volume*MIX_MAX_VOLUME)); music_snd=s; Mix_PlayMusic(music,loop?-1:1); }
static void a_music_stop(void){ if(music){ Mix_HaltMusic(); Mix_FreeMusic(music); music=NULL; } }
#else
static void a_sfx(int s,int l){(void)s;(void)l;} static void a_sfx_stop(int s){(void)s;} static void a_music(int s,int l){(void)s;(void)l;} static void a_music_stop(void){}
#endif

static Platform sdlp={s_upload,s_free,s_clear,s_clip,s_draw,s_rect,a_sfx,a_sfx_stop,a_music,a_music_stop};

/* ---------------- input ---------------- */
static int sc_to_vk(int sc){
  if(sc>=SDL_SCANCODE_A&&sc<=SDL_SCANCODE_Z) return 'A'+(sc-SDL_SCANCODE_A);
  if(sc>=SDL_SCANCODE_1&&sc<=SDL_SCANCODE_9) return '1'+(sc-SDL_SCANCODE_1);
  switch(sc){ case SDL_SCANCODE_0: return '0'; case SDL_SCANCODE_LEFT: return 37; case SDL_SCANCODE_UP: return 38; case SDL_SCANCODE_RIGHT: return 39; case SDL_SCANCODE_DOWN: return 40;
    case SDL_SCANCODE_SPACE: return 32; case SDL_SCANCODE_RETURN: return 13; case SDL_SCANCODE_ESCAPE: return 27; case SDL_SCANCODE_BACKSPACE: return 8; case SDL_SCANCODE_TAB: return 9;
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return 16; case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 17; case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 18; }
  return 0; }
static SDL_GameController *gc[8]; static SDL_Joystick *js[8]; static int njoy;
/* Odroid-GO family ("GO-Advance Gamepad", "GO-Super Gamepad"): d-pad is buttons 10-13, triggers 6/7, select 8, start 9 */
static void apply_go_defaults(void){
  if(joy_cfg_touched) return;
  rawmap[PL2]=6; rawmap[PR2]=7; rawmap[PSELECT]=8; rawmap[PSTART]=9; rawdpad[0]=10; rawdpad[1]=11; rawdpad[2]=12; rawdpad[3]=13; }
static void open_pads(void){
  for(int i=0;i<SDL_NumJoysticks()&&njoy<8;i++){
    const char*pn=SDL_JoystickNameForIndex(i); int is_go=pn&&!strncmp(pn,"GO-",3);
    if(is_go) apply_go_defaults();
    if(!cfg_rawpad&&!is_go&&SDL_IsGameController(i)){ gc[njoy]=SDL_GameControllerOpen(i); if(gc[njoy]){ fprintf(stderr,"pad %d: controller '%s'\n",i,SDL_GameControllerName(gc[njoy]));
        char*m=SDL_GameControllerMapping(gc[njoy]); if(m){ fprintf(stderr,"pad %d mapping: %s\n",i,m); SDL_free(m); } njoy++; } }
    else { js[njoy]=SDL_JoystickOpen(i); if(js[njoy]){ fprintf(stderr,"pad %d: raw joystick '%s' (%d buttons)\n",i,SDL_JoystickName(js[njoy]),SDL_JoystickNumButtons(js[njoy])); njoy++; } } } }
#define B(x) SDL_GameControllerGetButton(c,x)
static int stick_state(int v,int *held){          /* hysteresis: engage beyond deadzone, release well inside it */
  int engage=cfg_deadzone, release=cfg_deadzone*6/10; int a=v<0?-v:v;
  if(!*held&&a>engage) *held=v<0?-1:1; else if(*held&&a<release) *held=0; else if(*held&&((v<0)!=(*held<0))&&a>engage) *held=v<0?-1:1;
  return *held; }
static int stickx[8],sticky[8];
static void poll_pads(uint8_t k[256],int *quit){
  static int comboframes=0; int start=0,sel=0,l2=0,r2=0;
  for(int i=0;i<njoy;i++){
    if(gc[i]){ SDL_GameController*c=gc[i];
      if(B(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) k[37]=1; if(B(SDL_CONTROLLER_BUTTON_DPAD_UP)) k[38]=1; if(B(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) k[39]=1; if(B(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) k[40]=1;
      int ax=SDL_GameControllerGetAxis(c,SDL_CONTROLLER_AXIS_LEFTX), ay=SDL_GameControllerGetAxis(c,SDL_CONTROLLER_AXIS_LEFTY);
      { int sx=stick_state(ax,&stickx[i&7]), sy=stick_state(ay,&sticky[i&7]); if(sx<0)k[37]=1; if(sx>0)k[39]=1; if(sy<0)k[38]=1; if(sy>0)k[40]=1; }
      if(B(SDL_CONTROLLER_BUTTON_A)) k[padmap[PA]]=1; if(B(SDL_CONTROLLER_BUTTON_B)) k[padmap[PB]]=1; if(B(SDL_CONTROLLER_BUTTON_X)) k[padmap[PX]]=1; if(B(SDL_CONTROLLER_BUTTON_Y)) k[padmap[PY]]=1;
      if(B(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) k[padmap[PL1]]=1; if(B(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) k[padmap[PR1]]=1;
      if(B(SDL_CONTROLLER_BUTTON_START)){ k[padmap[PSTART]]=1; start=1; } if(B(SDL_CONTROLLER_BUTTON_BACK)){ k[padmap[PSELECT]]=1; sel=1; }
      if(SDL_GameControllerGetAxis(c,SDL_CONTROLLER_AXIS_TRIGGERLEFT)>12000){ k[padmap[PL2]]=1; l2=1; } if(SDL_GameControllerGetAxis(c,SDL_CONTROLLER_AXIS_TRIGGERRIGHT)>12000){ k[padmap[PR2]]=1; r2=1; }
    } else if(js[i]){ SDL_Joystick*j=js[i]; int nb=SDL_JoystickNumButtons(j);
      /* generic handheld layout (check with JOYDEBUG=1 and adjust via sonicexe.cfg 'joy*' entries) */
      for(int d=0;d<4;d++){ int bi=rawdpad[d]; if(bi>=0&&bi<nb&&SDL_JoystickGetButton(j,bi)) k[(int[]){38,40,37,39}[d]]=1; }
      for(int b=0;b<PCOUNT;b++){ int bi=rawmap[b]; if(bi>=0&&bi<nb&&SDL_JoystickGetButton(j,bi)){ k[padmap[b]]=1; if(b==PSTART) start=1; if(b==PSELECT) sel=1; if(b==PL2) l2=1; if(b==PR2) r2=1; } }
      if(joydebug) for(int b=0;b<nb;b++) if(SDL_JoystickGetButton(j,b)) fprintf(stderr,"joy%d button %d\n",i,b);
      int hat=SDL_JoystickNumHats(j)?SDL_JoystickGetHat(j,0):0; if(hat&SDL_HAT_LEFT) k[37]=1; if(hat&SDL_HAT_UP) k[38]=1; if(hat&SDL_HAT_RIGHT) k[39]=1; if(hat&SDL_HAT_DOWN) k[40]=1;
      if(SDL_JoystickNumAxes(j)>=2){ int sx=stick_state(SDL_JoystickGetAxis(j,0),&stickx[i&7]), sy=stick_state(SDL_JoystickGetAxis(j,1),&sticky[i&7]);
        if(sx<0)k[37]=1; if(sx>0)k[39]=1; if(sy<0)k[38]=1; if(sy>0)k[40]=1; }
    } }
  if((start&&sel)||(l2&&r2)){ if(++comboframes>90) *quit=1; } else comboframes=0; }   /* Start+Select (or L2+R2) held ~1.5 s */

#undef B
/* ---------------- config ---------------- */
static int cfg_fullscreen=1, cfg_linear=0, cfg_showfps=0, cfg_vsync=1;
static void load_cfg(const char *dir){
  char p[1024]; snprintf(p,sizeof p,"%s/sonicexe.cfg",dir); FILE*f=fopen(p,"r"); if(!f) return; char l[256];
  while(fgets(l,sizeof l,f)){ char*e=strchr(l,'='); if(!e||l[0]=='#') continue; *e=0; char*v=e+1; v[strcspn(v,"\r\n")]=0; int n=atoi(v);
    if(!strcmp(l,"fullscreen")) cfg_fullscreen=n; else if(!strcmp(l,"linear")) cfg_linear=n; else if(!strcmp(l,"fps")) cfg_showfps=n; else if(!strcmp(l,"vsync")) cfg_vsync=n;
    else if(!strcmp(l,"texbudget")) g_tex_budget_mb=n; else if(!strcmp(l,"touch")) g_hide_touch=!n;
    else if(!strcmp(l,"a")) padmap[PA]=n; else if(!strcmp(l,"b")) padmap[PB]=n; else if(!strcmp(l,"x")) padmap[PX]=n; else if(!strcmp(l,"y")) padmap[PY]=n;
    else if(!strcmp(l,"l1")) padmap[PL1]=n; else if(!strcmp(l,"r1")) padmap[PR1]=n; else if(!strcmp(l,"start")) padmap[PSTART]=n; else if(!strcmp(l,"select")) padmap[PSELECT]=n;
    else if(!strcmp(l,"rawpad")) cfg_rawpad=n; else if(!strcmp(l,"deadzone")) cfg_deadzone=n;
    else if(!strncmp(l,"joy_",4)&&(joy_cfg_touched=1)&&0) {} else if(!strcmp(l,"joy_a")) rawmap[PA]=n; else if(!strcmp(l,"joy_b")) rawmap[PB]=n; else if(!strcmp(l,"joy_x")) rawmap[PX]=n; else if(!strcmp(l,"joy_y")) rawmap[PY]=n;
    else if(!strcmp(l,"joy_l1")) rawmap[PL1]=n; else if(!strcmp(l,"joy_r1")) rawmap[PR1]=n; else if(!strcmp(l,"joy_start")) rawmap[PSTART]=n; else if(!strcmp(l,"joy_select")) rawmap[PSELECT]=n;
    else if(!strcmp(l,"joy_up")) rawdpad[0]=n; else if(!strcmp(l,"joy_down")) rawdpad[1]=n; else if(!strcmp(l,"joy_left")) rawdpad[2]=n; else if(!strcmp(l,"joy_right")) rawdpad[3]=n;
    else if(!strcmp(l,"l2")) padmap[PL2]=n; else if(!strcmp(l,"r2")) padmap[PR2]=n; }
  fclose(f); }

int main(int argc,char**argv){
  if(argc>1&&argv[1][0]!='-') gamedir=argv[1]; if(getenv("JOYDEBUG")) joydebug=1; if(getenv("SONIC_VERBOSE")) g_verbose=1;
  load_cfg(gamedir);
  char path[1024]; snprintf(path,sizeof path,"%s/data.win",gamedir); if(data_load(path)) return 1;
  /* Without keyboard focus (typical on handhelds) SDL silently drops joystick input unless this is set */
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
  if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER|SDL_INIT_AUDIO)<0){ fprintf(stderr,"SDL_Init: %s\n",SDL_GetError()); return 1; }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,cfg_linear?"1":"0"); SDL_ShowCursor(SDL_DISABLE);
  win=SDL_CreateWindow("Sonic.EXE",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,LOGICAL_W,LOGICAL_H,cfg_fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:SDL_WINDOW_RESIZABLE);
  if(!win){ fprintf(stderr,"window: %s\n",SDL_GetError()); return 1; }
  ren=SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|(cfg_vsync?SDL_RENDERER_PRESENTVSYNC:0)); if(!ren){ fprintf(stderr,"accelerated renderer failed (%s), using software\n",SDL_GetError()); ren=SDL_CreateRenderer(win,-1,SDL_RENDERER_SOFTWARE); }
  if(!ren){ fprintf(stderr,"renderer: %s\n",SDL_GetError()); return 1; }
  SDL_RenderSetLogicalSize(ren,LOGICAL_W,LOGICAL_H);
  { SDL_RendererInfo ri; if(!SDL_GetRendererInfo(ren,&ri)){ fprintf(stderr,"renderer: %s, max texture %dx%d\n",ri.name,ri.max_texture_width,ri.max_texture_height);
      for(int i=0;i<D.ntex;i++) if(D.tex[i].w>ri.max_texture_width||D.tex[i].h>ri.max_texture_height) fprintf(stderr,"WARNING: texture page %d (%dx%d) exceeds GPU limit\n",i,D.tex[i].w,D.tex[i].h); } }
#ifndef NO_AUDIO
  if(Mix_OpenAudio(44100,MIX_DEFAULT_FORMAT,2,2048)<0) fprintf(stderr,"audio disabled: %s\n",Mix_GetError()); else { Mix_AllocateChannels(16); Mix_Init(MIX_INIT_OGG); }
#endif
  open_pads(); PL=&sdlp; fprintf(stderr,"input: %d pad(s) open, rawpad=%d. Press buttons: they are logged as [input] lines.\n",njoy,cfg_rawpad);
  if(engine_init(0)) return 1;
  uint8_t keys[256]; int quit=0; Uint32 next=SDL_GetTicks(); Uint32 fpst=SDL_GetTicks(); int fc=0; double frame_ms=1000.0/60.0; double acc=0;
  while(!quit){
    SDL_Event ev; while(SDL_PollEvent(&ev)){ if(ev.type==SDL_QUIT) quit=1;
      if(inlog<600){ if(ev.type==SDL_CONTROLLERBUTTONDOWN||ev.type==SDL_CONTROLLERBUTTONUP) { fprintf(stderr,"[input] controller button %d %s\n",ev.cbutton.button,ev.type==SDL_CONTROLLERBUTTONDOWN?"down":"up"); inlog++; }
        else if(ev.type==SDL_JOYBUTTONDOWN||ev.type==SDL_JOYBUTTONUP){ fprintf(stderr,"[input] joy button %d %s\n",ev.jbutton.button,ev.type==SDL_JOYBUTTONDOWN?"down":"up"); inlog++; }
        else if(ev.type==SDL_JOYHATMOTION){ fprintf(stderr,"[input] joy hat %d value %d\n",ev.jhat.hat,ev.jhat.value); inlog++; }
        else if(ev.type==SDL_JOYAXISMOTION&&ev.jaxis.axis<16){ int sg=ev.jaxis.value>16000?1:(ev.jaxis.value<-16000?-1:(abs(ev.jaxis.value)<8000?0:lastsign[ev.jaxis.axis]));
          if(sg!=lastsign[ev.jaxis.axis]){ lastsign[ev.jaxis.axis]=sg; fprintf(stderr,"[input] joy axis %d -> %d\n",ev.jaxis.axis,sg); inlog++; } } } if(ev.type==SDL_JOYDEVICEADDED||ev.type==SDL_CONTROLLERDEVICEADDED) { if(!njoy) open_pads(); } }
    memset(keys,0,sizeof keys); const Uint8*ks=SDL_GetKeyboardState(NULL);
    for(int sc=0;sc<SDL_NUM_SCANCODES;sc++) if(ks[sc]){ int vk=sc_to_vk(sc); if(vk) keys[vk]=1; }
    poll_pads(keys,&quit);
    if(ks[SDL_SCANCODE_F10]) quit=1;
    engine_frame(keys); SDL_RenderPresent(ren);
    fc++; Uint32 now=SDL_GetTicks();
    if(cfg_showfps&&now-fpst>=2000){ fprintf(stderr,"fps %.1f room %d inst %d\n",fc*1000.0/(now-fpst),engine_room(),engine_ninst()); fc=0; fpst=now; }
    acc+=frame_ms; next+=(Uint32)acc; acc-=(Uint32)acc; if((int)(next-now)>0&&(int)(next-now)<40) SDL_Delay(next-now); else if((int)(now-next)>100) next=now;
  }
#ifndef NO_AUDIO
  Mix_CloseAudio();
#endif
  SDL_Quit(); return 0; }
