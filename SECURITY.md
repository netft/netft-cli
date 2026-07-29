# Security Policy

## Supported versions

Security fixes are provided for the latest published release and the `main` branch.

| Version | Supported |
| --- | --- |
| 0.1.x | Yes |
| Older releases | No |

## Report a vulnerability

Do not open a public issue or pull request for a suspected vulnerability. Use [GitHub private vulnerability reporting](https://github.com/netft/netft-cli/security/advisories/new) so maintainers can assess the report before disclosure.

Include the affected version and platform, the security boundary involved, reproducible steps or a minimal proof of concept, the expected impact, and any suggested mitigation. Remove sensor addresses, private network details, credentials, recordings, and personal data.

## Software security

Install from this repository's GitHub Releases or the provided installers. The installers use HTTPS, verify the published checksum inventory, validate the archive layout, and check the installed executable version. Release binaries include their private `netft-cpp` core and libcurl dependency.

Treat CLI output as sensor data rather than trusted control input. Validate units, calibration, status, freshness, and acceptable ranges before using it in automation. Applying bias changes subsequent readings and must be performed only with explicit authorization and a safe mechanical setup.

## Network security

ATI Net F/T configuration discovery uses HTTP and the RDT stream uses UDP. These device protocols do not provide transport encryption, peer authentication, or message integrity, and this project does not add those properties. Configuration and samples must not be considered authenticated solely because they came from the configured host.

The supported deployment model places the sensor and CLI on a trusted, access-controlled network segment. Do not expose the sensor HTTP or RDT ports directly to the public internet or an untrusted shared network. Use network isolation, firewall allowlists, controlled routing, and an authenticated boundary when traffic must cross an untrusted network.

Verify discovered calibration and units against the intended sensor configuration before using measurements in a controller or safety-related decision. This project is not a substitute for an emergency stop, a safety-rated control path, or independent stale-data and limit monitoring.
