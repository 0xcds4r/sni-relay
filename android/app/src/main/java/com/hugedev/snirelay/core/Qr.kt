package com.hugedev.snirelay.core

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Shader
import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.qrcode.QRCodeWriter
import com.google.zxing.qrcode.decoder.ErrorCorrectionLevel

/** QR-код с логотипом (уровень H — центр безопасен для лого). */
object Qr {

    fun make(text: String, size: Int = 720, withLogo: Boolean = true): Bitmap? {
        return try {
            val hints = mapOf(
                EncodeHintType.ERROR_CORRECTION to ErrorCorrectionLevel.H,
                EncodeHintType.MARGIN to 1,
            )
            val matrix = QRCodeWriter().encode(text, BarcodeFormat.QR_CODE, size, size, hints)
            val bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
            val dark = Color.BLACK
            val light = Color.WHITE
            val px = IntArray(size * size)
            for (y in 0 until size) {
                val off = y * size
                for (x in 0 until size) {
                    px[off + x] = if (matrix.get(x, y)) dark else light
                }
            }
            bmp.setPixels(px, 0, size, 0, 0, size, size)
            if (withLogo) drawLogo(bmp)
            bmp
        } catch (_: Exception) {
            null
        }
    }

    private fun drawLogo(bmp: Bitmap) {
        val size = bmp.width
        val c = Canvas(bmp)
        val box = size * 24 / 100
        val left = (size - box) / 2f
        val top = (size - box) / 2f
        val bg = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE }
        c.drawRoundRect(left, top, left + box, top + box, box * 0.22f, box * 0.22f, bg)

        // Повторяем общий desktop/launcher знак: сине-фиолетовый rounded-square,
        // два белых узла и встречные стрелки.
        val inset = box * 0.055f
        val iconSize = box - inset * 2f
        val iconLeft = left + inset
        val iconTop = top + inset
        val iconRight = iconLeft + iconSize
        val iconBottom = iconTop + iconSize
        val iconBg = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            shader = LinearGradient(
                iconLeft, iconTop, iconRight, iconBottom,
                Color.rgb(0x2B, 0x6C, 0xFF), Color.rgb(0x7A, 0x3C, 0xFF),
                Shader.TileMode.CLAMP,
            )
        }
        c.drawRoundRect(iconLeft, iconTop, iconRight, iconBottom, iconSize * 0.203f, iconSize * 0.203f, iconBg)

        val white = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE }
        val centerY = iconTop + iconSize * 0.5f
        c.drawCircle(iconLeft + iconSize * (68f / 256f), centerY, iconSize * (15f / 256f), white)
        c.drawCircle(iconLeft + iconSize * (188f / 256f), centerY, iconSize * (15f / 256f), white)

        val arrows = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            style = Paint.Style.STROKE
            strokeWidth = iconSize * (11f / 256f)
            strokeCap = Paint.Cap.ROUND
            strokeJoin = Paint.Join.ROUND
        }
        val p = Path().apply {
            moveTo(iconLeft + iconSize * (92f / 256f), iconTop + iconSize * (110f / 256f))
            lineTo(iconLeft + iconSize * (160f / 256f), iconTop + iconSize * (110f / 256f))
            lineTo(iconLeft + iconSize * (145f / 256f), iconTop + iconSize * (97f / 256f))
            moveTo(iconLeft + iconSize * (160f / 256f), iconTop + iconSize * (110f / 256f))
            lineTo(iconLeft + iconSize * (145f / 256f), iconTop + iconSize * (123f / 256f))
            moveTo(iconLeft + iconSize * (164f / 256f), iconTop + iconSize * (146f / 256f))
            lineTo(iconLeft + iconSize * (96f / 256f), iconTop + iconSize * (146f / 256f))
            lineTo(iconLeft + iconSize * (111f / 256f), iconTop + iconSize * (133f / 256f))
            moveTo(iconLeft + iconSize * (96f / 256f), iconTop + iconSize * (146f / 256f))
            lineTo(iconLeft + iconSize * (111f / 256f), iconTop + iconSize * (159f / 256f))
        }
        c.drawPath(p, arrows)
    }
}
