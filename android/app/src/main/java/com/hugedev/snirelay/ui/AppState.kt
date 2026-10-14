package com.hugedev.snirelay.ui

import android.app.Application
import android.content.Intent
import android.net.Uri
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

    var realityStatus by mutableStateOf("Нажми «Обновить статус»")
        private set
    var realityInstalled by mutableStateOf(false)
        private set
    var realityHasUnit by mutableStateOf(false)
        private set
    var realityActive by mutableStateOf(false)
        private set
    private var realityStatusInFlight = false

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
        desc == "Импорт relay-доменов с VDS" && line.startsWith("DOMAINS=") -> "DOMAINS=[список получен]"
        line.trimStart().startsWith("TOML_SECRET=") -> "TOML_SECRET=[скрыт]"
        line.trimStart().startsWith("SIDE_B64=") -> "SIDE_B64=[скрыт]"
        line.trimStart().startsWith("PRIV=") -> "PRIV=[скрыт]"
        line.trimStart().startsWith("PUB=") -> "PUB=[скрыт]"
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
            if (r.ok) {
                logOk("VDS доступен")
                importRelayDomainsFromVds()
            } else {
                logErr("Проверка VDS: ошибка")
            }
        }
    }

    private fun importRelayDomainsFromVds() {
        val backend = Gen.qshell(settings.relayExit.trim().ifEmpty { "127.0.0.1:9443" })
        val script = """
            f=/etc/nginx/stream-enabled/relay.conf
            [ -r "${'$'}f" ] || { echo NO_RELAY_CONF; exit 1; }
            awk -v backend=$backend '
                BEGIN { printf "DOMAINS=" }
                index(${ '$' }0, "map ") && index(${ '$' }0, "relay_backend") { inmap=1; next }
                inmap && ${ '$' }1 == "}" { exit }
                inmap {
                    sub(/#.*/, "")
                    if (NF >= 2 && ${ '$' }2 == backend ";") {
                        if (found++) printf " "
                        printf "%s", ${ '$' }1
                    }
                }
                END { printf "\n" }
            ' "${'$'}f"
        """.trimIndent()
        runSsh(script, "Импорт relay-доменов с VDS") { r ->
            val domainPattern = Regex("^[A-Za-z0-9-]+(?:\\.[A-Za-z0-9-]+)+${'$'}")
            val domainLine = r.output.lineSequence().firstOrNull { it.startsWith("DOMAINS=") }
            val imported = domainLine?.removePrefix("DOMAINS=")?.trim()?.split(Regex("\\s+"))
                ?.filter { domainPattern.matches(it) }
                ?.distinct()
                .orEmpty()
            if (!r.ok || imported.isEmpty()) {
                logErr("relay-домены из VDS не прочитаны; локальный список не изменён")
                return@runSsh
            }
            update { domains = imported.toMutableList() }
            logOk("список доменов синхронизирован с VDS (${imported.size})")
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

    // ---- Reality (VLESS + Xray) ----

    fun realityInstallDeploy() {
        runSsh(Gen.realityInstall(), "Установка Xray") { r ->
            if (!r.output.contains("XRAY_INSTALLED")) { logErr("установка Xray не удалась"); refreshRealityStatus(); return@runSsh }
            realityGenKeys()
        }
    }

    fun realityGenKeys() {
        runSsh(Gen.realityGenKeys(), "Генерация ключей Reality") { r ->
            if (!r.output.contains("REALITY_KEYS_OK")) { logErr("ключи не сгенерированы (Xray установлен?)"); refreshRealityStatus(); return@runSsh }
            var uuid = ""; var priv = ""; var pub = ""; var sid = ""
            for (l in r.output.lines()) {
                val t = l.trim()
                when {
                    t.startsWith("UUID=") -> uuid = t.substring(5)
                    t.startsWith("PRIV=") -> priv = t.substring(5)
                    t.startsWith("PUB=") -> pub = t.substring(4)
                    t.startsWith("SHORTID=") -> sid = t.substring(8)
                }
            }
            update {
                if (uuid.isNotEmpty()) realityUuid = uuid
                if (priv.isNotEmpty()) realityPrivateKey = priv
                if (pub.isNotEmpty()) realityPublicKey = pub
                if (sid.isNotEmpty()) realityShortId = sid
            }
            logOk("Ключи Reality сгенерированы")
            realityDeploy()
        }
    }

    fun realityDeploy() {
        val s = settings
        if (s.realityUuid.trim().isEmpty() || s.realityPrivateKey.trim().isEmpty() || s.realityShortId.trim().isEmpty()) {
            logErr("Нужны UUID, private key и short id — сначала «Сгенерировать ключи»"); refreshRealityStatus(); return
        }
        runSsh(Gen.realityDeployScript(s), "Развёртывание Reality") { r ->
            if (r.output.contains("REALITY_DEPLOYED")) logOk("VLESS+Reality развёрнут") else logErr("развёртывание Reality не удалось")
            refreshRealityStatus()
        }
    }

    /** Открыть ссылку vless:// во внешнем клиенте (v2rayNG/Nekoray/sing-box). */
    fun realityOpenInClient() {
        val s = settings
        if (s.host.isBlank() || s.realityUuid.isBlank() || s.realityPublicKey.isBlank()) {
            logErr("Нужны хост VDS, UUID и public key — сначала разверни Reality"); return
        }
        val link = Gen.realityClientLink(s)
        try {
            val i = Intent(Intent.ACTION_VIEW, Uri.parse(link)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            getApplication<Application>().startActivity(i)
            logOk("Открываю vless:// в клиенте…")
        } catch (e: Exception) {
            logErr("Клиент не найден — скопируй ссылку или отсканируй QR")
        }
    }

    /** Показать внешний IP: должен совпасть с VDS, если VPN клиента активен. */
    fun realityCheckIp() {
        val vds = settings.host.trim()
        viewModelScope.launch {
            val ip = withContext(Dispatchers.IO) {
                try {
                    val c = (java.net.URL("https://ipinfo.io/ip").openConnection() as java.net.HttpURLConnection)
                    c.connectTimeout = 8000; c.readTimeout = 8000
                    c.inputStream.bufferedReader().use { it.readText().trim() }
                } catch (e: Exception) { "ошибка: ${e.message}" }
            }
            if (ip == vds) logOk("Внешний IP: $ip (VDS) — VPN активен")
            else logLine("[!] Внешний IP: $ip (ожидался $vds) — клиент не подключён?")
        }
    }

    fun realityStart() = runSsh("systemctl start xray && systemctl is-active xray", "Старт Xray") { refreshRealityStatus() }
    fun realityStop() = runSsh("systemctl stop xray || true", "Стоп Xray") { refreshRealityStatus() }
    fun realityRestart() = runSsh("systemctl restart xray && systemctl is-active xray", "Рестарт Xray") { refreshRealityStatus() }

    fun realityRemove() {
        runSsh(Gen.realityRemove(), "Удаление Xray") { r ->
            if (r.output.contains("REALITY_REMOVED")) logOk("Xray удалён") else logErr("не удалось удалить Xray")
            refreshRealityStatus()
        }
    }

    fun refreshRealityStatus() {
        if (settings.host.isBlank()) { realityStatus = "Статус: укажи VDS на вкладке «VDS»"; return }
        if (realityStatusInFlight) return
        realityStatusInFlight = true
        realityStatus = "Проверяю Reality на VDS…"
        refreshRealityStatusAttempt(0)
    }

    private fun refreshRealityStatusAttempt(attempt: Int) {
        runSsh(Gen.realityStatus(settings), "Статус Reality") { r ->
            val complete = listOf("VER=", "UNIT=", "ACTIVE=", "PORT=", "CFG=")
                .all { marker -> r.output.lineSequence().any { it.trim().startsWith(marker) } }
            if ((!r.ok || !complete) && attempt == 0) {
                realityStatus = "Ответ неполный, повторяю проверку…"
                viewModelScope.launch { delay(800); refreshRealityStatusAttempt(1) }
                return@runSsh
            }
            realityStatusInFlight = false
            if (!complete) {
                realityStatus = "Не удалось получить статус Reality — проверь SSH и повтори"
                logErr("ответ статуса Reality неполный"); return@runSsh
            }
            var ver = "?"; var unit = "no"; var active = "inactive"; var port = "no"; var cfg = "no"; var side = "no"; var b64 = ""
            for (l in r.output.lines()) {
                val t = l.trim()
                when {
                    t.startsWith("VER=") -> ver = t.substring(4)
                    t.startsWith("UNIT=") -> unit = t.substring(5)
                    t.startsWith("ACTIVE=") -> active = t.substring(7)
                    t.startsWith("PORT=") -> port = t.substring(5)
                    t.startsWith("CFG=") -> cfg = t.substring(4)
                    t.startsWith("SIDE=") -> side = t.substring(5)
                    t.startsWith("SIDE_B64=") -> b64 = t.substring(9)
                }
            }
            realityInstalled = ver != "NO" && ver != "?"
            realityHasUnit = unit == "yes"
            realityActive = active == "active"
            if (side == "yes" && b64.isNotBlank()) {
                try {
                    val j = org.json.JSONObject(String(android.util.Base64.decode(b64.trim(), android.util.Base64.DEFAULT)))
                    val grpc = j.optString("transport", "reality") == "grpc"
                    update {
                        if (j.has("transport")) realityTransport = if (grpc) "grpc" else "reality"
                        if (j.has("domain")) realityDomain = j.optString("domain", realityDomain)
                        if (j.has("serviceName")) realityServiceName = j.optString("serviceName", realityServiceName)
                        if (j.has("grpcPort")) realityGrpcPort = j.optString("grpcPort", realityGrpcPort)
                        if (j.has("port")) {
                            val p = j.optString("port")
                            if (p.isNotBlank()) { if (grpc) realityGrpcPort = p else realityPort = p }
                        }
                        if (j.has("dest")) realityDest = j.optString("dest", realityDest)
                        if (j.has("flow")) realityFlow = j.optString("flow", realityFlow)
                        if (j.has("fingerprint")) realityFingerprint = j.optString("fingerprint", realityFingerprint)
                        if (j.has("uuid")) realityUuid = j.optString("uuid", realityUuid)
                        if (j.has("publicKey")) realityPublicKey = j.optString("publicKey", realityPublicKey)
                        if (j.has("privateKey")) realityPrivateKey = j.optString("privateKey", realityPrivateKey)
                        if (j.has("shortId")) realityShortId = j.optString("shortId", realityShortId)
                    }
                } catch (_: Exception) {}
            }
            realityStatus = buildString {
                append("Xray: "); append(if (realityInstalled) ver else "не установлен")
                append(" · сервис: "); append(if (realityHasUnit) active else "нет")
                append(" · порт: "); append(if (port == "yes") "слушается" else "не слушается")
                if (cfg == "yes") append(" · конфиг: есть")
                if (side == "yes") append(" · параметры с VDS")
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
