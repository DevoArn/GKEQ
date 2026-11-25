# Security policy

## Reporting a vulnerability

Please **do not open a public issue** for security problems. Report them
privately via GitHub: *Security → Report a vulnerability* on
https://github.com/DevoArn/GKEQ (private vulnerability reporting).

Include the GKEQ version, what you found, and how to reproduce it. You will
get an answer as soon as possible; fixes are released as a new version with a
note in the [changelog](CHANGELOG.md).

## Scope

- **Installer** (`GKEQ x.y.z Setup.exe`): runs with administrator rights, so
  anything that lets other code run elevated (DLL planting, path search
  issues, writing outside the install folders) is in scope.
- **Plug-in**: crashes or hangs caused by project files, presets or audio
  input, and memory-safety bugs.

## Verifying downloads

Only download GKEQ from the project's GitHub Releases page. Each release lists
the SHA-256 checksum of the installer; compare it with

```
certutil -hashfile "GKEQ 1.0.0 Setup.exe" SHA256
```

The installer is not code-signed yet, so Windows SmartScreen may warn about an
unknown publisher.
