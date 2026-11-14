package com.hdk2d.zeldanative

import android.app.Activity
import android.content.Intent
import android.graphics.Bitmap
import android.widget.ImageView
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

class MainActivity : Activity() {
    private external fun nativeStatus(): String
    private external fun nativeLoadRom(data: ByteArray): String
    private external fun nativeLinkPixels(): IntArray?
    private external fun nativeBackgroundPixels(): IntArray?
    private external fun nativeBackgroundBrownPixels(): IntArray?

    private lateinit var status: TextView
    private lateinit var linkPreview: ImageView
    private lateinit var backgroundPreview: ImageView
    private lateinit var backgroundBrownPreview: ImageView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        System.loadLibrary("zelda_native")

        val title = TextView(this).apply {
            text = "Zelda Native"
            textSize = 28f
            gravity = Gravity.CENTER
        }
        val description = TextView(this).apply {
            text = "Port nativo C++20 para Android\nSin emulador ni Libretro"
            textSize = 15f
            gravity = Gravity.CENTER
            setPadding(16, 12, 16, 20)
        }
        val openButton = Button(this).apply {
            text = "Seleccionar archivo .nes"
            setOnClickListener { openRomPicker() }
        }
        status = TextView(this).apply {
            text = nativeStatus()
            textSize = 14f
            setTextIsSelectable(true)
            setPadding(16, 20, 16, 20)
        }
        val linkTitle = TextView(this).apply {
            text = "Vista de Link (patrones originales)"
            textSize = 18f
            gravity = Gravity.CENTER
            setPadding(8, 20, 8, 8)
        }
        linkPreview = ImageView(this).apply {
            scaleType = ImageView.ScaleType.FIT_CENTER
            setBackgroundColor(0xFF202020.toInt())
            setPadding(24, 24, 24, 24)
        }
        val backgroundTitle = TextView(this).apply {
            text = "Patrones originales del ambiente (tiles 00–FF)"
            textSize = 18f
            gravity = Gravity.CENTER
            setPadding(8, 24, 8, 8)
        }
        backgroundPreview = ImageView(this).apply {
            scaleType = ImageView.ScaleType.FIT_CENTER
            setBackgroundColor(0xFF202020.toInt())
            setPadding(12, 12, 12, 12)
        }
        val brownTitle = TextView(this).apply {
            text = "Mismos patrones con paleta marrón de montaña"
            textSize = 16f
            gravity = Gravity.CENTER
            setPadding(8, 12, 8, 8)
        }
        backgroundBrownPreview = ImageView(this).apply {
            scaleType = ImageView.ScaleType.FIT_CENTER
            setBackgroundColor(0xFF202020.toInt())
            setPadding(12, 12, 12, 12)
        }
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(20, 28, 20, 20)
            addView(title, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            addView(description, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            addView(openButton, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            addView(linkTitle, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            addView(linkPreview, ViewGroup.LayoutParams.MATCH_PARENT, 360)
            addView(backgroundTitle, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            addView(backgroundPreview, ViewGroup.LayoutParams.MATCH_PARENT, 300)
            addView(brownTitle, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            addView(backgroundBrownPreview, ViewGroup.LayoutParams.MATCH_PARENT, 300)
            addView(status, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        }
        setContentView(ScrollView(this).apply { addView(content) })
    }

    private fun openRomPicker() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/octet-stream", "application/x-nes"))
        }
        startActivityForResult(intent, REQUEST_ROM)
    }

    @Deprecated("Use Activity Result APIs when AndroidX is added")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQUEST_ROM || resultCode != RESULT_OK) return
        val uri: Uri = data?.data ?: return
        status.text = "Leyendo archivo seleccionado…"
        try {
            val bytes = contentResolver.openInputStream(uri)?.use { it.readBytes() }
                ?: throw IllegalStateException("No se pudo abrir el archivo.")
            status.text = nativeLoadRom(bytes)
            val pixels = nativeLinkPixels()
            if (pixels != null && pixels.size == 16 * 16) {
                val bitmap = Bitmap.createBitmap(pixels, 16, 16, Bitmap.Config.ARGB_8888)
                linkPreview.setImageBitmap(bitmap)
            } else {
                linkPreview.setImageDrawable(null)
            }
            val backgroundPixels = nativeBackgroundPixels()
            if (backgroundPixels != null && backgroundPixels.size == 128 * 128) {
                val backgroundBitmap = Bitmap.createBitmap(
                    backgroundPixels, 128, 128, Bitmap.Config.ARGB_8888
                )
                backgroundPreview.setImageBitmap(backgroundBitmap)
            } else {
                backgroundPreview.setImageDrawable(null)
            }
            val brownPixels = nativeBackgroundBrownPixels()
            if (brownPixels != null && brownPixels.size == 128 * 128) {
                val brownBitmap = Bitmap.createBitmap(
                    brownPixels, 128, 128, Bitmap.Config.ARGB_8888
                )
                backgroundBrownPreview.setImageBitmap(brownBitmap)
            } else {
                backgroundBrownPreview.setImageDrawable(null)
            }
        } catch (e: Exception) {
            status.text = "Error al leer el archivo: ${e.message ?: "desconocido"}"
        }
    }

    companion object {
        private const val REQUEST_ROM = 4101
        init {
            // La biblioteca nativa se carga en onCreate.
        }
    }
}
