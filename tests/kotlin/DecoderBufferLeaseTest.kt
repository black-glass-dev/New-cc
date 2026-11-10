package com.owncut.app

fun main() {
    fun rejected(block: () -> Unit) { check(runCatching(block).isFailure) }
    val lease=DecoderBufferLease()
    rejected { lease.acquire(0,false) }
    lease.formatChanged();check(lease.epoch==1L)
    lease.acquire(7,false)
    check(lease.requireHeld()==7)
    rejected { lease.formatChanged() };rejected { lease.acquire(8,false) }
    lease.releaseHeld();rejected { lease.releaseHeld() }
    repeat(10000) { i ->
        lease.formatChanged();lease.acquire(i,false);check(lease.requireHeld()==i);lease.releaseHeld()
    }
    lease.inputQueued(false);lease.inputQueued(true)
    rejected { lease.inputQueued(false) }
    lease.acquire(3,true);check(!lease.ended)
    lease.releaseHeld();check(lease.ended)
    rejected { lease.acquire(4,false) };rejected { lease.formatChanged() }
    lease.close();lease.close();rejected { lease.checkOpen() }
    val failed=DecoderBufferLease();failed.formatChanged();failed.fail()
    rejected { failed.acquire(0,false) };rejected { failed.formatChanged() }
    failed.close()
    val empty=DecoderBufferLease()
    rejected { empty.emptyEnd() }
    empty.formatChanged();rejected { empty.acquire(0,true) }
    empty.inputQueued(true);empty.emptyEnd();check(empty.ended)
    rejected { empty.emptyEnd() }
    rejected { empty.acquire(0,false) }
    println("Production decoder lease: 10000 epochs, held-buffer isolation, input/output EOS and terminal fail/close PASS")
}
