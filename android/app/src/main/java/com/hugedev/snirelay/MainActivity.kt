package com.hugedev.snirelay

import android.app.Activity
import android.net.VpnService
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import com.hugedev.snirelay.ui.AppState
import com.hugedev.snirelay.ui.SniRelayApp
import com.hugedev.snirelay.ui.SniRelayTheme
import com.hugedev.snirelay.vpn.RelayVpnService

class MainActivity : ComponentActivity() {

    private val vm: AppState by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val vpnLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { res ->
            if (res.resultCode == Activity.RESULT_OK) vm.startVpnService()
            else vm.logLine("[!] VPN не разрешён")
        }
        setContent {
            SniRelayTheme {
                SniRelayApp(vm, onStartVpn = {
                    val intent = VpnService.prepare(this)
                    if (intent != null) vpnLauncher.launch(intent) else vm.startVpnService()
                })
            }
        }
    }

    override fun onResume() {
        super.onResume()
        vm.vpnRunning = RelayVpnService.running
    }
}
