// Windows integration test: hidden real GL context; no game files required.
#include <windows.h>
#include <GL/gl.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include "../src/splash.h"

HWND bb_game_hwnd;
void bb_log(const char *format, ...) { (void)format; }

int main(void)
{
    WNDCLASSA wc={0};
    wc.style=CS_OWNDC; wc.lpfnWndProc=DefWindowProcA;
    wc.hInstance=GetModuleHandleA(NULL); wc.lpszClassName="BBLIT splash test";
    assert(RegisterClassA(&wc));
    bb_game_hwnd=CreateWindowA(wc.lpszClassName,"",WS_POPUP,0,0,160,90,NULL,NULL,wc.hInstance,NULL);
    assert(bb_game_hwnd);
    HDC dc=GetDC(bb_game_hwnd);
    PIXELFORMATDESCRIPTOR pfd={0};
    pfd.nSize=sizeof pfd; pfd.nVersion=1;
    pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
    pfd.iPixelType=PFD_TYPE_RGBA; pfd.cColorBits=24;
    int pf=ChoosePixelFormat(dc,&pfd); assert(pf && SetPixelFormat(dc,pf,&pfd));
    HGLRC ctx=wglCreateContext(dc); assert(ctx && wglMakeCurrent(dc,ctx));
    const float colors[3][3]={{248.0f/255,248.0f/255,248.0f/255},{0,0,1},{0,0,0}};
    for(int c=0;c<3;c++) {
        glDisable(GL_SCISSOR_TEST); glColorMask(1,1,1,1);
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
        glViewport(20,0,120,90);
        // Simulate an image with a narrow black raster border. Sampling one
        // pixel from its edge would incorrectly choose black instead of white.
        glEnable(GL_SCISSOR_TEST); glScissor(22,2,116,86);
        glClearColor(colors[c][0],colors[c][1],colors[c][2],1); glClear(GL_COLOR_BUFFER_BIT);
        glScissor(3,4,5,6); glColorMask(1,0,1,0); glClearColor(.2f,.3f,.4f,.5f);
        GLfloat clear_before[4]; glGetFloatv(GL_COLOR_CLEAR_VALUE,clear_before);
        bb_splash_fill();
        GLint vp[4],sc[4]; GLboolean mask[4]; GLfloat clear[4];
        glGetIntegerv(GL_VIEWPORT,vp); glGetIntegerv(GL_SCISSOR_BOX,sc);
        glGetBooleanv(GL_COLOR_WRITEMASK,mask); glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);
        assert(vp[0]==20 && vp[2]==120 && vp[3]==90);
        assert(sc[0]==3 && sc[1]==4 && sc[2]==5 && sc[3]==6 && glIsEnabled(GL_SCISSOR_TEST));
        assert(mask[0] && !mask[1] && mask[2] && !mask[3]);
        for(int k=0;k<4;k++) assert(fabsf(clear[k]-clear_before[k])<0.000001f);
        unsigned char left[4],right[4],center[4],border[4];
        glReadBuffer(GL_BACK); glReadPixels(1,45,1,1,GL_RGBA,GL_UNSIGNED_BYTE,left);
        glReadPixels(158,45,1,1,GL_RGBA,GL_UNSIGNED_BYTE,right);
        glReadPixels(80,45,1,1,GL_RGBA,GL_UNSIGNED_BYTE,center);
        glReadPixels(20,45,1,1,GL_RGBA,GL_UNSIGNED_BYTE,border);
        for(int k=0;k<3;k++) {
            assert(left[k]==center[k] && right[k]==center[k]);
            assert(border[k]==center[k]); // The narrow black seam is covered.
        }
        assert(glGetError()==GL_NO_ERROR);
    }
    wglMakeCurrent(NULL,NULL); wglDeleteContext(ctx);
    ReleaseDC(bb_game_hwnd,dc); DestroyWindow(bb_game_hwnd);
    puts("Splash GL: matching bars, seam repair, image centre and state restoration passed");
    return 0;
}
