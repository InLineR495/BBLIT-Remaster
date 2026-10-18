// Startup logos precede the first recorded frame. Extend their rendered
// background into the bars, without changing the image's viewport or aspect.
#include <windows.h>
#include <GL/gl.h>
#include "window.h"
#include "log.h"
#include "splash.h"
#include "splash_policy.h"

static struct {
    void (WINAPI *GetIntegerv)(GLenum,GLint *);
    void (WINAPI *PushAttrib)(GLbitfield);
    void (WINAPI *PopAttrib)(void);
    void (WINAPI *PushClientAttrib)(GLbitfield);
    void (WINAPI *PopClientAttrib)(void);
    void (WINAPI *PixelStorei)(GLenum,GLint);
    void (WINAPI *ReadBuffer)(GLenum);
    void (WINAPI *ReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void *);
    void (WINAPI *Enable)(GLenum);
    void (WINAPI *Scissor)(GLint,GLint,GLsizei,GLsizei);
    void (WINAPI *ColorMask)(GLboolean,GLboolean,GLboolean,GLboolean);
    void (WINAPI *ClearColor)(GLclampf,GLclampf,GLclampf,GLclampf);
    void (WINAPI *Clear)(GLbitfield);
} gl;

void bb_splash_fill(void)
{
    static int loaded;
    static unsigned reported;
    RECT rc;
    GLint vp[4],draw;
    if(!bb_game_hwnd || !GetClientRect(bb_game_hwnd,&rc)) return;
    if(!loaded) {
        HMODULE mod=GetModuleHandleA("opengl32.dll");
        if(!mod) return;
#define LOAD(name) gl.name=(void *)GetProcAddress(mod,"gl" #name); if(!gl.name) return
        LOAD(GetIntegerv); LOAD(PushAttrib); LOAD(PopAttrib); LOAD(PushClientAttrib); LOAD(PopClientAttrib);
        LOAD(PixelStorei); LOAD(ReadBuffer); LOAD(ReadPixels); LOAD(Enable); LOAD(Scissor);
        LOAD(ColorMask); LOAD(ClearColor); LOAD(Clear);
#undef LOAD
        loaded=1;
    }
    gl.GetIntegerv(GL_VIEWPORT,vp);
    int cw=rc.right,ch=rc.bottom;
    if(vp[0]<0 || vp[1]<0 || vp[2]<20 || vp[3]<20 || vp[2]>cw-vp[0] || vp[3]>ch-vp[1] ||
       (vp[2]==cw && vp[3]==ch)) return;
    gl.GetIntegerv(GL_DRAW_BUFFER,&draw);
    if(draw!=GL_BACK && draw!=GL_FRONT) return;
    gl.PushAttrib(GL_COLOR_BUFFER_BIT|GL_SCISSOR_BIT|GL_ENABLE_BIT);
    gl.PushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    gl.PixelStorei(GL_PACK_ALIGNMENT,1); gl.PixelStorei(GL_PACK_ROW_LENGTH,0);
    gl.PixelStorei(GL_PACK_SKIP_ROWS,0); gl.PixelStorei(GL_PACK_SKIP_PIXELS,0);
    gl.ReadBuffer((GLenum)draw);
    unsigned char corners[4][4]={{0}};
    for(int i=0;i<4;i++)
        gl.ReadPixels(vp[0]+(i&1 ? vp[2]-vp[2]/20-1 : vp[2]/20),
                      vp[1]+(i&2 ? vp[3]-vp[3]/20-1 : vp[3]/20),1,1,GL_RGBA,GL_UNSIGNED_BYTE,corners[i]);
    gl.PopClientAttrib();
    float rgb[3];
    if(bb_splash_color(corners,rgb)) {
        gl.Enable(GL_SCISSOR_TEST); gl.ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        gl.ClearColor(rgb[0],rgb[1],rgb[2],1);
        // Cover the thin raster seam just inside a padded image edge. Do not
        // touch unpadded edges or resize the artwork; overlap is only 2 pixels.
        const int seam=2;
        int right=cw-vp[0]-vp[2],top=ch-vp[1]-vp[3];
        const int bars[4][4]={{0,0,vp[0]>0 ? vp[0]+seam : 0,ch},
            {vp[0]+vp[2]-seam,0,right>0 ? right+seam : 0,ch},
            {vp[0],0,vp[2],vp[1]>0 ? vp[1]+seam : 0},
            {vp[0],vp[1]+vp[3]-seam,vp[2],top>0 ? top+seam : 0}};
        for(int i=0;i<4;i++) if(bars[i][2]>0 && bars[i][3]>0) {
            gl.Scissor(bars[i][0],bars[i][1],bars[i][2],bars[i][3]); gl.Clear(GL_COLOR_BUFFER_BIT);
        }
        if(reported++<8) bb_log("splash: bars RGB %.3f/%.3f/%.3f, viewport %d,%d %dx%d",rgb[0],rgb[1],rgb[2],vp[0],vp[1],vp[2],vp[3]);
    } else if(reported++<8) {
        bb_log("splash: nonuniform samples %u/%u/%u/%u viewport %d,%d %dx%d draw=%x",corners[0][0],corners[1][0],corners[2][0],corners[3][0],vp[0],vp[1],vp[2],vp[3],draw);
    }
    gl.PopAttrib();
}
