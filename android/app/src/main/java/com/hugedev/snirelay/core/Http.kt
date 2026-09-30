package com.hugedev.snirelay.core

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.Dns
import okhttp3.OkHttpClient
import okhttp3.Request
import java.net.InetAddress
import java.util.concurrent.TimeUnit

/** HTTP-проверки доменов через релей (аналог curl через /etc/hosts). */
object Http {

    data class CheckResult(val status: String, val details: String)

    suspend fun checkDomain(vdsHost: String, domain: String): CheckResult = withContext(Dispatchers.IO) {
        val ip = try { InetAddress.getByName(vdsHost) } catch (_: Exception) { null }
            ?: return@withContext CheckResult("нет ответа", "не резолвится хост VDS")
        val dns = object : Dns {
            override fun lookup(hostname: String): List<InetAddress> = listOf(ip)
        }
        val client = OkHttpClient.Builder()
            .dns(dns)
            .followRedirects(false)
            .followSslRedirects(false)
            .connectTimeout(12, TimeUnit.SECONDS)
            .readTimeout(12, TimeUnit.SECONDS)
            .build()
        val req = Request.Builder()
            .url("https://$domain/")
            .header("User-Agent", "Mozilla/5.0 (Android) SNI-Relay-Check")
            .get()
            .build()
        try {
            client.newCall(req).execute().use { resp ->
                val code = resp.code
                val loc = resp.header("location") ?: ""
                val cf = resp.header("cf-mitigated") ?: ""
                val status = when {
                    loc.contains("app-unavailable-in-region") -> "REGION BLOCK"
                    cf.contains("challenge") || code == 403 -> "challenge"
                    else -> "OK"
                }
                val det = buildString {
                    append("HTTP $code")
                    if (cf.isNotEmpty()) append(" · cf-mitigated: $cf")
                    if (loc.isNotEmpty()) append(" · location: ${loc.take(60)}")
                }
                CheckResult(status, det)
            }
        } catch (e: Exception) {
            CheckResult("нет ответа", e.message?.take(80) ?: "ошибка")
        }
    }
}
