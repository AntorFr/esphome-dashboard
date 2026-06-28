# Référence de configuration

> Schéma YAML destiné à l'utilisateur final + explication de l'auto-injection
> (cf. [ADR-0006](adr/0006-config-auto-inject.md)). Cible : décrire **uniquement**
> `display` + `groups` + `cards`.

## Configuration minimale

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
    - name: "Cuisine"
      icon: kitchen
      cards:
        - { type: light, entity: light.cuisine_spots, color: "#FF8800" }
```

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
| `type` | enum | requis | **v1 : `light`, `switch`** ; ensuite `cover`, `media_player`, `climate` |
| `entity` | str | requis | entité HA (1 par card en v1) |
| `color` | hex | optionnel | override ; pour une light, le live RGB l'emporte |
| `name` | str | optionnel | défaut = nom convivial de l'entité |
| `icon` | str | optionnel | défaut = icône du domaine |

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
