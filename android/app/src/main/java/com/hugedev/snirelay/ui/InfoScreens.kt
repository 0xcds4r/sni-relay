package com.hugedev.snirelay.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.clickable
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.unit.sp

@Composable
fun FaqScreen() {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        SectionCard("Частые проблемы") {
            FaqItem(
                "app-unavailable-in-region",
                "VDS в неподдерживаемой стране — нужен VDS в поддерживаемой (для Claude/ChatGPT — не РФ).",
            )
            FaqItem(
                "403 cf-mitigated: challenge не проходит",
                "Датацентровый IP VDS; нужен резидентный IP или прохождение капчи в браузере.",
            )
            FaqItem(
                "Релей не сработал",
                "Включён DoH/VPN. Выключи Secure DNS в браузере; на Android — включи VPN на вкладке «Клиент».",
            )
            FaqItem(
                "Сайт видит всех как 127.0.0.1",
                "Забыт proxy_protocol в listen сайта или real_ip_header на VDS.",
            )
            FaqItem(
                "MTProto: Telegram не подключается",
                "Проверь, что mtg слушает на 443 через релей и что фронт-домен не заблокирован. Попробуй другой фронт-домен.",
            )
            FaqItem(
                "«map directive is not allowed here»",
                "map должен быть внутри stream {} — переразверни релей из приложения.",
            )
        }
    }
}

@Composable
private fun FaqItem(q: String, a: String) {
    Column {
        Text(q, color = TextMain, fontSize = 14.sp, fontWeight = androidx.compose.ui.text.font.FontWeight.SemiBold)
        Text(a, color = TextDim, fontSize = 13.sp)
    }
}

@Composable
fun AboutScreen() {
    val uriHandler = LocalUriHandler.current
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        SectionCard("SNI Relay Manager") {
            Text("Версия 1.3.1 (Android)", color = TextMain)
            Text("GUI для своего SNI-релея на VDS: развёртывание, домены, VPN-клиент, проверка, MTProto.", color = TextDim, fontSize = 13.sp)
            Text("© 0xcds4r · MIT", color = TextDim, fontSize = 13.sp)
        }
        SectionCard("Ссылки") {
            Text(
                "Исходники и гайд · github.com/0xcds4r/sni-relay",
                color = Accent, fontSize = 13.sp,
                modifier = Modifier.clickable { uriHandler.openUri("https://github.com/0xcds4r/sni-relay") },
            )
            Text(
                "MTProto-гайд",
                color = Accent, fontSize = 13.sp,
                modifier = Modifier.clickable { uriHandler.openUri("https://github.com/0xcds4r/sni-relay/blob/main/MTProto.md") },
            )
            Text(
                "Релизы",
                color = Accent, fontSize = 13.sp,
                modifier = Modifier.clickable { uriHandler.openUri("https://github.com/0xcds4r/sni-relay/releases") },
            )
        }
    }
}
