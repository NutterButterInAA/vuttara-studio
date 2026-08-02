# Architecture

The application is divided into two principal layers:

1. **Qt interface layer** — windows, docks, commands, settings presentation and
   user interaction.
2. **Vuttara engine layer** — the sole owner of libobs initialization, objects,
   callbacks, threading and shutdown.

The interface must not call arbitrary libobs APIs directly. This boundary is a
core stability requirement for every later milestone.
