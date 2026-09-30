package com.hugedev.snirelay.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ScrollableTabRow
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

enum class TabId(val title: String) {
    VDS("VDS"), DOMAINS("Домены"), RELAY("Релей"), CLIENT("Клиент"),
    CHECK("Проверка"), MTPROTO("MTProto"), INTEGRATIONS("Интеграции"),
    FAQ("FAQ"), ABOUT("О программе"),
}

@Composable
fun SniRelayApp(state: AppState, onStartVpn: () -> Unit) {
    var tab by remember { mutableStateOf(TabId.VDS) }
    LaunchedEffect(tab) {
        if (tab == TabId.MTPROTO && state.settings.host.isNotBlank()) {
            state.refreshMtgStatus()
        }
    }
    Box(Modifier.fillMaxSize().background(Bg)) {
        Column(Modifier.fillMaxSize()) {
            ScrollableTabRow(
                selectedTabIndex = tab.ordinal,
                containerColor = Bg,
                contentColor = Accent,
                edgePadding = 8.dp,
            ) {
                TabId.entries.forEach { t ->
                    Tab(
                        selected = tab == t,
                        onClick = { tab = t },
                        text = { Text(t.title, color = if (tab == t) Accent else TextDim, fontSize = 13.sp) },
                    )
                }
            }
            Box(Modifier.weight(1f)) {
                when (tab) {
                    TabId.VDS -> VdsScreen(state)
                    TabId.DOMAINS -> DomainsScreen(state)
                    TabId.RELAY -> RelayScreen(state)
                    TabId.CLIENT -> ClientScreen(state, onStartVpn)
                    TabId.CHECK -> CheckScreen(state)
                    TabId.MTPROTO -> MtprotoScreen(state)
                    TabId.INTEGRATIONS -> IntegrationsScreen(state)
                    TabId.FAQ -> FaqScreen()
                    TabId.ABOUT -> AboutScreen()
                }
            }
            LogPanel(state)
        }
        state.busy?.let { BusyOverlay(it) }
    }
}

@Composable
private fun LogPanel(state: AppState) {
    var expanded by remember { mutableStateOf(true) }
    Column(Modifier.fillMaxWidth().background(Card)) {
        Row(
            Modifier.fillMaxWidth().padding(start = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("Лог", color = TextDim, fontSize = 12.sp, modifier = Modifier.weight(1f))
            TextButton(onClick = { expanded = !expanded }) { Text(if (expanded) "Свернуть" else "Развернуть", fontSize = 12.sp) }
            TextButton(onClick = { state.log.clear() }) { Text("Очистить", fontSize = 12.sp) }
        }
        if (expanded) {
            val scroll = rememberScrollState()
            LaunchedEffect(state.log.size) { scroll.animateScrollTo(scroll.maxValue) }
            Box(
                Modifier
                    .fillMaxWidth()
                    .height(150.dp)
                    .verticalScroll(scroll)
                    .padding(horizontal = 12.dp),
            ) {
                Text(
                    if (state.log.isEmpty()) "—" else state.log.joinToString("\n"),
                    color = TextMain,
                    fontFamily = FontFamily.Monospace,
                    fontSize = 11.sp,
                )
            }
        }
    }
}

@Composable
private fun BusyOverlay(text: String) {
    Box(Modifier.fillMaxSize().background(Color(0xCC0F1014)), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            CircularProgressIndicator(color = Accent)
            Text(text, color = TextMain, modifier = Modifier.padding(top = 12.dp))
        }
    }
}
