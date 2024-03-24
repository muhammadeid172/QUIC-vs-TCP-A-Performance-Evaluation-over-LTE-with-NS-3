/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// Network topology
//
//       n0 ----------- n1
//            500 Kbps
//             5 ms
//
// - Flow from n0 to n1 using BulkSendApplication.
//   and pcap tracing available when tracing is turned on.

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/packet-sink.h"
#include "ns3/point-to-point-module.h"
#include "ns3/quic-module.h"

#include <fstream>
#include <string>

using namespace ns3;


int
main(int argc, char* argv[])
{
    //
    // Explicitly create the nodes required by the topology (shown above).
    //
    NodeContainer nodes;
    nodes.Create(2);

    //
    // Explicitly create the point-to-point link required by the topology (shown above).
    //
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("500Kbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("5ms"));

    NetDeviceContainer devices;
    devices = pointToPoint.Install(nodes);

    //
    // Install the quic stack on the nodes
    //
    QuicHelper stack;
    stack.InstallQuic(nodes);

    //
    // We've got the "hardware" in place.  Now we need to add IP addresses.
    //
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i = ipv4.Assign(devices);

// Setup the applications needed for the 2 QUIC traffics from the node0 to node1
    // Setup the first flow:
    uint16_t dlPort1 = 1100; // port for the first flow
    Address remoteAddr1(InetSocketAddress(i.GetAddress(1), dlPort1));
    BulkSendHelper bulkSendHelper1("ns3::QuicSocketFactory", remoteAddr1);
    bulkSendHelper1.SetAttribute("MaxBytes", UintegerValue(0));
    bulkSendHelper1.SetAttribute("SendSize", UintegerValue(512));
    ApplicationContainer sourceApps1 = bulkSendHelper1.Install(nodes.Get(0));
    sourceApps1.Start(Seconds(2));
    sourceApps1.Stop(Seconds(10));

    PacketSinkHelper packetSinkHelper1("ns3::QuicSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), dlPort1));
    packetSinkHelper1.SetAttribute("Protocol", TypeIdValue(QuicSocketFactory::GetTypeId()));//muask
    ApplicationContainer sinkApps1 = packetSinkHelper1.Install(nodes.Get(1));
    sinkApps1.Start(Seconds(0));
    sinkApps1.Stop(Seconds(10));


    // Setup the second flow:
    uint16_t dlPort2 = 1200; // port for the second flow
    Address remoteAddr2(InetSocketAddress(i.GetAddress(1), dlPort2)); // Same IP address, different port
    BulkSendHelper bulkSendHelper2("ns3::QuicSocketFactory", remoteAddr2);
    bulkSendHelper2.SetAttribute("MaxBytes", UintegerValue(0));
    bulkSendHelper2.SetAttribute("SendSize", UintegerValue(512));
    ApplicationContainer sourceApps2 = bulkSendHelper2.Install(nodes.Get(0));
    sourceApps2.Start(Seconds(2));
    sourceApps2.Stop(Seconds(10));

    PacketSinkHelper packetSinkHelper2("ns3::QuicSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), dlPort2));
    packetSinkHelper2.SetAttribute("Protocol", TypeIdValue(QuicSocketFactory::GetTypeId()));//muask
    ApplicationContainer sinkApps2 = packetSinkHelper2.Install(nodes.Get(1));
    sinkApps2.Start(Seconds(0));
    sinkApps2.Stop(Seconds(10));

    //
    // Now, do the actual simulation.
    //
    Simulator::Stop(Seconds(10.0));
    Simulator::Run();
    Simulator::Destroy();

    Ptr<PacketSink> sink1 = DynamicCast<PacketSink>(sinkApps1.Get(0));
    std::cout << "Total Bytes Received: " << sink1->GetTotalRx() << std::endl;
    Ptr<PacketSink> sink2 = DynamicCast<PacketSink>(sinkApps2.Get(0));
    std::cout << "Total Bytes Received: " << sink2->GetTotalRx() << std::endl;

    return 0;
}
