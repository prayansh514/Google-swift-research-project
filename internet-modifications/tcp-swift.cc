/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "tcp-swift.h"
#include "tcp-socket-state.h"
#include "ns3/simulator.h"
#include "rtt-estimator.h"
#include "ns3/nstime.h"
#include "tcp-socket-base.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpSwift");
NS_OBJECT_ENSURE_REGISTERED (TcpSwift);

TypeId
TcpSwift::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpSwift")
    .SetParent<TcpNewReno> ()
    .AddConstructor<TcpSwift> ()
    .SetGroupName ("Internet")
    .AddAttribute ("Alpha", "Additive increase parameter",
                   UintegerValue (2),
                   MakeUintegerAccessor (&TcpSwift::m_alpha),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Beta", "Multiplicative decrease aggressiveness multiplier",
                   UintegerValue (4),
                   MakeUintegerAccessor (&TcpSwift::m_beta),
                   MakeUintegerChecker<uint32_t> ());
  return tid;
}

TcpSwift::TcpSwift (void)
  : TcpNewReno (),
    m_alpha (2),
    m_beta (4),
    m_baseRtt (Time::Max ()),
    m_minRtt (Time::Max ()),
    m_lastRtt (Time::Max ()), // Tracks instantaneous delay
    m_cntRtt (0),
    m_doingSwiftNow (true),
    m_begSndNxt (0)
{
  NS_LOG_FUNCTION (this);
}

TcpSwift::TcpSwift (const TcpSwift& sock)
  : TcpNewReno (sock),
    m_alpha (sock.m_alpha),
    m_beta (sock.m_beta),
    m_baseRtt (sock.m_baseRtt),
    m_minRtt (sock.m_minRtt),
    m_lastRtt (sock.m_lastRtt),
    m_cntRtt (sock.m_cntRtt),
    m_doingSwiftNow (true),
    m_begSndNxt (0)
{
  NS_LOG_FUNCTION (this);
}

TcpSwift::~TcpSwift (void)
{
  NS_LOG_FUNCTION (this);
}

Ptr<TcpCongestionOps>
TcpSwift::Fork (void)
{
  return CopyObject<TcpSwift> (this);
}

void
TcpSwift::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt)
{
  tcb->retransmit_count=0;
  if (rtt.IsZero ())
    {
      return;
    }

  m_lastRtt = rtt; // Capture instantaneous delay for Swift math
  m_minRtt = std::min (m_minRtt, rtt);
  m_baseRtt = std::min (m_baseRtt, rtt);
  m_cntRtt++;
}

void
TcpSwift::EnableSwift (Ptr<TcpSocketState> tcb)
{
  m_doingSwiftNow = true;
  m_begSndNxt = tcb->m_nextTxSequence;
  m_cntRtt = 0;
  m_minRtt = Time::Max ();
  m_lastRtt = Time::Max ();
}

void
TcpSwift::DisableSwift ()
{
  m_doingSwiftNow = false;
}

void
TcpSwift::CongestionStateSet (Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCongState_t newState)
{
  if (newState == TcpSocketState::CA_OPEN)
    {
      EnableSwift (tcb);
    }
  else
    {
      DisableSwift ();
    }
}

void
TcpSwift::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
  if (!m_doingSwiftNow)
    {
      TcpNewReno::IncreaseWindow (tcb, segmentsAcked);
      return;
    }

  if (tcb->m_lastAckedSeq >= m_begSndNxt)
    { 
      // A Swift cycle has finished, adjust cwnd every RTT.
      tcb->m_canDecrease = true; 
      m_begSndNxt = tcb->m_nextTxSequence;

      if (m_cntRtt <= 2)
        { 
          TcpNewReno::IncreaseWindow (tcb, segmentsAcked);
        }
      else
        {
          if (tcb->m_cWnd < tcb->m_ssThresh)
            { 
              TcpNewReno::SlowStart (tcb, segmentsAcked);//only for initial time
            }
          else
            { 
              // --- PROPOSAL 2.2: CORE AIMD ENGINE ---
              double current_delay = m_lastRtt.GetSeconds();
              double target_delay = TargetDelay(tcb);

              if (current_delay < target_delay)
              {
                  // --- EXPERIMENTAL: DYNAMIC AI-SCALING ---

                  double current_rtt_us = m_lastRtt.GetMicroSeconds();
                  double base_rtt_us = m_baseRtt.GetMicroSeconds();

                  // 1. Calculate how much worse our delay is compared to the baseline
                  // (Using max(1.0) to prevent divide-by-zero crashes)
                  double scale_factor = current_rtt_us / std::max(1.0, base_rtt_us);

                  // 2. SAFETY CAP: Prevent massive bursts. 
                  // If a flow spikes, we don't want it dumping 50 packets at once.
                  scale_factor = std::min(5.0, scale_factor); 

                  // 3. Calculate the dynamic alpha
                  uint32_t dynamic_alpha = (uint32_t)(m_alpha * scale_factor);

                  // Ensure it always grows by at least 1 packet
                  dynamic_alpha = std::max((uint32_t)1, dynamic_alpha); 

                  // 4. Apply the dynamic increase
                  tcb->m_cWnd = tcb->m_cWnd + (dynamic_alpha * tcb->m_segmentSize);
              }
              else
              {
                // Multiplicative Decrease 
                if (tcb->m_canDecrease)
                {
                  double beta_decimal = (double) m_beta / 10.0; // Scale beta for smooth fractions
                  
                  // Calculate the precise reduction factor
                  double delay_diff = current_delay - target_delay;
                  double reduction_factor = 1.0 - (beta_decimal * (delay_diff / current_delay));
                  
                  // Ensure we don't multiply by a negative number
                  reduction_factor = std::max(0.1, reduction_factor);
                  
                  // Apply multiplicative decrease directly to the current window
                  uint32_t new_cwnd = (uint32_t) (tcb->m_cWnd.Get() * reduction_factor);
                  
                  // Allow window to fall below one packet for extreme pacing
                  tcb->m_cWnd = std::max((uint32_t)1, new_cwnd);
                }
              }
            }
          tcb->m_ssThresh = std::max (tcb->m_ssThresh, 3 * tcb->m_cWnd / 4);
        }

        // --- PROPOSAL 2.2: SUB-ONE-PACKET PACING ENGINE ---
        double current_rtt_sec = m_lastRtt.GetSeconds();
        if (current_rtt_sec > 0.0) 
        {
          // pacing_delay = RTT / cwnd
          double cwnd_bits = (double)tcb->m_cWnd.Get() * 8.0; 
          double pacing_bps = cwnd_bits / current_rtt_sec; 
          
          tcb->m_maxPacingRate = DataRate (pacing_bps);
        }
        // --------------------------------------------------

      m_cntRtt = 0;
      m_minRtt = Time::Max ();
    }
    else if (tcb->m_cWnd < tcb->m_ssThresh)
      {
        TcpNewReno::SlowStart (tcb, segmentsAcked);
      }
}

std::string
TcpSwift::GetName () const
{
  return "TcpSwift";
}

uint32_t
TcpSwift::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
  return std::max (std::min (tcb->m_ssThresh.Get (), tcb->m_cWnd.Get () - tcb->m_segmentSize), 2 * tcb->m_segmentSize);
}

// --- PROPOSAL 2.3: EXACT DYNAMIC TARGET DELAY SCALING ---
double TcpSwift::TargetDelay(Ptr<TcpSocketState> tcb)
{
    double base_rtt_sec = m_baseRtt.GetSeconds();
    
    // Safety check: If base RTT is uninitialized
    if (base_rtt_sec >= 1.0 || base_rtt_sec <= 0.0) {
        return 0.000020; 
    }

    // 1. Extract the IP Time-to-Live (TTL) field from incoming ACKs
    uint8_t received_ttl = tcb->m_rcvTtl;
    if (received_ttl == 0) {
        received_ttl = 64; // Fallback to prevent divide-by-zero anomalies
    }

    // 2. Calculate the hop count (64 - Received TTL)
    int estimated_hops = 64 - received_ttl;

    // Safety check: ensure at least 1 hop if routing starts at a different TTL (e.g., 128 or 255)
    if (estimated_hops < 1 || estimated_hops > 64) {
        estimated_hops = 1; 
    }

    // 3. Dynamically adjust the target delay for each individual flow
    double per_hop_allowance = 0.000005; // 5us allowance per physical hop
    double dynamic_target_delay = base_rtt_sec + (estimated_hops * per_hop_allowance);
    
    return dynamic_target_delay;
} }// namespace ns3