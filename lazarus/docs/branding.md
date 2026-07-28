# Lazarus Branding Themes

Lazarus uses `Lazarus Default Theme` when no branding keys are present. A workplace can override the theme in its bench profile without rebuilding the application.

```ini
branding_theme=Northwind Recovery
branding_product_name=Northwind Lazarus
branding_subtitle=Offline Imaging | Recovery | Hardware Migration
branding_accent=#2aa7a0
branding_background=#101820
branding_surface=#1b2730
branding_text=#edf5f4
branding_icon=#2aa7a0
branding_logo=/etc/arcology-lazarus/northwind-logo.png
branding_report_footer=Prepared locally by Northwind IT. SMART results describe reported device facts.
```

The color values must be six- or eight-digit hexadecimal CSS colors. Invalid values use the Lazarus defaults. Branding does not change safety policy, device roles, destructive-operation controls, or service permissions.

The current runtime applies the color tokens to the GTK interface and the product name/footer to printed SMART reports. `branding_logo` is reserved for the configured workplace logo asset; the built-in Lazarus emblem remains the fallback when no valid custom asset is available.

## ACS Computer Services

The ACS Computer Services preset is included at `examples/acs-computer-services.profile`. It uses the website-inspired palette:

- ACS red actions: `#cf3b2b`
- ACS blue icons: `#1787c4`
- White appliance background: `#ffffff`
- Cool light-gray surfaces: `#f5f8fb`
- Charcoal text: `#15171a`

The preset is intentionally missing physical device assignments. Configure those for each bench before using it for real imaging.
