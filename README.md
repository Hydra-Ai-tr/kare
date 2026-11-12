# Kare

Prompt yazın, videoyu bilgisayarınızda oluşturun ve kurgulayın.

Kare, C++17 ile yazılmış yerel bir video stüdyosudur. Bir cümle ya da madde listesi verirsiniz; uygulama bunu süreli sahnelere böler, renk ve hareketi seçer, kareleri işlemcinizde çizer ve MP4 ya da AVI olarak kaydeder. İnternet gerekmez. İsterseniz sahne metnini Ollama, LM Studio, Kor Studio ya da bir bulut API'si yazsın; pikseller yine bu bilgisayarda üretilir.

0.1 sürümü hazır bir video difüzyon modeli değildir. İndirilecek dev bir model dosyası yoktur. Ekrandaki görüntü; başlık, metin, renk, ışık ve kamera hareketinden oluşan bir kurgu kartıdır. Metni, süreyi, sırayı ve görünümü siz değiştirir, sonra yeniden dışa aktarırsınız.

## Ne yapar

- Türkçe ve İngilizce promptu çevrimdışı kurgular (`8 saniye`, `dikey`, `başlık: "Boğaz"`, madde listesi).
- Görünümler: sinema, gün batımı, gece, deniz, orman, neon, kağıt, sade.
- Hareket: zoom, drift, rise. Sahneler arasında kararma.
- Kurgu: metin, süre, sıra, çoğaltma, silme, süreyi yeniden dağıtma.
- Önizlemeyi pencerede oynatır.
- Windows'ta H.264 MP4 (Media Foundation). Her yerde oynatılabilen MJPEG AVI.
- İsteğe bağlı OpenAI uyumlu API, yalnızca hikâye panosu için.

## Kurulum

[Sürümler](https://github.com/Hydra-Ai-tr/kare/releases/latest) sayfasından `kare-0.1.0-windows-x64.zip` dosyasını indirip açın. `kare.exe` masaüstü uygulamasıdır, `kare-cli.exe` komut satırıdır. Windows 10 veya daha yenisi gerekir.

İmza henüz yok. SmartScreen uyarırsa **Diğer bilgiler → Yine de çalıştır**.

## Hızlı başlangıç

1. **Oluştur** sayfasına bir prompt yazın. Örnek: `Gün batımında sahil, 8 saniye. Başlık: "Boğaz"`
2. **Planla**. Kurgu sayfasında sahneleri değiştirin.
3. Alttaki yola bir `.mp4` yazıp **Dışa aktar**.

Komut satırı:

```text
kare-cli plan "Gün batımında sahil, 8 saniye"
kare-cli render "Dikey tanıtım" -o tanitim.mp4 --seconds 10 --aspect 9:16
kare-cli frame "Night market, 6 seconds" -o kare.png
kare-cli render --project proje.kare -o cikti.mp4
kare-cli selftest
```

Dikey video için prompta `dikey` veya `9:16` yazın. Kare kare için `1:1` veya `square`. Süreyi `8 saniye` / `6 seconds` diye söyleyin. Başlığı `Başlık: "Boğaz"` veya `title: Hello` diye ayırın.

## API

Ayarlar'dan bir adres seçin. Kapalıyken her şey çevrimdışıdır. Açıkken yalnızca prompt, seçtiğiniz adrese gider; video kareleri gitmez.

| Sağlayıcı | Adres |
|---|---|
| Ollama | `http://127.0.0.1:11434/v1` |
| LM Studio | `http://127.0.0.1:1234/v1` |
| Kor Studio | `http://127.0.0.1:8080/v1` (`kor-cli serve`) |
| OpenAI, Groq | bulut, API anahtarı gerekir |

Anahtar `%APPDATA%\Kare\config.json` içindedir. API cevap vermezse Kare yerel kurgusuna döner ve nedenini yazar.

## Derleme

CMake 3.20+, Ninja ve MinGW-w64 GCC 13+ (örneğin `winget install BrechtSanders.WinLibs.POSIX.UCRT`) ya da Visual Studio 2022. GLFW ve Dear ImGui, CMake tarafından indirilir.

```powershell
git clone https://github.com/Hydra-Ai-tr/kare
cd kare
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\bin\kare-cli.exe selftest
build\bin\kare.exe
```

## Gizlilik

Çevrimdışı kurgu ve dışa aktarma ağ kullanmaz. Ağ, yalnızca Ayarlar'dan açtığınız API içindir.

## Lisans

[MIT](LICENSE). İstediğiniz gibi kullanabilir, değiştirebilir ve dağıtabilirsiniz.

Üçüncü taraf: [Dear ImGui](https://github.com/ocornut/imgui) (MIT), [GLFW](https://www.glfw.org/) (zlib), [stb](https://github.com/nothings/stb) (public domain), [Roboto](https://github.com/googlefonts/roboto) (Apache-2.0, `assets/fonts/LICENSE-ROBOTO.txt`).

---

# English

Write a prompt. Kare builds the video on your computer, and you can edit it before export.

Kare is a local video studio written in C++17. You give it a sentence or a list. It turns that into timed scenes, picks color and motion, draws the frames on your CPU, and saves an MP4 or an AVI. No internet is required. Optionally, Ollama, LM Studio, Kor Studio, or a cloud API can write the storyboard; the pixels are still produced locally.

Version 0.1 is not a video diffusion model, and it does not download one. A frame is a title card: type, color, light, and camera move. You change the words, the timing, the order, and the look, then export again.

## What it does

- Plans Turkish and English prompts offline (`8 seconds`, `9:16`, `title: "Hello"`, numbered lists).
- Looks: cinema, dusk, night, sea, forest, neon, paper, minimal.
- Motion: zoom, drift, rise, with fades between scenes.
- Edit text, duration, order, duplicates, and the total length.
- Play the preview in the window.
- H.264 MP4 on Windows (Media Foundation), plus MJPEG AVI.
- Optional OpenAI-compatible API, used only for the storyboard.

## Install

Download `kare-0.1.0-windows-x64.zip` from [Releases](https://github.com/Hydra-Ai-tr/kare/releases/latest) and unzip it. `kare.exe` is the app, `kare-cli.exe` is the command line. Windows 10 or newer.

The build is not signed yet. If SmartScreen appears, choose **More info → Run anyway**.

## Quick start

1. On **Create**, write a prompt. Example: `Dusk over the harbor, 8 seconds. title: "Harbor"`
2. Press **Plan**. Change the scenes on **Edit**.
3. Set a `.mp4` path at the bottom and press **Export**.

```text
kare-cli plan "Night market, 6 seconds"
kare-cli render "three steps" -o intro.mp4 --seconds 10 --aspect 9:16
kare-cli frame "Hello" -o frame.png
kare-cli selftest
```

## API

Pick an endpoint in Settings. With it off, everything stays offline. With it on, only the prompt is sent to the address you chose. Frames are not uploaded.

| Provider | Address |
|---|---|
| Ollama | `http://127.0.0.1:11434/v1` |
| LM Studio | `http://127.0.0.1:1234/v1` |
| Kor Studio | `http://127.0.0.1:8080/v1` (`kor-cli serve`) |
| OpenAI, Groq | cloud, an API key is required |

The key is stored in `%APPDATA%\Kare\config.json`. If the API fails, Kare falls back to the local planner and shows why.

## Build

CMake 3.20+, Ninja, and MinGW-w64 GCC 13+ or Visual Studio 2022. CMake downloads GLFW and Dear ImGui.

```powershell
git clone https://github.com/Hydra-Ai-tr/kare
cd kare
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\bin\kare-cli.exe selftest
build\bin\kare.exe
```

## Privacy

Offline planning and export do not use the network. The network is used only for the API you enable in Settings.

## License

[MIT](LICENSE).

Third party: Dear ImGui (MIT), GLFW (zlib), stb (public domain), Roboto (Apache-2.0, `assets/fonts/LICENSE-ROBOTO.txt`).
