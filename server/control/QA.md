# Server acceptance inventory

- Access: anonymous/wrong password rejected; authorized data and static assets work; private listeners verified from host and remote client.
- Command gate: authenticated commands return 423; isolated real-broker tests deny every command writer and cross-device publication.
- Data: synthetic identity remains separate; no physical measurements are populated by tests; persistence and replay rejection survive service recreation.
- Connectivity: new telemetry permits online display; Last Will/offline, age threshold, server restart and broker loss prevent stale data being presented as current.
- Operations: native service starts, recovers after process loss, reconnects after broker restart; release update preserves DB; online backup opens with valid integrity and expected rows.
- UI controls: Overview/Events/System navigation, physical/simulation/all source filters, manual refresh and automatic refresh toggle; authentication precedes all data; no hardware operation control.
- Desktop visual check: 1440 × 960, readable Hebrew RTL, physical robot empty state and service state separated, synthetic events hidden from the default overview.
- Responsive check: all three views at 320, 390, 768 and 1440px; no horizontal page overflow. Visual inspection at 390 × 844 and outage at 320px; long times and status labels remain readable.
- Accessibility: keyboard skip link, navigation focus and pressed/current states; axe WCAG A/AA and best-practice checks on all three views. The testing context alone bypasses CSP to inject axe; production CSP stays restrictive.
- Adverse UI check: API failure removes online claims, shows an error and unknown service status; recovery restores current readings.
- Exploratory check: missing physical telemetry stays as unknown values rather than a fabricated zero battery or a simulation value.

Observed results are recorded in DEPLOYMENT.md. The redesign was checked against a local preview,
with explicitly mocked hardware data only inside the test browser for fresh/stale/outage cases.
Live navigation, responsive layouts, access controls and axe WCAG 2/2.1 A/AA checks were repeated
through the private server connection after deployment. Unit/broker tests do not communicate
with hardware. None prove physical robot behavior.
