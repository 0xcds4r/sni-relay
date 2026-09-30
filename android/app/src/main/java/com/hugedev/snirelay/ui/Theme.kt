package com.hugedev.snirelay.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

val Bg = Color(0xFF0F1014)
val Card = Color(0xFF1A1B21)
val CardVariant = Color(0xFF23252E)
val Border = Color(0xFF33353F)
val TextMain = Color(0xFFE8E8EA)
val TextDim = Color(0xFF9AA0AE)
val Accent = Color(0xFF4C8DFF)
val Ok = Color(0xFF3FBF7F)
val Warn = Color(0xFFE0A94A)
val Danger = Color(0xFFE5534B)

private val scheme = darkColorScheme(
    primary = Accent,
    onPrimary = Color.White,
    secondary = Accent,
    background = Bg,
    onBackground = TextMain,
    surface = Card,
    onSurface = TextMain,
    surfaceVariant = CardVariant,
    onSurfaceVariant = TextDim,
    outline = Border,
    error = Danger,
    onError = Color.White,
)

@Composable
fun SniRelayTheme(content: @Composable () -> Unit) {
    @Suppress("UNUSED_EXPRESSION") isSystemInDarkTheme()
    MaterialTheme(colorScheme = scheme, content = content)
}
