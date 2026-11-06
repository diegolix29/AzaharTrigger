// Copyright AzalonDS Project
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.ds

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.drawable.BitmapDrawable
import android.view.View
import androidx.core.content.ContextCompat
import kotlin.math.abs
import org.citra.citra_emu.R

/**
 * On-screen DS controls: D-pad, the four face buttons, L/R and Start/Select.
 *
 * This view only draws and hit-tests; it never consumes touches itself.
 * [DsEmulationActivity] runs one touch listener for the whole screen, so a finger
 * on the touch screen and a finger on a button work at the same time, and asks
 * this view which keys a point is over through [keysAt].
 */
class DsInputOverlay(context: Context) : View(context) {

    private class Control(
        val keys: Int,
        val normal: Bitmap?,
        val pressed: Bitmap?,
        val bounds: Rect = Rect()
    )

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { alpha = OPACITY }
    private val controls = mutableListOf<Control>()
    private var dpad: Control? = null

    /** Keys currently held through this overlay, for the pressed-state drawing. */
    private var heldKeys = 0

    init {
        isClickable = false
        isFocusable = false
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        buildControls(w, h)
    }

    /** Which DS keys, if any, the given point activates. */
    fun keysAt(x: Int, y: Int): Int {
        var keys = 0
        dpad?.let { pad ->
            if (pad.bounds.contains(x, y)) {
                keys = keys or dpadDirection(pad.bounds, x, y)
            }
        }
        for (control in controls) {
            if (control.bounds.contains(x, y)) {
                keys = keys or control.keys
            }
        }
        return keys
    }

    /** Tells the overlay which keys are held, so it can draw them pressed. */
    fun setHeldKeys(keys: Int) {
        if (keys != heldKeys) {
            heldKeys = keys
            invalidate()
        }
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        dpad?.let { draw(canvas, it, (heldKeys and DPAD_KEYS) != 0) }
        for (control in controls) {
            draw(canvas, control, (heldKeys and control.keys) != 0)
        }
    }

    private fun draw(canvas: Canvas, control: Control, pressed: Boolean) {
        val bitmap = (if (pressed) control.pressed else control.normal) ?: return
        canvas.drawBitmap(bitmap, null, control.bounds, paint)
    }

    /**
     * Eight-way D-pad: the touch position inside the pad decides the direction,
     * with a dead zone in the middle and diagonals near the corners.
     */
    private fun dpadDirection(bounds: Rect, x: Int, y: Int): Int {
        val dx = (x - bounds.centerX()).toFloat() / (bounds.width() / 2f)
        val dy = (y - bounds.centerY()).toFloat() / (bounds.height() / 2f)
        if (abs(dx) < DEAD_ZONE && abs(dy) < DEAD_ZONE) return 0

        var keys = 0
        // A direction counts when it dominates, or when both are large (diagonal).
        if (dx <= -DEAD_ZONE && abs(dx) > abs(dy) * DIAGONAL) keys = keys or DsNative.Key.LEFT
        if (dx >= DEAD_ZONE && abs(dx) > abs(dy) * DIAGONAL) keys = keys or DsNative.Key.RIGHT
        if (dy <= -DEAD_ZONE && abs(dy) > abs(dx) * DIAGONAL) keys = keys or DsNative.Key.UP
        if (dy >= DEAD_ZONE && abs(dy) > abs(dx) * DIAGONAL) keys = keys or DsNative.Key.DOWN
        return keys
    }

    private fun buildControls(width: Int, height: Int) {
        controls.clear()
        dpad = null
        if (width <= 0 || height <= 0) return

        val portrait = height >= width
        // One scale for everything, so the pad keeps its proportions on any screen.
        val unit = (if (portrait) width else height).toFloat()
        val buttonSize = (unit * (if (portrait) 0.13f else 0.17f)).toInt()
        val dpadSize = (unit * (if (portrait) 0.34f else 0.44f)).toInt()
        val smallSize = (unit * (if (portrait) 0.16f else 0.20f)).toInt()

        // In portrait the controls live in the band DsScreensView keeps free below
        // the screens; in landscape they sit over the screens' left and right edges.
        val padCenterY = (height * (if (portrait) 0.855f else 0.68f)).toInt()
        val padCenterX = (width * (if (portrait) 0.22f else 0.11f)).toInt()
        val faceCenterX = (width * (if (portrait) 0.78f else 0.89f)).toInt()

        dpad = Control(
            keys = DPAD_KEYS,
            normal = bitmap(R.drawable.dpad),
            pressed = bitmap(R.drawable.dpad_pressed_one_direction)
        ).also { centerAt(it.bounds, padCenterX, padCenterY, dpadSize, dpadSize) }

        // A/B/X/Y in the usual diamond: X on top, B at the bottom, Y left, A right.
        val spread = (buttonSize * 0.78f).toInt()
        add(DsNative.Key.X, R.drawable.button_x, R.drawable.button_x_pressed,
            faceCenterX, padCenterY - spread, buttonSize)
        add(DsNative.Key.B, R.drawable.button_b, R.drawable.button_b_pressed,
            faceCenterX, padCenterY + spread, buttonSize)
        add(DsNative.Key.Y, R.drawable.button_y, R.drawable.button_y_pressed,
            faceCenterX - spread, padCenterY, buttonSize)
        add(DsNative.Key.A, R.drawable.button_a, R.drawable.button_a_pressed,
            faceCenterX + spread, padCenterY, buttonSize)

        // Shoulder buttons above the thumbs, still inside the controls band.
        val shoulderY = (height * (if (portrait) 0.715f else 0.16f)).toInt()
        add(DsNative.Key.L, R.drawable.button_l, R.drawable.button_l_pressed,
            (width * 0.12f).toInt(), shoulderY, smallSize)
        add(DsNative.Key.R, R.drawable.button_r, R.drawable.button_r_pressed,
            (width * 0.88f).toInt(), shoulderY, smallSize)

        // Start and Select at the bottom, between the thumbs.
        val startY = (height * (if (portrait) 0.965f else 0.93f)).toInt()
        add(DsNative.Key.SELECT, R.drawable.button_select, R.drawable.button_select_pressed,
            (width * 0.42f).toInt(), startY, smallSize)
        add(DsNative.Key.START, R.drawable.button_start, R.drawable.button_start_pressed,
            (width * 0.58f).toInt(), startY, smallSize)

        invalidate()
    }

    @Suppress("LongParameterList")
    private fun add(
        key: Int,
        normalId: Int,
        pressedId: Int,
        centerX: Int,
        centerY: Int,
        size: Int
    ) {
        val normal = bitmap(normalId)
        val pressed = bitmap(pressedId)
        // Start/Select are wider than they are tall.
        val height = if (normal != null && normal.width > 0) {
            size * normal.height / normal.width
        } else {
            size
        }
        val control = Control(key, normal, pressed)
        centerAt(control.bounds, centerX, centerY, size, height)
        controls.add(control)
    }

    private fun centerAt(bounds: Rect, centerX: Int, centerY: Int, width: Int, height: Int) {
        bounds.set(
            centerX - width / 2,
            centerY - height / 2,
            centerX + width / 2,
            centerY + height / 2
        )
    }

    private fun bitmap(resourceId: Int): Bitmap? {
        if (resourceId == 0) return null
        val drawable = ContextCompat.getDrawable(context, resourceId)
        return (drawable as? BitmapDrawable)?.bitmap
    }

    companion object {
        private const val OPACITY = 170
        private const val DEAD_ZONE = 0.22f
        private const val DIAGONAL = 0.45f

        private const val DPAD_KEYS =
            DsNative.Key.UP or DsNative.Key.DOWN or DsNative.Key.LEFT or DsNative.Key.RIGHT
    }
}
