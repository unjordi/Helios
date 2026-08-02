---
name: nota-plasmashell-reload-2026-07-24
description: systemctl --user reload plasma-plasmashell.service ya NO hace nada (hardening 2026-07-24) — si necesitas recargar el escritorio tras tocar KWin/iGPU, usa kquitapp6+kstart
metadata:
  type: reference
---

**Para cuando toques KWin/pin de GPU/reinicio del escritorio** (relevante para el flip de GPU
escritorio↔iGPU / Wolf de `diseno-headless-multisesion.md`, y para el runbook
`wolf-deploy/RUNBOOK-igpu-desktop.md`, que ya documenta el incidente hermano del
2026-07-23 — mismo síntoma, causa distinta):

La unit nativa de KDE `plasma-plasmashell.service` no traía `ExecReload=`, así que CUALQUIER
`systemctl --user reload plasma-plasmashell.service` la mataba (SIGHUP no manejado) y, si pasaba
varias veces seguidas, agotaba `StartLimitBurst=3` sin que `Restart=on-failure` pudiera revivirla
— quedaba muerta para siempre. Esto pasó de verdad el 2026-07-24 (trigger: el auto-update del
plasmoide `claude-brain`, ya arreglado en su repo — PR #187/#188).

**Hardening aplicado en esta máquina** (systemd de usuario, no viaja por git):
`~/.config/systemd/user/plasma-plasmashell.service.d/override.conf` → `ExecReload=/bin/true`
(no-op seguro) + `StartLimitBurst=20`. Verificado en vivo: el PID no cambia tras un
`systemctl --user reload` explícito.

**Qué significa para este proyecto:** si necesitas recargar plasmashell tras cambiar
`KWIN_DRM_DEVICES` o cualquier config de sesión, `systemctl --user reload
plasma-plasmashell.service` YA NO SIRVE (es un no-op a propósito) — usa
`kquitapp6 plasmashell` + relanzar (`kstart plasmashell`), o logout/login real si el cambio
requiere que `kwin_wayland` (con `kglobalaccel` embebido) redescubra algo.

Detalle técnico completo: memoria global `project_kde_plasmashell_reload.md`
(`~/.claude/projects/-home-unjordi/memory/`).
