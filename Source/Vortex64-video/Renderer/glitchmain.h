// Project64 - A Nintendo 64 emulator
// https://www.pj64-emu.com/
// Copyright(C) 2001-2021 Project64
// Copyright(C) 2003-2009 Sergey 'Gonetz' Lipski
// Copyright(C) 2002 Dave2001
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html

#pragma once

//#define VPDEBUG
#ifdef VPDEBUG
void dump_tex(int id);
void dump_start();
void dump_stop();
extern int dumping;
#endif

#include <Vortex64-video/trace.h>
#include "types.h"

#include <stdio.h>

#include "opengl.h"

extern "C" {
    extern PFNGLACTIVETEXTUREARBPROC pj64_glActiveTextureARB;
    extern PFNGLMULTITEXCOORD2FARBPROC pj64_glMultiTexCoord2fARB;
    extern PFNGLATTACHOBJECTARBPROC pj64_glAttachObjectARB;
    extern PFNGLBINDFRAMEBUFFEREXTPROC pj64_glBindFramebufferEXT;
    extern PFNGLBINDRENDERBUFFEREXTPROC pj64_glBindRenderbufferEXT;
    extern PFNGLBLENDFUNCSEPARATEEXTPROC pj64_glBlendFuncSeparateEXT;
    extern PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC pj64_glCheckFramebufferStatusEXT;
    extern PFNGLCOMPILESHADERARBPROC pj64_glCompileShaderARB;
    extern PFNGLCREATEPROGRAMOBJECTARBPROC pj64_glCreateProgramObjectARB;
    extern PFNGLCREATESHADEROBJECTARBPROC pj64_glCreateShaderObjectARB;
    extern PFNGLDELETERENDERBUFFERSEXTPROC pj64_glDeleteRenderbuffersEXT;
    extern PFNGLDELETEFRAMEBUFFERSEXTPROC pj64_glDeleteFramebuffersEXT;
    extern PFNGLFOGCOORDFEXTPROC pj64_glFogCoordfEXT;
    extern PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC pj64_glFramebufferRenderbufferEXT;
    extern PFNGLFRAMEBUFFERTEXTURE2DEXTPROC pj64_glFramebufferTexture2DEXT;
    extern PFNGLGENFRAMEBUFFERSEXTPROC pj64_glGenFramebuffersEXT;
    extern PFNGLGENRENDERBUFFERSEXTPROC pj64_glGenRenderbuffersEXT;
    extern PFNGLGETINFOLOGARBPROC pj64_glGetInfoLogARB;
    extern PFNGLGETOBJECTPARAMETERIVARBPROC pj64_glGetObjectParameterivARB;
    extern PFNGLGETUNIFORMLOCATIONARBPROC pj64_glGetUniformLocationARB;
    extern PFNGLLINKPROGRAMARBPROC pj64_glLinkProgramARB;
    extern PFNGLRENDERBUFFERSTORAGEEXTPROC pj64_glRenderbufferStorageEXT;
    extern PFNGLSECONDARYCOLOR3FPROC pj64_glSecondaryColor3f;
    extern PFNGLSHADERSOURCEARBPROC pj64_glShaderSourceARB;
    extern PFNGLUNIFORM1FARBPROC pj64_glUniform1fARB;
    extern PFNGLUNIFORM1IARBPROC pj64_glUniform1iARB;
    extern PFNGLUNIFORM4FARBPROC pj64_glUniform4fARB;
    extern PFNGLUSEPROGRAMOBJECTARBPROC pj64_glUseProgramObjectARB;
    extern PFNGLGETHANDLEARBPROC pj64_glGetHandleARB;
}

// Do not let calls to these ARB entry points bind through libGL's global ELF
// symbols.  They must use the context-specific functions resolved by SDL.
#define glActiveTextureARB pj64_glActiveTextureARB
#define glMultiTexCoord2fARB pj64_glMultiTexCoord2fARB

#define zscale 1.0f

// VP added this utility function
// Returns the bytes per pixel of a given GR texture format
int grTexFormatSize(int fmt);

// 03/07/2015 Comment by cxd4 -- regulated GL state machine debugging using glGetError
extern int grDisplayGLError(const char* message);

extern int packed_pixels_support;
extern int ati_sucks;
extern float largest_supported_anisotropy;

extern GLuint default_texture; // The infamous "32*1024*1024" is now configurable
extern GLuint depth_texture;
void set_depth_shader();
void set_bw_shader();
extern float invtex[2];
extern int buffer_cleared; // Mark that the buffer has been cleared, used to check if we need to reload the texture buffer content

void init_geometry();
void init_textures();
void init_combiner();
void free_textures();
void updateCombiner(int i);
void updateCombinera(int i);
void remove_tex(unsigned int idmin, unsigned int idmax);
void add_tex(unsigned int id);

void check_link(GLuint program);
void vbo_enable();
void vbo_disable();

// Vertex attribute locations
#define POSITION_ATTR 0
#define COLOUR_ATTR 1
#define TEXCOORD_0_ATTR 2
#define TEXCOORD_1_ATTR 3
#define FOG_ATTR 4

extern int w_buffer_mode;
extern int g_width, g_height, widtho, heighto;
extern int tex0_width, tex0_height, tex1_width, tex1_height;
extern float lambda;
extern int need_lambda[2];
extern float lambda_color[2][4];
extern int inverted_culling;
extern gfxCullMode_t culling_mode;
extern int render_to_texture;
extern int lfb_color_fmt;
extern int need_to_compile;
extern int blackandwhite0;
extern int blackandwhite1;
extern int TMU_SIZE;

extern int blend_func_separate_support;
extern int fog_coord_support;
extern int glsl_support;
extern unsigned int pBufferAddress;
extern int viewport_width, viewport_height, g_viewport_offset, nvidia_viewport_hack;
extern int UMAmode;

void updateTexture();
void reloadTexture();
void free_combiners();
void compile_shader();
void set_lambda();
void set_copy_shader();
void disable_textureSizes();
void ExitFullScreen();

void CHECK_FRAMEBUFFER_STATUS(void);
