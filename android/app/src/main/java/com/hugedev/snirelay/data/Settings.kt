package com.hugedev.snirelay.data

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * Настройки приложения. Хранятся в JSON (filesDir/config.json).
 * Приватный ключ SSH — в filesDir/id_key (если вставлен вручную).
 */
data class Settings(
    var host: String = "",
    var port: String = "22",
    var user: String = "root",
    var keyPath: String = "",
    var usePassword: Boolean = false,
    var savePassword: Boolean = false,
    var password: String = "",

    var relayExit: String = "127.0.0.1:9443",
    var siteBackend: String = "127.0.0.1:8443",
    var hasSite: Boolean = true,
    var siteDomains: MutableList<String> = mutableListOf(),
    var domains: MutableList<String> = defaultDomains().toMutableList(),

    var mtgPort: String = "10443",
    var mtgFront: String = "www.google.com",
    var mtgSecret: String = "",
    var mtgVia443: Boolean = true,
    var mtgLocalPort: String = "9999",
) {
    fun save(ctx: Context) {
        val o = JSONObject()
        o.put("host", host)
        o.put("port", port)
        o.put("user", user)
        o.put("keyPath", keyPath)
        o.put("usePassword", usePassword)
        o.put("savePassword", savePassword)
        o.put("password", if (savePassword) password else "")
        o.put("relayExit", relayExit)
        o.put("siteBackend", siteBackend)
        o.put("hasSite", hasSite)
        o.put("siteDomains", JSONArray(siteDomains))
        o.put("domains", JSONArray(domains))
        o.put("mtgPort", mtgPort)
        o.put("mtgFront", mtgFront)
        o.put("mtgSecret", mtgSecret)
        o.put("mtgVia443", mtgVia443)
        o.put("mtgLocalPort", mtgLocalPort)
        configFile(ctx).writeText(o.toString(2))
    }

    /** Путь к приватному ключу: сохранённый файл либо внешний путь. */
    fun resolvedKeyPath(ctx: Context): String {
        val f = File(ctx.filesDir, "id_key")
        return if (f.exists() && f.length() > 0) f.absolutePath else keyPath
    }

    fun effectiveMtgPort(): String = if (mtgVia443) "443" else mtgPort.ifBlank { "10443" }

    companion object {
        fun configFile(ctx: Context) = File(ctx.filesDir, "config.json")

        fun defaultDomains(): List<String> = listOf(
            "claude.ai", "a.claude.ai", "console.anthropic.com",
            "api.anthropic.com", "platform.claude.com",
            "chatgpt.com", "ab.chatgpt.com", "auth.openai.com", "auth0.openai.com",
            "platform.openai.com", "cdn.oaistatic.com", "files.oaiusercontent.com",
            "tcr9i.chat.openai.com", "webrtc.chatgpt.com", "android.chat.openai.com",
            "api.openai.com", "operator.chatgpt.com", "sora.chatgpt.com",
            "videos.openai.com", "ios.chat.openai.com", "cdn.platform.openai.com",
            "developers.openai.com",
            "sdmntprukwest.oaiusercontent.com", "sdmntpritalynorth.oaiusercontent.com",
            "sdmntprpolandcentral.oaiusercontent.com", "sdmntprnortheu.oaiusercontent.com",
        )

        fun load(ctx: Context): Settings {
            val s = Settings()
            val f = configFile(ctx)
            if (!f.exists()) return s
            return try {
                val o = JSONObject(f.readText())
                s.host = o.optString("host", s.host)
                s.port = o.optString("port", s.port)
                s.user = o.optString("user", s.user)
                s.keyPath = o.optString("keyPath", s.keyPath)
                s.usePassword = o.optBoolean("usePassword", s.usePassword)
                s.savePassword = o.optBoolean("savePassword", s.savePassword)
                if (s.savePassword) s.password = o.optString("password", "")
                s.relayExit = o.optString("relayExit", s.relayExit)
                s.siteBackend = o.optString("siteBackend", s.siteBackend)
                s.hasSite = o.optBoolean("hasSite", s.hasSite)
                o.optJSONArray("siteDomains")?.let { s.siteDomains = it.toStringList().toMutableList() }
                o.optJSONArray("domains")?.let { s.domains = it.toStringList().toMutableList() }
                s.mtgPort = o.optString("mtgPort", s.mtgPort)
                s.mtgFront = o.optString("mtgFront", s.mtgFront)
                s.mtgSecret = o.optString("mtgSecret", s.mtgSecret)
                s.mtgVia443 = o.optBoolean("mtgVia443", s.mtgVia443)
                s.mtgLocalPort = o.optString("mtgLocalPort", s.mtgLocalPort)
                s
            } catch (_: Exception) {
                s
            }
        }

        private fun JSONArray.toStringList(): List<String> =
            (0 until length()).map { optString(it) }
    }
}
