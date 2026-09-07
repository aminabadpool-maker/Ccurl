# SEEKER - Web Content Extractor (C + libcurl)

This project is a C-based web scraper using libcurl and a Finite State Machine (FSM) HTML parser.

## Features
- Fetches HTML content via libcurl with custom User-Agent.
- Filters out SVGs, icons, math renderers, and small images.
- Extracts structured text (p, h1, h2, h3, li) formatted like Markdown.
- Resolves relative URLs to full absolute URLs.

## Build and Run (Termux)
```bash
pkg update && pkg install git clang libcurl -y
gcc 1.c -o seeker -licurl
./seeker https://example.com
```J
## Setup & SSH Push
```bash
git remote set-url origin git@github.com:aminabadpool-maker/Ccurl.git
git push -u origin main
```
