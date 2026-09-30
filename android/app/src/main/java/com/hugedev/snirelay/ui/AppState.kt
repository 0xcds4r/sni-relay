package com.hugedev.snirelay.ui

import android.app.Application
import android.content.Intent
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.hugedev.snirelay.core.Gen
import com.hugedev.snirelay.core.Http
import com.hugedev.snirelay.core.Shell
import com.hugedev.snirelay.data.Settings
import com.hugedev.snirelay.vpn.RelayVpnService
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext

data class DomainResult(
    val domain: String,
    val http: String = "…",
    val status: String = "",
    val details: String = "",
)

class AppState(app: Application) : AndroidViewModel(app) {

    var settings by mutableStateOf(Settings.load(app))
        private set

    val log = mutableStateListOf<String>()

    var busy by mutableStateOf<String?>(null)
        private set

    var vdsInfo by mutableStateOf<String?>(null)
        private set

    val results = mutableStateListOf<DomainResult>()
    var checking by mutableStateOf(false)
        private set

    var mtgStatus by mutableStateOf("Нажми «Обновить статус»")
        private set
    var mtgInstalled by mutableStateOf(false)
        private set
    var mtgHasUnit by mutableStateOf(false)
        private set
    var mtgActive by mutableStateOf(false)
        private set
    private var mtgStatusInFlight = false

    var vpnRunning by mutableStateOf(false)

    fun update(block: Settings.() -> Unit) {
        val copy = settings.copy(domains = settings.domains.toMutableList(), siteDomains = settings.siteDomains.toMutableList())
        copy.block()
        settings = copy
        settings.save(getApplication())
    }

    fun logLine(line: String) {
        log.add(line)
        if (log.size > 500) log.removeAt(0)
    }

    private fun logOk(s: String) = logLine("[OK] $s")
    private fun logErr(s: String) = logLine("[!] $s")

    private fun safeOutputLine(line: String, desc: String): String = when {
        line.trimStart().startsWith("TOML_SECRET=") -> "TOML_SECRET=[скрыт]"
        desc.contains("секрет", ignoreCase = true) &&
            line.trim().length >= 20 && !line.contains(' ') -> "[секрет скрыт]"
        else -> line
    }

    fun runRaw(cmd: String) = runSsh(cmd, "Команда на VDS")

    private fun runSsh(cmd: String, desc: String, onDone: ((Shell.Result) -> Unit)? = null) {
        viewModelScope.launch {
            busy = desc
            logLine("→ $desc")
            val r = Shell.run(getApplication(), settings, cmd) { line ->
                if (line.isNotBlank()) logLine("  ${safeOutputLine(line, desc)}")
            }
            r.output.lines().filter { it.isNotBlank() }.takeLast(6).forEach { line ->
                logLine("  ${safeOutputLine(line, desc)}")
            }
            busy = null
            onDone?.invoke(r)
        }
    }

    // ---- VDS ----

    fun vdsCheck() {
        runSsh(Shell.vdsCheckScript(), "Проверка подключения") { r ->
            vdsInfo = if (!r.ok && r.output.isBlank()) "Не удалось подключиться" else r.output.trim()
            if (r.ok) logOk("VDS доступен") else logErr("Проверка VDS: ошибка")
        }
    }

    // ---- Relay ----

    fun deployRelay() {
        runSsh(Gen.deployScript(settings), "Развёртывание релея") { r ->
            if (r.output.contains("DEPLOY_OK")) logOk("Релей развёрнут") else logErr("Развёртывание не удалось")
        }
    }

    fun rollbackRelay() {
        runSsh(Gen.rollbackScript(), "Откат релея") { r ->
            if (r.output.contains("ROLLBACK_OK")) logOk("Откат выполнен") else logErr("Откат не удался")
        }
    }

    // ---- Проверка доменов ----

    fun checkAll() {
        if (settings.host.isBlank()) { logErr("Укажи хост VDS"); return }
        val doms = settings.domains.map { it.trim() }.filter { it.isNotEmpty() && !it.startsWith("#") }
        results.clear()
        doms.forEach { results.add(DomainResult(it)) }
        checking = true
        viewModelScope.launch {
            val sem = Semaphore(12)
            doms.mapIndexed { idx, d ->
                async(Dispatchers.IO) {
                    sem.withPermit {
                        val res = Http.checkDomain(settings.host.trim(), d)
                        withContext(Dispatchers.Main) {
                            val i = results.indexOfFirst { it.domain == d }
                            if (i >= 0) results[i] = DomainResult(d, res.details, res.status, res.details)
                        }
                    }
                }
            }.awaitAll()
            checking = false
            val ok = results.count { it.status == "OK" }
            logOk("Проверка завершена: $ok/${results.size} OK")
        }
    }

    // ---- MTProto ----

    fun mtgInstallDeploy() {
        runSsh(Gen.mtgInstall(), "Установка mtg") { r ->
            if (!r.output.contains("MTG_INSTALLED")) { logErr("установка mtg не удалась"); refreshMtgStatus(); return@runSsh }
            if (settings.mtgSecret.trim().isEmpty()) {
                val front = settings.mtgFront.trim().ifEmpty { "www.google.com" }
                runSsh(Gen.mtgGenSecret(front), "Генерация секрета") { r2 ->
                    val sec = parseSecret(r2.output)
                    if (sec != null) { update { mtgSecret = sec }; logOk("Секрет сгенерирован") }
                    mtgDeploy()
                }
            } else mtgDeploy()
        }
    }

    fun mtgGenSecret() {
        val front = settings.mtgFront.trim().ifEmpty { "www.google.com" }
        runSsh(Gen.mtgGenSecret(front), "Генерация секрета") { r ->
            val sec = parseSecret(r.output)
            if (sec != null) { update { mtgSecret = sec }; logOk("Секрет получен") } else logErr("секрет не распознан")
        }
    }

    private fun parseSecret(out: String): String? =
        out.lines().map { it.trim() }
            .lastOrNull { it.isNotEmpty() && !it.contains(' ') && it.length >= 20 && !it.startsWith("->") && !it.startsWith("set ") }

    fun mtgDeploy() {
        if (settings.mtgSecret.trim().isEmpty()) { logErr("Сначала сгенерируй секрет"); refreshMtgStatus(); return }
        runSsh(Gen.mtgDeploy(settings), "Развёртывание mtg") { r ->
            val ok = r.output.contains("MTG_DEPLOYED")
            if (ok) logOk("MTProto-прокси развёрнут") else logErr("Развёртывание mtg не удалось")
            if (ok && settings.mtgVia443) {
                runSsh(Gen.deployScript(settings), "Развёртывание релея") { refreshMtgStatus() }
            } else refreshMtgStatus()
        }
    }

    fun mtgStart() = runSsh("systemctl start mtg && systemctl is-active mtg", "Старт mtg") { refreshMtgStatus() }
    fun mtgStop() = runSsh("systemctl stop mtg || true", "Стоп mtg") { refreshMtgStatus() }
    fun mtgRestart() = runSsh("systemctl restart mtg && systemctl is-active mtg", "Рестарт mtg") { refreshMtgStatus() }

    fun mtgRemove() {
        runSsh(Gen.mtgRemove(), "Удаление mtg") { r ->
            if (r.output.contains("MTG_REMOVED")) logOk("mtg удалён") else logErr("не удалось удалить mtg")
            refreshMtgStatus()
        }
    }

    fun refreshMtgStatus() {
        if (settings.host.isBlank()) { mtgStatus = "Статус: укажи VDS на вкладке «VDS»"; return }
        if (mtgStatusInFlight) return
        mtgStatusInFlight = true
        mtgStatus = "Проверяю mtg на VDS…"
        refreshMtgStatusAttempt(0)
    }

    private fun refreshMtgStatusAttempt(attempt: Int) {
        runSsh(Gen.mtgStatus(settings), "Статус mtg") { r ->
            val complete = listOf("VER=", "UNIT=", "ACTIVE=", "PORT=", "TOML=")
                .all { marker -> r.output.lineSequence().any { it.trim().startsWith(marker) } }
            if ((!r.ok || !complete) && attempt == 0) {
                mtgStatus = "Ответ неполный, повторяю проверку…"
                viewModelScope.launch {
                    delay(800)
                    refreshMtgStatusAttempt(1)
                }
                return@runSsh
            }
            mtgStatusInFlight = false
            if (!complete) {
                mtgStatus = "Не удалось получить статус mtg — проверь SSH и повтори"
                logErr("ответ статуса mtg неполный")
                return@runSsh
            }
            var ver = "?"; var unit = "no"; var active = "inactive"; var port = "no"; var toml = "no"
            var tsec = ""; var tbind = ""
            for (l in r.output.lines()) {
                val t = l.trim()
                when {
                    t.startsWith("VER=") -> ver = t.substring(4)
                    t.startsWith("UNIT=") -> unit = t.substring(5)
                    t.startsWith("ACTIVE=") -> active = t.substring(7)
                    t.startsWith("PORT=") -> port = t.substring(5)
                    t.startsWith("TOML=") -> toml = t.substring(5)
                    t.startsWith("TOML_SECRET=") -> tsec = t.substring(12)
                    t.startsWith("TOML_BIND=") -> tbind = t.substring(10)
                }
            }
            mtgInstalled = ver != "NO" && ver != "?"
            mtgHasUnit = unit == "yes"
            mtgActive = active == "active"
            if (toml == "yes" && tsec.isNotBlank()) {
                val idx = tbind.lastIndexOf(':')
                val p = if (idx >= 0) tbind.substring(idx + 1).toIntOrNull() ?: 0 else 0
                val via = tbind.startsWith("127.0.0.1:")
                val front = Gen.mtgFrontFromSecret(tsec.trim())
                update {
                    mtgSecret = tsec.trim()
                    mtgVia443 = via
                    if (via && p > 0) mtgLocalPort = p.toString()
                    if (!via && p > 0) mtgPort = p.toString()
                    if (front.isNotEmpty()) mtgFront = front
                }
            }
            mtgStatus = buildString {
                append("mtg: "); append(if (mtgInstalled) ver else "не установлен")
                append(" · сервис: "); append(if (mtgHasUnit) active else "нет")
                append(" · порт: "); append(if (port == "yes") "слушается" else "не слушается")
                if (toml == "yes") append(" · конфиг: подставлен с VDS")
            }
        }
    }

    // ---- VPN ----

    fun startVpnService() {
        val domains = settings.domains.map { it.trim() }.filter { it.isNotEmpty() }
        val ip = settings.host.trim()
        if (ip.isEmpty()) { logErr("Укажи хост VDS"); return }
        val intent = Intent(getApplication(), RelayVpnService::class.java).apply {
            action = RelayVpnService.ACTION_START
            putStringArrayListExtra(RelayVpnService.EXTRA_DOMAINS, ArrayList(domains))
            putExtra(RelayVpnService.EXTRA_VDS_IP, ip)
        }
        getApplication<Application>().startForegroundService(intent)
        vpnRunning = true
        logOk("VPN запущен (${domains.size} доменов → $ip)")
    }

    fun stopVpnService() {
        val intent = Intent(getApplication(), RelayVpnService::class.java).apply {
            action = RelayVpnService.ACTION_STOP
        }
        getApplication<Application>().startService(intent)
        vpnRunning = false
        logOk("VPN остановлен")
    }

    /** Self-test: резолвим relay-домен через системный DNS (он идёт через наш VPN). */
    fun vpnSelfTest() {
        val d = settings.domains.firstOrNull { it.isNotBlank() } ?: "claude.ai"
        viewModelScope.launch {
            val ip = withContext(Dispatchers.IO) {
                try { java.net.InetAddress.getByName(d).hostAddress } catch (e: Exception) { "ошибка: ${e.message}" }
            }
            if (ip == settings.host) logOk("DNS $d → $ip · VPN работает")
            else logLine("[!] DNS $d → $ip (ожидался ${settings.host})")
        }
    }
}
