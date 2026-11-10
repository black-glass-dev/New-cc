#!/usr/bin/env python3
"""Production C++ GLSL vs CPU on an actual EGL/GLES implementation (software allowed).
Usage: python3 tests/run_host_gpu.py EGL_LIBRARY GLES_LIBRARY
Builds original probe into a temporary directory; never an Android/device/codec test.
Libraries are developer-provided, not downloaded or bundled by this script.
"""
import ctypes as c
import os
from pathlib import Path
import subprocess as sp
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def bind(lib,name,result,args):
    fn=getattr(lib,name);fn.restype=result;fn.argtypes=args;return fn

def main():
    egl=c.CDLL(str(Path(sys.argv[1]).resolve()),mode=c.RTLD_GLOBAL)
    gl=c.CDLL(str(Path(sys.argv[2]).resolve()),mode=c.RTLD_GLOBAL)
    ptr=c.c_void_p; integer=c.c_int; uint=c.c_uint; boolean=c.c_ubyte
    # ANGLE SwiftShader needs an explicit software device and offscreen platform.
    if os.environ.get('OWNCUT_SWIFTSHADER') == '1':
        get_proc=bind(egl,'eglGetProcAddress',ptr,[c.c_char_p])
        platform=c.CFUNCTYPE(ptr,uint,ptr,c.POINTER(integer))(get_proc(b'eglGetPlatformDisplayEXT'))
        display=platform(0x3202,None,(integer*7)(0x3203,0x3450,0x3209,0x3487,0x348F,0x34A5,0x3038))
    else:
        display=bind(egl,'eglGetDisplay',ptr,[ptr])(None)
    initialize=bind(egl,'eglInitialize',uint,[ptr,c.POINTER(integer),c.POINTER(integer)])
    major,minor=integer(),integer()
    assert initialize(display,c.byref(major),c.byref(minor)), 'eglInitialize failed'
    choose=bind(egl,'eglChooseConfig',uint,[ptr,c.POINTER(integer),c.POINTER(ptr),integer,c.POINTER(integer)])
    attrs=(integer*15)(0x3033,1,0x3040,4,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038,0,0)
    config,count=ptr(),integer();assert choose(display,attrs,c.byref(config),1,c.byref(count)) and count.value>0
    bind(egl,'eglBindAPI',uint,[uint])(0x30A0)
    context=bind(egl,'eglCreateContext',ptr,[ptr,ptr,ptr,c.POINTER(integer)])(display,config,None,(integer*3)(0x3098,2,0x3038))
    surface=bind(egl,'eglCreatePbufferSurface',ptr,[ptr,ptr,c.POINTER(integer)])(display,config,(integer*5)(0x3057,32,0x3056,16,0x3038))
    assert context and surface
    make_current=bind(egl,'eglMakeCurrent',uint,[ptr,ptr,ptr,ptr]);assert make_current(display,surface,surface,context)
    get_error=bind(gl,'glGetError',uint,[])
    def clean(): assert get_error()==0, 'GLES error'
    string=bind(gl,'glGetString',c.c_char_p,[uint]);print('EGL renderer:',string(0x1F01).decode(),';',string(0x1F02).decode())
    precision_range=(integer*2)();precision=integer()
    bind(gl,'glGetShaderPrecisionFormat',None,[uint,uint,c.POINTER(integer),c.POINTER(integer)])(0x8B30,0x8DF2,precision_range,c.byref(precision))
    assert precision.value>=23 and precision_range[0]>=126 and precision_range[1]>=127, 'full float32 fragment highp required'
    create_shader=bind(gl,'glCreateShader',uint,[uint])
    shader_source=bind(gl,'glShaderSource',None,[uint,integer,c.POINTER(c.c_char_p),ptr])
    compile_shader=bind(gl,'glCompileShader',None,[uint]);shader_iv=bind(gl,'glGetShaderiv',None,[uint,uint,c.POINTER(integer)])
    shader_log=bind(gl,'glGetShaderInfoLog',None,[uint,integer,ptr,ptr])
    delete_shader=bind(gl,'glDeleteShader',None,[uint])
    create_program=bind(gl,'glCreateProgram',uint,[]);attach=bind(gl,'glAttachShader',None,[uint,uint]);link=bind(gl,'glLinkProgram',None,[uint])
    program_iv=bind(gl,'glGetProgramiv',None,[uint,uint,c.POINTER(integer)]);program_log=bind(gl,'glGetProgramInfoLog',None,[uint,integer,ptr,ptr]);delete_program=bind(gl,'glDeleteProgram',None,[uint])
    use=bind(gl,'glUseProgram',None,[uint]);attrib=bind(gl,'glGetAttribLocation',integer,[uint,c.c_char_p]);enable=bind(gl,'glEnableVertexAttribArray',None,[uint]);disable=bind(gl,'glDisableVertexAttribArray',None,[uint])
    vertex_pointer=bind(gl,'glVertexAttribPointer',None,[uint,integer,uint,boolean,integer,ptr]);uniform=bind(gl,'glGetUniformLocation',integer,[uint,c.c_char_p]);uniform1=bind(gl,'glUniform1i',None,[integer,integer])
    gen=bind(gl,'glGenTextures',None,[integer,c.POINTER(uint)]);texbind=bind(gl,'glBindTexture',None,[uint,uint]);texparam=bind(gl,'glTexParameteri',None,[uint,uint,integer]);teximage=bind(gl,'glTexImage2D',None,[uint,integer,integer,integer,integer,integer,uint,uint,ptr]);deltex=bind(gl,'glDeleteTextures',None,[integer,c.POINTER(uint)])
    viewport=bind(gl,'glViewport',None,[integer,integer,integer,integer]);draw=bind(gl,'glDrawArrays',None,[uint,integer,integer]);read=bind(gl,'glReadPixels',None,[integer,integer,integer,integer,uint,uint,ptr])
    bind(gl,'glDisable',None,[uint])(0x0BD0) # deterministic byte tests: no dithering
    def compile_one(kind,source):
        shader=create_shader(kind);data=c.c_char_p(source);shader_source(shader,1,c.byref(data),None);compile_shader(shader)
        status=integer();shader_iv(shader,0x8B81,c.byref(status))
        if not status.value:
            log=c.create_string_buffer(8192);shader_log(shader,8192,None,log);raise AssertionError(log.value.decode())
        return shader
    try:
        with tempfile.TemporaryDirectory(prefix='owncut-gpu-') as directory:
            binary=Path(directory)/'probe'
            sp.run(['g++','-std=c++20','-O2','-Wall','-Wextra','-Wpedantic','-I'+str(ROOT/'engine/color/include'),
                str(ROOT/'engine/color/src/color.cpp'),str(ROOT/'engine/color/src/color_gpu.cpp'),str(ROOT/'tests/color_gpu_probe.cpp'),'-o',str(binary)],check=True)
            def probe(mode,case=0,encoding=0): return sp.check_output([str(binary),mode,str(case),str(encoding)])
            vs=compile_one(0x8B31,probe('vertex'))
            pixels=probe('input');data=(c.c_ubyte*len(pixels)).from_buffer_copy(pixels)
            texture=uint();gen(1,c.byref(texture));texbind(0x0DE1,texture.value)
            for key,value in [(0x2801,0x2600),(0x2800,0x2600),(0x2802,0x812F),(0x2803,0x812F)]:texparam(0x0DE1,key,value)
            teximage(0x0DE1,0,0x1908,32,16,0,0x1908,0x1401,data)
            positions=(c.c_float*8)(-1,-1,1,-1,-1,1,1,1);uv=(c.c_float*8)(0,0,1,0,0,1,1,1)
            viewport(0,0,32,16);worst=0
            for encoding in range(3):
                for case in range(32):
                    fs=compile_one(0x8B30,probe('fragment',case,encoding));program=create_program();attach(program,vs);attach(program,fs);link(program)
                    status=integer();program_iv(program,0x8B82,c.byref(status))
                    if not status.value:
                        log=c.create_string_buffer(8192);program_log(program,8192,None,log);raise AssertionError(log.value.decode())
                    use(program)
                    a=attrib(program,b'aPosition');t=attrib(program,b'aTexCoord');enable(a);enable(t)
                    vertex_pointer(a,2,0x1406,0,0,positions);vertex_pointer(t,2,0x1406,0,0,uv);uniform1(uniform(program,b'uSource'),0)
                    draw(5,0,4);output=(c.c_ubyte*len(pixels))();read(0,0,32,16,0x1908,0x1401,output);clean()
                    reference=probe('reference',case,encoding)
                    errors=[abs(x-y) for i,(x,y) in enumerate(zip(output,reference)) if i%4!=3]
                    error=max(errors);worst=max(worst,error)
                    assert error<=2, (encoding,case,error,[(i,output[i],reference[i]) for i in range(len(pixels)) if abs(output[i]-reference[i])>2][:12])
                    assert all(output[i]==reference[i] for i in range(3,len(pixels),4)), 'alpha changed'
                    if case in (0,4,8):assert bytes(output)==pixels, 'identity/bypass not exact'
                    disable(a);disable(t);delete_program(program);delete_shader(fs)
            delete_shader(vs);deltex(1,c.byref(texture))
            print('Production GLES/CPU: 96 encoding/grade cases, 512 pixels each; max RGB error',worst,'/255; exact alpha/identity/bypass passed')
    finally:
        make_current(display,None,None,None)
        bind(egl,'eglDestroySurface',uint,[ptr,ptr])(display,surface)
        bind(egl,'eglDestroyContext',uint,[ptr,ptr])(display,context)
        bind(egl,'eglTerminate',uint,[ptr])(display)
if __name__=='__main__':main()
