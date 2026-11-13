package com.hdk2d.zeldanative

import android.app.Activity
import android.content.Intent
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

    private lateinit var status: TextView

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
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(20, 28, 20, 20)
            addView(title, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            addView(description, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            addView(openButton, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
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
