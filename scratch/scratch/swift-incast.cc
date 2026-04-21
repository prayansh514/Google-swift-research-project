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
  // Write the time and the most recent CWND to the file
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << g_currentCwnd << std::endl;

  // Schedule this exact function to run again in 1 millisecond
  Simulator::Schedule (MilliSeconds (1), &PollCwnd, stream);
}

// --- NEW: TRACE CONNECTION HELPER ---
// This function must run AFTER the applications have started
static void StartTracing (Ptr<OutputStreamWrapper> cwndStream, Ptr<OutputStreamWrapper> rttStream, Ptr<OutputStreamWrapper> qStream) 
{
  // 1. Hook the CWND event to update our global variable
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&CwndChange));

  // 2. Kick off the 1ms polling loop to write to the CWND file
  Simulator::ScheduleNow (&PollCwnd, cwndStream);

  // 3. Keep RTT and Queue as standard event-driven hooks
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/RTT", MakeBoundCallback (&RttChange, rttStream));
  Config::ConnectWithoutContext ("/NodeList/100/DeviceList/100/TxQueue/PacketsInQueue", MakeBoundCallback (&QueueLengthChange, qStream));
}

int main (int argc, char *argv[])
{
  // 1. Configure Global Precision per Proposal Section 2.1 [cite: 15, 17]
  Time::SetResolution (Time::NS);
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::TcpSwift"));
  Config::SetDefault ("ns3::DropTailQueue<Packet>::MaxSize", StringValue ("20p"));

  uint32_t nSenders = 100; 

  NodeContainer senders, router, receiver;
  senders.Create (nSenders);
  router.Create (1);
  receiver.Create (1);

  // 2. Setup Physical Links per Proposal Section 4 [cite: 40, 41]
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
  sinkApps.Stop (Seconds (10.0));

  BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (botInterfaces.GetAddress (1), port));
  source.SetAttribute ("MaxBytes", UintegerValue (0));

  for (uint32_t i = 0; i < nSenders; ++i) {
      ApplicationContainer sourceApps = source.Install (senders.Get (i));
      sourceApps.Start (Seconds (1.0 + (i * 0.001))); // Staggered start [cite: 23]
      sourceApps.Stop (Seconds (10.0));
  }

  // 4. File Output Setup
  AsciiTraceHelper ascii;
  Ptr<OutputStreamWrapper> cwndStream = ascii.CreateFileStream ("swift-incast-cwnd.data");
  Ptr<OutputStreamWrapper> rttStream = ascii.CreateFileStream ("swift-incast-rtt.data");
  Ptr<OutputStreamWrapper> qStream = ascii.CreateFileStream ("swift-incast-queue.data");

  // 5. SCHEDULE THE TRACING [Fixed Timing Issue]
  

  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();
  Simulator::Stop (Seconds (10.0)); // Ensure this matches your stop time
  Simulator::Schedule (Seconds (1.1), &StartTracing, cwndStream, rttStream, qStream);
  Simulator::Run ();

  monitor->SerializeToXmlFile("swift-incast-flowmon.xml", true, true);
  Simulator::Destroy ();

  return 0;
}