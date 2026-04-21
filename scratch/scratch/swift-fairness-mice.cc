#include "ns3/flow-monitor-module.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"

using namespace ns3;

// --- CWND TRACE HELPER ---
static void CwndChange (Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << newCwnd << std::endl;
}

static void TraceCwnd ()
{
  AsciiTraceHelper ascii;
  // We will save the data to a new file specifically for this fairness test
  Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("swift-fairness-mice-cwnd.data");
  // We track Node 0 to see how it behaves when 9 other nodes are fighting it
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeBoundCallback (&CwndChange, stream));
}
static void RttChange (Ptr<OutputStreamWrapper> stream, Time oldRtt, Time newRtt)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << newRtt.GetMicroSeconds () << std::endl;
}

static void TraceRtt ()
{
  AsciiTraceHelper ascii;
  Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("swift-fairness-mice-rtt.data");
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/RTT", MakeBoundCallback (&RttChange, stream));
}
// -------------------------

int main (int argc, char *argv[])
{
  Time::SetResolution (Time::NS);
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::TcpSwift"));
  
  // --- NEW: FORCE SWIFT PACING ON MICE FLOWS ---
  Config::SetDefault ("ns3::TcpSocketBase::Timestamp", BooleanValue (true)); // This one is fine
  
  // ---> THE FIX: Change TcpSocketBase to TcpSocket for these two <---
  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (1)); 
  Config::SetDefault ("ns3::TcpSocket::InitialSlowStartThreshold", UintegerValue (2 * 1460));


  uint32_t nSenders = 10;

  // 1. Create Nodes
  NodeContainer senders;
  senders.Create (nSenders);
  NodeContainer router;
  router.Create (1);
  NodeContainer receiver;
  receiver.Create (1);

  // 2. Setup the High-Speed Access Links (10Gbps)
  PointToPointHelper accessLink;
  accessLink.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  accessLink.SetChannelAttribute ("Delay", StringValue ("1us"));

  // 3. Setup the Bottleneck Link (1Gbps)
  PointToPointHelper bottleneckLink;
  bottleneckLink.SetDeviceAttribute ("DataRate", StringValue ("1Gbps"));
  bottleneckLink.SetChannelAttribute ("Delay", StringValue ("10us"));

  // 4. Install Internet Stack
  InternetStackHelper stack;
  stack.Install (senders);
  stack.Install (router);
  stack.Install (receiver);

  // 5. Connect Senders to Router and Assign IPs
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  
  for (uint32_t i = 0; i < nSenders; ++i) 
    {
      NetDeviceContainer link = accessLink.Install (senders.Get (i), router.Get (0));
      Ipv4InterfaceContainer interfaces = address.Assign (link);
      address.NewNetwork (); // Automatically increments IP to 10.1.2.0, etc.
    }

  // 6. Connect Router to Receiver (The Bottleneck)
  NetDeviceContainer botLink = bottleneckLink.Install (router.Get (0), receiver.Get (0));
  address.SetBase ("10.2.1.0", "255.255.255.0");
  Ipv4InterfaceContainer botInterfaces = address.Assign (botLink);

  // Enable routing across the network
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // 7. Setup the Receiver Application
  uint16_t port = 50000;
  Address sinkAddress (InetSocketAddress (botInterfaces.GetAddress (1), port));
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApps = packetSinkHelper.Install (receiver.Get (0));
  sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (Seconds (10.0));

// 8. Setup the 10 Sender Applications (MICE FLOWS ONLY)
  // Define 3 Mice Sizes (in Bytes): 10 KB, 50 KB, 100 KB
  uint32_t miceSizes[3] = {10240, 51200, 102400}; 

  for (uint32_t i = 0; i < nSenders; ++i) 
    {
      // Note: We move the BulkSendHelper inside the loop so we can assign 
      // a different MaxBytes size to each node individually
      BulkSendHelper source ("ns3::TcpSocketFactory", sinkAddress);
      
      // Cycle through the 3 mice sizes (Node 0 gets 10KB, Node 1 gets 50KB, Node 2 gets 100KB, Node 3 gets 10KB...)
      uint32_t flowSize = miceSizes[i % 3];
      source.SetAttribute ("MaxBytes", UintegerValue (flowSize));

      ApplicationContainer sourceApps = source.Install (senders.Get (i));
      // We stagger the start times slightly by 0.01s so they don't crash the router instantly
      sourceApps.Start (Seconds (1.0 + (i * 0.01))); 
      sourceApps.Stop (Seconds (10.0));
    }

  // 9. Start tracking the data
  Simulator::Schedule (Seconds (1.000001), &TraceCwnd);
  Simulator::Schedule (Seconds (1.000001), &TraceRtt);
  // 10. Run the Simulation for 10 seconds
  // --- 1. INITIALIZE FLOW MONITOR ---
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  Simulator::Stop (Seconds (10.0));
  Simulator::Run ();

  // --- 2. EXPORT THE DATA TO XML ---
  // This must happen AFTER Simulator::Run() but BEFORE Simulator::Destroy()
  monitor->SerializeToXmlFile("swift-fairness-mice-flowmon.xml", true, true);

  Simulator::Destroy ();
  return 0;
}