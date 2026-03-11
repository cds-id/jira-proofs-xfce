# jira-proofs-xfce

Fork of [xfce4-screenshooter](https://gitlab.xfce.org/apps/xfce4-screenshooter) with native **Cloudflare R2 upload**, **Jira integration**, and **video screen recording**.

Capture screenshots or record videos and automatically upload them to R2, then post as comments on Jira issues — all from the native XFCE screenshot tool.

## Features

Everything from the original xfce4-screenshooter, plus:

- **Video Screen Recording** — Record fullscreen, active window, or selected region via FFmpeg (MP4/H.264, 30fps)
- **Upload to Cloudflare R2** — S3-compatible upload with AWS Signature V4 (images and videos)
- **Post to Jira** — Search issues, pick a preset, and post screenshots/recordings as ADF comments with embedded media
- **CLI support** — `--upload-r2` / `-u`, `--jira` / `-j ISSUE_KEY`, `--record-fullscreen`, `--record-window`, `--record-region`
- **Composable actions** — Cloud actions work alongside save, clipboard, open, and custom actions
- **Config file** — `~/.config/xfce4-screenshooter/cloud.toml`

## Configuration

Create `~/.config/xfce4-screenshooter/cloud.toml`:

```toml
[jira]
base_url = "https://yourteam.atlassian.net"
email = "you@example.com"
api_token = "your-jira-api-token"
default_project = "PROJ"

[r2]
account_id = "your-cf-account-id"
access_key_id = "your-r2-access-key"
secret_access_key = "your-r2-secret"
bucket = "screenshots"
public_url = "https://assets.yourdomain.com"

[presets]
bug_evidence = "Bug Evidence"
work_evidence = "Work Evidence"
```

## Installation

### From source

```bash
sudo apt install meson ninja-build gcc pkg-config \
  libglib2.0-dev libgtk-3-dev libxfce4util-dev libxfce4ui-2-dev \
  libxfconf-0-dev libxfce4panel-2.0-dev libexo-2-dev \
  libx11-dev libxi-dev libxext-dev libxfixes-dev \
  libcurl4-openssl-dev libjson-glib-dev

meson setup build
meson compile -C build
sudo meson install -C build
```

### From release

Download the `.deb` package from [Releases](https://github.com/cds-id/jira-proofs-xfce/releases) and install:

```bash
sudo dpkg -i jira-proofs-xfce-*.deb
sudo apt-get install -f
```

## Usage

### GUI

Run `xfce4-screenshooter`. The region dialog has a "Record Video" checkbox. The actions dialog includes options for "Upload to R2" and "Post to Jira".

### CLI — Screenshots

```bash
# Fullscreen screenshot + upload to R2
xfce4-screenshooter -f -u

# Region screenshot + save + upload to R2
xfce4-screenshooter -r -s ~/Pictures/ -u

# Fullscreen + post to specific Jira issue (auto-uploads to R2)
xfce4-screenshooter -f -j BNS-2727
```

### CLI — Video Recording

Requires FFmpeg (`sudo apt install ffmpeg`). X11 only.

```bash
# Record fullscreen (stop with the floating button or Escape)
xfce4-screenshooter --record-fullscreen

# Record active window + upload to R2
xfce4-screenshooter --record-window -u

# Record selected region + post to Jira
xfce4-screenshooter --record-region -j BNS-2727

# Record fullscreen + save to directory
xfce4-screenshooter --record-fullscreen -s ~/Videos/
```

## Credits

This is a fork of **xfce4-screenshooter** originally developed by the XFCE project.

**Original repository:** https://gitlab.xfce.org/apps/xfce4-screenshooter

**Original authors and maintainers:**
- Jerome Guelfucci (current maintainer)
- Daniel Bobadilla Leal
- Jani Monoses

**Original contributors:**
- David Collins, Enrico Troger, Mike Massonnet, Fabrice Viale, Sam Swift, Tom Hope

The cloud integration (R2 + Jira) and video recording were added by [CDS](https://github.com/cds-id), ported from [jira-proofs](https://github.com/cds-id/jira-proofs) (Tauri/Rust).

## License

GPL-2.0-or-later (same as original xfce4-screenshooter)
