#include <jni.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <dlfcn.h>
#include <cstring>
#include <cmath>
#include <sys/mman.h>
#include <fstream>
#include <string>

#include "pl/Gloss.h"
#include "pl/Mod.h"
#include "pl/PreloaderInput.h"

#define TAG "FreeCam"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static bool  g_active  = false;
static float g_camX=0, g_camY=0, g_camZ=0;
static float g_camYaw=0, g_camPitch=0;
static float g_speed = 0.3f;
static int   g_screenW=1080, g_screenH=2400;
static float g_joyX=0, g_joyY=0;
static float g_lastTX=-1, g_lastTY=-1;
static bool  g_rotating=false;

static void (*g_orig_getCamPos)(void*,float*) = nullptr;
static void (*g_orig_getCamRot)(void*,float*) = nullptr;
static int  (*g_orig_getPerspective)(void*)   = nullptr;
static EGLBoolean (*g_orig_swap)(EGLDisplay,EGLSurface) = nullptr;

static void hook_getCamPos(void* s, float* o) {
    if (g_active){o[0]=g_camX;o[1]=g_camY;o[2]=g_camZ;return;}
    if (g_orig_getCamPos) g_orig_getCamPos(s,o);
}
static void hook_getCamRot(void* s, float* o) {
    if (g_active){o[0]=g_camYaw;o[1]=g_camPitch;return;}
    if (g_orig_getCamRot) g_orig_getCamRot(s,o);
}
static int hook_getPerspective(void* s) {
    if (g_active) return 0;
    return g_orig_getPerspective ? g_orig_getPerspective(s) : 0;
}

static uintptr_t findVtable(const char* name) {
    size_t len=strlen(name);
    std::ifstream m("/proc/self/maps"); std::string line;
    uintptr_t nameAddr=0;
    while(std::getline(m,line)){
        if(line.find("libminecraftpe.so")==std::string::npos) continue;
        uintptr_t s,e; if(sscanf(line.c_str(),"%lx-%lx",&s,&e)!=2) continue;
        for(uintptr_t a=s;a<e-len;a++) if(!memcmp((void*)a,name,len)){nameAddr=a;break;}
        if(nameAddr) break;
    }
    if(!nameAddr) return 0;
    std::ifstream m2("/proc/self/maps"); uintptr_t tiAddr=0;
    while(std::getline(m2,line)){
        if(line.find("libminecraftpe.so")==std::string::npos) continue;
        uintptr_t s,e; if(sscanf(line.c_str(),"%lx-%lx",&s,&e)!=2) continue;
        for(uintptr_t a=s;a<e-8;a+=8) if(*(uintptr_t*)a==nameAddr){tiAddr=a-8;break;}
        if(tiAddr) break;
    }
    if(!tiAddr) return 0;
    std::ifstream m3("/proc/self/maps"); uintptr_t vt=0;
    while(std::getline(m3,line)){
        if(line.find("libminecraftpe.so")==std::string::npos) continue;
        uintptr_t s,e; if(sscanf(line.c_str(),"%lx-%lx",&s,&e)!=2) continue;
        for(uintptr_t a=s;a<e-8;a+=8) if(*(uintptr_t*)a==tiAddr){vt=a+8;break;}
        if(vt) break;
    }
    return vt;
}

static bool patchSlot(uintptr_t vt,int slot,void* hook,void** orig){
    uintptr_t* p=(uintptr_t*)(vt+slot*8);
    *orig=(void*)(*p);
    uintptr_t pg=(uintptr_t)p&~4095UL;
    if(mprotect((void*)pg,4096,PROT_READ|PROT_WRITE)!=0) return false;
    *p=(uintptr_t)hook;
    mprotect((void*)pg,4096,PROT_READ);
    return true;
}

static inline bool inButton(float x,float y){
    int bx=g_screenW-90,by=10;
    return x>=bx&&x<=bx+80&&y>=by&&y<=by+80;
}
static inline bool inJoy(float x,float y){
    float dx=x-90,dy=y-(g_screenH-90);
    return dx*dx+dy*dy<=100.f*100.f;
}

static bool onTouch(int action,int,float x,float y){
    if(action==0){
        if(inButton(x,y)){
            g_active=!g_active;
            LOGI("FreeCam %s",g_active?"ON":"OFF");
            return true;
        }
        if(g_active&&!inJoy(x,y)){g_lastTX=x;g_lastTY=y;g_rotating=true;}
        if(g_active&&inJoy(x,y)){g_joyX=g_joyY=0;}
    }
    if(action==2){
        if(g_active&&g_rotating&&g_lastTX>=0){
            g_camYaw+=(x-g_lastTX)*0.2f;
            g_camPitch+=(y-g_lastTY)*0.2f;
            if(g_camPitch>90)g_camPitch=90;
            if(g_camPitch<-90)g_camPitch=-90;
            g_lastTX=x;g_lastTY=y;
        }
        if(g_active&&inJoy(x,y)){g_joyX=(x-90)/80.f;g_joyY=(y-(g_screenH-90))/80.f;}
    }
    if(action==1){g_rotating=false;g_lastTX=-1;g_joyX=g_joyY=0;}
    return false;
}

static EGLBoolean hook_swap(EGLDisplay dpy,EGLSurface surf){
    if(g_active){
        EGLint w=0,h=0;
        eglQuerySurface(dpy,surf,EGL_WIDTH,&w);
        eglQuerySurface(dpy,surf,EGL_HEIGHT,&h);
        if(w>0)g_screenW=w; if(h>0)g_screenH=h;
        if(g_joyX!=0||g_joyY!=0){
            float yr=g_camYaw*(float)M_PI/180.f;
            float sy=sinf(yr),cy=cosf(yr);
            g_camX+=(-g_joyY*sy+g_joyX*cy)*g_speed;
            g_camZ+=( g_joyY*cy+g_joyX*sy)*g_speed;
        }
    }
    return g_orig_swap?g_orig_swap(dpy,surf):EGL_FALSE;
}

extern "C" __attribute__((visibility("default")))
void LeviMod_Load(JavaVM* vm, const PLModInfo* info){
    LOGI("FreeCam loading...");
    GlossInit(true);

    uintptr_t vt=findVtable("16VanillaCameraAPI");
    if(vt){
        patchSlot(vt,7,(void*)hook_getPerspective,(void**)&g_orig_getPerspective);
        patchSlot(vt,8,(void*)hook_getCamPos,(void**)&g_orig_getCamPos);
        patchSlot(vt,9,(void*)hook_getCamRot,(void**)&g_orig_getCamRot);
        LOGI("Camera hooked");
    } else LOGE("VanillaCameraAPI not found");

    GHandle h=GlossOpen("libEGL.so");
    if(h){
        void* sw=(void*)GlossSymbol(h,"eglSwapBuffers",nullptr);
        if(sw) GlossHook(sw,(void*)hook_swap,(void**)&g_orig_swap);
    }

    auto* input=GetPreloaderInput();
    if(input&&input->RegisterTouchCallback){
        input->RegisterTouchCallback(onTouch);
        LOGI("Touch registered");
    }
    LOGI("FreeCam ready. Tap top-right corner to toggle.");
}
