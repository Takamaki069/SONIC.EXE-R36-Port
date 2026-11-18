/* sonicexe-runner: minimal GameMaker:Studio 1.x (bytecode v13) runner for "Sonic.EXE" (data.win)
 * Core is platform independent; see platform.h for the backend interface. */
#ifndef GM_H
#define GM_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define MAX_VARS 256
#define MAX_VIEWS 8
#define MAX_BG 8

/* ---------- values ---------- */
typedef struct { double d; const char *s; } Value;   /* s != NULL -> string */
static inline Value vnum(double d){ Value v; v.d=d; v.s=0; return v; }
static inline Value vstr(const char*s){ Value v; v.d=0; v.s=s; return v; }
const char *str_intern(const char *s);
const char *num_to_str(double d);

/* ---------- resources ---------- */
typedef struct { uint16_t sx,sy,sw,sh,tx,ty,tw,th,bw,bh,page; } TPag;
typedef struct {
  const char *name; int w,h,ml,mr,mb,mt,sep,ox,oy,nframes;
  TPag **fr; int nmasks; const uint8_t *masks; int stride;
} Sprite;
typedef struct { const char *name; TPag *tp; } Background;
typedef struct { uint16_t ch,x,y,w,h; int16_t shift,offset; } Glyph;
typedef struct { const char *name; int size; TPag *tp; int first,last,nglyphs; Glyph *g; int map[256]; } Font;
typedef struct { const char *name,*file; int embedded,audio_id; float volume; } Sound;
typedef struct { int sub,n; int *codes; } Event;
typedef struct {
  const char *name; int sprite,visible,solid,depth,persistent,parent,mask;
  int nev[12]; Event *ev[12];
} Object;
typedef struct { int x,y,obj; uint32_t id; int cc; float sx,sy; uint32_t color; float rot; } RoomInst;
typedef struct { int x,y,bg,sx,sy,w,h,depth; uint32_t id; float xs,ys; uint32_t color; } Tile;
typedef struct { int enabled,fg,bg,x,y,tilex,tiley,hspd,vspd,stretch; } RoomBg;
typedef struct { int enabled,vx,vy,vw,vh,px,py,pw,ph,bh,bv,hs,vs,obj; } RoomView;
typedef struct {
  const char *name; int w,h,speed,persistent; uint32_t bgcol; int drawbg,cc,flags;
  RoomBg bg[MAX_BG]; RoomView view[MAX_VIEWS];
  int ninst; RoomInst *inst; int ntile; Tile *tile;
} Room;
typedef struct { const char *name; uint8_t *bc; int len; } Code;
typedef struct { const char *name; int nmom; int *step; int *code; } Timeline;
typedef struct { const char *name; int builtin; } VarInfo;   /* builtin = enum BV_* or 0 for user var */
typedef struct { const char *name; void *fn; int script; } FuncInfo;
typedef struct { int w,h; const uint8_t *png; size_t pnglen; } TexInfo;

typedef struct {
  const uint8_t *data; size_t size;
  int nstr; const char **str;
  int nspr; Sprite *spr; int ntpag; TPag *tpag; int nbg; Background *bg;
  int nfont; Font *font; int nsnd; Sound *snd; int nobj; Object *obj;
  int nroom; Room *room; int ncode; Code *code; int ntl; Timeline *tl;
  int nvar; VarInfo *var; int nfunc; FuncInfo *func;
  int ntex; TexInfo *tex; int naud; const uint8_t **aud; uint32_t *audlen;
  int nscript; const char **scriptname; int *scriptcode;
  uint8_t *codebuf;
} GameData;
extern GameData D;

int  data_load(const char *path);
/* decode a texture page PNG into malloc'd RGBA (caller frees). returns NULL on failure */
uint8_t *png_decode_rgba(const uint8_t *png, size_t len, int *w, int *h);

/* ---------- platform backend ---------- */
typedef struct {
  void (*page_upload)(int page,int w,int h,const uint8_t *rgba);
  void (*page_free)(int page);
  void (*clear)(uint8_t r,uint8_t g,uint8_t b);
  void (*clip)(int x,int y,int w,int h);            /* w<=0: disable */
  void (*draw)(int page,int sx,int sy,int sw,int sh,float dx,float dy,float dw,float dh,
               float px,float py,float angle_ccw,uint8_t r,uint8_t g,uint8_t b,float a);
  void (*rect)(float x,float y,float w,float h,uint8_t r,uint8_t g,uint8_t b,float a);
  void (*sfx_play)(int snd,int loop);
  void (*sfx_stop)(int snd);
  void (*music_play)(int snd,int loop);
  void (*music_stop)(void);
} Platform;
extern Platform *PL;

/* ---------- engine ---------- */
#define LOGICAL_W 640
#define LOGICAL_H 480
int  engine_init(int os_type);
void engine_goto_room(int r);          /* immediate (used at start / by harness) */
void engine_frame(const uint8_t keys[256]);  /* one step + draw */
int  engine_room(void);
int  engine_ninst(void);
extern int g_tex_budget_mb;
extern int g_verbose;
extern int g_hide_touch;
#endif
