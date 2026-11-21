package org.owncolor;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
/** CPU reference only; Vulkan dispatch/device integration is intentionally not advertised. */
public final class ColorEngine implements AutoCloseable {
    static { System.loadLibrary("own_color_jni"); }
    private long handle;
    public ColorEngine(long budgetBytes, int tileEdge) { handle=nativeCreateCPU(budgetBytes,tileEdge); }
    public synchronized void addExposure(long id,long input,float stops) { nativeExposure(handle,id,input,stops); }
    public synchronized void renderCPU(ByteBuffer source,ByteBuffer destination,int width,int height,long sourceStrideFloats,long destinationStrideFloats,double time) {
        if(source==null||destination==null||!source.isDirect()||!destination.isDirect()||destination.isReadOnly()||source.order()!=ByteOrder.nativeOrder()||destination.order()!=ByteOrder.nativeOrder()) throw new IllegalArgumentException("Native-order direct buffers required");
        nativeRenderCPU(handle,source,destination,width,height,sourceStrideFloats,destinationStrideFloats,time);
    }
    @Override public synchronized void close() { if(handle!=0) { nativeClose(handle);handle=0; } }
    private static native long nativeCreateCPU(long budget,int tile);
    private static native void nativeClose(long handle);
    private static native void nativeExposure(long handle,long id,long input,float stops);
    private static native void nativeRenderCPU(long handle,ByteBuffer source,ByteBuffer destination,int w,int h,long sourceStride,long destinationStride,double time);
}
