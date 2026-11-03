"""Regenerate the English diagram at its legacy filename."""
from pathlib import Path
from draw_wiring import draw
(Path(__file__).resolve().parents[1]/'docs/diagrams/wiring-he.svg').write_text(draw(True),encoding='utf-8')
