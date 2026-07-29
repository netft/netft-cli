## Summary

Describe the user-visible or technical outcome and the reason for the change.

## Scope

- Related issue:
- Commands or components affected:
- Platforms and architectures affected:
- Sensor model and firmware, if hardware-specific:
- Core snapshot or release-asset impact:

## Verification

List the exact commands run and their results.

```text

```

For hardware changes, describe the safe fixture state, explicit opt-in, and sanitized results.

## Checklist

- [ ] Tests cover behavior and machine contracts rather than user-facing prose.
- [ ] `pixi run check` and `pixi run sanitizers` pass, or exceptions are explained above.
- [ ] User-visible changes are documented in README.md or CHANGELOG.md as appropriate.
- [ ] Logs and network details contain no credentials or exact private addresses.
- [ ] Hardware was not used, or every hardware action was explicitly authorized and performed safely.
- [ ] Core changes were accepted upstream in `netft-cpp` before the snapshot was synchronized.
- [ ] This pull request does not create a release tag, draft, or release asset.
