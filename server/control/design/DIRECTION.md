# RooCore management UI

The first screen answers whether the physical robot is connected and what needs attention. It is a working console, without a marketing headline, hero illustration or invented measurements.

Palette: ink #20242b, secondary #58616d, background #f5f6f8, surface #ffffff, line #d9dee5, functional blue #245db0. Green and amber only communicate service states, always with text.
Typography: local Heebo variable, 16px base, restrained 26px page heading, tabular numbers and explicit bidirectional isolation for model names and times. Font licensed under OFL.
Layout: compact header and real navigation; on Overview, a wide robot summary beside system status; prerequisites stay near the robot's empty state. Events have source filters, with physical robot selected by default. System details have their own view. At narrow widths, preserve reading order and stack content without horizontal tables.
Copy: short operational Hebrew. "הרובוט עדיין לא מחובר" rather than a slogan. Unknown values remain unknown. Connection status never implies that the robot is physically safe. Explain the physical-control hold once, with details available on demand.

Visual review of the previous live UI: oversized headline and imitation robot drawing push actual status far down; repetitive explanations; simulated telemetry dominates the default event list; long desktop table overflows inside mobile. The redesign removes these issues.

Guidance considered: frontend-design, ui-ux-pro-max (minimal operational dashboard result; glassmorphism/landing recommendations rejected as unsuitable), humanizer. No generic theme or remote runtime scripts.
