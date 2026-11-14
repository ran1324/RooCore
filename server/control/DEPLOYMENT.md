# Verified deployment — 2026-11-13

**Outcome:** RooCore runs on the user's Linux server as native services, without Docker.
The active application is `/opt/roocore/releases/20261113-ui-accessibility`, selected by
`/opt/roocore/current`. `roocore`, `mosquitto` and the daily backup timer are enabled.
The host now runs Ubuntu **24.04.5 LTS**, kernel **6.8.0-149-generic**. Root SSH,
RooCore and the existing core services were verified after a full host reboot.
Mosquitto was then updated to **2.1.2** for Noble and the tests passed again.

## Actual results

| Check | Observed result |
|---|---|
| Application and isolated real-MQTT test suite | **29 passed** on the server |
| Dependency consistency | `pip check` passed |
| Anonymous / incorrect administrator password | HTTP 401; no data returned |
| Authenticated command request | HTTP 423; no hardware action or MQTT publication |
| Anonymous / wrong-password broker client | Rejected by the isolated broker |
| Device identity isolation and server read-only permissions | Cross-device and command-topic writes rejected by the isolated broker |
| Real MQTT → API → database → browser | Synthetic `sim-620` data observed online, stored and labeled separately |
| Simulator killed without graceful disconnect | Last Will observed; simulation became offline |
| Broker stopped and started | Readiness changed to 503, all robots offline; recovered automatically without reviving historical data |
| Application process forcibly killed | systemd restarted it; readiness recovered and history remained |
| Physical robot state after all tests | No telemetry; offline; hardware commands disabled |
| Online SQLite backup | Backup service completed; a temporary restored copy passed integrity and row-count checks |
| Browser desktop | Redesigned Hebrew RTL console at 1440 × 960; Overview/Events/System navigation worked, no JavaScript errors |
| Browser responsive | All three views at widths 320, 390, 768 and 1440; no horizontal overflow; live mobile screenshot reviewed |
| Accessibility | Zero axe WCAG 2/2.1 A/AA and best-practice violations across all three live views after correcting the brand's accessible name |
| Browser API outage / recovery | Visible error, unknown robot/services; current status returned after recovery |
| Exposure | Server listeners only `127.0.0.1:8780` and `127.0.0.1:18883`; direct connections to public IP on both ports failed |
| Docker | No installed Docker executable remains; no daemon was ever started |

The test suite reports one non-failing Starlette deprecation warning about the `httpx`
TestClient backend. Locked versions passed; future test tooling should migrate that backend.
The initial native release was exercised after process/broker failure injection.
The redesigned service also came up automatically after a real Ubuntu upgrade reboot;
the final HTML accessibility correction was deployed afterward through the same tested
release process. Root SSH was verified through independent new connections. The daily
backup completed after reboot. See [OS_UPGRADE.md](OS_UPGRADE.md) for exact OS checks
and the limits of legacy application validation.

The redesign separates physical telemetry from simulation data and includes event-source
filters, runtime information, manual/automatic refresh, and explicit empty/outage states.
Fresh/stale/outage scenarios used mocked physical data only in the test browser; no physical
telemetry was written. Live access checks returned 401 for anonymous/wrong-password requests,
200 for authenticated status, and 423 for a command request. No robot command was published.
Accessibility injection bypassed CSP only in the test browser; production CSP stayed intact.

The database currently includes **synthetic test events only**. The server timestamps and
browser's local timezone are not hardware timing measurements. Screenshots and private login
details were saved in the user's local `Documents/Codex/RooCore Server` folder, outside Git.
The local SSH tunnel was opened and the live UI tested through it. The launcher in that folder
can recreate the tunnel later using the existing host key and encrypted SSH key.

## Remaining integration

- Implement and test the XIAO MQTT telemetry client; the current firmware uses its existing interfaces.
- Provide a protected home-network route or a separately reviewed TLS deployment for the controller.
- Verify SCI/OI against the actual Roomba 620, electrical commissioning and controller-local
  stopping/recovery policy before creating any command path.
- Complete domain delegation and Cloudflare Access/Tunnel; the zone is still pending.
  See [CLOUDFLARE.md](CLOUDFLARE.md). Current SSH access remains private and working.
- Add scheduled off-host backups. The pre-upgrade copy is off-host, but daily backups
  remain on the same machine.
- Resolve legacy PHP/forum compatibility and pre-existing certificate renewal failures;
  these are distinct from the verified RooCore service.

No firmware was changed or flashed, no UART was opened and no robot was operated.
Assembly wiring and power instructions were unchanged, so no assembly-guide regeneration was needed.
Source changes are in local branch `codex/roocore-server`; no GitHub push was performed.

[Runtime provenance](PROVISIONING.md) · [Operations](README.md) · [Hebrew guide](README-he.md).
