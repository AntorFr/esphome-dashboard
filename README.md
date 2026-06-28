# esphome-dashboard

Lib graphique modulaire **ESPHome + LVGL** pour piloter Home Assistant — une **même card
logique, plusieurs rendus** selon l'appareil. Cible v1 : **M5Stack Dial** (rond 240×240) et
**Seeed reTerminal D1001** (8" 800×1280 portrait).

> 🚧 En construction. Statut & jalons : [`docs/roadmap.md`](docs/roadmap.md).

## Base de connaissance (`docs/`)

| Doc | Contenu |
|-----|---------|
| [architecture.md](docs/architecture.md) | 3 couches ports & adapters, contrats d'interface |
| [adr/](docs/adr/) | Journal des décisions (0001→0006) |
| [lessons-learned.md](docs/lessons-learned.md) | Anti-patterns (dont le bug tactile) |
| [ux-interaction.md](docs/ux-interaction.md) | Modèle d'interaction commun |
| [interaction-dial.md](docs/interaction-dial.md) · [interaction-d1001.md](docs/interaction-d1001.md) | Specs par écran |
| [color-system.md](docs/color-system.md) | Couleurs alignées HA |
| [config-reference.md](docs/config-reference.md) | Schéma YAML utilisateur |

Brief produit d'origine : [`dashboard-design.md`](dashboard-design.md). Prototypes
interactifs (source de vérité) : [`dial-prototype.html`](dial-prototype.html),
[`d1001-prototype.html`](d1001-prototype.html).

## Configuration cible (aperçu)

```yaml
substitutions:
  display: dial            # dial | reterminal_d1001
packages:
  dashboard: github://AntorFR/esphome-dashboard/packages/entrypoint.yaml@main
dashboard:
  style: neon
  language: fr
  groups:
    - name: "Salon"
      icon: sofa
      cards:
        - { type: light,  entity: light.salon_plafond }
        - { type: switch, entity: switch.salon_prise }
```

## Développement

`reference/` (non-gité) peut accueillir des clones de libs d'inspiration (esphome-dial, etc.)
pour le contexte. Voir [`docs/adr/`](docs/adr/) avant de coder le renderer/input.
