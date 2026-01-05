# Tests

All tests are C++ binaries that run the simulator for a brief window, stop it with `SIGUSR2`, and assert on the generated log.

Build & run:
```bash
cmake -S . -B build
cmake --build build
cd build
ctest -V
```

Each test receives the path to the built `sor_sim` executable from CTest (`$<TARGET_FILE:sor_sim>`). Logs are written to your temp dir under `sor_sim_tests/`.
