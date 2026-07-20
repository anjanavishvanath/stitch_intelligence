# Project Scope Summary

The objective is to improve the efficiency of production lines in garment industry (typically 10–15 machines) by identifying sewing patterns and bottlenecks in real-time. The system uses ESP32-S3 based nodes  integrated into Juki and Hoshin industrial machines to capture granular telemetry. Using TinyML, the system will distinguish between specific garment operations (signatures) to identify cycle time deviations and "micro-stops" that indicate efficiency losses.

# Implementation Breakdown

## Phase 1: Hardware Integration & Signal Isolation

- Electrical Isolation: Interface the ESP32-S3 with the machine's data ports using high-speed optocouplers like the TLP2361 for the PULSEOUT stitch signal and TLP293-4 for digital signals like TRIMSIG and WIPSIG.
- Power Regulation: Utilize the onboard XL1509-5.0E1 buck converter to step down the factory +24VS to +5V , followed by the AP2114H-3.3 LDO to provide clean 3.3V power to the microcontroller.
- Pin Mapping: Configure IOs according to the machine interface, specifically IO14 for needle pulses, IO15 for the foot lifter, and IO16 for the thread trimmer.

## Phase 2: Firmware & Edge Intelligence (TinyML)
- Interrupt-Driven Capture: Set up hardware interrupts on IO14 to count PULSEOUT signals accurately.
- Pattern Learning: Conduct a "Gold Standard" training phase where a master operator performs specific sequences to record the timing between stitch bursts, foot lifts, and trims.
- Signature Recognition: Deploy a trained TinyML model to the ESP32-S3 to classify the current operation (e.g., "Sleeve Attachment") based on a 5–10 second sliding window of telemetry.

## Phase 3: Networking & Data Aggregation
- Mesh Communication: Use ESP-NOW or ESP-MESH to transmit machine states to a central gateway, ensuring reliable data transfer in an electrically noisy factory environment.
- Line Balancing Dashboard: Aggregate cycle times from all 15 machines to visualize the "Takt Time." The system flags a bottleneck when a specific station's actual cycle time exceeds the target baseline established by the master operator.