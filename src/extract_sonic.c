/* extract_sonic: unpack an IExpress/CAB (LZX) self-extractor such as SONIC.EXE. No dependencies. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t i32;
static u8 *in; static size_t inlen, ipos;
static u32 bitbuf; static int bitsleft;
static void ensure(int n){ while(bitsleft<n){ u32 w=0; if(ipos+1<inlen) w=in[ipos]|(in[ipos+1]<<8); else if(ipos<inlen) w=in[ipos]; ipos+=2; bitbuf|=w<<(16-bitsleft); bitsleft+=16; } }
static u32 peek(int n){ return n?bitbuf>>(32-n):0; }
static void rem(int n){ bitbuf<<=n; bitsleft-=n; }
static u32 rd(int n){ if(!n) return 0; ensure(n); u32 v=peek(n); rem(n); return v; }
typedef struct { u16 cnt[17]; u16 sym[1024]; } huff;
static void build(huff*h,u8*len,int n){ memset(h->cnt,0,sizeof h->cnt); for(int i=0;i<n;i++) h->cnt[len[i]]++; h->cnt[0]=0; u16 offs[18]; offs[1]=0; for(int l=1;l<=16;l++) offs[l+1]=offs[l]+h->cnt[l]; for(int i=0;i<n;i++) if(len[i]) h->sym[offs[len[i]]++]=i; }
static int dec(huff*h){ ensure(16); u32 b=bitbuf; int code=0,first=0,idx=0; for(int l=1;l<=16;l++){ code|=(b>>31); b<<=1; int c=h->cnt[l]; if(code-c<first){ rem(l); return h->sym[idx+(code-first)]; } idx+=c; first+=c; first<<=1; code<<=1; } fprintf(stderr,"bad huffman code\n"); exit(2); }
static u8 mainlen[256+50*8], lenlen[249+1]; 
static void readlens(u8*lens,int first,int last){ u8 pl[20]; huff pt; for(int i=0;i<20;i++) pl[i]=rd(4); build(&pt,pl,20);
 for(int x=first;x<last;){ int z=dec(&pt); int y;
  if(z==17){ y=rd(4)+4; while(y--&&x<last) lens[x++]=0; }
  else if(z==18){ y=rd(5)+20; while(y--&&x<last) lens[x++]=0; }
  else if(z==19){ y=rd(1)+4; z=dec(&pt); z=lens[x]-z; if(z<0) z+=17; while(y--&&x<last) lens[x++]=z; }
  else { z=lens[x]-z; if(z<0) z+=17; lens[x++]=z; } } }

/* decode folder stream: returns malloc'd buffer of `total` bytes */
static u8 *lzx_decode(int wb,size_t total){
 u8*out=calloc(1,total+300000); u8*fin=calloc(1,total+300000); size_t op=0; ipos=0; bitbuf=0; bitsleft=0;
 memset(mainlen,0,sizeof mainlen); memset(lenlen,0,sizeof lenlen);
 int slots[]={30,32,34,36,38,42,50}; int nslots=slots[wb-15]; int eb[52]; u32 pb[52]; { int j=0; for(int i=0;i<=50;i+=2){ eb[i]=eb[i+1]=j; if(i&&j<17) j++; } u32 b=0; for(int i=0;i<51;i++){ pb[i]=b; b+=1<<eb[i]; } }
 int mainsyms=256+nslots*8; int intel=0; u32 ifs=0; i32 icur=0; int hdr=0; int btype=0; long brem=0; u32 R0=1,R1=1,R2=1; huff mt,lt,at; u8 alen[8]; int frame=0;
 while(op<total){
  if(!hdr){ hdr=1; if(rd(1)){ u32 hi=rd(16),lo=rd(16); ifs=(hi<<16)|lo; intel=1; } }
  size_t fsz=32768; if(total-op<fsz) fsz=total-op; size_t fend=op+fsz;
  while(op<fend){
   if(brem==0){ btype=rd(3); u32 hi=rd(16),lo=rd(8); brem=(hi<<8)|lo;
    if(btype==2){ for(int i=0;i<8;i++) alen[i]=rd(3); build(&at,alen,8); }
    if(btype==1||btype==2){ readlens(mainlen,0,256); readlens(mainlen,256,mainsyms); build(&mt,mainlen,mainsyms); readlens(lenlen,0,249); build(&lt,lenlen,249); }
    else if(btype==3){ size_t P=ipos*8-bitsleft; size_t nP=(P/16+1)*16; ipos=nP/8; bitbuf=0; bitsleft=0;
      R0=in[ipos]|in[ipos+1]<<8|in[ipos+2]<<16|(u32)in[ipos+3]<<24; ipos+=4; R1=in[ipos]|in[ipos+1]<<8|in[ipos+2]<<16|(u32)in[ipos+3]<<24; ipos+=4; R2=in[ipos]|in[ipos+1]<<8|in[ipos+2]<<16|(u32)in[ipos+3]<<24; ipos+=4; }
    else { fprintf(stderr,"corrupt stream (block type %d)\n",btype); return NULL; } }
   long run=brem; if(run>(long)(fend-op)) run=fend-op; brem-=run;
   if(btype==3){ memcpy(out+op,in+ipos,run); ipos+=run; op+=run; if(brem==0&&(run&1)) ipos++; }
   else { long r=run;
    while(r>0){ int m=dec(&mt);
     if(m<256){ out[op++]=m; r--; continue; }
     m-=256; int ml=m&7; if(ml==7) ml+=dec(&lt); ml+=2; int slot=m>>3; u32 off;
     if(slot==0) off=R0; else if(slot==1){ off=R1; R1=R0; R0=off; } else if(slot==2){ off=R2; R2=R0; R0=off; }
     else { int e=eb[slot]; u32 vb; if(btype==2&&e>=3){ vb=rd(e-3)<<3; vb+=dec(&at); } else vb=rd(e); off=pb[slot]-2+vb; R2=R1; R1=R0; R0=off; }
     if(off>op){ fprintf(stderr,"corrupt stream (offset)\n"); return NULL; }
     for(int i=0;i<ml;i++){ out[op]=out[op-off]; op++; } r-=ml; }
    if(r<0){ if(-r>brem){ fprintf(stderr,"corrupt stream (overrun)\n"); return NULL; } brem+=r; }
    if(op>fend){ fprintf(stderr,"corrupt stream (frame)\n"); return NULL; } } }
  if(bitsleft>0) ensure(16); if(bitsleft&15) rem(bitsleft&15);
  memcpy(fin+fend-fsz,out+fend-fsz,fsz);   /* E8 fix-up runs on a copy: matches must see untranslated history */
  if(intel&&ifs&&frame<32768&&fsz>10){ u8*dp=fin+fend-fsz,*de=dp+fsz-10; i32 cur=icur; while(dp<de){ if(*dp++!=0xE8){cur++;continue;} i32 abs=dp[0]|dp[1]<<8|dp[2]<<16|(i32)((u32)dp[3]<<24);
     if(abs>=-cur&&abs<(i32)ifs){ i32 rel=abs>=0?abs-cur:abs+ifs; dp[0]=rel;dp[1]=rel>>8;dp[2]=rel>>16;dp[3]=rel>>24; } dp+=4; cur+=5; } }
  icur+=fsz; frame++;
  if((frame&31)==0||op>=total){ fprintf(stderr,"\r  Decompressing game data: %3d%%",(int)(op*100/total)); fflush(stderr); } }
 fprintf(stderr,"\n"); free(out); return fin; }

static u32 le32(const u8*p){ return p[0]|p[1]<<8|p[2]<<16|(u32)p[3]<<24; } static u16 le16(const u8*p){ return p[0]|p[1]<<8; }
static u32 cabsum(const u8*d,size_t n,u32 seed){ u32 s=seed; size_t i=0; for(;n-i>=4;i+=4) s^=le32(d+i); u32 t=0; size_t r=n-i; if(r==3) t=(d[i]<<16)|(d[i+1]<<8)|d[i+2]; else if(r==2) t=(d[i]<<8)|d[i+1]; else if(r==1) t=d[i]; return s^t; }

int main(int argc,char**argv){
 int game_only=0; const char*in_path=NULL,*out_dir=NULL;
 for(int i=1;i<argc;i++){ if(!strcmp(argv[i],"--game-only")) game_only=1; else if(!in_path) in_path=argv[i]; else if(!out_dir) out_dir=argv[i]; }
 if(!in_path||!out_dir){ fprintf(stderr,"usage: %s [--game-only] SONIC.EXE outdir\n  Extracts the cabinet embedded in an IExpress self-extractor (LZX).\n  --game-only  skip the Windows-only files (*.exe, *.dll)\n",argv[0]); return 1; }
 fprintf(stderr,"Sonic.EXE data extractor\n");
 FILE*f=fopen(in_path,"rb"); if(!f){ perror(in_path); return 1; } fseek(f,0,SEEK_END); long fs=ftell(f); fseek(f,0,SEEK_SET);
 fprintf(stderr,"Reading %s (%ld MB)...\n",in_path,fs>>20);
 u8*d=malloc(fs); if(!d||fread(d,1,fs,f)!=(size_t)fs){ fprintf(stderr,"ERROR: could not read %s\n",in_path); return 1; } fclose(f);
 long co=-1; for(long i=0;i+36<fs;i++) if(!memcmp(d+i,"MSCF\0\0\0\0",8)&&le32(d+i+8)<=(u32)(fs-i)&&le32(d+i+8)>1000){ co=i; break; }
 if(co<0){ fprintf(stderr,"ERROR: no game archive found in %s (is it the original SONIC.EXE?)\n",in_path); return 1; }
 const u8*c=d+co; u32 coffFiles=le32(c+16); int nfold=le16(c+26), nfiles=le16(c+28), flags=le16(c+30);
 if(nfold!=1||flags!=0){ fprintf(stderr,"ERROR: unsupported archive layout (folders=%d flags=%d)\n",nfold,flags); return 1; }
 u32 dataoff=le32(c+36); int nd=le16(c+40); int comp=le16(c+42); if((comp&15)!=3){ fprintf(stderr,"ERROR: unsupported compression type %d (need LZX)\n",comp&15); return 1; }
 int wb=(comp>>8)&31; if(wb<15||wb>21){ fprintf(stderr,"ERROR: bad window size\n"); return 1; }
 in=malloc(fs); inlen=0; size_t p=co+dataoff,total=0; int badsum=0;
 fprintf(stderr,"Checking archive integrity (%d blocks)...\n",nd);
 for(int i=0;i<nd;i++){ u32 cs=le32(d+p); u16 cb=le16(d+p+4),cu=le16(d+p+6); if(cs&&cabsum(d+p+4,4,cabsum(d+p+8,cb,0))!=cs) badsum++; memcpy(in+inlen,d+p+8,cb); inlen+=cb; p+=8+cb; total+=cu; }
 if(badsum){ fprintf(stderr,"ERROR: %d damaged blocks - the file is corrupt, please re-copy SONIC.EXE. Nothing was extracted.\n",badsum); return 2; }
 fprintf(stderr,"Archive OK: %d files, %zu MB unpacked.\n",nfiles,total>>20);
 u8*fin=lzx_decode(wb,total); if(!fin){ fprintf(stderr,"ERROR: decompression failed\n"); return 3; }
 char cmd[1200]; snprintf(cmd,sizeof cmd,"mkdir -p '%s'",out_dir); if(system(cmd)){}
 fprintf(stderr,"Writing files:\n"); size_t q=co+coffFiles; int written=0; size_t wbytes=0;
 for(int i=0;i<nfiles;i++){ u32 sz=le32(d+q),off=le32(d+q+4); q+=16; const char*nm=(const char*)d+q; q+=strlen(nm)+1;
  const char*b=strrchr(nm,'\\'); b=b?b+1:nm; size_t bl=strlen(b);
  if(game_only&&bl>4&&(!strcasecmp(b+bl-4,".exe")||!strcasecmp(b+bl-4,".dll"))){ fprintf(stderr,"  %-28s skipped (Windows-only)\n",b); continue; }
  char path[1100]; snprintf(path,sizeof path,"%s/%s",out_dir,b);
  if((size_t)off+sz>total){ fprintf(stderr,"ERROR: file %s out of range\n",nm); return 3; }
  FILE*o=fopen(path,"wb"); if(!o){ perror(path); return 3; } if(fwrite(fin+off,1,sz,o)!=sz){ fprintf(stderr,"ERROR: disk full while writing %s\n",b); fclose(o); return 3; } fclose(o);
  written++; wbytes+=sz; fprintf(stderr,"  %-28s %6.1f MB\n",b,sz/1048576.0); }
 fprintf(stderr,"Done: %d files (%zu MB) written to %s\n",written,wbytes>>20,out_dir);
 return 0; }
