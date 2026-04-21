# Evaluation of Google Swift Congestion Control in ns-3

**Authors:** Prayansh Kumar & Ritvji Gopal  
**Supervisor:** Prof. T. Venkatesh, Indian Institute of Technology Guwahati (IITG)

---

## 🚀 Project Overview
This research project explores the implementation and performance of **Google Swift**, a state-of-the-art congestion control protocol designed for high-performance datacenter environments. Using the **ns-3 (Network Simulator 3)**, we evaluate Swift's ability to manage ultra-low latency requirements and solve "Incast" congestion through precise delay-gradient pacing.

In modern datacenters, "Mice" flows (short-lived RPCs) and "Elephant" flows (long-lived bulk data) compete for the same bottleneck link. Standard loss-based protocols (like TCP Cubic) often fill router buffers, causing high tail-latency. Swift solves this by monitoring the slope of RTT ($dR/dt$) to react *before* packet loss occurs.

---

## 🛠️ Implementation & Protocol Tuning

### Core Protocol Modifications
We modified the core ns-3 internet stack to support Swift's delay-sensitive logic:
- **`src/internet/model/tcp-swift.cc & .h`**: Implemented the core window adjustment math:
  $$\text{cwnd} = \text{cwnd} + \alpha \left(1 - \frac{\text{delay}}{\text{target}}\right)$$
- **`src/internet/model/tcp-socket-base.cc & .h`**: Enabled microsecond-precision RTT sampling and per-packet timestamping to decompose RTT into Host and Fabric components.
- **`src/internet/model/tcp-socket-state.cc & .h`**: Updated to store the state variables required for multi-flow fairness.

### The "Swift Handcuffs" (Experimental Setup)
To observe Swift's behavior on small "Mice" flows that normally finish too fast for analysis, we manually tuned the TCP environment:
- **`InitialCwnd = 1`**: Forces the protocol to pace the transfer from the first byte.
- **`InitialSlowStartThreshold = 2`**: Effectively disables "Slow Start," forcing the flow into Swift's delay-governed Congestion Avoidance phase immediately.
- **`Time::SetResolution(Time::NS)`**: Mandatory for calculating sub-millisecond delay gradients.

---

## 📊 Simulation Scenarios

### 1. 2-Node Basic Validation (`swift-test-mice.cc`)
* **Setup:** Single sender and receiver on a 10 Mbps bottleneck.
* **Logic:** Isolate the algorithm’s reaction to a single bottleneck.
* **Result:** Proved the "Micro-Sawtooth" behavior. As the RTT gradient increases toward a threshold, the CWND proactively shrinks, preventing buffer overflow.

### 2. 10-Node Fairness Study (`swift-fairness-mice.cc`)
* **Setup:** 10 Senders in a Dumbbell topology with asymmetric file sizes (10KB, 50KB, 100KB) and staggered start times.
* **Logic:** Evaluates how Swift handles asymmetric competition and ensures "newcomer" flows can achieve their fair share.
* **Result:** Validated fairness. Existing flows proactively shrank their windows to accommodate new flows, maintaining a stable RTT for the entire group.

### 3. 100-Node Incast Stress Test (`swift-incast-mice.cc`)
* **Setup:** 100 simultaneous "Mice" flows hitting a single 10 Mbps bottleneck with a shallow 20-packet buffer.
* **Telemetry:** Implemented a **1ms recursive polling function** (`PollCwnd`) to track window dynamics across 100 concurrent sockets.
* **Result:** Demonstrated **Zero-Queuing**. The bottleneck router maintained a queue depth of $\leq 1$ packet, successfully eliminating Bufferbloat under heavy load.

### 4. Mixed Workload: Mice vs. Elephants (`swift-incast-mixed.cc`)
* **Setup:** A heterogeneous environment with an 80/20 distribution (80% Mice, 20% Elephants). Specifically tracked a 1MB Elephant (Node 0).
* **Logic:** The Elephant flow navigates the "noise" created by 80 bursty Mice flows starting and stopping.
* **Result:** The Elephant's CWND exhibits a "jagged" pattern as it micro-adjusts to make room for the Mice. This proves Swift's ability to protect the latency of short RPCs while maximizing link utilization.

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
