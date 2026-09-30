@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.hugedev.snirelay.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.hugedev.snirelay.core.Gen
import java.io.File

@Composable
fun VdsScreen(state: AppState) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        SectionCard("Подключение к VDS") {
            LabeledField("Хост / IP", state.settings.host, { v -> state.update { host = v } })
            LabeledField("Порт SSH", state.settings.port, { v -> state.update { port = v } }, keyboard = KeyboardType.Number)
            LabeledField("Пользователь", state.settings.user, { v -> state.update { user = v } })
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = state.settings.usePassword, onCheckedChange = { c -> state.update { usePassword = c } })
                Text("Авторизация по паролю", color = TextMain)
            }
            if (state.settings.usePassword) {
                OutlinedTextField(
                    value = state.settings.password,
                    onValueChange = { v -> state.update { password = v } },
                    label = { Text("Пароль") },
                    singleLine = true,
                    visualTransformation = PasswordVisualTransformation(),
                    modifier = Modifier.fillMaxWidth(),
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Checkbox(checked = state.settings.savePassword, onCheckedChange = { c -> state.update { savePassword = c } })
                    Text("Сохранить пароль", color = TextMain)
                }
            } else {
                var keyDialog by remember { mutableStateOf(false) }
                val ctx = androidx.compose.ui.platform.LocalContext.current
                val keyFile = File(ctx.filesDir, "id_key")
                Text(
                    if (keyFile.exists() && keyFile.length() > 0) "Ключ: вставлен (${keyFile.length()} б)"
                    else if (state.settings.keyPath.isNotBlank()) "Ключ: ${state.settings.keyPath}"
                    else "Ключ: не задан",
                    color = TextDim, fontSize = 13.sp,
                )
                RowSpace {
                    ActionButton("Вставить ключ", { keyDialog = true })
                    if (keyFile.exists()) ActionButton("Удалить ключ", {
                        keyFile.delete(); state.logLine("[OK] ключ удалён")
                    })
                }
                if (keyDialog) {
                    var text by remember { mutableStateOf("") }
                    AlertDialog(
                        onDismissRequest = { keyDialog = false },
                        title = { Text("Приватный SSH-ключ") },
                        text = {
                            OutlinedTextField(
                                value = text, onValueChange = { text = it },
                                label = { Text("PEM / OpenSSH private key") },
                                modifier = Modifier.fillMaxWidth(),
                                textStyle = androidx.compose.material3.MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
                            )
                        },
                        confirmButton = {
                            TextButton(onClick = {
                                if (text.isNotBlank()) {
                                    keyFile.writeText(text.trim() + "\n")
                                    state.update { keyPath = keyFile.absolutePath }
                                    state.logLine("[OK] ключ сохранён")
                                }
                                keyDialog = false
                            }) { Text("Сохранить") }
                        },
                        dismissButton = { TextButton(onClick = { keyDialog = false }) { Text("Отмена") } },
                    )
                }
            }
            RowSpace {
                ActionButton("Проверить подключение", { state.vdsCheck() }, primary = true)
            }
        }
        state.vdsInfo?.let { info ->
            SectionCard("Статус VDS") {
                Text(info, color = TextMain, fontFamily = FontFamily.Monospace, fontSize = 13.sp)
            }
        }
    }
}

@Composable
fun DomainsScreen(state: AppState) {
    val domains = remember { mutableStateListOf<String>().also { it.addAll(state.settings.domains) } }
    val siteDomains = remember { mutableStateListOf<String>().also { it.addAll(state.settings.siteDomains) } }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        SectionCard("Домены релея (${domains.size})") {
            domains.forEachIndexed { i, d ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    OutlinedTextField(
                        value = d,
                        onValueChange = { domains[i] = it },
                        singleLine = true,
                        modifier = Modifier.weight(1f),
                        textStyle = androidx.compose.material3.MaterialTheme.typography.bodyMedium.copy(fontFamily = FontFamily.Monospace),
                    )
                    IconButton(onClick = { domains.removeAt(i) }) {
                        Icon(Icons.Filled.Delete, contentDescription = "Удалить", tint = Danger)
                    }
                }
            }
            RowSpace {
                ActionButton("Добавить", { domains.add("") })
                ActionButton("Сбросить", { domains.clear(); domains.addAll(com.hugedev.snirelay.data.Settings.defaultDomains()) })
                ActionButton("Сохранить", {
                    state.update { this.domains = domains.filter { it.isNotBlank() }.toMutableList() }
                    state.logLine("[OK] домены сохранены (${domains.size})")
                }, primary = true)
            }
        }
        SectionCard("Локальный сайт на VDS") {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = state.settings.hasSite, onCheckedChange = { c -> state.update { hasSite = c } })
                Text("На VDS есть сайт на 443", color = TextMain)
            }
            LabeledField("Backend сайта", state.settings.siteBackend, { v -> state.update { siteBackend = v } })
            siteDomains.forEachIndexed { i, d ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    OutlinedTextField(
                        value = d, onValueChange = { siteDomains[i] = it }, singleLine = true,
                        modifier = Modifier.weight(1f),
                    )
                    IconButton(onClick = { siteDomains.removeAt(i) }) { Icon(Icons.Filled.Delete, null, tint = Danger) }
                }
            }
            RowSpace {
                ActionButton("Добавить SNI", { siteDomains.add("") })
                ActionButton("Сохранить", {
                    state.update { this.siteDomains = siteDomains.filter { it.isNotBlank() }.toMutableList() }
                }, primary = true)
            }
        }
    }
}

@Composable
fun RelayScreen(state: AppState) {
    var showConf by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        SectionCard("Релей") {
            LabeledField("Куда уходит релеенный трафик", state.settings.relayExit, { v -> state.update { relayExit = v } }, mono = true)
            ActionButton("Развернуть / обновить релей на VDS", { state.deployRelay() }, primary = true, modifier = Modifier.fillMaxWidth())
            ActionButton("Откатить последний бэкап на VDS", { state.rollbackRelay() }, modifier = Modifier.fillMaxWidth())
            ActionButton("Показать генерируемый конфиг", { showConf = true }, modifier = Modifier.fillMaxWidth())
            Text(
                "Развёртывание: бэкап /etc/nginx, конфиг релея, перенос сайта с 443 на 127.0.0.1:8443, nginx -t, reload. При ошибке — автооткат.",
                color = TextDim, fontSize = 13.sp,
            )
        }
        if (showConf) {
            AlertDialog(
                onDismissRequest = { showConf = false },
                title = { Text("relay.conf") },
                text = {
                    Text(
                        Gen.relayConf(state.settings),
                        fontFamily = FontFamily.Monospace, fontSize = 12.sp,
                        modifier = Modifier.verticalScroll(rememberScrollState()),
                    )
                },
                confirmButton = { TextButton(onClick = { showConf = false }) { Text("Закрыть") } },
            )
        }
    }
}

@Composable
fun ClientScreen(state: AppState, onStartVpn: () -> Unit) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        SectionCard("Клиент (VPN / DNS)") {
            Text(
                "На телефоне без root нельзя править /etc/hosts. Вместо этого приложение поднимает локальный VPN, " +
                    "который перехватывает только DNS и для доменов релея отдаёт IP вашего VDS. Остальной трафик идёт напрямую.",
                color = TextDim, fontSize = 14.sp,
            )
            Pill(
                if (state.vpnRunning) "VPN: включён" else "VPN: выключен",
                if (state.vpnRunning) Ok else TextDim,
            )
            RowSpace {
                if (!state.vpnRunning)
                    ActionButton("Включить VPN", { onStartVpn() }, primary = true)
                else
                    ActionButton("Выключить VPN", { state.stopVpnService() }, danger = true)
            }
            Text(
                "Важно: выключите Secure DNS / DoH в браузере, иначе он резолвит мимо VPN.",
                color = Warn, fontSize = 13.sp,
            )
        }
        SectionCard("Проверить VPN") {
            Text("Резолвит первый домен релея через системный DNS и сравнивает с IP VDS.", color = TextDim, fontSize = 13.sp)
            ActionButton("Проверить DNS", { state.vpnSelfTest() })
        }
    }
}

@Composable
fun CheckScreen(state: AppState) {
    Column(Modifier.fillMaxSize()) {
        Box(Modifier.padding(12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                ActionButton("Проверить все домены", { state.checkAll() }, primary = true, enabled = !state.checking)
                if (state.checking) CircularProgressIndicator(Modifier.padding(4.dp), strokeWidth = 2.dp)
            }
        }
        LazyColumn(Modifier.fillMaxSize().padding(horizontal = 12.dp)) {
            items(state.results) { r ->
                Row(
                    Modifier.fillMaxWidth().padding(vertical = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Text(r.domain, color = TextMain, fontSize = 14.sp, modifier = Modifier.weight(1f))
                    val color = when (r.status) {
                        "OK" -> Ok
                        "challenge" -> Warn
                        "REGION BLOCK" -> Danger
                        "нет ответа" -> Danger
                        else -> TextDim
                    }
                    if (r.status.isNotEmpty()) Pill(r.status, color) else Text(r.http, color = TextDim, fontSize = 12.sp)
                }
            }
        }
    }
}
