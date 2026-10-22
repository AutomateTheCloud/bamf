# README integration example

The kit is in the `brand_kit/` folder at the repository root. Add this to the root README.md:

```html
<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="brand_kit/logos/svg/bamf-stacked-dark.svg">
    <img src="brand_kit/logos/svg/bamf-stacked-light.svg" width="560" alt="BAMF - BASH Application Management Framework">
  </picture>
</p>
```

For hosts that strip SVG or picture elements, use `brand_kit/logos/png/bamf-stacked-on-light-1024.png` as a single image. Its background is included for predictable display.

For GitHub's repository social preview, upload `social/github-social-1200x630.png` in repository Settings. The kit does not upload it automatically.
