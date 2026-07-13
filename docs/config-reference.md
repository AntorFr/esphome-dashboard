# Référence de configuration

> Schéma YAML destiné à l'utilisateur final + explication de l'auto-injection
> (cf. [ADR-0006](adr/0006-config-auto-inject.md)). Cible : décrire **uniquement**
> `display` + `groups` + `cards`.

## Configuration cible (auto-inject — à terminer, cf. ADR-0006)

```yaml
substitutions:
  display: dial            # dial | reterminal_d1001

packages:
  dashboard: github://AntorFR/esphome-dashboard/packages/entrypoint.yaml@main

dashboard:
  style: neon              # défaut: neon
  language: fr             # fr | en (défaut: en)
  groups:
    - name: "Salon"
      icon: sofa
      cards:
        - { type: light,  entity: light.salon_plafond }
        - { type: switch, entity: switch.salon_prise }
```

## Configuration ACTUELLE (prototype M0.5)

Tant que l'auto-inject n'est pas terminé, l'utilisateur inclut le board explicitement et
règle `profile:` sur le composant `ha_dashboard:` (voir `examples/` et `tests/`) :

```yaml
external_components:
  - source: github://AntorFR/esphome-dashboard   # ou local en dev

packages:
  board: !include packages/boards/m5stack_dial.yaml   # ou reterminal_d1001.yaml

# La card `switch` se lie à un esphome switch (souvent plateforme `homeassistant`).
switch:
  - platform: homeassistant
    id: sw_prise_salon
    entity_id: switch.salon_prise

# Les cards cover/climate/media_player/light se lient au composant homeassistant_addon
# (proxys qui reflètent l'état HA et recommandent vers HA).
homeassistant_addon:
  lights:
    - id: light_salon
      entity_id: light.salon_plafond

ha_dashboard:
  profile: dial                 # dial | reterminal_d1001
  language: fr                  # fr | en
  inactivity_timeout: 30s
  encoder: dial_encoder         # (Dial) id du rotary_encoder fourni par le board
  encoder_button: dial_button   # (Dial) id du binary_sensor bouton
  groups:
    - name: "Salon"
      icon: sofa
      cards:
        - { type: switch, switch_id: sw_prise_salon, name: "Prise salon" }
        - { type: light,  light_id: light_salon, name: "Plafond salon" }
```

### Schéma de card
| `type` | Champ requis | Binding |
|--------|--------------|---------|
| `switch` | `switch_id` (esphome switch) | **réel** : `->state` / `->toggle()` |
| `light`  | `light_id` (homeassistant_addon light) | **réel** : on/off + luminosité (tap = toggle, sheet = variateur) |
| `cover`  | `cover_id` (homeassistant_addon cover) | **réel** : position / open-close-stop (+ `cover_kind`) |
| `climate`| `climate_id` (homeassistant_addon climate) | **réel** : mode + consigne (+ `climate_kind`) |
| `media_player` | `media_player_id` (homeassistant_addon media_player) | **réel** : play/pause/next/prev + volume |

Communs : `name` (optionnel, défaut = nom de l'entité/switch), `color` (optionnel `#RRGGBB`).

> Le board fournit `display:`, `touchscreen:`, `lvgl:` (+ encodeur/bouton sur le Dial).
> Clé du composant = `ha_dashboard:` (= nom du dossier composant).
> `switch` se lie à un esphome switch (plateforme `homeassistant` : switch, input_boolean,
> fan, siren…). light / cover / climate / media_player passent par `homeassistant_addon`.

## Schéma `dashboard:`

| Clé | Type | Défaut | Notes |
|-----|------|--------|-------|
| `style` | enum | `neon` | nom d'un package `styles/<style>.yaml` |
| `language` | enum `fr`/`en` | `en` | sélectionne tables i18n + fonts |
| `groups` | liste | requis | voir ci-dessous |

### `groups[]`
| Clé | Type | Défaut | Notes |
|-----|------|--------|-------|
| `name` | str | requis | affiché au centre du menu (Dial) / onglet (D1001) |
| `icon` | str | optionnel | nom d'icône (jeu Tabler, cf. prototypes) |
| `cards` | liste | requis | groupe à 1 card → niveau Groupe sauté |

### `cards[]`
| Clé | Type | Défaut | Notes |
|-----|------|--------|-------|
| `type` | enum | requis | `switch`, `light`, `cover`, `media_player`, `climate` |
| `<type>_id` | id | requis | binding : `switch_id` / `light_id` / `cover_id` / `climate_id` / `media_player_id` |
| `color` | hex | optionnel | override ; pour une light, le live RGB l'emporte |
| `name` | str | optionnel | défaut = nom convivial de l'entité |
| `icon` | str | optionnel | défaut = icône du domaine |
| `cover_kind` | enum | optionnel (cover) | `shutter` \| `garage` \| `gate` — illustration + sens du mouvement |
| `climate_kind` | enum | optionnel (climate) | `radiator` (chauffe seul) \| `ac` (froid seul) \| `thermostat` (réversible, défaut). Pilote l'icône (radiateur/climatiseur/thermostat) et les modes du sheet. Couleur auto : chauffe = ambre, refroidit = bleu. |

## Ce qui est auto-injecté (l'utilisateur ne l'écrit pas)

Via `packages/entrypoint.yaml` (piloté par `${display}`) :
- `boards/<display>.yaml` : `esp32`, `psram`, framework, bus, **display driver** (mipi_spi /
  mipi_dsi), **touchscreen**, encodeur/bouton (Dial), IMU (D1001).
- `lvgl_base.yaml` : initialisation LVGL, buffers.
- `fonts/fonts_base.yaml` + `fonts/fonts_<language>.yaml`.

Via le codegen du composant `dashboard:` (`__init__.py`) :
- Souscriptions HA (`homeassistant` sensor/text_sensor) pour état + attributs de chaque
  `entity` listée.
- Actions de commande HA (services) throttlées.
- Instanciation du renderer LVGL + input adapter selon `display`.

## Contraintes

- **ESPHome ≥ 2026.5** (requis ESP32-P4 / D1001).
- **PSRAM obligatoire** sur D1001.
- Un `secrets.yaml` (wifi/api) reste à la charge de l'utilisateur (non géré par la lib).
