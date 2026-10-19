# PowKiddy V90 Video Player (`videoplayer`)

A lightweight, distraction-free full-screen video & audio player ([`videoplayer.c`](videoplayer.c)) built for the **PowKiddy V90 / Q90** running **MiyooCFW 1.3.3** (`320×240` 16-bit RGB565 display, Allwinner F1C100S `ARM926EJ-S`), alongside a native **x86_64 Linux (`X11`)** target (`videoplayer_linux`) for desktop playback and debugging.

---

## Screenshots

| `/mnt/media` File Browser | Full-Screen Video Playback (`320×240`) | Paused View with Device Help (`A`) |
| :---: | :---: | :---: |
| ![File Browser](promo/video_browser.png) | ![Playing View](promo/video_playing.png) | ![Paused Overlay](promo/video_paused.png) |

---

## 1. Design of the Application

### Why Replace the Stock `/mnt/apps/mplayer` Wrapper?
On stock MiyooCFW 1.3.3, `/mnt/apps/mplayer/run.sh` invokes a command-line `mplayer` binary (`15.3 MB`) via GMenu2X's external file selector (`selectorbrowser=true`). It provides no built-in `/mnt/media` file browser, exits immediately back to GMenu2X when a clip ends, and offers no on-screen button guide when paused.

### Architecture & Multimedia Pipeline

```mermaid
flowchart LR
    A["/mnt/media\nFile Browser"] -->|Select Video (A)| B["libavformat\navformat_open_input()"]
    B --> C["Demux Loop\nav_read_frame()"]
    C -->|Video Packet| D["libavcodec\nVideo Decoder"]
    C -->|Audio Packet| E["libavcodec\nAudio Decoder"]
    D --> F["libswscale\nYUV420P -> 320x240 RGB"]
    E --> G["libswresample\nS16LE Stereo PCM"]
    G --> H["ALSA PCM Output\n(libasound)"]
    H -->|A/V PTS Sync| I["Full-Screen 320x240\nVideo Framebuffer"]
    I -->|If Paused (A)| J["Compact Pause &\nDevice Help Overlay"]
```

1. **Startup `/mnt/media` File Picker**:
   - Opens automatically in **`/mnt/media`** on the PowKiddy V90 SD card (falling back to `./media` on the Linux desktop so sample `.mp4` files in [`media/`](media/) are immediately ready to play).
   - Lists directories and video files (`.mp4`, `.mkv`, `.avi`, `.mov`, `.webm`, `.mpg`, `.mpeg`, `.flv`, `.ogv`, `.m4v`, `.3gp`) with file size indicators.
2. **Contemporary FFmpeg + ALSA Playback Engine**:
   - **Demuxing & Decoding (`libavformat` + `libavcodec`)**: Opens container streams with `avformat_open_input()` and decodes video and audio packets via `avcodec_send_packet()` / `avcodec_receive_frame()`.
   - **Color-Space Conversion (`libswscale`)**: Converts decoded `YUV420P` video frames into the `320×240` framebuffer using `sws_scale()`.
   - **Audio Resampling & Output (`libswresample` + `libasound`)**: Resamples decoded audio streams into interleaved 16-bit signed stereo PCM (`SND_PCM_FORMAT_S16_LE`) and streams them through ALSA (`snd_pcm_writei`).
3. **Distraction-Free Full-Screen Video (`320×240`)**:
   - During playback (`STATE_PLAYING`), the video occupies the entire `320×240` screen with zero status bars, borders, or overlays.
4. **Play / Pause (`A`) with Compact Device Help Overlay**:
   - Pressing **`A`** pauses video and audio playback (`STATE_PAUSED`) and displays a translucent **PAUSED** card showing the video title, elapsed/total duration (`MM:SS / MM:SS`), a progress bar, and the device button guide.
5. **Clean Exit (`START` / `MENU`)**:
   - Pressing **`START`** or **`MENU`** immediately closes active audio/video streams and exits.

---

## 2. Controls

| Mode | Action | PowKiddy V90 Button | Linux Desktop Key (`videoplayer_linux`) |
| :--- | :--- | :--- | :--- |
| **File Browser (`/mnt/media`)** | Move Selection Up / Down | **`DPAD UP` / `DOWN`** | `Up` / `Down Arrow` |
| **File Browser (`/mnt/media`)** | Play Video or Enter Folder | **`A`** (`LAlt`) | `A`, `Space`, or `Left Alt` |
| **File Browser (`/mnt/media`)** | Go to Parent Directory (`..`) | **`B`** (`LCtrl`) | `B` or `Left Ctrl` |
| **Video Playback / Pause** | Toggle Play / Pause (Show Help) | **`A`** (`LAlt`) | `A`, `Space`, or `Left Alt` |
| **Video Playback / Pause** | Seek Backward / Forward (`-5s` / `+5s`) | **`DPAD LEFT` / `RIGHT`** (or `L1` / `R1`) | `Left` / `Right Arrow` |
| **Video Playback / Pause** | Stop & Return to `/mnt/media` Browser | **`B`** (`LCtrl`) or **`X`** (`LShift`) | `B` or `X` |
| **All Views** | Exit Application | **`START`** (`Return`) or **`MENU`** (`RCtrl`) | `Enter`, `Right Ctrl`, or `Q` |

---

## 3. Development Setup (Linux Desktop & Cross-Compiler)

### Step 1: Install Required Linux Desktop Packages (`apt`)

Install the FFmpeg (`libavformat`, `libavcodec`, `libswscale`, `libswresample`, `libavutil`), ALSA (`libasound2`), and X11/SDL development libraries:

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libavutil-dev libasound2-dev libsdl1.2-dev libx11-dev ffmpeg curl tar python3
```

### Step 2: Install the PowKiddy V90 Cross-Compilation Toolchain (`arm-miyoo-linux-uclibcgnueabi`)

Extract the prebuilt MiyooCFW SDK (which includes `arm-miyoo-linux-uclibcgnueabi-gcc` along with target `libavformat`, `libavcodec`, `libswscale`, `libswresample`, `libavutil`, `libasound`, and `libSDL`) into `/opt/miyoo` (or `/tmp/miyoo_sdk_extract`, both auto-detected by [`Makefile`](Makefile)):

```bash
sudo mkdir -p /opt/miyoo
TOKEN=$(curl -s "https://auth.docker.io/token?service=registry.docker.io&scope=repository:nfriedly/miyoo-toolchain:pull" \
  | python3 -c "import sys,json; print(json.load(sys.stdin)['token'])")
curl -fL -H "Authorization: Bearer $TOKEN" \
  "https://registry-1.docker.io/v2/nfriedly/miyoo-toolchain/blobs/sha256:b03cca7cff1f30eb44d7d4929d65eecaa4515437b1d1b53804e2cfbdbcee51b1" \
  | sudo tar -xz -C /
```

---

## 4. Building, Running & Preparing `320×240` Videos

From [`videoplayer/`](.) inside the repository:

```bash
# Build both PowKiddy V90 ARM binary (./videoplayer) and Linux x86_64 binary (./videoplayer_linux)
make

# Launch the native Linux desktop player (opens ./media/ by default)
make run-linux

# Build the SD card deployment package in ./sdcard_package/
make package
```

### Transcoding Videos to Native `320×240` for the PowKiddy V90
To pre-scale any video file to the PowKiddy V90's native `320×240` resolution using `ffmpeg`:

```bash
ffmpeg -i input.mp4 -vf "scale=320:240:force_original_aspect_ratio=decrease,pad=320:240:(ow-iw)/2:(oh-ih)/2" \
  -r 30 -c:v mpeg4 -q:v 4 -pix_fmt yuv420p -c:a aac -ar 44100 -ac 2 -b:a 128k output_320x240.mp4
```

---

## 5. How to Overwrite the Default Media Player on the PowKiddy V90

Running `make package` creates [`sdcard_package/`](sdcard_package/), which matches the directory structure of the PowKiddy V90 **`main`** (`FAT32`, mounted at `/mnt`) partition and bundles the stripped ARMv5TE FFmpeg shared libraries in `apps/videoplayer/libs/`:

```text
sdcard_package/
├── apps/
│   └── videoplayer/
│       ├── videoplayer                    # ARMv5TE uClibc video player binary
│       ├── run.sh                         # Launcher script (sets LD_LIBRARY_PATH to ./libs)
│       ├── icon.png                       # 32x32 application icon
│       └── libs/                          # Stripped ARMv5TE FFmpeg shared libraries
│           ├── libavformat.so.57
│           ├── libavcodec.so.57
│           ├── libswscale.so.4
│           ├── libswresample.so.2
│           └── libavutil.so.55
├── gmenu2x/
│   └── sections/
│       └── applications/
│           └── mplayer                    # Overwrites stock mplayer launcher in GMenu2X
└── media/
    ├── Fractal_Zoom_240p.mp4              # Sample 320x240 video with audio
    └── PowKiddy_320x240_Demo.mp4          # Sample 320x240 video with audio
```

### Steps to Deploy to Your MicroSD Card
1. Insert the PowKiddy V90 MicroSD card into your Linux PC and mount the **`main`** (`FAT32`) partition (e.g. `/media/$USER/main`).
2. Copy `sdcard_package/*` onto the `main` partition to install `/mnt/apps/videoplayer`, overwrite `/mnt/gmenu2x/sections/applications/mplayer`, and copy the sample videos into `/mnt/media/`:
   ```bash
   cp -r sdcard_package/* /media/$USER/main/
   sync
   ```
3. Copy any additional `320×240` `.mp4` / `.mkv` / `.avi` videos into `/media/$USER/main/media/` (`/mnt/media` on the device).
4. Eject the MicroSD card, boot the PowKiddy V90, and select **Video Player** under the **applications** section in GMenu2X.
