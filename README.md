# Evaluation of Google Swift Congestion Control in ns-3

**Authors:** Prayansh Kumar & Ritvij Gopal  
**Supervisor:** Prof. T. Venkatesh, Indian Institute of Technology Guwahati (IITG)

---

## 🚀 Project Overview
This research project implements and evaluates **Google Swift**, a delay-based congestion control protocol designed for high-performance datacenter environments. Using the **ns-3 (Network Simulator 3)**, we analyze Swift's ability to maintain ultra-low latency and solve "Incast" congestion through precise delay-gradient pacing.

In modern datacenters, "Mice" flows (short-lived RPCs) and "Elephant" flows (long-lived bulk data) compete for the same bottleneck. Standard loss-based protocols (like TCP Cubic) often fill router buffers, causing **Bufferbloat**. Swift solves this by monitoring the slope of RTT ($dR/dt$) to react *before* packet loss occurs.

---

## 🛠️ Implementation & Protocol Tuning

### Core Protocol Modifications
We modified the core ns-3 internet stack to support Swift's delay-sensitive logic:
- **`src/internet/model/tcp-swift.cc & .h`**: Implementation of the delay-gradient logic and window adjustment math:
  $$cwnd = cwnd + \alpha \left(1 - \frac{delay}{target}\right)$$
- **`src/internet/model/tcp-socket-base.cc & .h`**: Enhanced for microsecond-precision RTT sampling and RTT decomposition into **Host** and **Fabric** components.
- **`src/internet/model/tcp-socket-state.cc & .h`**: Updated to track Swift-specific congestion state variables.

### The "Swift Handcuffs" (Experimental Setup)
To observe Swift's behavior on tiny "Mice" flows that usually finish before the Congestion Avoidance phase, we tuned the TCP environment:
- **`InitialCwnd = 1`**: Forces the protocol to pace the transfer from the first byte.
- **`InitialSlowStartThreshold = 2`**: Effectively disables "Slow Start," forcing the flow into Swift's delay-governed phase immediately.
- **`Time::SetResolution(Time::NS)`**: Mandatory for calculating sub-millisecond delay gradients.

---

## 🧪 Experimental Methodology

The performance was evaluated across three topological scales, shifting from high-speed datacenter links to extreme bottleneck scenarios.

| Script | Scale | Bottleneck | Target Metric |
| :--- | :--- | :--- | :--- |
| `swift-test.cc` | 2-Node | **10 Gbps** | Host Delay vs. Fabric Delay |
| `swift-fairness.cc` | 10-Node | **1 Gbps** | Fairness & Flow Convergence |
| `swift-incast.cc` | 100-Node | **100 Mbps** | 100:1 Oversubscription Resilience |

### High Oversubscription Analysis
We specifically tested the "Incast" scenario with a **100:1 Oversubscription Ratio**. 
- **Aggregated Ingress Capacity:** 100 Nodes $\times$ 100 Mbps = 10,000 Mbps (10 Gbps).
- **Bottleneck Capacity:** 100 Mbps.
- **The Result:** Despite this extreme pressure, Swift maintained a **near-zero queue depth ($\leq 1$ packet)**. This proves that Swift’s delay-pacing is robust enough to handle the "Incast" bursts common in distributed computing (MapReduce/ML) without requiring deep, expensive switch buffers.

---

## 📊 Simulation Scenarios (`scratch/`)

### 1. Basic Validation (`swift-test-mice.cc`)
* **Logic:** Isolates the algorithm’s reaction to a single bottleneck.
* **Result:** Proved the "Micro-Sawtooth" behavior. As the RTT gradient increases toward a threshold, the CWND proactively shrinks, preventing buffer overflow.

### 2. Fairness Study (`swift-fairness-mice.cc`)
* **Logic:** Evaluates how Swift handles asymmetric competition (10KB, 50KB, 100KB sizes) and staggered starts.
* **Result:** Existing flows proactively shrank their windows to accommodate new flows, maintaining a stable RTT for the entire group.

### 3. 100-Node Incast Stress Test (`swift-incast-mice.cc`)
* **Telemetry:** Implemented a **1ms recursive polling function** (`PollCwnd`) to track window dynamics across 100 concurrent sockets.
* **Result:** Demonstrated **Zero-Queuing**. Proved that Swift can handle a massive burst without dropping packets or filling the 20-packet router buffer.

### 4. Mixed Workload: Mice vs. Elephants (`swift-incast-mixed.cc`)
* **Workload:** 80% Mice (Short RPCs) and 20% Elephants (Bulk Data).
* **Logic:** Specifically tracked Node 0 (1MB Elephant) navigating the "noise" of 80 bursty Mice flows.
* **Result:** The Elephant's CWND exhibited a "jagged" pattern as it micro-adjusted to protect the latency of short RPCs while maximizing total link utilization.

---

## 📂 Repository Structure

```text
.
├── internet-modifications/   # Core ns-3 stack changes (Copy to src/internet/model)
│   ├── tcp-swift.cc/h
│   ├── tcp-socket-base.cc/h
│   ├── tcp-socket-state.cc/h
│   └── tcp-socket-factory.cc/h
├── scratch/                  # Simulation scripts
│   ├── swift-test-mice.cc    # 2-Node Validation
│   ├── swift-fairness-mice.cc # 10-Node Study
│   ├── swift-incast-mice.cc   # 100-Node Stress Test
│   └── swift-incast-mixed.cc  # 80/20 Mice vs. Elephant
├── plots/                    # Result graphs (CWND, RTT, Queue Depth)
├── swift_presentation.pptx   # Final Research Presentation
└── swift_Research_paper.pdf  # Final Project Report
