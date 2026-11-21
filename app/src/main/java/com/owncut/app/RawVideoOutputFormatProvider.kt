package com.owncut.app

import android.media.MediaFormat

/** Optional capability of our future evidence-preserving Codec adapter, not Media3's
 * default Codec. Called on the codec owner thread, with the current output buffer held.
 * Must return raw evidence for THAT buffer's format epoch without dequeuing/advancing.
 * Null means unavailable, not permission to fall back to a lossy Format descriptor.
 * Consumers must normalize/validate immediately; never cache this mutable MediaFormat.
 * No reflection/private Media3 API is permitted to implement this capability.
 */
internal interface RawVideoOutputFormatProvider {
    fun rawOutputFormatForCurrentBuffer(): MediaFormat?
}
