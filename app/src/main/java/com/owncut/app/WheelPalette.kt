package com.owncut.app

/** Clockwise Android screen angles match native (x, -x/2-sqrt(3)y/2, -x/2+sqrt(3)y/2).
 * RGB-primary direction is red right, blue lower-left, green upper-left.
 */
internal object WheelPalette {
    fun colors()=intArrayOf(0xffe66c6c.toInt(),0xffe66ce6.toInt(),0xff6c6ce6.toInt(),
        0xff6ce6e6.toInt(),0xff6ce66c.toInt(),0xffe6e66c.toInt(),0xffe66c6c.toInt())
}
