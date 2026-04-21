#include "ns3/flow-monitor-module.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

// --- CWND TRACE HELPER FUNCTIONS --- changed by me
static void CwndChange (Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
  // This writes the timestamp and the window size to our file
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << newCwnd << std::endl;
}

static void TraceCwnd ()
{
  // This creates the text file and hooks our function into the Swift sender node
  AsciiTraceHelper ascii;
  Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("swift-cwnd.data");
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeBoundCallback (&CwndChange, stream));
}
static void RttChange (Ptr<OutputStreamWrapper> stream, Time oldRtt, Time newRtt)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << newRtt.GetMicroSeconds () << std::endl;
}

static void TraceRtt ()
{
  AsciiTraceHelper ascii;
  Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("swift-test-rtt.data");
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/RTT", MakeBoundCallback (&RttChange, stream));
}
// -----------------------------------

int main (int argc, char *argv[])
{
  // 1. SET NANOSECOND RESOLUTION (Crucial for Swift's microsecond targets)
  Time::SetResolution (Time::NS);

  // 2. TELL NS-3 TO USE SWIFT INSTEAD OF STANDARD TCP
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::TcpSwift"));

  // 3. Create 2 Nodes (The Computers)
  NodeContainer nodes;
  nodes.Create (2);

  // 4. Connect them with a 1Gbps wire and 10 microsecond delay
  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("1Gbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("10us"));

  NetDeviceContainer devices;
  devices = pointToPoint.Install (nodes);

  // 5. Install the Internet Stack (This will use Swift because of Step 2)
  InternetStackHelper stack;
  stack.Install (nodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  // 6. Setup Applications (Node 1 is the Receiver, Node 0 is the Sender)
  uint16_t port = 50000;
  
  // Receiver (PacketSink)
  Address sinkAddress (InetSocketAddress (interfaces.GetAddress (1), port));
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApps = packetSinkHelper.Install (nodes.Get (1));
  sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (Seconds (5.0));

  // Sender (BulkSend - pushing as much data as possible)
  BulkSendHelper source ("ns3::TcpSocketFactory", sinkAddress);
  source.SetAttribute ("MaxBytes", UintegerValue (0)); // 0 means send forever
  ApplicationContainer sourceApps = source.Install (nodes.Get (0));
  sourceApps.Start (Seconds (1.0));
  sourceApps.Stop (Seconds (5.0));

  // 7. Enable PCAP Tracing so we can "see" the packets later
  pointToPoint.EnablePcapAll ("swift-test");//-----------------------------------------------------------------------------------

  // 8. Run the Simulation for 5 virtual seconds
  // 2. INITIALIZE FLOW MONITOR BEFORE RUNNING
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  Simulator::Stop (Seconds (5.0));
  Simulator::Schedule (Seconds (1.00001), &TraceCwnd);
  Simulator::Schedule (Seconds (1.00001), &TraceRtt);
  
  Simulator::Run ();

  // 3. EXPORT DATA AFTER RUN BUT BEFORE DESTROY
  monitor->SerializeToXmlFile("swift-test-flowmon.xml", true, true);
  Simulator::Destroy ();

  return 0;
}