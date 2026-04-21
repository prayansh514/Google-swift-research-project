#include "ns3/flow-monitor-module.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

// --- GLOBAL VARIABLE ---
static uint32_t g_currentCwnd = 0;

// --- TRACE HELPERS ---
static void CwndChange (uint32_t oldCwnd, uint32_t newCwnd) 
{
  g_currentCwnd = newCwnd; 
}
static void RttChange (Ptr<OutputStreamWrapper> stream, Time oldRtt, Time newRtt) {
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << newRtt.GetMicroSeconds () << std::endl;
}
static void QueueLengthChange (Ptr<OutputStreamWrapper> stream, uint32_t oldVal, uint32_t newVal) {
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << newVal << std::endl;
}

// --- TIME-BASED POLLING FIX ---
static void PollCwnd (Ptr<OutputStreamWrapper> stream) 
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << g_currentCwnd << std::endl;
  Simulator::Schedule (MilliSeconds (1), &PollCwnd, stream);
}

// --- NEW: TRACE CONNECTION HELPER ---
static void StartTracing (Ptr<OutputStreamWrapper> cwndStream, Ptr<OutputStreamWrapper> rttStream, Ptr<OutputStreamWrapper> qStream) 
{
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&CwndChange));
  Simulator::ScheduleNow (&PollCwnd, cwndStream);
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/RTT", MakeBoundCallback (&RttChange, rttStream));
  Config::ConnectWithoutContext ("/NodeList/100/DeviceList/100/TxQueue/PacketsInQueue", MakeBoundCallback (&QueueLengthChange, qStream));
}

int main (int argc, char *argv[])
{
  // 1. Configure Global Precision & Force Swift
  Time::SetResolution (Time::NS);
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::TcpSwift"));
  Config::SetDefault ("ns3::DropTailQueue<Packet>::MaxSize", StringValue ("20p"));

  // --- FORCE SWIFT PACING & HIGH RES RTT ---
  Config::SetDefault ("ns3::TcpSocketBase::Timestamp", BooleanValue (true)); 
  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (1)); 
  Config::SetDefault ("ns3::TcpSocket::InitialSlowStartThreshold", UintegerValue (2 * 1460)); 

  uint32_t nSenders = 100; 

  NodeContainer senders, router, receiver;
  senders.Create (nSenders);
  router.Create (1);
  receiver.Create (1);

  // 2. Setup Physical Links
  PointToPointHelper accessLink;
  accessLink.SetDeviceAttribute ("DataRate", StringValue ("100Mbps"));
  accessLink.SetChannelAttribute ("Delay", StringValue ("1us"));

  PointToPointHelper bottleneckLink;
  bottleneckLink.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  bottleneckLink.SetChannelAttribute ("Delay", StringValue ("10us"));

  InternetStackHelper stack;
  stack.Install (senders);
  stack.Install (router);
  stack.Install (receiver);

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  for (uint32_t i = 0; i < nSenders; ++i) {
      NetDeviceContainer link = accessLink.Install (senders.Get (i), router.Get (0));
      address.Assign (link);
      address.NewNetwork (); 
  }

  NetDeviceContainer botLink = bottleneckLink.Install (router.Get (0), receiver.Get (0));
  address.SetBase ("10.2.1.0", "255.255.255.0");
  Ipv4InterfaceContainer botInterfaces = address.Assign (botLink);

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // 3. Applications
  uint16_t port = 50000;
  PacketSinkHelper sink ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApps = sink.Install (receiver.Get (0));
  sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (Seconds (60.0)); // INCREASED TO 60 SECONDS

  // --- MICE VS ELEPHANTS SETUP ---
  uint32_t miceSizes[3] = {10240, 51200, 102400};        // 10KB, 50KB, 100KB
  uint32_t elephantSizes[3] = {1048576, 2097152, 5242880}; // 1MB, 2MB, 5MB

  for (uint32_t i = 0; i < nSenders; ++i) {
      BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (botInterfaces.GetAddress (1), port));
      
      uint32_t flowSize = 0;
      
      // 20% Elephants, 80% Mice
      if (i % 5 == 0) {
          flowSize = elephantSizes[(i / 5) % 3]; // Assign an Elephant size
      } else {
          flowSize = miceSizes[i % 3];           // Assign a Mouse size
      }

      source.SetAttribute ("MaxBytes", UintegerValue (flowSize));
      ApplicationContainer sourceApps = source.Install (senders.Get (i));
      sourceApps.Start (Seconds (1.0 + (i * 0.001))); 
      sourceApps.Stop (Seconds (60.0)); // INCREASED TO 60 SECONDS
  }

  // 4. File Output Setup (Using new "mixed" filenames)
  AsciiTraceHelper ascii;
  Ptr<OutputStreamWrapper> cwndStream = ascii.CreateFileStream ("swift-mixed-cwnd.data");
  Ptr<OutputStreamWrapper> rttStream = ascii.CreateFileStream ("swift-mixed-rtt.data");
  Ptr<OutputStreamWrapper> qStream = ascii.CreateFileStream ("swift-mixed-queue.data");

  // 5. SCHEDULE THE TRACING
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();
  
  Simulator::Stop (Seconds (60.0)); // INCREASED TO 60 SECONDS
  
  // High precision tracing start (5 zeros)
  Simulator::Schedule (Seconds (1.000001), &StartTracing, cwndStream, rttStream, qStream);
  Simulator::Run ();

  monitor->SerializeToXmlFile("swift-mixed-flowmon.xml", true, true);
  Simulator::Destroy ();

  return 0;
}