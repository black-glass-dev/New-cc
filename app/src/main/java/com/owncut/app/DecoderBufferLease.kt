package com.owncut.app

/** Pure codec transport state. One held output buffer binds one immutable format epoch.
 * Fail/close are terminal; EOS is complete only after the last held buffer is released.
 */
internal class DecoderBufferLease {
    var heldIndex=-1; private set
    var epoch=0L; private set
    var ended=false; private set
    var inputEnded=false; private set
    private var heldEos=false
    private var terminal=false
    fun checkOpen() { check(!terminal) { "Decoder lease is terminal" } }
    fun formatChanged() {
        checkOpen();check(heldIndex<0 && !ended)
        epoch=Math.addExact(epoch,1L)
    }
    fun acquire(index: Int, eos: Boolean) {
        checkOpen();check(!ended && heldIndex<0 && epoch>0);require(index>=0)
        check(!eos || inputEnded) { "Unexpected output EOS before input EOS" }
        heldIndex=index;heldEos=eos
    }
    fun requireHeld(): Int { checkOpen();check(heldIndex>=0);return heldIndex }
    fun releaseHeld() {
        requireHeld();heldIndex=-1
        if(heldEos) ended=true
        heldEos=false
    }
    fun emptyEnd() { checkOpen();check(heldIndex<0 && !ended && inputEnded);ended=true }
    fun inputQueued(eos: Boolean) { checkOpen();check(!inputEnded);inputEnded=eos }
    fun fail() { terminal=true }
    fun close() { terminal=true;heldIndex=-1 }
}
