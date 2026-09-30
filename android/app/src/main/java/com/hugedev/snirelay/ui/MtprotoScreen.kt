@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.hugedev.snirelay.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.hugedev.snirelay.core.Gen
import com.hugedev.snirelay.core.Qr

private fun copyToClipboard(ctx: Context, label: String, text: String) {
    val cm = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
    cm.setPrimaryClip(ClipData.newPlainText(label, text))
}

@Composable
fun MtprotoScreen(state: AppState) {
    val ctx = LocalContext.current
    var showSecret by remember { mutableStateOf(false) }
    var showQr by remember { mutableStateOf(false) }
    var frontMenu by remember { mutableStateOf(false) }
    val s = state.settings

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        SectionCard("Статус и подключение") {
            Text(state.mtgStatus, color = TextMain, fontSize = 14.sp)
            RowSpace {
                ActionButton("Показать QR", { showQr = true })
                ActionButton("Копировать ссылку", {
                    if (s.host.isNotBlank() && s.mtgSecret.isNotBlank()) {
                        copyToClipboard(ctx, "tg", Gen.tgProxyUrl(s.host, s.effectiveMtgPort(), s.mtgSecret))
                        state.logLine("[OK] ссылка скопирована")
                    } else state.logLine("[!] нужны хост, порт и секрет")
                })
                ActionButton("Добавить в Telegram", {
                    if (s.host.isNotBlank() && s.mtgSecret.isNotBlank()) {
                        val url = Gen.tgQrUrl(s.host, s.effectiveMtgPort(), s.mtgSecret)
                        try { ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
                        catch (_: Exception) { state.logLine("[!] Telegram не установлен — используйте ссылку/QR") }
                    }
                })
            }
        }
        SectionCard("Параметры") {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = s.mtgVia443, onCheckedChange = { c -> state.update { mtgVia443 = c } })
                Text("Слушать на 443 через релей (рекомендуется)", color = TextMain)
            }
            LabeledField("Порт прокси", s.mtgPort, { v -> state.update { mtgPort = v } })
            Column {
                OutlinedTextField(
                    value = s.mtgFront,
                    onValueChange = { v -> state.update { mtgFront = v } },
                    label = { Text("Фронт-домен") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                TextButton(onClick = { frontMenu = true }) { Text("Популярные домены ▾") }
                DropdownMenu(expanded = frontMenu, onDismissRequest = { frontMenu = false }) {
                    listOf("www.google.com", "www.cloudflare.com", "www.bing.com", "www.apple.com").forEach {
                        DropdownMenuItem(text = { Text(it) }, onClick = { state.update { mtgFront = it }; frontMenu = false })
                    }
                }
            }
            OutlinedTextField(
                value = s.mtgSecret,
                onValueChange = { v -> state.update { mtgSecret = v } },
                label = { Text("Secret") },
                singleLine = true,
                visualTransformation = if (showSecret) androidx.compose.ui.text.input.VisualTransformation.None else PasswordVisualTransformation(),
                textStyle = androidx.compose.material3.MaterialTheme.typography.bodyMedium.copy(fontFamily = FontFamily.Monospace),
                modifier = Modifier.fillMaxWidth(),
            )
            RowSpace {
                ActionButton(if (showSecret) "Скрыть" else "Показать", { showSecret = !showSecret })
                ActionButton("Копировать", {
                    if (s.mtgSecret.isNotBlank()) { copyToClipboard(ctx, "secret", s.mtgSecret); state.logLine("[OK] секрет скопирован") }
                })
            }
        }
        SectionCard("Управление сервисом") {
            ActionButton(
                if (state.mtgInstalled && state.mtgHasUnit) "Обновить mtg и сервис" else "Установить и развернуть",
                { state.mtgInstallDeploy() }, primary = true,
            )
            RowSpace {
                ActionButton("Сгенерировать секрет", { state.mtgGenSecret() }, enabled = state.mtgInstalled)
            }
            RowSpace {
                ActionButton("Старт", { state.mtgStart() }, enabled = state.mtgHasUnit && !state.mtgActive)
                ActionButton("Стоп", { state.mtgStop() }, enabled = state.mtgHasUnit && state.mtgActive)
                ActionButton("Рестарт", { state.mtgRestart() }, enabled = state.mtgHasUnit)
                ActionButton("Обновить статус", { state.refreshMtgStatus() })
            }
            ActionButton("Удалить mtg и сервис", { state.mtgRemove() }, danger = true, enabled = state.mtgInstalled || state.mtgHasUnit)
        }
    }

    if (showQr) {
        val bmp = remember(s.host, s.mtgSecret, s.mtgPort, s.mtgVia443) {
            if (s.host.isNotBlank() && s.mtgSecret.isNotBlank())
                Qr.make(Gen.tgQrUrl(s.host, s.effectiveMtgPort(), s.mtgSecret), 720)
            else null
        }
        AlertDialog(
            onDismissRequest = { showQr = false },
            title = { Text("QR для Telegram") },
            text = {
                Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.fillMaxWidth()) {
                    if (bmp != null) Image(bmp.asImageBitmap(), contentDescription = "QR", modifier = Modifier.size(300.dp))
                    else Text("Нужны хост, порт и секрет", color = Danger)
                    Text(Gen.tgProxyUrl(s.host, s.effectiveMtgPort(), s.mtgSecret), color = TextDim, fontSize = 12.sp)
                }
            },
            confirmButton = { TextButton(onClick = { showQr = false }) { Text("Закрыть") } },
        )
    }
}

@Composable
fun IntegrationsScreen(state: AppState) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        SectionCard("Порты на VDS") {
            Text("Проверь занятые порты и подбери свободный для mtg.", color = TextDim, fontSize = 13.sp)
            RowSpace {
                ActionButton("Показать занятые порты", {
                    state.logLine("→ занятые порты")
                    state.runRaw("ss -tlnp 2>/dev/null | awk '{print \$4}' | sort -u | head -40")
                })
                ActionButton("Свободный порт для mtg", {
                    state.runRaw("for p in \$(seq 20000 20100); do ss -tln | grep -q \":\$p \" || { echo \"FREE \$p\"; break; }; done")
                })
            }
        }
        SectionCard("Браузер и zapret") {
            Text(
                "Отключение AsyncDns в Chrome и пересечения с zapret-списками доступны только в десктопной версии (Linux). " +
                    "На Android DNS решается через локальный VPN (вкладка «Клиент»).",
                color = TextDim, fontSize = 13.sp,
            )
        }
    }
}
