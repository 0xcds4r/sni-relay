package com.hugedev.snirelay.core

import android.content.Context
import com.hugedev.snirelay.data.Settings
import com.jcraft.jsch.ChannelExec
import com.jcraft.jsch.JSch
import com.jcraft.jsch.Session
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import java.util.Properties

/** SSH-исполнитель на JSch. Заменяет QProcess+ssh из десктопной версии. */
object Shell {

    data class Result(val ok: Boolean, val output: String, val exit: Int)

    @Volatile private var bcReady = false

    /** JSch нужен BouncyCastle для ssh-ed25519; на Android системный BC его не даёт. */
    private fun ensureBouncyCastle() {
        if (bcReady) return
        synchronized(this) {
            if (bcReady) return
            try {
                java.security.Security.removeProvider("BC")
            } catch (_: Exception) {
            }
            java.security.Security.addProvider(org.bouncycastle.jce.provider.BouncyCastleProvider())
            bcReady = true
        }
    }

    suspend fun run(
        ctx: Context,
        s: Settings,
        cmd: String,
        onLine: ((String) -> Unit)? = null,
    ): Result = withContext(Dispatchers.IO) {
        if (s.host.isBlank()) return@withContext Result(false, "не задан хост VDS", -1)
        var session: Session? = null
        try {
            ensureBouncyCastle()
            val jsch = JSch()
            if (s.usePassword && s.password.isNotEmpty()) {
                // пароль
            } else {
                val keyPath = s.resolvedKeyPath(ctx)
                if (keyPath.isNotBlank()) {
                    try {
                        jsch.addIdentity(keyPath)
                    } catch (e: Exception) {
                        return@withContext Result(false, "ключ не принят: ${e.message}", -1)
                    }
                }
            }
            val port = s.port.trim().ifEmpty { "22" }.toIntOrNull() ?: 22
            session = jsch.getSession(s.user.ifBlank { "root" }, s.host.trim(), port)
            if (s.usePassword && s.password.isNotEmpty()) session.setPassword(s.password)
            val cfg = Properties().apply {
                put("StrictHostKeyChecking", "no")
                put("PreferredAuthentications", "publickey,password,keyboard-interactive")
            }
            session.setConfig(cfg)
            session.connect(15000)

            val channel = session.openChannel("exec") as ChannelExec
            val err = ByteArrayOutputStream()
            channel.setErrStream(err)
            channel.setCommand(cmd)
            channel.connect(15000)

            val out = ByteArrayOutputStream()
            val input = channel.inputStream
            val buf = ByteArray(4096)
            val lineBuf = StringBuilder()
            while (true) {
                val n = input.read(buf)
                if (n < 0) break
                out.write(buf, 0, n)
                if (onLine != null) {
                    lineBuf.append(String(buf, 0, n, Charsets.UTF_8))
                    var idx: Int
                    while (lineBuf.indexOf("\n").also { idx = it } >= 0) {
                        val line = lineBuf.substring(0, idx)
                        lineBuf.delete(0, idx + 1)
                        onLine(line)
                    }
                }
            }
            while (!channel.isClosed) Thread.sleep(30)
            val exit = channel.exitStatus
            channel.disconnect()
            val text = out.toString("UTF-8") + err.toString("UTF-8")
            Result(exit == 0, text, exit)
        } catch (e: Exception) {
            Result(false, "SSH: ${e.message}", -1)
        } finally {
            try { session?.disconnect() } catch (_: Exception) {}
        }
    }

    /** Проверка подключения: geo/nginx/stream/доступность claude.ai. */
    fun vdsCheckScript(): String =
        "echo \"HOST=\$(hostname 2>/dev/null)\"\n" +
            "echo \"OS=\$(. /etc/os-release 2>/dev/null; echo \$PRETTY_NAME)\"\n" +
            "echo \"COUNTRY=\$(curl -s --max-time 8 https://ipinfo.io/country 2>/dev/null)\"\n" +
            "command -v nginx >/dev/null && echo NGINX=yes || echo NGINX=no\n" +
            "nginx -V 2>&1 | grep -q -- '--with-stream' && echo STREAM=yes || echo STREAM=no\n" +
            "echo \"CLAUDE=\$(curl -sI --max-time 10 https://claude.ai/ | grep -iE '^HTTP|^location|cf-mitigated' | tr '\\n' ' ')\"\n"
}
