package com.hugedev.snirelay.vpn

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.VpnService
import android.os.Build
import android.os.ParcelFileDescriptor
import androidx.core.app.ServiceCompat
import com.hugedev.snirelay.MainActivity
import com.hugedev.snirelay.data.Settings
import java.io.FileInputStream
import java.io.FileOutputStream
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress

/**
 * Локальный VPN: перехватывает только DNS и для relay-доменов отдаёт IP VDS.
 * Аналог подмены /etc/hosts, но без root. Трафик сайтов напрямую (не через туннель).
 */
class RelayVpnService : VpnService() {

    companion object {
        const val ACTION_START = "com.hugedev.snirelay.VPN_START"
        const val ACTION_STOP = "com.hugedev.snirelay.VPN_STOP"
        const val EXTRA_DOMAINS = "domains"
        const val EXTRA_VDS_IP = "vds_ip"

        @Volatile var running: Boolean = false
            private set

        private const val TUN_ADDR = "10.111.0.2"
        private const val DNS_ADDR = "10.111.0.1"
        private const val CHANNEL_ID = "sni_relay_vpn"
        private const val NOTIF_ID = 4242
    }

    private var tun: ParcelFileDescriptor? = null
    private var worker: Thread? = null
    @Volatile private var stopFlag = false
    private var domains: Set<String> = emptySet()
    private var vdsIp: ByteArray? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                stopVpn()
                return START_NOT_STICKY
            }
            else -> {
                domains = (intent?.getStringArrayListExtra(EXTRA_DOMAINS) ?: arrayListOf())
                    .map { it.trim().lowercase() }.filter { it.isNotEmpty() }.toSet()
                vdsIp = try { InetAddress.getByName(intent?.getStringExtra(EXTRA_VDS_IP) ?: "") .address } catch (_: Exception) { null }
                if (vdsIp == null || vdsIp!!.size != 4) { stopVpn(); return START_NOT_STICKY }
                startVpn()
            }
        }
        return START_STICKY
    }

    private fun startVpn() {
        if (running) return
        val builder = Builder()
            .setSession("SNI Relay")
            .setMtu(1500)
            .addAddress(TUN_ADDR, 32)
            .addDnsServer(DNS_ADDR)
            .addRoute(DNS_ADDR, 32)
        if (Build.VERSION.SDK_INT >= 29) builder.setMetered(false)
        val pfd = builder.establish() ?: return
        tun = pfd
        stopFlag = false
        running = true
        ServiceCompat.startForeground(
            this, NOTIF_ID, notification(),
            if (Build.VERSION.SDK_INT >= 34) ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE else 0,
        )
        worker = Thread({ loop(pfd) }, "relay-dns").also { it.start() }
    }

    private fun stopVpn() {
        stopFlag = true
        running = false
        try { worker?.interrupt() } catch (_: Exception) {}
        try { tun?.close() } catch (_: Exception) {}
        tun = null
        worker = null
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun loop(pfd: ParcelFileDescriptor) {
        val input = FileInputStream(pfd.fileDescriptor)
        val output = FileOutputStream(pfd.fileDescriptor)
        val buf = ByteArray(32767)
        while (!stopFlag) {
            val n = try { input.read(buf) } catch (_: Exception) { break }
            if (n <= 0) continue
            try {
                val reply = handlePacket(buf, n)
                if (reply != null) output.write(reply)
            } catch (_: Exception) {
                // ignore malformed
            }
        }
    }

    private fun handlePacket(pkt: ByteArray, len: Int): ByteArray? {
        if (len < 28) return null
        val version = (pkt[0].toInt() ushr 4) and 0xF
        if (version != 4) return null
        val ihl = (pkt[0].toInt() and 0xF) * 4
        if (ihl < 20 || len < ihl + 8) return null
        val proto = pkt[9].toInt() and 0xFF
        if (proto != 17) return null // UDP
        val dstPort = ((pkt[ihl + 2].toInt() and 0xFF) shl 8) or (pkt[ihl + 3].toInt() and 0xFF)
        if (dstPort != 53) return null
        val udpLen = ((pkt[ihl + 4].toInt() and 0xFF) shl 8) or (pkt[ihl + 5].toInt() and 0xFF)
        if (udpLen < 8 || ihl + udpLen > len) return null
        val dns = pkt.copyOfRange(ihl + 8, ihl + udpLen)

        val parsed = parseQuestion(dns) ?: return null
        val (qname, qtype) = parsed

        val answer = when {
            domains.contains(qname.lowercase()) && qtype == 1 -> buildAResponse(dns, vdsIp!!)
            domains.contains(qname.lowercase()) && qtype == 28 -> buildEmptyResponse(dns)
            else -> forward(dns)
        } ?: return null

        return wrapUdp(pkt, ihl, answer)
    }

    private fun parseQuestion(dns: ByteArray): Pair<String, Int>? {
        if (dns.size < 12) return null
        var i = 12
        val sb = StringBuilder()
        while (i < dns.size) {
            val l = dns[i].toInt() and 0xFF
            if (l == 0) { i++; break }
            if (l and 0xC0 == 0xC0) return null // compression not expected in question
            i++
            if (i + l > dns.size) return null
            if (sb.isNotEmpty()) sb.append('.')
            sb.append(String(dns, i, l, Charsets.US_ASCII))
            i += l
        }
        if (i + 4 > dns.size) return null
        val qtype = ((dns[i].toInt() and 0xFF) shl 8) or (dns[i + 1].toInt() and 0xFF)
        return sb.toString() to qtype
    }

    private fun buildAResponse(query: ByteArray, ip: ByteArray): ByteArray {
        val q = query.copyOfRange(0, questionEnd(query))
        val out = ByteArray(q.size + 16)
        System.arraycopy(q, 0, out, 0, q.size)
        // header
        out[2] = (0x81).toByte(); out[3] = (0x80).toByte() // QR=1, RD, RA
        out[6] = 0; out[7] = 1 // ANCOUNT=1
        var p = q.size
        out[p++] = 0xC0.toByte(); out[p++] = 0x0C // pointer to name
        out[p++] = 0; out[p++] = 1 // TYPE A
        out[p++] = 0; out[p++] = 1 // CLASS IN
        out[p++] = 0; out[p++] = 0; out[p++] = 0; out[p++] = 60 // TTL
        out[p++] = 0; out[p++] = 4 // RDLENGTH
        System.arraycopy(ip, 0, out, p, 4)
        return out
    }

    private fun buildEmptyResponse(query: ByteArray): ByteArray {
        val q = query.copyOfRange(0, questionEnd(query))
        val out = ByteArray(q.size)
        System.arraycopy(q, 0, out, 0, q.size)
        out[2] = (0x81).toByte(); out[3] = (0x80).toByte()
        out[6] = 0; out[7] = 0
        return out
    }

    private fun questionEnd(dns: ByteArray): Int {
        var i = 12
        while (i < dns.size) {
            val l = dns[i].toInt() and 0xFF
            if (l == 0) { i++; break }
            if (l and 0xC0 == 0xC0) { i += 2; break }
            i += 1 + l
        }
        return minOf(i + 4, dns.size)
    }

    private fun forward(query: ByteArray): ByteArray? {
        return try {
            val sock = DatagramSocket()
            protect(sock)
            sock.soTimeout = 4000
            sock.send(DatagramPacket(query, query.size, InetSocketAddress(InetAddress.getByName("1.1.1.1"), 53)))
            val buf = ByteArray(4096)
            val resp = DatagramPacket(buf, buf.size)
            sock.receive(resp)
            sock.close()
            resp.data.copyOfRange(0, resp.length)
        } catch (_: Exception) {
            null
        }
    }

    private fun wrapUdp(request: ByteArray, ihl: Int, payload: ByteArray): ByteArray {
        val srcPort = ((request[ihl].toInt() and 0xFF) shl 8) or (request[ihl + 1].toInt() and 0xFF)
        val dstPort = ((request[ihl + 2].toInt() and 0xFF) shl 8) or (request[ihl + 3].toInt() and 0xFF)
        val total = 20 + 8 + payload.size
        val out = ByteArray(total)
        // IPv4 header
        out[0] = 0x45
        out[1] = 0
        out[2] = ((total ushr 8) and 0xFF).toByte(); out[3] = (total and 0xFF).toByte()
        out[4] = 0; out[5] = 0
        out[6] = 0; out[7] = 0
        out[8] = 64
        out[9] = 17
        out[10] = 0; out[11] = 0
        // src = request dst, dst = request src
        System.arraycopy(request, 16, out, 12, 4)
        System.arraycopy(request, 12, out, 16, 4)
        val csum = ipChecksum(out, 0, 20)
        out[10] = ((csum ushr 8) and 0xFF).toByte(); out[11] = (csum and 0xFF).toByte()
        // UDP header
        out[20] = ((dstPort ushr 8) and 0xFF).toByte(); out[21] = (dstPort and 0xFF).toByte()
        out[22] = ((srcPort ushr 8) and 0xFF).toByte(); out[23] = (srcPort and 0xFF).toByte()
        val ulen = 8 + payload.size
        out[24] = ((ulen ushr 8) and 0xFF).toByte(); out[25] = (ulen and 0xFF).toByte()
        out[26] = 0; out[27] = 0 // checksum optional for IPv4
        System.arraycopy(payload, 0, out, 28, payload.size)
        return out
    }

    private fun ipChecksum(b: ByteArray, off: Int, len: Int): Int {
        var sum = 0
        var i = off
        while (i < off + len) {
            sum += ((b[i].toInt() and 0xFF) shl 8) or (b[i + 1].toInt() and 0xFF)
            i += 2
        }
        while (sum shr 16 != 0) sum = (sum and 0xFFFF) + (sum shr 16)
        return sum.inv() and 0xFFFF
    }

    private fun notification(): Notification {
        val nm = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= 26) {
            nm.createNotificationChannel(
                NotificationChannel(CHANNEL_ID, "SNI Relay VPN", NotificationManager.IMPORTANCE_LOW)
            )
        }
        val pi = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val b = if (Build.VERSION.SDK_INT >= 26)
            Notification.Builder(this, CHANNEL_ID)
        else
            @Suppress("DEPRECATION") Notification.Builder(this)
        return b.setContentTitle("SNI Relay")
            .setContentText("VPN активен: DNS relay-доменов → VDS")
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setContentIntent(pi)
            .setOngoing(true)
            .build()
    }

    override fun onRevoke() {
        stopVpn()
        super.onRevoke()
    }

    override fun onDestroy() {
        stopVpn()
        super.onDestroy()
    }
}
