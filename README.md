# esphome_esp-video

Composants externes [ESPHome](https://esphome.io/) pour la capture vidéo, les
pilotes de capteurs caméra et l'affichage caméra LVGL sur les cibles Espressif
ESP32 (en particulier l'**ESP32-P4** avec interface MIPI-CSI).

## Composants

| Composant | Rôle |
|-----------|------|
| `esp_video` | Pipeline vidéo (CSI / DVP / ISP / JPEG) basé sur le framework `esp_video` d'Espressif. Doit toujours être présent. |
| `esp_cam_sensor` | Pilotes de capteurs caméra + transformations matérielles (PPA : crop / resize / rotation / miroir). |
| `lvgl_camera_display` | Widget LVGL affichant le flux caméra en direct sur un `canvas`, avec superpositions de détection optionnelles. |

Ces trois composants fonctionnent ensemble :

```
[capteur MIPI-CSI] → esp_cam_sensor → esp_video (ISP/encodage) → lvgl_camera_display → canvas LVGL
```

## Installation dans ESPHome

Référencez ce dépôt comme source de composants externes :

```yaml
external_components:
  # Pilotes caméra + affichage (ce dépôt)
  - source:
      type: git
      url: https://github.com/youkorr/esphome_esp-video
    components: [esp_video, esp_cam_sensor, lvgl_camera_display]
    refresh: 0s

  # LVGL 9.5 personnalisé — REQUIS par lvgl_camera_display (accélération PPA)
  - source:
      type: git
      url: https://github.com/youkorr/lvgl_9.5
      ref: main
    components: [lvgl, image, font]
    refresh: always

  # Interface d'affichage MIPI-DSI (ESP32-P4)
  - source: github://pr#13608
    components: [mipi_dsi]
    refresh: always
```

> ⚠️ Ces composants ciblent l'**ESP32-P4** (`esp32` + framework `esp-idf`).
> La PSRAM est obligatoire pour les buffers vidéo.

> 🔗 **Dépendance LVGL obligatoire.** `lvgl_camera_display` recopie les images
> dans un `canvas` LVGL via l'accélération matérielle **PPA**. Cela nécessite le
> fork **LVGL 9.5** (`https://github.com/youkorr/lvgl_9.5`, licence MIT) — le
> composant `lvgl` officiel d'ESPHome ne fournit pas `use_ppa`. Le dépôt
> `lvgl_9.4` est obsolète et ne doit plus être utilisé.

### 1. Bus I²C

Le capteur est piloté via I²C (SCCB). Déclarez le bus et donnez-lui un `id`
qui sera réutilisé par `esp_video` et `esp_cam_sensor` :

```yaml
i2c:
  - id: bsp_bus
    sda: GPIO31
    scl: GPIO32
    frequency: 400kHz

psram:
  mode: hex
  speed: 200MHz
```

### 2. Pipeline vidéo : `esp_video`

```yaml
esp_video:
  i2c_id: bsp_bus
  xclk_pin: GPIO36          # broche horloge XCLK (ou -1 / NO_CLOCK si oscillateur PCB)
  xclk_freq: 24000000       # 1 à 40 MHz (24 MHz typique)
  enable_jpeg: true         # encodeur JPEG matériel
  enable_isp: true          # pipeline ISP (RAW → RGB565)
  use_heap_allocator: true  # allocation des buffers en PSRAM
```

| Option | Défaut | Description |
|--------|--------|-------------|
| `i2c_id` | *(requis)* | Bus I²C partagé avec le capteur |
| `xclk_pin` | `GPIO36` | Broche XCLK (`GPIO36`, un entier, `-1` ou `NO_CLOCK`) |
| `xclk_freq` | `24000000` | Fréquence XCLK (1–40 MHz) |
| `enable_jpeg` | `true` | Active l'encodeur JPEG matériel |
| `enable_isp` | `true` | Active l'ISP (conversion RAW → RGB565) |
| `use_heap_allocator` | `true` | Place les buffers vidéo en PSRAM |
| `enable_xclk_init` | `false` | Génère XCLK via LEDC (cartes hors M5Stack) |

> ℹ️ **L'encodeur H.264 est désactivé.** Le device matériel H.264 est compilé
> sous `#if CONFIG_ESP_VIDEO_ENABLE_HW_H264_VIDEO_DEVICE`, flag jamais positionné
> par le composant — `/dev/video11` n'est donc pas créé. Il n'existe pas d'option
> `enable_h264` (elle serait rejetée à la validation). Seuls **ISP** et **JPEG
> matériel** sont actifs.

### 3. Capteur : `esp_cam_sensor`

```yaml
esp_cam_sensor:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: ov5647       # ov5647 | ov02c10 | sc202cs
  resolution: "640x480"     # voir tableaux par capteur ci-dessous
  pixel_format: "RGB565"    # RGB565 (zero-copy LVGL) | YUYV | UYVY | NV12 | JPEG | RAW8
  framerate: 30             # 1–60 fps
  jpeg_quality: 15          # 1–63 (si pixel_format JPEG)
  mirror_x: false           # miroir horizontal (matériel PPA)
  mirror_y: false           # miroir vertical (matériel PPA)
  rotation: 0               # 0 / 90 / 180 / 270° (matériel PPA)
  crop_offset_x: 0          # crop horizontal (pixels depuis la gauche)
```

| Option | Défaut | Description |
|--------|--------|-------------|
| `sensor_type` | `sc202cs` | `ov5647`, `ov02c10` ou `sc202cs` (`sensor:` accepté en alias) |
| `i2c_id` | `0` | Bus I²C partagé |
| `lane` | `1` | Nombre de lanes MIPI (1–4) |
| `xclk_pin` | `GPIO36` | Broche XCLK |
| `xclk_freq` | `24000000` | Fréquence XCLK |
| `sensor_addr` | `0x36` | Adresse I²C du capteur |
| `resolution` | `720P` | Résolution (alias ou `LxH`, voir ci-dessous) |
| `pixel_format` | `JPEG` | Format de pixel de sortie |
| `framerate` | `30` | Images par seconde (1–60) |
| `mirror_x` / `mirror_y` | – | Symétrie matérielle (PPA) |
| `rotation` | – | Rotation matérielle 0/90/180/270° (PPA) |
| `crop_offset_x` | `0` | Décalage de crop (0–800) |
| `output_width` / `output_height` | `0` | Redimensionnement matériel PPA (0 = pas de resize) |

#### Alias de résolution génériques

Valables pour tous les capteurs (passés au pilote natif) :

| Alias | Dimensions |
|-------|------------|
| `QVGA` | 320 × 240 |
| `VGA` / `480P` | 640 × 480 |
| `720P` | 1280 × 720 |
| `1080P` | 1920 × 1080 |
| `"LxH"` | dimensions libres (ex. `"800x600"`) |

## Capteurs supportés et résolutions

Trois capteurs sont pleinement validés (détection, ISP et balance des blancs
gérées automatiquement via leurs registres et leur configuration IPA JSON).

### OV5647 (MIPI 2-lane, 5 MP)

Capteur de type Raspberry Pi Camera v1. Sortie RAW convertie en RGB565 par l'ISP.

| Résolution | FPS | Notes |
|------------|-----|-------|
| 640 × 480 | 30 | VGA (format custom) |
| 800 × 600 | 50 | SVGA — mouvement fluide, idéal écrans 1024×600 |
| 800 × 640 | 50 | natif |
| 800 × 800 | 50 | natif RAW8 |
| 1024 × 600 | 30 | format custom (écrans larges) |
| 1280 × 960 | 45 | natif RAW10 |
| 1920 × 1080 | 30 | natif RAW10 (1080P) |

```yaml
esp_cam_sensor:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: ov5647
  resolution: "640x480"
  pixel_format: "RGB565"
  framerate: 30
```

### OV02C10 (MIPI 1-lane, 2 MP)

| Résolution | FPS | Notes |
|------------|-----|-------|
| 640 × 368 | 30 | **recommandé** — ~16:9, FOV ~98 %, aligné 16 octets (rotation sûre) |
| 640 × 480 | 30 | VGA 4:3 — ⚠️ crop horizontal 25 % (zoom 1,33×, FOV 75 %) |
| 800 × 600 | 30 | SVGA 4:3 — crop horizontal 25 % |
| 480 × 640 | 30 | portrait (rotation 270° gérée par LVGL) |
| 1288 × 728 | 30 | proche HD 16:9, capteur complet downscalé par l'ISP |
| 1920 × 1080 | 30 | 1080P — capteur complet, FOV 100 % |

> ❌ `960x540` est **désactivé** (watchdog persistant) — utilisez `800x600` ou `1288x728`.

```yaml
esp_cam_sensor:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: ov02c10
  resolution: "640x368"   # meilleur compromis FOV / 16:9
  pixel_format: "RGB565"
  framerate: 30
```

### SC202CS (MIPI 1-lane, 2 MP)

Capteur natif 1600 × 1200 avec binning 2×2.

| Résolution | FPS | Notes |
|------------|-----|-------|
| 800 × 600 | 30 | format natif RAW8, crop centré (recommandé petits écrans) |
| 1280 × 720 | 30 | 720P (format pilote par défaut) |

```yaml
esp_cam_sensor:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: sc202cs
  resolution: "800x600"
  pixel_format: "RGB565"
  framerate: 30
```

> 💡 Pour LVGL, utilisez de préférence `pixel_format: RGB565` : c'est le format
> natif du `canvas` LVGL, ce qui permet une copie « zero-copy » sans conversion.

## Affichage caméra dans LVGL (Canvas)

`lvgl_camera_display` recopie chaque image du capteur dans un widget `canvas`
LVGL. La mise en place se fait en **deux temps** :

1. **Déclarer un widget `canvas`** dans une page LVGL.
2. **Associer le canvas au composant** avec `configure_canvas()` une fois LVGL prêt.

### Déclaration du composant

```yaml
lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam        # id du esp_cam_sensor
  canvas_id: camera_canvas   # id du widget canvas LVGL
  update_interval: 33ms      # ~30 FPS
  # superpositions de détection optionnelles :
  # face_detection_id: face_detect
  # yolo11_detection_id: yolo_detect
  # pedestrian_detection_id: ped_detect
```

| Option | Défaut | Description |
|--------|--------|-------------|
| `camera_id` | *(requis)* | `id` du `esp_cam_sensor` |
| `canvas_id` | *(requis)* | `id` du widget `canvas` LVGL cible |
| `update_interval` | `33ms` | Période de rafraîchissement (33 ms ≈ 30 FPS) |
| `face_detection_id` | – | Superposition détection de visages |
| `yolo11_detection_id` | – | Superposition détection YOLO11 |
| `pedestrian_detection_id` | – | Superposition détection piétons |

### Widget Canvas dans LVGL

Le bloc `lvgl` doit utiliser le fork **LVGL 9.5** avec l'accélération PPA :

```yaml
lvgl:
  use_ppa: true               # accélération matérielle PPA (fork LVGL 9.5)
  byte_order: little_endian
  displays:
    - main_display
  touchscreens:
    - touch
  pages:
    - id: camera_page
      widgets:
        - canvas:
            id: camera_canvas
            width: 640          # = largeur de la résolution capteur
            height: 480         # = hauteur de la résolution capteur
            x: 192              # position à l'écran
            y: 60
            bg_color: 0x000000
            border_width: 0
            radius: 0
            pad_all: 0
```

⚠️ **Le `canvas` doit avoir exactement les dimensions de la résolution du
capteur** (ici 640×480) sinon l'image sera tronquée ou déformée.

### Activation : `configure_canvas()`

Le canvas doit être lié au composant **une fois LVGL initialisé**. Trois
déclencheurs possibles : `on_idle` de `lvgl` (méthode utilisée en production), un
interrupteur `template`, ou l'`on_load` de la page.

**Méthode recommandée — `on_idle` de LVGL** (le canvas est lié une seule fois) :

```yaml
lvgl:
  use_ppa: true
  byte_order: little_endian
  displays:
    - main_display
  on_idle:
    - timeout: 5s
      then:
        - lambda: |-
            static bool canvas_configured = false;
            if (!canvas_configured) {
              auto canvas = id(camera_canvas);
              if (canvas != nullptr) {
                id(camera_display).configure_canvas(canvas);
                canvas_configured = true;
                ESP_LOGI("lvgl", "Canvas configuré pour caméra 640x480");
              }
            }
```

**Variante — interrupteur `template`** (active/désactive le flux à la demande) :

```yaml
switch:
  - platform: template
    name: "LVGL Camera Display"
    id: lvgl_display_enable_switch
    restore_mode: RESTORE_DEFAULT_OFF
    optimistic: true
    turn_on_action:
      - lambda: |-
          auto canvas = id(camera_canvas);
          auto *disp = id(camera_display);
          if (canvas != nullptr && disp != nullptr) {
            disp->configure_canvas(canvas);
            disp->set_enabled(true);     // démarrer le rafraîchissement
          }
    turn_off_action:
      - lambda: |-
          id(camera_display).set_enabled(false);
```

## Exemple minimal complet

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/youkorr/esphome_esp-video
    components: [esp_video, esp_cam_sensor, lvgl_camera_display]
    refresh: 0s
  - source:
      type: git
      url: https://github.com/youkorr/lvgl_9.5
      ref: main
    components: [lvgl, image, font]
    refresh: always
  - source: github://pr#13608
    components: [mipi_dsi]
    refresh: always

psram:
  mode: hex
  speed: 200MHz

i2c:
  - id: bsp_bus
    sda: GPIO31
    scl: GPIO32
    frequency: 400kHz

esp_video:
  i2c_id: bsp_bus
  xclk_pin: GPIO36
  xclk_freq: 24000000
  enable_jpeg: true
  enable_isp: true
  use_heap_allocator: true

esp_cam_sensor:
  id: tab5_cam
  i2c_id: bsp_bus
  sensor_type: ov5647
  resolution: "640x480"
  pixel_format: "RGB565"
  framerate: 30

lvgl_camera_display:
  id: camera_display
  camera_id: tab5_cam
  canvas_id: camera_canvas
  update_interval: 33ms

lvgl:
  use_ppa: true
  byte_order: little_endian
  displays:
    - main_display
  on_idle:
    - timeout: 5s
      then:
        - lambda: |-
            static bool canvas_configured = false;
            if (!canvas_configured) {
              if (id(camera_canvas) != nullptr) {
                id(camera_display).configure_canvas(id(camera_canvas));
                canvas_configured = true;
              }
            }
  pages:
    - id: camera_page
      widgets:
        - canvas:
            id: camera_canvas
            width: 640
            height: 480
            x: 0
            y: 0
```

## Licence

Projet distribué sous [Licence MIT](LICENSE).

Détenteurs des droits d'auteur :

- ESPHome
- Espressif Systems (Shanghai) CO LTD
- youkorr (Sapphire Younes)
