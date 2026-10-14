@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.hugedev.snirelay.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
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
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.hugedev.snirelay.core.Gen
import com.hugedev.snirelay.core.Qr

private fun copyText(ctx: Context, label: String, text: String) {
    val cm = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
    cm.setPrimaryClip(ClipData.newPlainText(label, text))
}

@Composable
fun RealityScreen(state: AppState) {
    val ctx = LocalContext.current
    var showPriv by remember { mutableStateOf(false) }
    var showQr by remember { mutableStateOf(false) }
    var destMenu by remember { mutableStateOf(false) }
    var flowMenu by remember { mutableStateOf(false) }
    var fpMenu by remember { mutableStateOf(false) }
    var transportMenu by remember { mutableStateOf(false) }
    val s = state.settings

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        SectionCard("Подключение") {
            Text(state.realityStatus, color = TextMain, fontSize = 14.sp)
            RowSpace {
                ActionButton("Подключиться", { state.realityOpenInClient() }, primary = true)
                ActionButton("Копировать vless://", {
                    if (s.host.isNotBlank() && s.realityUuid.isNotBlank()) {
                        copyText(ctx, "vless", Gen.realityClientLink(s))
                        state.logLine("[OK] ссылка скопирована")
                    } else state.logLine("[!] нужны хост VDS и UUID")
                })
                ActionButton("Показать QR", { showQr = true })
                ActionButton("Проверить IP", { state.realityCheckIp() })
            }
            Text(
                "«Подключиться» открывает ссылку vless:// в установленном клиенте (v2rayNG / Nekoray / sing-box). " +
                    "Включение/отключение VPN — в самом клиенте. «Проверить IP» покажет, идёт ли трафик через VDS.",
                color = TextDim, fontSize = 12.sp,
            )
        }
        SectionCard("Параметры") {
            Column {
                OutlinedTextField(
                    value = if (s.realityTransport == "grpc") "gRPC + TLS (Cloudflare)" else "Reality (TCP)",
                    onValueChange = {},
                    readOnly = true,
                    label = { Text("Транспорт") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                TextButton(onClick = { transportMenu = true }) { Text("Сменить ▾") }
                DropdownMenu(expanded = transportMenu, onDismissRequest = { transportMenu = false }) {
                    DropdownMenuItem(text = { Text("Reality (TCP)") }, onClick = { state.update { realityTransport = "reality" }; transportMenu = false })
                    DropdownMenuItem(text = { Text("gRPC + TLS (Cloudflare)") }, onClick = { state.update { realityTransport = "grpc" }; transportMenu = false })
                }
            }
            LabeledField("Домен (Cloudflare)", s.realityDomain, { v -> state.update { realityDomain = v } })
            LabeledField("serviceName", s.realityServiceName, { v -> state.update { realityServiceName = v } })
            LabeledField("Порт origin (gRPC)", s.realityGrpcPort, { v -> state.update { realityGrpcPort = v } })
            LabeledField("Порт (Reality)", s.realityPort, { v -> state.update { realityPort = v } })
            Column {
                OutlinedTextField(
                    value = s.realityDest,
                    onValueChange = { v -> state.update { realityDest = v } },
                    label = { Text("Маскировочный домен") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                TextButton(onClick = { destMenu = true }) { Text("Популярные домены ▾") }
                DropdownMenu(expanded = destMenu, onDismissRequest = { destMenu = false }) {
                    listOf(
                        "dl.google.com", "www.microsoft.com", "www.samsung.com",
                        "www.lovelive-anime.jp", "www.apple.com", "www.cloudflare.com",
                    ).forEach {
                        DropdownMenuItem(text = { Text(it) }, onClick = { state.update { realityDest = it }; destMenu = false })
                    }
                }
            }
            Column {
                OutlinedTextField(
                    value = s.realityFlow,
                    onValueChange = { v -> state.update { realityFlow = v } },
                    label = { Text("Flow") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                TextButton(onClick = { flowMenu = true }) { Text("Выбрать ▾") }
                DropdownMenu(expanded = flowMenu, onDismissRequest = { flowMenu = false }) {
                    listOf("xtls-rprx-vision", "none").forEach {
                        DropdownMenuItem(text = { Text(it) }, onClick = { state.update { realityFlow = it }; flowMenu = false })
                    }
                }
            }
            Column {
                OutlinedTextField(
                    value = s.realityFingerprint,
                    onValueChange = { v -> state.update { realityFingerprint = v } },
                    label = { Text("Fingerprint") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                TextButton(onClick = { fpMenu = true }) { Text("Выбрать ▾") }
                DropdownMenu(expanded = fpMenu, onDismissRequest = { fpMenu = false }) {
                    listOf("chrome", "firefox", "safari", "ios", "android", "edge", "random", "randomized").forEach {
                        DropdownMenuItem(text = { Text(it) }, onClick = { state.update { realityFingerprint = it }; fpMenu = false })
                    }
                }
            }
            LabeledField("UUID", s.realityUuid, { v -> state.update { realityUuid = v } }, mono = true)
            LabeledField("Public key (pbk)", s.realityPublicKey, { v -> state.update { realityPublicKey = v } }, mono = true)
            OutlinedTextField(
                value = s.realityPrivateKey,
                onValueChange = { v -> state.update { realityPrivateKey = v } },
                label = { Text("Private key (сервер)") },
                singleLine = true,
                visualTransformation = if (showPriv) VisualTransformation.None else PasswordVisualTransformation(),
                textStyle = androidx.compose.material3.MaterialTheme.typography.bodyMedium.copy(fontFamily = FontFamily.Monospace),
                modifier = Modifier.fillMaxWidth(),
            )
            RowSpace { ActionButton(if (showPriv) "Скрыть" else "Показать", { showPriv = !showPriv }) }
            LabeledField("Short id", s.realityShortId, { v -> state.update { realityShortId = v } }, mono = true)
            Text(
                "gRPC+TLS за Cloudflare: A-запись домена → IP VDS, Proxy ON (оранжевое облако), " +
                    "SSL/TLS = Flexible, Network → gRPC = ON. Клиент идёт на домен:443; origin слушает порт выше. " +
                    "Для Reality поля «домен/serviceName/порт gRPC» не нужны.",
                color = TextDim, fontSize = 12.sp,
            )
        }
        SectionCard("Управление сервисом") {
            ActionButton(
                if (state.realityInstalled && state.realityHasUnit) "Обновить Xray и сервис" else "Установить Xray и развернуть",
                { state.realityInstallDeploy() }, primary = true,
            )
            RowSpace { ActionButton("Сгенерировать ключи", { state.realityGenKeys() }, enabled = state.realityInstalled) }
            RowSpace {
                ActionButton("Старт", { state.realityStart() }, enabled = state.realityHasUnit && !state.realityActive)
                ActionButton("Стоп", { state.realityStop() }, enabled = state.realityHasUnit && state.realityActive)
                ActionButton("Рестарт", { state.realityRestart() }, enabled = state.realityHasUnit)
                ActionButton("Обновить статус", { state.refreshRealityStatus() })
            }
            ActionButton("Удалить Xray и сервис", { state.realityRemove() }, danger = true, enabled = state.realityInstalled || state.realityHasUnit)
        }
        SectionCard("Как пользоваться") {
            Text(
                "VLESS + Reality обходит блокировку по SNI (в отличие от SNI-релея): провайдер видит обычный " +
                    "TLS к маскировочному домену, а настоящий адрес — внутри шифра. Работает для X, Instagram, " +
                    "Facebook и др. Установи клиент (v2rayNG / Nekoray / sing-box), отсканируй QR или вставь " +
                    "ссылку vless:// выше.",
                color = TextDim, fontSize = 13.sp,
            )
        }
    }

    if (showQr) {
        val bmp = remember(s.host, s.realityUuid, s.realityPublicKey, s.realityShortId, s.realityPort, s.realityDest) {
            if (s.host.isNotBlank() && s.realityUuid.isNotBlank() && s.realityPublicKey.isNotBlank())
                Qr.make(Gen.realityClientLink(s), 720)
            else null
        }
        AlertDialog(
            onDismissRequest = { showQr = false },
            title = { Text("QR для клиента (vless://)") },
            text = {
                Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.fillMaxWidth()) {
                    if (bmp != null) Image(bmp.asImageBitmap(), contentDescription = "QR", modifier = Modifier.size(300.dp))
                    else Text("Нужны хост VDS, UUID и public key", color = Danger)
                    Text(Gen.realityClientLink(s), color = TextDim, fontSize = 12.sp)
                }
            },
            confirmButton = { TextButton(onClick = { showQr = false }) { Text("Закрыть") } },
        )
    }
}
