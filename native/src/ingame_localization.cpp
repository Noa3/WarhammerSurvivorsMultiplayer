#include "wsm/ingame_localization.h"

#include <array>
#include <cstddef>

namespace wsm::i18n {
namespace {
constexpr std::size_t count = static_cast<std::size_t>(Text::Count);
using Row = std::array<std::string_view, count>;
constexpr std::array<std::string_view, 13> languages = {
    "en", "de", "fr", "it", "es", "ja", "ko", "pl", "pt-BR", "ru", "zh-CN", "tr", "uk"
};
// Rows follow Text exactly; values remain static for the lifetime of the process.
constexpr std::array<Row, 13> catalog = {{
{{"Co-op [F6]", "Co-op connection", "IP address", "Port", "Host", "Join", "Leave",
  "Trusted LAN/VPN only. Lobby preview; co-op gameplay is not available yet.",
  "Ready.", "Not connected.", "Cannot leave now. Try again.", "Cannot host now. Try again.", "Cannot join now. Try again.",
  "Enter a port from 1 to 65535.", "Enter a host IP address.", "Action rejected. Check the connection and try again.",
  "Networking is missing. Check the installation and try again.", "Network status unavailable. Try again.",
  "Hosting on LAN, port {port}. Players: {peers}/3.", "Hosting locally, port {port}. Players: {peers}/3.",
  "Connecting...", "Connected.", "Host disconnected. Reconnect or host a new lobby.",
  "Connection refused. Check the address and host, then retry.", "Connection timed out. Check the network and retry.",
  "Connection rejected. Check game versions and retry.", "Lobby is full. Wait for a slot and retry.",
  "Host unreachable. Check the address and network, then retry.", "Port already in use. Choose another port and retry.",
  "Network is busy. Wait and retry.", "Invalid connection details. Check them and retry.", "Network error. Try again.", "Interface error. Close and reopen the panel."}},
{{"Koop [F6]", "Koop-Verbindung", "IP-Adresse", "Port", "Hosten", "Beitreten", "Verlassen",
  "Nur vertrauenswürdiges LAN/VPN. Lobby-Vorschau, noch kein Koop-Lauf.",
  "Bereit.", "Nicht verbunden.", "Verlassen derzeit nicht möglich. Erneut versuchen.", "Hosten derzeit nicht möglich. Erneut versuchen.", "Beitreten derzeit nicht möglich. Erneut versuchen.",
  "Port zwischen 1 und 65535 eingeben.", "IP-Adresse des Hosts eingeben.", "Aktion abgelehnt. Verbindung prüfen und erneut versuchen.",
  "Netzwerkkomponente fehlt. Installation prüfen und erneut versuchen.", "Netzwerkstatus nicht verfügbar. Erneut versuchen.",
  "LAN-Host, Port {port}. Mitspieler: {peers}/3.", "Lokaler Host, Port {port}. Mitspieler: {peers}/3.",
  "Verbinde...", "Verbunden.", "Host getrennt. Erneut verbinden oder neue Lobby erstellen.",
  "Verbindung verweigert. Adresse und Host prüfen, dann erneut versuchen.", "Zeitüberschreitung. Netzwerk prüfen und erneut versuchen.",
  "Verbindung abgelehnt. Spielversionen prüfen und erneut versuchen.", "Lobby voll. Auf freien Platz warten und erneut versuchen.",
  "Host nicht erreichbar. Adresse und Netzwerk prüfen, dann erneut versuchen.", "Port belegt. Anderen Port wählen und erneut versuchen.",
  "Netzwerk beschäftigt. Warten und erneut versuchen.", "Ungültige Verbindungsdaten. Prüfen und erneut versuchen.", "Netzwerkfehler. Erneut versuchen.", "Oberflächenfehler. Menü schließen und erneut öffnen."}},
{{"Coop [F6]", "Connexion coop", "Adresse IP", "Port", "Héberger", "Rejoindre", "Quitter",
  "LAN/VPN de confiance uniquement. Aperçu du salon ; partie coop indisponible.",
  "Prêt.", "Non connecté.", "Impossible de quitter. Réessayez.", "Impossible d'héberger. Réessayez.", "Impossible de rejoindre. Réessayez.",
  "Saisissez un port de 1 à 65535.", "Saisissez l'adresse IP de l'hôte.", "Action refusée. Vérifiez la connexion et réessayez.",
  "Module réseau absent. Vérifiez l'installation et réessayez.", "État du réseau indisponible. Réessayez.",
  "Hébergement LAN, port {port}. Joueurs : {peers}/3.", "Hébergement local, port {port}. Joueurs : {peers}/3.",
  "Connexion en cours...", "Connecté.", "Hôte déconnecté. Reconnectez-vous ou créez un salon.",
  "Connexion refusée. Vérifiez l'adresse et l'hôte, puis réessayez.", "Délai dépassé. Vérifiez le réseau et réessayez.",
  "Connexion rejetée. Vérifiez les versions du jeu et réessayez.", "Salon complet. Attendez une place et réessayez.",
  "Hôte inaccessible. Vérifiez l'adresse et le réseau, puis réessayez.", "Port occupé. Choisissez un autre port et réessayez.",
  "Réseau occupé. Attendez et réessayez.", "Paramètres invalides. Vérifiez-les et réessayez.", "Erreur réseau. Réessayez.", "Erreur d’interface. Fermez et rouvrez le menu."}},
{{"Co-op [F6]", "Connessione co-op", "Indirizzo IP", "Porta", "Ospita", "Unisciti", "Esci",
  "Solo LAN/VPN attendibili. Anteprima lobby; partita co-op non ancora disponibile.",
  "Pronto.", "Non connesso.", "Impossibile uscire. Riprova.", "Impossibile ospitare. Riprova.", "Impossibile unirsi. Riprova.",
  "Inserisci una porta da 1 a 65535.", "Inserisci l'indirizzo IP dell'host.", "Azione rifiutata. Controlla la connessione e riprova.",
  "Componente di rete assente. Controlla l'installazione e riprova.", "Stato della rete non disponibile. Riprova.",
  "Host LAN, porta {port}. Giocatori: {peers}/3.", "Host locale, porta {port}. Giocatori: {peers}/3.",
  "Connessione in corso...", "Connesso.", "Host disconnesso. Riconnettiti o crea una lobby.",
  "Connessione rifiutata. Controlla indirizzo e host, poi riprova.", "Tempo scaduto. Controlla la rete e riprova.",
  "Connessione respinta. Controlla le versioni del gioco e riprova.", "Lobby piena. Attendi un posto e riprova.",
  "Host irraggiungibile. Controlla indirizzo e rete, poi riprova.", "Porta occupata. Scegline un'altra e riprova.",
  "Rete occupata. Attendi e riprova.", "Dati di connessione non validi. Controlla e riprova.", "Errore di rete. Riprova.", "Errore di interfaccia. Chiudi e riapri il menu."}},
{{"Coop. [F6]", "Conexión cooperativa", "Dirección IP", "Puerto", "Crear", "Unirse", "Salir",
  "Solo LAN/VPN de confianza. Vista previa de sala; aún no hay partida cooperativa.",
  "Listo.", "Sin conexión.", "No se puede salir. Inténtalo de nuevo.", "No se puede crear la sala. Inténtalo de nuevo.", "No se puede unir. Inténtalo de nuevo.",
  "Introduce un puerto entre 1 y 65535.", "Introduce la IP del anfitrión.", "Acción rechazada. Comprueba la conexión y vuelve a intentarlo.",
  "Falta el componente de red. Comprueba la instalación y vuelve a intentarlo.", "Estado de red no disponible. Inténtalo de nuevo.",
  "Anfitrión LAN, puerto {port}. Jugadores: {peers}/3.", "Anfitrión local, puerto {port}. Jugadores: {peers}/3.",
  "Conectando...", "Conectado.", "Anfitrión desconectado. Reconecta o crea otra sala.",
  "Conexión denegada. Comprueba la dirección y el anfitrión y reintenta.", "Tiempo agotado. Comprueba la red y reintenta.",
  "Conexión rechazada. Comprueba las versiones del juego y reintenta.", "Sala llena. Espera una plaza y vuelve a intentarlo.",
  "Anfitrión inaccesible. Comprueba la dirección y la red y reintenta.", "Puerto ocupado. Elige otro y vuelve a intentarlo.",
  "Red ocupada. Espera y vuelve a intentarlo.", "Datos de conexión inválidos. Compruébalos y reintenta.", "Error de red. Inténtalo de nuevo.", "Error de interfaz. Cierra y vuelve a abrir el menú."}},
{{"協力 [F6]", "協力接続", "IPアドレス", "ポート", "ホスト", "参加", "退出",
  "信頼できるLAN/VPNのみ。ロビーのプレビューです。協力プレイは未実装です。",
  "準備完了。", "未接続。", "退出できません。再試行してください。", "ホストできません。再試行してください。", "参加できません。再試行してください。",
  "1～65535のポートを入力してください。", "ホストのIPアドレスを入力してください。", "操作が拒否されました。接続を確認して再試行してください。",
  "ネットワーク機能がありません。インストールを確認して再試行してください。", "ネットワーク状態を取得できません。再試行してください。",
  "LANホスト、ポート {port}。参加者: {peers}/3。", "ローカルホスト、ポート {port}。参加者: {peers}/3。",
  "接続中...", "接続済み。", "ホストが切断されました。再接続するか新しいロビーを作成してください。",
  "接続が拒否されました。アドレスとホストを確認して再試行してください。", "タイムアウトしました。ネットワークを確認して再試行してください。",
  "接続を拒否されました。ゲームのバージョンを確認して再試行してください。", "ロビーが満員です。空きを待って再試行してください。",
  "ホストに到達できません。アドレスとネットワークを確認して再試行してください。", "ポート使用中です。別のポートで再試行してください。",
  "ネットワークがビジーです。待ってから再試行してください。", "接続情報が無効です。確認して再試行してください。", "ネットワークエラー。再試行してください。", "画面エラー。メニューを閉じて開き直してください。"}},
{{"협동 [F6]", "협동 연결", "IP 주소", "포트", "호스트", "참가", "나가기",
  "신뢰할 수 있는 LAN/VPN만 사용하세요. 로비 미리보기이며 협동 플레이는 아직 불가능합니다.",
  "준비 완료.", "연결되지 않음.", "나갈 수 없습니다. 다시 시도하세요.", "호스트할 수 없습니다. 다시 시도하세요.", "참가할 수 없습니다. 다시 시도하세요.",
  "1~65535 사이의 포트를 입력하세요.", "호스트 IP 주소를 입력하세요.", "요청이 거부되었습니다. 연결을 확인하고 다시 시도하세요.",
  "네트워크 구성 요소가 없습니다. 설치를 확인하고 다시 시도하세요.", "네트워크 상태를 사용할 수 없습니다. 다시 시도하세요.",
  "LAN 호스트, 포트 {port}. 참가자: {peers}/3.", "로컬 호스트, 포트 {port}. 참가자: {peers}/3.",
  "연결 중...", "연결됨.", "호스트 연결이 끊겼습니다. 다시 연결하거나 새 로비를 만드세요.",
  "연결이 거부되었습니다. 주소와 호스트를 확인하고 다시 시도하세요.", "시간이 초과되었습니다. 네트워크를 확인하고 다시 시도하세요.",
  "연결이 거절되었습니다. 게임 버전을 확인하고 다시 시도하세요.", "로비가 가득 찼습니다. 자리를 기다린 후 다시 시도하세요.",
  "호스트에 연결할 수 없습니다. 주소와 네트워크를 확인하고 다시 시도하세요.", "포트 사용 중. 다른 포트를 선택하고 다시 시도하세요.",
  "네트워크가 사용 중입니다. 기다린 후 다시 시도하세요.", "연결 정보가 잘못되었습니다. 확인하고 다시 시도하세요.", "네트워크 오류. 다시 시도하세요.", "화면 오류. 메뉴를 닫고 다시 여세요."}},
{{"Koop. [F6]", "Połączenie kooperacyjne", "Adres IP", "Port", "Utwórz", "Dołącz", "Opuść",
  "Tylko zaufana sieć LAN/VPN. Podgląd lobby; rozgrywka kooperacyjna jeszcze niedostępna.",
  "Gotowe.", "Brak połączenia.", "Nie można opuścić. Spróbuj ponownie.", "Nie można utworzyć. Spróbuj ponownie.", "Nie można dołączyć. Spróbuj ponownie.",
  "Wpisz port od 1 do 65535.", "Wpisz adres IP hosta.", "Odrzucono działanie. Sprawdź połączenie i spróbuj ponownie.",
  "Brak modułu sieciowego. Sprawdź instalację i spróbuj ponownie.", "Stan sieci niedostępny. Spróbuj ponownie.",
  "Host LAN, port {port}. Gracze: {peers}/3.", "Host lokalny, port {port}. Gracze: {peers}/3.",
  "Łączenie...", "Połączono.", "Host rozłączony. Połącz się ponownie lub utwórz lobby.",
  "Odmowa połączenia. Sprawdź adres i hosta, a potem spróbuj ponownie.", "Przekroczono czas. Sprawdź sieć i spróbuj ponownie.",
  "Odrzucono połączenie. Sprawdź wersje gry i spróbuj ponownie.", "Lobby pełne. Poczekaj na miejsce i spróbuj ponownie.",
  "Host nieosiągalny. Sprawdź adres i sieć, a potem spróbuj ponownie.", "Port zajęty. Wybierz inny i spróbuj ponownie.",
  "Sieć zajęta. Poczekaj i spróbuj ponownie.", "Nieprawidłowe dane połączenia. Sprawdź je i spróbuj ponownie.", "Błąd sieci. Spróbuj ponownie.", "Błąd interfejsu. Zamknij i otwórz menu."}},
{{"Coop [F6]", "Conexão cooperativa", "Endereço IP", "Porta", "Hospedar", "Entrar", "Sair",
  "Apenas LAN/VPN confiável. Prévia da sala; partida cooperativa ainda indisponível.",
  "Pronto.", "Desconectado.", "Não é possível sair. Tente novamente.", "Não é possível hospedar. Tente novamente.", "Não é possível entrar. Tente novamente.",
  "Informe uma porta de 1 a 65535.", "Informe o IP do anfitrião.", "Ação rejeitada. Verifique a conexão e tente novamente.",
  "Componente de rede ausente. Verifique a instalação e tente novamente.", "Estado da rede indisponível. Tente novamente.",
  "Hospedando na LAN, porta {port}. Jogadores: {peers}/3.", "Hospedando localmente, porta {port}. Jogadores: {peers}/3.",
  "Conectando...", "Conectado.", "Anfitrião desconectado. Reconecte ou crie outra sala.",
  "Conexão recusada. Verifique o endereço e o anfitrião e tente novamente.", "Tempo esgotado. Verifique a rede e tente novamente.",
  "Conexão rejeitada. Verifique as versões do jogo e tente novamente.", "Sala cheia. Aguarde uma vaga e tente novamente.",
  "Anfitrião inacessível. Verifique o endereço e a rede e tente novamente.", "Porta em uso. Escolha outra e tente novamente.",
  "Rede ocupada. Aguarde e tente novamente.", "Dados de conexão inválidos. Verifique e tente novamente.", "Erro de rede. Tente novamente.", "Erro de interface. Feche e reabra o menu."}},
{{"Кооп [F6]", "Кооперативное соединение", "IP-адрес", "Порт", "Создать", "Вступить", "Выйти",
  "Только доверенная LAN/VPN. Предпросмотр лобби; совместная игра пока недоступна.",
  "Готово.", "Нет соединения.", "Не удалось выйти. Повторите попытку.", "Не удалось создать лобби. Повторите попытку.", "Не удалось вступить. Повторите попытку.",
  "Введите порт от 1 до 65535.", "Введите IP-адрес хоста.", "Действие отклонено. Проверьте соединение и повторите попытку.",
  "Сетевой компонент отсутствует. Проверьте установку и повторите попытку.", "Состояние сети недоступно. Повторите попытку.",
  "Хост LAN, порт {port}. Игроков: {peers}/3.", "Локальный хост, порт {port}. Игроков: {peers}/3.",
  "Подключение...", "Подключено.", "Хост отключился. Подключитесь снова или создайте лобби.",
  "Соединение отклонено. Проверьте адрес и хост и повторите попытку.", "Время ожидания истекло. Проверьте сеть и повторите попытку.",
  "Подключение отклонено. Проверьте версии игры и повторите попытку.", "Лобби заполнено. Дождитесь места и повторите попытку.",
  "Хост недоступен. Проверьте адрес и сеть и повторите попытку.", "Порт занят. Выберите другой и повторите попытку.",
  "Сеть занята. Подождите и повторите попытку.", "Неверные данные соединения. Проверьте их и повторите попытку.", "Ошибка сети. Повторите попытку.", "Ошибка интерфейса. Закройте и откройте меню."}},
{{"合作 [F6]", "合作连接", "IP地址", "端口", "创建主机", "加入", "离开",
  "仅限可信的局域网/VPN。此为大厅预览，合作游戏尚不可用。",
  "就绪。", "未连接。", "无法离开，请重试。", "无法创建主机，请重试。", "无法加入，请重试。",
  "请输入1到65535之间的端口。", "请输入主机IP地址。", "操作被拒绝。请检查连接并重试。",
  "缺少网络组件。请检查安装并重试。", "网络状态不可用，请重试。",
  "局域网主机，端口 {port}。玩家：{peers}/3。", "本地主机，端口 {port}。玩家：{peers}/3。",
  "正在连接...", "已连接。", "主机已断开。请重新连接或创建新大厅。",
  "连接被拒绝。请检查地址和主机后重试。", "连接超时。请检查网络并重试。",
  "连接被拒绝。请检查游戏版本并重试。", "大厅已满。请等待空位后重试。",
  "无法连接主机。请检查地址和网络后重试。", "端口已被占用。请选择其他端口并重试。",
  "网络繁忙。请稍后重试。", "连接信息无效。请检查后重试。", "网络错误，请重试。", "界面错误。请关闭并重新打开菜单。"}},
{{"Eşli [F6]", "Eşli bağlantı", "IP adresi", "Port", "Oluştur", "Katıl", "Ayrıl",
  "Yalnızca güvenilir LAN/VPN. Lobi önizlemesi; eşli oyun henüz kullanılamıyor.",
  "Hazır.", "Bağlı değil.", "Ayrılamıyor. Tekrar deneyin.", "Oluşturulamıyor. Tekrar deneyin.", "Katılınamıyor. Tekrar deneyin.",
  "1 ile 65535 arasında bir port girin.", "Sunucunun IP adresini girin.", "İşlem reddedildi. Bağlantıyı kontrol edip tekrar deneyin.",
  "Ağ bileşeni eksik. Kurulumu kontrol edip tekrar deneyin.", "Ağ durumu kullanılamıyor. Tekrar deneyin.",
  "LAN sunucusu, port {port}. Oyuncular: {peers}/3.", "Yerel sunucu, port {port}. Oyuncular: {peers}/3.",
  "Bağlanılıyor...", "Bağlandı.", "Sunucu bağlantısı kesildi. Yeniden bağlanın veya lobi oluşturun.",
  "Bağlantı reddedildi. Adresi ve sunucuyu kontrol edip tekrar deneyin.", "Zaman aşımı. Ağı kontrol edip tekrar deneyin.",
  "Bağlantı reddedildi. Oyun sürümlerini kontrol edip tekrar deneyin.", "Lobi dolu. Yer açılmasını bekleyip tekrar deneyin.",
  "Sunucuya ulaşılamıyor. Adresi ve ağı kontrol edip tekrar deneyin.", "Port kullanımda. Başka bir port seçip tekrar deneyin.",
  "Ağ meşgul. Bekleyip tekrar deneyin.", "Bağlantı bilgileri geçersiz. Kontrol edip tekrar deneyin.", "Ağ hatası. Tekrar deneyin.", "Arayüz hatası. Menüyü kapatıp tekrar açın."}},
{{"Кооп [F6]", "Спільне з'єднання", "IP-адреса", "Порт", "Створити", "Приєднатися", "Вийти",
  "Лише довірена LAN/VPN. Попередній перегляд лобі; спільна гра ще недоступна.",
  "Готово.", "Не підключено.", "Не вдалося вийти. Спробуйте ще раз.", "Не вдалося створити лобі. Спробуйте ще раз.", "Не вдалося приєднатися. Спробуйте ще раз.",
  "Введіть порт від 1 до 65535.", "Введіть IP-адресу хоста.", "Дію відхилено. Перевірте з'єднання та спробуйте ще раз.",
  "Мережевий компонент відсутній. Перевірте встановлення та спробуйте ще раз.", "Стан мережі недоступний. Спробуйте ще раз.",
  "Хост LAN, порт {port}. Гравців: {peers}/3.", "Локальний хост, порт {port}. Гравців: {peers}/3.",
  "Підключення...", "Підключено.", "Хост відключився. Підключіться знову або створіть лобі.",
  "У з'єднанні відмовлено. Перевірте адресу й хост та спробуйте ще раз.", "Час очікування минув. Перевірте мережу та спробуйте ще раз.",
  "З'єднання відхилено. Перевірте версії гри та спробуйте ще раз.", "Лобі заповнене. Дочекайтеся місця та спробуйте ще раз.",
  "Хост недоступний. Перевірте адресу й мережу та спробуйте ще раз.", "Порт зайнятий. Виберіть інший та спробуйте ще раз.",
  "Мережа зайнята. Зачекайте та спробуйте ще раз.", "Невірні дані з'єднання. Перевірте їх та спробуйте ще раз.", "Помилка мережі. Спробуйте ще раз.", "Помилка інтерфейсу. Закрийте й знову відкрийте меню."}}
}};

std::string folded(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (char c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == '_') c = '-';
        result.push_back(c);
    }
    return result;
}
} // namespace

std::string normalize_language(std::string_view code) {
    const auto value = folded(code);
    constexpr std::array<std::string_view, 13> names = {
        "english", "german", "french", "italian", "spanish", "japanese", "korean", "polish",
        "portuguese", "russian", "chinese", "turkish", "ukrainian"
    };
    for (std::size_t i = 0; i < languages.size(); ++i) {
        const auto key = folded(languages[i]);
        if (value == key || value == names[i]) return std::string(languages[i]);
    }
    if (value == "portuguese (brazil)" || value == "brazilian portuguese") return "pt-BR";
    if (value == "simplified chinese" || value == "chinese (simplified)" || value == "zh-chs" || value == "zh-hans" || value.rfind("zh-hans-", 0) == 0) return "zh-CN";
    if (value == "spanish (spain)" || value == "spanish - spain") return "es";
    // A regional suffix is accepted for supported base languages. Other Chinese
    // scripts and regions must not silently map to Simplified Chinese.
    auto dash = value.find('-');
    auto base = value.substr(0, dash);
    if (base == "pt") return "pt-BR";
    if (base == "zh") return "en";
    for (std::size_t i = 0; i < languages.size(); ++i)
        if (base == languages[i] && (dash == std::string::npos || dash + 1 < value.size())) return std::string(languages[i]);
    return "en";
}

std::string_view text(Text id, std::string_view language) {
    auto index = static_cast<std::size_t>(id);
    if (index >= count) index = static_cast<std::size_t>(Text::NetworkUnavailable);
    const auto canonical = normalize_language(language);
    for (std::size_t i = 0; i < languages.size(); ++i)
        if (canonical == languages[i]) return catalog[i][index];
    return catalog[0][index];
}

std::string format(Text id, std::string_view language,
                   std::initializer_list<std::pair<std::string_view, std::string>> values) {
    const auto input = text(id, language);
    std::string output;
    for (std::size_t pos = 0; pos < input.size();) {
        const auto start = input.find('{', pos);
        if (start == input.npos) { output.append(input.substr(pos)); break; }
        output.append(input.substr(pos, start - pos));
        const auto end = input.find('}', start + 1);
        if (end == input.npos) { output.append(input.substr(start)); break; }
        const auto name = input.substr(start + 1, end - start - 1);
        bool replaced = false;
        for (const auto& [key, value] : values) {
            if (key == name) { output.append(value); replaced = true; break; }
        }
        if (!replaced) output.append(input.substr(start, end - start + 1));
        pos = end + 1;
    }
    return output;
}

} // namespace wsm::i18n
