# HW1 IMU Feature Extraction Skeleton

This folder is the starting point for Home Assignment 1 on the Raspberry Pi Pico / Pico W. It copies the ICM-20948 driver from `lab_0` and the algorithm scaffolding from `lab_1`, but keeps the original lab folders untouched. The default firmware streams raw accelerometer and gyroscope values over USB so the build pipeline can be validated before adding signal processing and features.

## Layout

- `src/` – application entry point and placeholders for filters, features, and classifier code.
- `include/` – configurable parameters in `config.h`.
- `external/icm20948/` – driver copied from `lab_0/imu_example`.
- `lab_algorithms/` – Lab 1 reference implementations (`fft.c`, `statistic.c`, `quantization.c`).

## Build

```
mkdir -p build
cd build
cmake ..
ninja
```

This produces `imu_features.uf2` in the `build/` directory with USB CDC logging enabled and UART disabled.

## Flash & Run

1. Hold BOOTSEL while connecting the Pico.
2. Drag-and-drop `build/imu_features.uf2` onto the RPI-RP2 mass storage device.
3. Open a terminal at `115200` baud (e.g. `screen /dev/cu.usbmodemXXXX 115200`).
4. You should see a CSV stream with the header `ms,ax,ay,az,gx,gy,gz` at ~100 Hz.

## Next Steps

- Edit `src/filters.c` / `.h` to add the Lab 1 low-pass, high-pass, and moving-average filters (`TODO` markers show where to work).
- Port feature extraction logic into `src/features.c` and define the feature vector structures in `src/features.h`.
- Implement the gesture classifier inside `src/classifier.c` once features are available.
- Tune sampling, window, and filter parameters via `include/config.h`.

With those pieces in place you can migrate the quantization and FFT utilities from `lab_algorithms/` into the runtime processing path for the full HW1 submission.
