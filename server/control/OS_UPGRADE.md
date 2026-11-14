# Ubuntu maintenance — 2026-11-13

**Ubuntu 24.04.5 LTS is running and root SSH was verified after a real reboot.**
The host was upgraded sequentially from 20.04.6 through 22.04.5 to 24.04.5.
Running kernel: **6.8.0-149-generic**. Recovery and service continuity took priority;
no further release upgrade was started. This is not a claim that 24.04 is the
newest Ubuntu release.

## Backups

Before the first upgrade, configuration/application files, an all-database MySQL
dump (including routines/events/triggers), a consistent RooCore SQLite copy, and
service/package/table inventories were saved under `/var/backups/roocore-os-20261113`
and copied to the owner's computer. All four archive/database checksums matched;
archive and gzip read checks passed. These private files stay outside Git.

This is a service/data backup, not a provider snapshot or full disk image. Large
inactive game-server directories under `/root` were preserved but excluded from
the archive. A one-time off-host copy does not replace scheduled off-host backups.

## SSH recovery

During 22.04 → 24.04, port 22 stopped accepting connections. The provider console
showed the upgrader waiting for a configuration-file decision, with `ssh` and
`ssh.socket` inactive. The owner authenticated to Linux using the separate root
credential. Starting `ssh` restored the existing encrypted-key connection. All
subsequent package work was performed through root SSH.

A temporary key-only listener on port 1022 provided a second recovery route during
package completion. Existing SSH, sysctl, GRUB and website configuration was kept.
Broad obsolete-package removal was declined to preserve legacy dependencies during
recovery. No password reset, reinstallation or forced provider power action occurred.

OpenSSH now uses Ubuntu socket activation: **`ssh.socket` is enabled and active**,
listening on port 22. `ssh.service` itself need not be enabled for this arrangement.
The temporary 1022 listener disappeared on reboot and is no longer listening.
The local noVNC proxy was stopped. Existing SSH keys and strict host-key validation
were retained; authentication protections were not weakened.

## After the reboot

The planned restart completed at approximately 14:15 Israel time. A new root SSH
connection returned a new boot ID and kernel 6.8.0-149. A further independent root
connection at 14:18 succeeded.

| Check | Result |
|---|---|
| RooCore, Mosquitto, MySQL, Nginx, alert-manager | Started automatically and active |
| SSH socket and daily RooCore backup timer | Enabled and active |
| Failed systemd units | None at the post-reboot check |
| RooCore readiness | HTTP 200; MQTT connected and database ready |
| Hardware | No physical telemetry; commands unconditionally blocked |
| MySQL | 8.0.46-0ubuntu0.24.04.5 |
| Table counts | minecraft 19; phpmyadmin 19; remogone_BetaMogo 105 |
| Nginx static site, port 6400 | HTTP 200 |
| RooCore backup | Completed after reboot with SQLite integrity check |
| Package/configuration checks | dpkg audit, apt-get check, sshd and Nginx validation passed |

Counts/process status are not full application or per-row verification. RooCore,
MQTT and MySQL remain bound to loopback. The redesigned UI was tested through the
restored encrypted SSH tunnel; see [DEPLOYMENT.md](DEPLOYMENT.md).

The Mosquitto PPA was changed from its disabled Focal source to its signed Noble
source. **Mosquitto 2.1.2** replaced the Focal build, with no package removals.
All **29 application and isolated real-broker tests passed** against the new broker;
`pip check` passed. The broker and RooCore reconnected automatically.

## Existing applications and limits

- Nginx was already failed because an enabled `default.save` duplicated a listener.
  Moving that backup out of `sites-enabled` restored the static website.
- Apache remained disabled/inactive, matching the baseline. Its stale PHP 8.1 module
  reference was disabled after that module was removed; its config check then passed.
- PHP 7.4 FPM was disabled/inactive before maintenance. Nginx still points its PHP
  route to that unavailable socket. PHP 8.3 FPM is installed/active, but legacy PHP
  traffic was not switched blindly. A compatibility scan passed 1,993 files before
  stopping on the forum's `s9e/sweetdom` override of `DOMElement::insertAdjacentElement`
  (`forums/vendor/s9e/sweetdom/src/Element.php:139`). Legacy PHP applications remain
  **unverified and require compatibility work**.
- Alert-manager started with no restarts. Its source returned 403 after the first
  OS step. After the final reboot, a separate request using its headers returned
  **200, application/xml and parseable RSS**. No feed errors appeared in the observed
  current-boot log. No test notification was sent; recipient delivery is unverified.
- Pre-existing Certbot renewal failures for `dachuf.co.il` and `tipost.co.il` remain:
  manual validation lacks an automated authentication hook. No DNS credentials
  or hooks were invented.
- Retained obsolete packages need a reviewed cleanup. Not every legacy application
  is maintained or compatible merely because its process is active.

Upgrade and deployment logs are in `/var/backups/roocore-os-20261113`.
