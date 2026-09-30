package com.hugedev.snirelay.core

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
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

        val circle = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#4C8DFF") }
        c.drawCircle(size / 2f, size / 2f, box * 0.36f, circle)

        val arrow = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.parseColor("#0F1014")
            style = Paint.Style.FILL
        }
        val p = Path()
        val cx = size / 2f
        val cy = size / 2f
        val s = box * 0.16f
        p.moveTo(cx - s, cy - s)
        p.lineTo(cx + s * 0.2f, cy - s)
        p.lineTo(cx + s * 0.2f, cy - s * 1.6f)
        p.lineTo(cx + s * 1.6f, cy)
        p.lineTo(cx + s * 0.2f, cy + s * 1.6f)
        p.lineTo(cx + s * 0.2f, cy + s)
        p.lineTo(cx - s, cy + s)
        p.close()
        c.drawPath(p, arrow)
    }
}
