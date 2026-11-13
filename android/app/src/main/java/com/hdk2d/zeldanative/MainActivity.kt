package com.hdk2d.zeldanative

import android.app.Activity
import android.os.Bundle
import android.view.Gravity
import android.widget.LinearLayout
import android.widget.TextView

class MainActivity : Activity() {
    private external fun nativeStatus(): String

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        System.loadLibrary("zelda_native")

        val title = TextView(this).apply {
            text = "Zelda Native"
            textSize = 28f
            gravity = Gravity.CENTER
        }
        val status = TextView(this).apply {
            text = nativeStatus()
            textSize = 16f
            gravity = Gravity.CENTER
            setPadding(24, 24, 24, 24)
        }
        val note = TextView(this).apply {
            text = "Base de desarrollo: el juego todavía no está implementado.\nPort nativo C++20/NDK; no utiliza emulador."
            textSize = 14f
            gravity = Gravity.CENTER
            setPadding(24, 16, 24, 24)
        }

        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(24, 24, 24, 24)
            addView(title)
            addView(status)
            addView(note)
        })
    }

    companion object {
        init {
            // La biblioteca se carga en onCreate para mostrar errores de carga claramente.
        }
    }
}
