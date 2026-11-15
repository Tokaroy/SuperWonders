// The settings menu's tabs (Video, Audio, Controls, Overlay, System), at parity with the Win32 menu
// (pc_menu_win.cpp), whose language settings it shares. The layout follows Savage Wind's in-game
// Settings window (github.com/Revivedx/SavageWindRecomp, runtime/ui/menu.mm menu_render and tabs_body, MIT): a title
// with a close X, tabs, and two-pane pages of option rows with the selected row's description on the right
// (option_rows.cpp). Descriptions are English for now.
#include <algorithm>
#include <cstdio>
#include <iterator>

#include "imgui.h"
#include "imgui_internal.h"  // NavCursorVisible

#include <nx/runtime.h>

#include "overlay/option_rows.h"
#include "overlay/overlay_internal.h"
#include "overlay/overlay_ui.h"

namespace nx::overlay {

namespace {

struct Labels {
    const char *title, *close, *video, *audio, *controls, *overlay, *window_size, *internal_resolution, *fullscreen,
        *topmost, *volume, *mute, *game_language, *game_language_restart, *control_preset, *standard_desc,
        *nintendo_desc, *restart_note, *frame_cap_label, *system, *memory_limit, *low_vram, *restart_low_vram;
};

constexpr Labels kLabels[] = {
    {"Quick Settings", "F1, Esc or Minus + Plus to close", "Video", "Audio", "Controls", "Overlay",
     "Window / output size", "Internal resolution", "Fullscreen", "Always on top", "Game volume", "Mute audio",
     "Game language", "Restart the game to apply the language.", "Button layout",
     "A: bottom (A / Cross)     B: right (B / Circle)", "A: right (B / Circle)     B: bottom (A / Cross)",
     "Internal resolution applies after restarting the game.", "Frame rate cap", "System", "Memory cleanup threshold", "Low VRAM profile",
     "Restart the game to apply the texture profile."},
    {"Impostazioni", "F1, Esc o Meno + Più per chiudere", "Video", "Audio", "Controlli", "Overlay",
     "Dimensione finestra / output", "Risoluzione interna", "Schermo intero", "Sempre in primo piano",
     "Volume gioco", "Disattiva audio", "Lingua del gioco", "Riavvia il gioco per applicare la lingua.",
     "Disposizione tasti", "A: in basso (A / Croce)     B: a destra (B / Cerchio)",
     "A: a destra (B / Cerchio)     B: in basso (A / Croce)", "La risoluzione interna si applica al riavvio.",
     "Limite FPS", "Sistema", "Soglia pulizia memoria", "Profilo VRAM ridotta",
     "Riavvia il gioco per applicare il profilo texture."},
    {"Einstellungen", "F1, Esc oder Minus + Plus zum Schließen", "Video", "Audio", "Steuerung", "Overlay",
     "Fenster- / Ausgabegröße", "Interne Auflösung", "Vollbild", "Immer im Vordergrund", "Spiel-Lautstärke",
     "Ton stummschalten", "Spielsprache", "Spiel zum Übernehmen neu starten.", "Tastenbelegung",
     "A: unten (A / Kreuz)     B: rechts (B / Kreis)", "A: rechts (B / Kreis)     B: unten (A / Kreuz)",
     "Interne Auflösung wird nach Neustart übernommen.", "FPS-Begrenzung", "System", "Speicherbereinigungsschwelle",
     "Niedriger VRAM-Verbrauch", "Spiel neu starten, um das Texturprofil anzuwenden."},
    {"Paramètres", "F1, Échap ou Moins + Plus pour fermer", "Vidéo", "Audio", "Commandes", "Overlay",
     "Taille fenêtre / sortie", "Résolution interne", "Plein écran", "Toujours au premier plan", "Volume du jeu",
     "Couper le son", "Langue du jeu", "Redémarrez le jeu pour appliquer la langue.", "Disposition des touches",
     "A : bas (A / Croix)     B : droite (B / Cercle)", "A : droite (B / Cercle)     B : bas (A / Croix)",
     "La résolution interne s'applique après redémarrage.", "Limite d’images/s", "Système", "Seuil de nettoyage mémoire",
     "Profil VRAM réduit", "Redémarrez pour appliquer le profil de textures."},
    {"Ajustes rápidos", "F1, Esc o Menos + Más para cerrar", "Vídeo", "Audio", "Controles", "Superposición",
     "Tamaño de ventana / salida", "Resolución interna", "Pantalla completa", "Siempre visible", "Volumen del juego", "Silenciar audio",
     "Idioma del juego", "Reinicia el juego para aplicar el idioma.", "Distribución de botones", "A: abajo (A / Cruz)     B: derecha (B / Círculo)",
     "A: derecha (B / Círculo)     B: abajo (A / Cruz)", "La resolución interna se aplica al reiniciar.", "Límite de FPS", "Sistema", "Umbral de limpieza de memoria", "Perfil de poca VRAM",
     "Reinicia el juego para aplicar el perfil de texturas."},
    {"빠른 설정", "F1, Esc 또는 마이너스 + 플러스로 닫기", "비디오", "오디오", "조작", "오버레이",
     "창 / 출력 크기", "내부 해상도", "전체 화면", "항상 위에 표시", "게임 음량", "음소거",
     "게임 언어", "언어를 적용하려면 게임을 다시 시작하세요.", "버튼 배치", "A: 아래 (A / 십자)     B: 오른쪽 (B / 원)",
     "A: 오른쪽 (B / 원)     B: 아래 (A / 십자)", "내부 해상도는 재시작 후 적용됩니다.", "프레임 제한", "시스템", "메모리 정리 기준", "저 VRAM 프로필",
     "텍스처 프로필을 적용하려면 게임을 다시 시작하세요."},
    {"クイック設定", "F1、Esc またはマイナス + プラスで閉じる", "映像", "音声", "操作", "オーバーレイ",
     "ウィンドウ / 出力サイズ", "内部解像度", "全画面表示", "常に手前に表示", "ゲーム音量", "ミュート",
     "ゲームの言語", "言語の変更にはゲームの再起動が必要です。", "ボタン配置", "A: 下 (A / クロス)     B: 右 (B / サークル)",
     "A: 右 (B / サークル)     B: 下 (A / クロス)", "内部解像度は再起動後に適用されます。", "フレームレート上限", "システム", "メモリ解放のしきい値", "低 VRAM プロファイル",
     "テクスチャ設定の適用には再起動が必要です。"},
    {"الإعدادات السريعة", "اضغط F1 أو Esc أو ناقص + زائد للإغلاق", "الفيديو", "الصوت", "التحكم", "عرض الأداء",
     "حجم النافذة / الإخراج", "الدقة الداخلية", "ملء الشاشة", "دائماً في المقدمة", "مستوى صوت اللعبة", "كتم الصوت",
     "لغة اللعبة", "أعد تشغيل اللعبة لتطبيق اللغة.", "تخطيط الأزرار", "A: أسفل     B: يمين", "A: يمين     B: أسفل",
     "تُطبق الدقة الداخلية بعد إعادة التشغيل.", "حد معدل الإطارات", "النظام", "حد تنظيف الذاكرة", "ملف VRAM منخفض",
     "أعد تشغيل اللعبة لتطبيق ملف الأنسجة."},
    {"Быстрые настройки", "F1, Esc или Минус + Плюс — закрыть", "Видео", "Звук", "Управление", "Оверлей",
     "Размер окна / вывода", "Внутреннее разрешение", "Полный экран", "Поверх других окон", "Громкость игры", "Без звука",
     "Язык игры", "Перезапустите игру, чтобы применить язык.", "Раскладка кнопок", "A: снизу     B: справа", "A: справа     B: снизу",
     "Внутреннее разрешение применится после перезапуска.", "Ограничение FPS", "Система", "Порог очистки памяти", "Профиль низкой VRAM",
     "Перезапустите игру, чтобы применить профиль текстур."},
    {"Hızlı Ayarlar", "Kapatmak için F1, Esc veya Eksi + Artı", "Video", "Ses", "Kontroller", "Performans katmanı",
     "Pencere / çıktı boyutu", "Dahili çözünürlük", "Tam ekran", "Her zaman üstte", "Oyun sesi", "Sesi kapat",
     "Oyun dili", "Dili uygulamak için oyunu yeniden başlatın.", "Düğme düzeni", "A: altta     B: sağda", "A: sağda     B: altta",
     "Dahili çözünürlük yeniden başlatınca uygulanır.", "Kare hızı sınırı", "Sistem", "Bellek temizleme eşiği", "Düşük VRAM profili",
     "Doku profilini uygulamak için oyunu yeniden başlatın."},
    {"Snelle instellingen", "Druk op F1, Esc of Min + Plus om te sluiten", "Beeld", "Audio", "Besturing", "Overlay",
     "Venster- / uitvoergrootte", "Interne resolutie", "Volledig scherm", "Altijd op voorgrond", "Spelvolume", "Geluid dempen",
     "Speltaal", "Start het spel opnieuw om de taal toe te passen.", "Knopindeling", "A: onder     B: rechts", "A: rechts     B: onder",
     "Interne resolutie wordt toegepast na opnieuw starten.", "FPS-limiet", "Systeem", "Geheugenopruimdrempel", "Laag-VRAM-profiel",
     "Start het spel opnieuw om het textuurprofiel toe te passen."},
    {"快速设置", "按 F1、Esc 或减号 + 加号关闭", "视频", "音频", "控制", "性能叠加层",
     "窗口 / 输出大小", "内部解析度", "全屏", "窗口置顶", "游戏音量", "静音",
     "游戏语言", "重新启动游戏以应用语言。", "按键布局", "A：下方     B：右侧", "A：右侧     B：下方",
     "重新启动游戏后应用内部解析度。", "帧率上限", "系统", "内存清理阈值", "低显存配置",
     "重新启动游戏以应用纹理配置。"},
    {"快速設定", "按 F1、Esc 或減號 + 加號關閉", "影片", "音訊", "控制", "效能覆蓋層",
     "視窗 / 輸出大小", "內部解析度", "全螢幕", "視窗置頂", "遊戲音量", "靜音",
     "遊戲語言", "重新啟動遊戲以套用語言。", "按鍵配置", "A：下方     B：右側", "A：右側     B：下方",
     "重新啟動遊戲後套用內部解析度。", "影格率上限", "系統", "記憶體清理閾值", "低顯示記憶體設定",
     "重新啟動遊戲以套用材質設定。"},
};
constexpr const char* kLanguageNames[] = {"English", "Italiano", "Deutsch", "Français", "Español", "한국어", "日本語", "العربية", "Русский", "Türkçe", "Nederlands", "简体中文", "繁體中文"};
constexpr const char* kGameLanguageNames[] = {"English", "Italiano", "Deutsch", "Français", "Português (Brasil)", "Español", "한국어", "日本語", "Nederlands", "Русский", "Simplified Chinese", "Traditional Chinese"};
constexpr const char* kInternal[13][7] = {
    {"Original / Dynamic", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Native (screen)"},
    {"Originale / dinamica", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Nativa (schermo)"},
    {"Original / Dynamisch", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Nativ (Bildschirm)"},
    {"Originale / dynamique", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Native (écran)"},
    {"Original / dinámico", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Nativa (pantalla)"},
    {"원본 / 동적", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "네이티브 (화면)"},
    {"オリジナル / 動的", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "ネイティブ (画面)"},
    {"أصلية / ديناميكية", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "أصلية (الشاشة)"},
    {"Исходное / динамическое", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Экранное"},
    {"Özgün / Dinamik", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Doğal (ekran)"},
    {"Origineel / dynamisch", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Schermresolutie"},
    {"原始 / 动态", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "屏幕原生"},
    {"原始 / 動態", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "螢幕原生"},
};
constexpr int kSizes[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
constexpr const char* kOverlayLabels[13][6] = {
    {"Metrics overlay", "FPS", "1% lows", "Game speed", "Frametime graph", "Overlay position"},
    {"Overlay metriche", "FPS", "Minimi 1%", "Velocità gioco", "Grafico frametime", "Posizione overlay"},
    {"Metrik-Overlay", "FPS", "1%-Tiefstwerte", "Spieltempo", "Frametime-Diagramm", "Overlay-Position"},
    {"Superposition métriques", "FPS", "1 % les plus bas", "Vitesse du jeu", "Graphique frametime",
     "Position de l’overlay"},
    {"Superposición de métricas", "FPS", "Mínimos del 1 %", "Velocidad del juego", "Gráfico de frametime", "Posición"},
    {"성능 오버레이", "FPS", "1% 하위 FPS", "게임 속도", "프레임 시간 그래프", "오버레이 위치"},
    {"パフォーマンス表示", "FPS", "1% Low", "ゲーム速度", "フレーム時間グラフ", "表示位置"},
    {"عرض الأداء", "FPS", "أدنى 1٪", "سرعة اللعبة", "مخطط زمن الإطار", "موضع العرض"},
    {"Оверлей производительности", "FPS", "1% минимум", "Скорость игры", "График времени кадра", "Положение"},
    {"Performans katmanı", "FPS", "%1 düşük", "Oyun hızı", "Kare süresi grafiği", "Konum"},
    {"Prestatie-overlay", "FPS", "1%-laagste", "Spelsnelheid", "Framet grafiek", "Overlaypositie"},
    {"性能叠加层", "FPS", "1%低帧", "游戏速度", "帧时间图表", "叠加层位置"},
    {"效能覆蓋層", "FPS", "1%低幀", "遊戲速度", "影格時間圖表", "覆蓋層位置"},
};
constexpr const char* kPositions[13][4] = {
    {"Top left", "Top right", "Bottom left", "Bottom right"},
    {"In alto a sinistra", "In alto a destra", "In basso a sinistra", "In basso a destra"},
    {"Oben links", "Oben rechts", "Unten links", "Unten rechts"},
    {"En haut à gauche", "En haut à droite", "En bas à gauche", "En bas à droite"},
    {"Arriba izquierda", "Arriba derecha", "Abajo izquierda", "Abajo derecha"},
    {"Sol üst", "Sağ üst", "Sol alt", "Sağ alt"},
    {"左上", "右上", "左下", "右下"},
    {"أعلى اليسار", "أعلى اليمين", "أسفل اليسار", "أسفل اليمين"},
    {"Сверху слева", "Сверху справа", "Снизу слева", "Снизу справа"},
    {"Sol üst", "Sağ üst", "Sol alt", "Sağ alt"},
    {"Linksboven", "Rechtsboven", "Linksonder", "Rechtsonder"},
    {"左上", "右上", "左下", "右下"},
    {"左上", "右上", "左下", "右下"},
};
constexpr const char* kHeaders[13][2] = {
    {"Keyboard", "Controller"}, {"Tastiera", "Controller"}, {"Tastatur", "Controller"}, {"Clavier", "Manette"},
    {"Teclado", "Mando"}, {"키보드", "컨트롤러"}, {"キーボード", "コントローラー"}, {"لوحة المفاتيح", "وحدة التحكم"},
    {"Клавиатура", "Контроллер"}, {"Klavye", "Kontrolcü"}, {"Toetsenbord", "Controller"},
    {"键盘", "控制器"}, {"鍵盤", "控制器"}};
// Secondary option names and section headings not covered by the main label table.
constexpr const char* kCommon[13][12] = {
    {"Display", "Image", "Frame pacing", "Upscaling", "FSR sharpness", "Anti-aliasing", "Anisotropic filtering", "VSync", "Rumble", "Game", "Quit game", "Menu language"},
    {"Schermo", "Immagine", "Sincronizzazione", "Upscaling", "Nitidezza FSR", "Anti-aliasing", "Filtro anisotropico", "V-Sync", "Vibrazione", "Gioco", "Esci dal gioco", "Lingua menu"},
    {"Anzeige", "Bild", "Frame-Pacing", "Upscaling", "FSR-Schärfe", "Kantenglättung", "Anisotrope Filterung", "VSync", "Vibration", "Spiel", "Spiel beenden", "Menüsprache"},
    {"Affichage", "Image", "Synchronisation", "Mise à l’échelle", "Netteté FSR", "Anticrénelage", "Filtrage anisotrope", "VSync", "Vibration", "Jeu", "Quitter le jeu", "Langue du menu"},
    {"Pantalla", "Imagen", "Ritmo de fotogramas", "Escalado", "Nitidez FSR", "Antialiasing", "Filtrado anisotrópico", "VSync", "Vibración", "Juego", "Salir del juego", "Idioma del menú"},
    {"디스플레이", "화면", "프레임 동기화", "업스케일링", "FSR 선명도", "안티앨리어싱", "비등방성 필터링", "VSync", "진동", "게임", "게임 종료", "메뉴 언어"},
    {"画面", "映像", "フレーム同期", "アップスケーリング", "FSR シャープネス", "アンチエイリアス", "異方性フィルタリング", "VSync", "振動", "ゲーム", "ゲームを終了", "メニュー言語"},
    {"الشاشة", "الصورة", "مزامنة الإطارات", "تحسين الدقة", "حدة FSR", "تنعيم الحواف", "ترشيح متباين الخواص", "مزامنة رأسية", "اهتزاز", "اللعبة", "إنهاء اللعبة", "لغة القائمة"},
    {"Экран", "Изображение", "Синхронизация кадров", "Масштабирование", "Резкость FSR", "Сглаживание", "Анизотропная фильтрация", "VSync", "Вибрация", "Игра", "Выйти из игры", "Язык меню"},
    {"Ekran", "Görüntü", "Kare zamanlaması", "Yükseltme", "FSR keskinliği", "Kenar yumuşatma", "Anizotropik filtreleme", "VSync", "Titreşim", "Oyun", "Oyundan çık", "Menü dili"},
    {"Beeldscherm", "Afbeelding", "Framepacing", "Opschaling", "FSR-scherpte", "Anti-aliasing", "Anisotrope filtering", "VSync", "Trillen", "Spel", "Spel afsluiten", "Menutaal"},
    {"显示", "画面", "帧同步", "升频", "FSR锐化", "抗锯齿", "各向异性过滤", "垂直同步", "震动", "游戏", "退出游戏", "菜单语言"},
    {"顯示", "畫面", "影格同步", "升頻", "FSR銳化", "反鋸齒", "各向異性過濾", "垂直同步", "震動", "遊戲", "結束遊戲", "選單語言"},
};
constexpr const char* kActions[kControlCount] = {
    "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "Plus", "Minus", "L Stick", "R Stick",
    "D-pad Left", "D-pad Up", "D-pad Right", "D-pad Down",
};
constexpr const char* kPhysical[kControlCount] = {
    "A / Cross", "B / Circle", "X / Square", "Y / Triangle", "LB / L1", "RB / R1", "LT / L2", "RT / R2",
    "Start / +", "Back / -", "L3 / LS", "R3 / RS", "D-pad Left", "D-pad Up", "D-pad Right", "D-pad Down",
};
constexpr int kTabs = 5;

namespace desc {
constexpr const char* language = "Language of this menu.";
constexpr const char* window_size = "Size of the game window in pixels. Not available in fullscreen.";
constexpr const char* internal = "The resolution the game renders at. Original lets the game choose (it lowers it under "
                                 "load); higher is sharper and costs more GPU time. Native renders at the size the "
                                 "image is shown, so nothing is stretched. Applies after restarting the game.";
constexpr const char* vsync = "How frames reach the screen. On: waits for the display (no tearing; smooth with "
                              "FreeSync/G-Sync). Fast: the newest frame at each refresh (the default). Adaptive: "
                              "waits unless a frame is late. Off: at once (can tear).";
constexpr const char* frame_cap = "Maximum frame rate. 30, 60, 90, 120 and 144 FPS are available. Higher caps need a "
                                  "display and PC that can sustain them; the game simulation remains at normal speed.";
constexpr const char* fullscreen = "Fill the whole screen. F11 also switches it.";
constexpr const char* topmost = "Keep the game window above other windows. Not available on Wayland.";
constexpr const char* volume = "The game's volume.";
constexpr const char* mute = "Silence the game without changing its volume.";
constexpr const char* game_language = "Select the game's text language. Changes apply after restarting.";
constexpr const char* preset = "Which controller button is A. Xbox / DualSense: A at the bottom. Nintendo: A on the "
                               "right, as on a Switch controller.";
constexpr const char* rumble = "How strongly the controller vibrates when the game asks for it. 0% turns it off.";
constexpr const char* key = "Select, then press the key for this action. Esc cancels.";
constexpr const char* pad = "The controller button for this action: left / right picks another. The layout's default "
                            "clears the change.";
constexpr const char* metrics = "A performance panel in a corner of the screen.";
constexpr const char* fps = "Frames per second over the last 60 frames.";
constexpr const char* low = "The frame rate of the slowest 1% of the last 300 frames: it shows stutter.";
constexpr const char* speed = "How fast the game runs against its own target: 100% is full speed.";
constexpr const char* graph = "The time of each of the last 300 frames, 0 to 50 ms.";
constexpr const char* position = "The corner the panel sits in.";
constexpr const char* memory = "On desktop, passing this threshold asks Vulkan to release unused reusable images and "
                               "keeps the game running. Android uses it as a safety stop. It cannot free textures in use "
                               "and is not a direct GPU VRAM limit.";
constexpr const char* upscaler = "How the game's image is enlarged when it is smaller than the window (a lower internal "
                                 "resolution, or fullscreen above 1080p). FSR 1 (AMD FidelityFX Super Resolution) keeps "
                                 "edges sharp; Bilinear is a plain stretch.";
constexpr const char* antialiasing = "Smooths jagged edges on the final image. FXAA is quick and can soften fine "
                                     "detail a little; the game's own anti-aliasing stays as it is.";
constexpr const char* anisotropy = "Keeps textures sharp on surfaces seen at an angle. Game default uses the game's "
                                   "own setting; higher costs a little GPU time. Applies after restarting the game.";
constexpr const char* sharpness = "FSR 1's sharpening: higher is sharper (more contrast on edges), lower is softer.";
constexpr const char* low_vram = "For graphics cards with little video memory: decoded textures are kept compressed "
                                 "(BC3), at a small cost in quality. Applies after restarting the game.";
constexpr const char* quit = "Close the game. Progress since the game last saved is lost. Select twice to confirm.";
}  // namespace desc

const Settings kDefaults{};

void video(MenuContext& c, const Labels& l) {
    Settings& s = c.settings;
    const int lang = std::clamp(s.language, 0, 12);
    rows::begin("##video", c.height);
    rows::section(kCommon[lang][0]);
    // the window's size: one of four, or the size it has now (dragged) as a fifth
    const char* sizes[5] = {"1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", nullptr};
    char custom[32];
    int size = -1;
    for (int i = 0; i < 4; ++i)
        if (kSizes[i][0] == s.width && kSizes[i][1] == s.height) size = i;
    int n = 4;
    if (size < 0) std::snprintf(custom, sizeof custom, "%d x %d", s.width, s.height), sizes[4] = custom, size = 4, n = 5;
    if (!c.host.can_resize || s.fullscreen) rows::disable_next();
    if (rows::choice(l.window_size, &size, sizes, n, 0, desc::window_size) && size < 4)
        s.width = kSizes[size][0], s.height = kSizes[size][1], settings_changed(true);
    if (rows::toggle(l.fullscreen, &s.fullscreen, kDefaults.fullscreen, desc::fullscreen)) settings_changed(true);
    if (!c.host.can_always_on_top) rows::disable_next();
    if (rows::toggle(l.topmost, &s.always_on_top, kDefaults.always_on_top, desc::topmost)) settings_changed(true);
    rows::section(kCommon[lang][1]);
    if (rows::choice(l.internal_resolution, &s.internal_resolution, kInternal[lang], 7, kDefaults.internal_resolution,
                     desc::internal))
        settings_changed(true), ui::notify(l.restart_note);
    const char* upscalers[] = {"Bilinear", "FSR 1"};
    if (rows::choice(kCommon[lang][3], &s.upscaler, upscalers, 2, kDefaults.upscaler, desc::upscaler)) settings_changed(true);
    rows::indent(1);
    if (s.upscaler != 1) rows::disable_next();
    int sharp = 200 - s.fsr_sharpness;  // shown as sharpness: 0 soft .. 200 sharp
    if (rows::integer(kCommon[lang][4], &sharp, 0, 200, 5, 200 - kDefaults.fsr_sharpness, "%d%%", desc::sharpness))
        s.fsr_sharpness = 200 - sharp, settings_changed(true);
    rows::indent(-1);
    const char* aa[] = {"Off", "FXAA"};
    if (rows::choice(kCommon[lang][5], &s.antialiasing, aa, 2, kDefaults.antialiasing, desc::antialiasing))
        settings_changed(true);
    const char* af_names[] = {"Game default", "2x", "4x", "8x", "16x"};
    const int af_levels[] = {0, 2, 4, 8, 16};
    int af = 0;
    for (int i = 0; i < 5; ++i)
        if (af_levels[i] == s.anisotropy) af = i;
    if (rows::choice(kCommon[lang][6], &af, af_names, 5, 0, desc::anisotropy))
        s.anisotropy = af_levels[af], settings_changed(true), ui::notify(l.restart_note);
    rows::section(kCommon[lang][2]);
    const char* vsync_names[] = {"On", "Fast", "Adaptive", "Off"};
    if (rows::choice(kCommon[lang][7], &s.vsync, vsync_names, 4, kDefaults.vsync, desc::vsync)) settings_changed(true);
    const char* caps[] = {"30 fps", "60 fps", "90 fps", "120 fps", "144 fps"};
    constexpr int cap_values[] = {30, 60, 90, 120, 144};
    int cap = 1;
    for (int i = 0; i < 5; ++i)
        if (cap_values[i] == s.frame_cap) cap = i;
    if (rows::choice(l.frame_cap_label, &cap, caps, 5, 1, desc::frame_cap))
        s.frame_cap = cap_values[cap], settings_changed(true);
    rows::page_note(l.restart_note);
    rows::end();
}

void audio(MenuContext& c, const Labels& l) {
    Settings& s = c.settings;
    rows::begin("##audio", c.height);
    rows::section(l.audio);
    if (rows::integer(l.volume, &s.volume, 0, 100, 5, kDefaults.volume, "%d%%", desc::volume)) settings_changed(true);
    if (rows::toggle(l.mute, &s.muted, kDefaults.muted, desc::mute)) settings_changed(true);
    rows::section(l.game_language);
    if (rows::choice(l.game_language, &s.game_language, kGameLanguageNames, int(std::size(kGameLanguageNames)),
                     kDefaults.game_language, desc::game_language))
        settings_changed(true), ui::notify(l.game_language_restart);
    rows::page_note(l.game_language_restart);
    rows::end();
}

void controls(MenuContext& c, const Labels& l) {
    Settings& s = c.settings;
    const int lang = std::clamp(s.language, 0, 12);
    rows::begin("##controls", c.height);
    rows::section(kHeaders[lang][1]);
    const char* presets[] = {"Xbox / DualSense", "Nintendo"};
    if (rows::choice(l.control_preset, &s.controller_preset, presets, 2, kDefaults.controller_preset, desc::preset))
        settings_changed(true);
    if (rows::integer(kCommon[lang][8], &s.rumble, 0, 100, 10, kDefaults.rumble, "%d%%", desc::rumble)) settings_changed(true);
    for (size_t a = 0; a < kControlCount; ++a) {
        int phys = controller_binding(s, a);
        const int def = default_controller_binding(a, s.controller_preset);
        char id[48];
        std::snprintf(id, sizeof id, "%s###pad%zu", kActions[a], a);
        if (rows::choice(id, &phys, kPhysical, int(kControlCount), def, desc::pad)) {
            s.controller_bindings[a] = phys;
            s.controller_custom_mask |= 1u << a;
            if (phys == def) s.controller_custom_mask &= ~(1u << a);
            settings_changed(true);
        }
    }
    rows::section(kHeaders[lang][0]);
    for (size_t a = 0; a < kControlCount; ++a) {
        char id[48];
        std::snprintf(id, sizeof id, "%s###key%zu", kActions[a], a);
        const bool waiting = c.capture_action == int(a);
        const char* name = waiting ? "Press a key..." : c.host.key_name ? c.host.key_name(s.keyboard_bindings[a]) : "?";
        if (rows::action(id, name, desc::key, true, waiting)) c.capture_action = int(a);  // overlay.cpp takes the next key
    }
    rows::page_note(s.controller_preset == 1 ? l.nintendo_desc : l.standard_desc);
    rows::end();
}

void overlay_tab(MenuContext& c) {
    Settings& s = c.settings;
    const int lang = std::clamp(s.language, 0, 12);
    const auto& o = kOverlayLabels[lang];
    rows::begin("##overlay", c.height);
    if (rows::toggle(o[0], &s.metrics_enabled, kDefaults.metrics_enabled, desc::metrics)) settings_changed(true);
    rows::indent(1);
    const struct {
        bool* v;
        bool def;
        const char *label, *desc;
    } items[] = {{&s.metric_fps, kDefaults.metric_fps, o[1], desc::fps},
                 {&s.metric_one_percent_low, kDefaults.metric_one_percent_low, o[2], desc::low},
                 {&s.metric_game_speed, kDefaults.metric_game_speed, o[3], desc::speed},
                 {&s.metric_frametime_graph, kDefaults.metric_frametime_graph, o[4], desc::graph}};
    for (const auto& it : items) {
        if (!s.metrics_enabled) rows::disable_next();
        if (rows::toggle(it.label, it.v, it.def, it.desc)) settings_changed(true);
    }
    rows::indent(-1);
    if (!s.metrics_enabled) rows::disable_next();
    if (rows::choice(o[5], &s.overlay_position, kPositions[lang], 4, kDefaults.overlay_position, desc::position))
        settings_changed(true);
    rows::end();
}

void system_tab(MenuContext& c, const Labels& l) {
    Settings& s = c.settings;
    rows::begin("##system", c.height);
    rows::section(l.system);
    const int lang = std::clamp(s.language, 0, 12);
    if (rows::choice(kCommon[lang][11], &s.language, kLanguageNames, int(std::size(kLanguageNames)), kDefaults.language, desc::language))
        settings_changed(true);
    if (rows::integer(l.memory_limit, &s.memory_limit_gb, 2, 32, 1, kDefaults.memory_limit_gb, "%d GB", desc::memory))
        settings_changed(true);
    if (rows::toggle(l.low_vram, &s.low_vram_profile, kDefaults.low_vram_profile, desc::low_vram))
        settings_changed(true), ui::notify(l.restart_low_vram);
    // Quit: A once arms it (the row pulses, "Press again to quit"), A again within 4 s quits as closing the window does
    // (the pipeline cache is saved); progress since the game's last save is lost
    static double armed_at = -10;
    const bool armed = ImGui::GetTime() - armed_at < 4.0;
    rows::section(kCommon[lang][9]);
    if (rows::action(kCommon[lang][10], armed ? "Press again to quit" : "", desc::quit, true, armed)) {
        if (armed) log("overlay: quit from the menu"), plat::exit_process(0);
        armed_at = ImGui::GetTime();
    }
    rows::page_note(l.restart_low_vram);
    rows::end();
}

// The close X in the top-right corner (as Savage Wind's menu.mm: lit when hovered or selected).
bool close_button(float k) {
    const float bx = 44 * k;
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
    const bool close = ImGui::InvisibleButton("##close", ImVec2(bx, bx), ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    const bool hot = ImGui::IsItemHovered() || (ImGui::IsItemFocused() && ImGui::GetCurrentContext()->NavCursorVisible);
    const float t = ui::eased(ImGui::GetItemID(), hot ? 1.0f : 0.0f, 22.0f);
    const ImU32 col = ui::fade(ui::mix(ui::theme().text_dim, ui::theme().accent, t));
    const float m = bx * 0.3f, w = (2.4f + 0.6f * t) * 1.5f * k;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + bx, p.y + bx), ui::fade(IM_COL32(255, 255, 255, 20), t), 8 * k);
    dl->AddLine(ImVec2(p.x + m, p.y + m), ImVec2(p.x + bx - m, p.y + bx - m), col, w);
    dl->AddLine(ImVec2(p.x + bx - m, p.y + m), ImVec2(p.x + m, p.y + bx - m), col, w);
    return close;
}

}  // namespace

int menu_tab_count() { return kTabs; }
const char* const* menu_language_names() { return kLanguageNames; }

void draw_menu(MenuContext& c) {
    Settings& s = c.settings;
    const Labels& l = kLabels[std::clamp(s.language, 0, 12)];
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float k = std::clamp(c.height / 1080.0f, 0.5f, 3.0f);
    // Savage Wind's in-game Settings window: up to 1500 px wide at 1080 p, the full height less a slim margin; it
    // rises a little as it fades in (c.reveal: 0..1).
    const ImVec2 size{std::min(display.x * 0.92f, 1500 * k), display.y - 2 * 22 * k};
    const float rise = (1.0f - ui::smoothstep(c.reveal)) * 26 * k;
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.5f + rise}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    if (c.appearing) ImGui::SetNextWindowFocus();  // keyboard and controller navigation start in the menu
    if (!ImGui::Begin("##nx_menu", nullptr, f)) {
        ImGui::End();
        return;
    }
    ImDrawList* wdl = ImGui::GetWindowDrawList();
    {  // a thin accent rule along the top edge
        const ImVec2 a = ImGui::GetWindowPos(), b(a.x + ImGui::GetWindowWidth(), a.y);
        wdl->AddRectFilled(ImVec2(a.x + 14 * k, a.y), ImVec2(b.x - 14 * k, a.y + 3 * k), ui::fade(ui::theme().accent, 0.85f));
    }
    ImGui::PushFont(ui::title());
    ImGui::SetWindowFontScale(0.74f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::theme().accent));
    ImGui::TextUnformatted(l.title);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();
    if (close_button(k)) c.open = false;

    const char* names[kTabs] = {l.video, l.audio, l.controls, l.overlay, l.system};
    int current = -1;
    // Tabs follow the keyboard / controller focus: moving onto a tab with the arrows or the D-pad shows it (next
    // frame), with no Enter or A needed. The mouse still selects by clicking. Only a change of focus moves the tab: L / R
    // select a tab without moving the focus, which must not pull it back.
    static int follow = -1, focused_before = -1, shown_tab = -1, hold_follow = 0;
    // L / R: the controller's cursor goes to the new page's first row, and following the focus pauses a few frames.
    // Otherwise the cursor, left on a row of the page that went away, fell back onto the first tab and the
    // tabs-follow-focus rule switched straight back (seen as "tab 1, tab 0" in the logs, before this change too).
    if (c.tab_request >= 0 && c.tab_request != shown_tab) rows::focus_first(), hold_follow = 4;
    static float page_a = 1, page_dir = 0, line_x0 = -1, line_x1 = -1;
    int follow_next = -1, focused = -1;
    ImVec2 tab_min[kTabs], tab_max[kTabs];
    const float footer = 64 * k;
    // Down with the cursor on the tab bar: the page's first row (Savage Wind found ImGui's own pick across the nested
    // panes unreliable, menu.mm tabs_body)
    if (focused_before >= 0 && ImGui::GetCurrentContext()->NavCursorVisible &&
        (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickDown) ||
         ImGui::IsKeyPressed(ImGuiKey_DownArrow)))
        rows::focus_first();
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));  // (the underline shows where the cursor is)
    if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_NoTooltip | ImGuiTabBarFlags_NoTabListScrollingButtons)) {
        for (int t = 0; t < kTabs; ++t) {
            ImGuiTabItemFlags tf = c.tab_request == t || follow == t ? ImGuiTabItemFlags_SetSelected : 0;
            char label[64];
            std::snprintf(label, sizeof label, "  %s  ###tab%d", names[t], t);
            const bool shown = ImGui::BeginTabItem(label, nullptr, tf);
            tab_min[t] = ImGui::GetItemRectMin(), tab_max[t] = ImGui::GetItemRectMax();
            if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible) focused = t;
            if (!shown && focused == t && focused_before != t && hold_follow == 0) follow_next = t;
            if (shown && c.appearing) ImGui::SetItemDefaultFocus();  // navigation starts on the open tab
            if (!shown) continue;
            current = t;
            if (shown_tab != t) {  // another page: it slides in from the side it was asked from, and fades in
                page_dir = shown_tab < 0 ? 0.0f : t > shown_tab ? 1.0f : -1.0f;
                page_a = shown_tab < 0 ? 1.0f : 0.0f, shown_tab = t;
            }
            page_a = ui::approach(page_a, 1.0f, 14.0f);
            ImGui::PopStyleColor();  // (the rows want their own)
            ImGui::Dummy(ImVec2(0, 4 * k));
            const ImVec2 at = ImGui::GetCursorPos();
            const float page_w = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPos(ImVec2(at.x + (1.0f - ui::smoothstep(page_a)) * 36 * k * page_dir, at.y));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (0.15f + 0.85f * ui::smoothstep(page_a)));
            ImGui::BeginChild("page", {page_w, -footer}, ImGuiChildFlags_NavFlattened);
            switch (t) {
                case 0: video(c, l); break;
                case 1: audio(c, l); break;
                case 2: controls(c, l); break;
                case 3: overlay_tab(c); break;
                default: system_tab(c, l); break;
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleColor();
    // the underline glides to the selected tab; brighter and thicker while the cursor is on the tab bar
    if (current >= 0) {
        const float x0 = tab_min[current].x + 6 * k, x1 = tab_max[current].x - 6 * k;
        if (line_x0 < 0 || c.appearing) line_x0 = x0, line_x1 = x1;
        line_x0 = ui::approach(line_x0, x0, 18.0f), line_x1 = ui::approach(line_x1, x1, 18.0f);
        const float on_bar = ui::eased(ImGui::GetID("##tabfocus"), focused >= 0 ? 1.0f : 0.0f, 18.0f);
        const float th = (3.0f + 2.0f * on_bar) * k, y = tab_max[current].y;
        wdl->AddRectFilled(ImVec2(line_x0, y - th), ImVec2(line_x1, y), ui::fade(ui::theme().accent, 0.75f + 0.25f * on_bar), th);
    }
    follow = follow_next;
    focused_before = focused;
    if (hold_follow > 0) hold_follow--;
    c.tab_request = current;  // out: the tab shown, for the shoulder buttons

    // the footer: what the buttons do, as Savage Wind's start screen shows them (badges)
    const ImVec2 wp = ImGui::GetWindowPos();
    const float fy = wp.y + ImGui::GetWindowHeight() - footer * 0.5f - 4 * k;
    wdl->AddLine(ImVec2(wp.x + 28 * k, fy - footer * 0.5f + 4 * k),
                 ImVec2(wp.x + ImGui::GetWindowWidth() - 28 * k, fy - footer * 0.5f + 4 * k), ui::fade(IM_COL32(255, 255, 255, 26)));
    const ui::Hint list[] = {{"A", "Select"}, {"B", "Back"}, {"L", ""}, {"R", "Tab"}, {"\xe2\x80\x93 +", "Close"},
                             {nullptr, "F1 / Esc"}};
    ui::hints(wdl, wp.x + ImGui::GetWindowWidth() * 0.5f, fy, 30 * k, list, int(sizeof list / sizeof list[0]));
    ImGui::End();
}

}  // namespace nx::overlay
