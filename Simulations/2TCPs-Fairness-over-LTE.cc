#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/error-model.h"
#include "ns3/quic-module.h"

// #include "ns3/gtk-config-store.h"

using namespace ns3;

/**
 * This is a simulation script for LTE+EPC. It instantiates one eNodeB, attaches one UE to the eNodeB,
 * and starts a TCP flow from a remote host to the UE over the LTE RAN.
 */

int
main(int argc, char* argv[])
{
    double distance = 250; // Default distance value.
    double simulationDuration = 40.0; // Default simulation duration in seconds.

    CommandLine cmd(__FILE__);
    cmd.AddValue("distance", "Distance between nodes (in meters)", distance);
    cmd.Parse(argc, argv);

    // Set the RNG seed and run number
    RngSeedManager::SetSeed(time(NULL)); // Sets the seed to the current time
    RngSeedManager::SetRun(rand()); // Sets a random run number

    uint16_t numOfEnbNodes = 1;

    ConfigStore inputConfig;
    inputConfig.ConfigureDefaults();

    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();

    // The transmission buffer of the Evolved Node B (eNB) is set at 512 kB:
    Config::SetDefault("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue(512 * 1024)); // muask(test)
    
    // Setup LTE propagation loss and fading:
    lteHelper->SetPathlossModelType(TypeId::LookupByName("ns3::ThreeLogDistancePropagationLossModel"));
    lteHelper->SetFadingModel("ns3::TraceFadingLossModel");
    lteHelper->SetFadingModelAttribute("TraceFilename", StringValue("src/lte/model/fading-traces/fading_trace.fad"));

    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();

    /*
        Setup the S1-U interface (The S1 User Plane interface).
        The S1-U interface is a part of the S1 interface that connects the
        E-UTRAN (Evolved Universal Terrestrial Radio Access Network)
        to the EPC (Evolved Packet Core).
    */
    epcHelper->SetAttribute("S1uLinkDataRate", DataRateValue(DataRate("1Gb/s"))); // The data rate to be used for the next S1-U link to be created
    epcHelper->SetAttribute("S1uLinkDelay", ns3::TimeValue(ns3::MilliSeconds(5))); // The delay to be used for the next S1-U link to be created

    lteHelper->SetEpcHelper(epcHelper); // Link the EpcHelper with the lteHelper
    /*
        The above step is necessary so that the LTE helper will trigger the appropriate EPC configuration in correspondence with some important configuration,
        such as when a new eNB or UE is added to the simulation, or an EPS bearer is created.
        The EPC helper will automatically take care of the necessary setup, such as S1 link creation and S1 bearer setup.
        All this will be done without the intervention of the user.
    */

    Ptr<Node> pgw = epcHelper->GetPgwNode();

    // Create a two RemoteHosts for the TCP server and QUIC server.
    NodeContainer tcpRemoteHostContainer;
    tcpRemoteHostContainer.Create(1);
    Ptr<Node> tcpRemoteHost = tcpRemoteHostContainer.Get(0);
    InternetStackHelper internet;
    internet.Install(tcpRemoteHostContainer);

    NodeContainer quicRemoteHostContainer;
    quicRemoteHostContainer.Create(1);
    Ptr<Node> quicRemoteHost = quicRemoteHostContainer.Get(0);
    QuicHelper quicStack;
    quicStack.InstallQuic(quicRemoteHostContainer);

    // Create the Internet
    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", StringValue("1Gbps"));  // muask: check the 'lena-simple-epc.cc' file for syntax.
    // p2ph.SetDeviceAttribute("Mtu", UintegerValue(1500));     // muask: check what is the default and if it needs to be modified.
    p2ph.SetChannelAttribute("Delay", StringValue("12ms"));
    NetDeviceContainer internetDevices = p2ph.Install(pgw, tcpRemoteHost);
    // Create an error model with a 0.5% packet loss rate
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(0.005)); // 0.5% packet loss ratio
    em->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET")); // Packet level error
    // Apply the error model to both devices of the P2P link
    internetDevices.Get(0)->SetAttribute("ReceiveErrorModel", PointerValue(em));    // muask: not sure if both (this line or the next one) are needed, or only one of them. double-check.
    internetDevices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0"); // Network address = "1.0.0.0", Mask = "255.0.0.0".
    Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);
    // interface 0 is localhost, 1 is the p2p device
    //Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress(1); //muask: unused

    // Setup static routing:
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting(tcpRemoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(epcHelper->GetUeDefaultGatewayAddress(), Ipv4Mask("255.0.0.0"), 1); // muask: what is the 1?

    // Create LTE nodes:
    NodeContainer tcpUeNodes;
    NodeContainer quicUeNodes;
    NodeContainer enbNodes;
    tcpUeNodes.Create(1);
    quicUeNodes.Create(1);
    enbNodes.Create(numOfEnbNodes);

    // Install Mobility Model:
    // Setup the LTE node's positions:
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0)); // The position of the eNB node
    positionAlloc->Add(Vector(distance, 0.0, 0.0)); // The position of the "TCP UE"
    positionAlloc->Add(Vector(0.0, distance, 0.0)); // The position of the "QUIC UE" // muask(QUIC): uncomment this.
    // Create and configure the MobilityHelper
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.SetPositionAllocator(positionAlloc);
    mobility.Install(enbNodes);
    mobility.Install(tcpUeNodes);
    mobility.Install(quicUeNodes);

    // Install LTE Devices to the nodes:
    NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer tcpUeLteDevs = lteHelper->InstallUeDevice(tcpUeNodes);
    NetDeviceContainer quicUeLteDevs = lteHelper->InstallUeDevice(quicUeNodes);

    // Set transmission power of the eNb to 46 dBm:
    Ptr<LteEnbNetDevice> lteEnbDev = enbLteDevs.Get(0)->GetObject<LteEnbNetDevice>();    
    lteEnbDev->GetPhy()->SetTxPower(46);
    // Set transmission power of the UEs to 23 dBm:
    Ptr<LteUeNetDevice> lteTcpUeDev = tcpUeLteDevs.Get(0)->GetObject<LteUeNetDevice>();
    lteTcpUeDev->GetPhy()->SetTxPower(23);
    Ptr<LteUeNetDevice> lteQuicUeDev = quicUeLteDevs.Get(0)->GetObject<LteUeNetDevice>();
    lteQuicUeDev->GetPhy()->SetTxPower(23); // muask(QUIC): uncomment this.

    // Install the IP stack on the UEs
    internet.Install(tcpUeNodes);
    quicStack.InstallQuic(quicUeNodes);
    Ipv4InterfaceContainer ueIpIface;
    NetDeviceContainer allUeLteDevs = NetDeviceContainer(tcpUeLteDevs);
    allUeLteDevs.Add(quicUeLteDevs);
    ueIpIface = epcHelper->AssignUeIpv4Address(allUeLteDevs);//muask(array?)
    // Assign IP address to UEs, and install applications
    for (uint32_t i = 0; i < tcpUeNodes.GetN(); ++i)
    {
        Ptr<Node> tcpUeNode = tcpUeNodes.Get(i);
        // Set the default gateway for the UE
        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(tcpUeNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1); // muask: what is the 1?
    }
    for (uint32_t i = 0; i < quicUeNodes.GetN(); ++i)
    {
        Ptr<Node> quicUeNode = quicUeNodes.Get(i);
        // Set the default gateway for the UE
        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(quicUeNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1); // muask: what is the 1?
    }
    // lteHelper->ActivateEpsBearer (ueLteDevs, EpsBearer (EpsBearer::NGBR_VIDEO_TCP_DEFAULT), EpcTft::Default ()); // muask: this is used in the 'lena-simple-epc.cc' file. is it needed?

    // Attach the UEs to the eNodeB:
    for (uint16_t i = 0; i < tcpUeNodes.GetN(); i++) // muask(QUIC): This will be affected.
    {
        lteHelper->Attach(tcpUeLteDevs.Get(i), enbLteDevs.Get(0));
        // Side effect: the default EPS bearer will be activated.
    }
    for (uint16_t i = 0; i < quicUeNodes.GetN(); i++) // muask(QUIC): This will be affected.
    {
        lteHelper->Attach(quicUeLteDevs.Get(i), enbLteDevs.Get(0));
        // Side effect: the default EPS bearer will be activated.
    }

// Setup the applications needed for the 2 TCP traffics from the 'TCP server' to 'UE-0':
    // Setup the first flow:
    uint16_t dlPort1 = 1100; // port for the first flow
    Address remoteAddr1(InetSocketAddress(ueIpIface.GetAddress(0), dlPort1));
    BulkSendHelper bulkSendHelper1("ns3::TcpSocketFactory", remoteAddr1);
    bulkSendHelper1.SetAttribute("MaxBytes", UintegerValue(0)); // Zero is unlimited.
    bulkSendHelper1.SetAttribute("SendSize", UintegerValue(512)); // TCP segment size in bytes
    ApplicationContainer sourceApps1 = bulkSendHelper1.Install(tcpRemoteHost);
    sourceApps1.Start(Seconds(0));
    sourceApps1.Stop(Seconds(simulationDuration));

    PacketSinkHelper packetSinkHelper1("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), dlPort1));
    ApplicationContainer sinkApps1 = packetSinkHelper1.Install(tcpUeNodes.Get(0));
    sinkApps1.Start(Seconds(0));
    sinkApps1.Stop(Seconds(simulationDuration));


    // Setup the second flow:
    uint16_t dlPort2 = 1200; // port for the second flow
    Address remoteAddr2(InetSocketAddress(ueIpIface.GetAddress(0), dlPort2)); // Same IP address, different port
    BulkSendHelper bulkSendHelper2("ns3::TcpSocketFactory", remoteAddr2);
    bulkSendHelper2.SetAttribute("MaxBytes", UintegerValue(0)); // Zero is unlimited.
    bulkSendHelper2.SetAttribute("SendSize", UintegerValue(512)); // TCP segment size in bytes
    ApplicationContainer sourceApps2 = bulkSendHelper2.Install(tcpRemoteHost);
    sourceApps2.Start(Seconds(0));
    sourceApps2.Stop(Seconds(simulationDuration));

    PacketSinkHelper packetSinkHelper2("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), dlPort2));
    ApplicationContainer sinkApps2 = packetSinkHelper2.Install(tcpUeNodes.Get(0));
    sinkApps2.Start(Seconds(0));
    sinkApps2.Stop(Seconds(simulationDuration));


    // Setup the applications needed for the QUIC traffic from the 'QUIC server' to 'UE-0':
    uint16_t dlPortQuic = 1600;

    // Create and configure a QUIC BulkSendApplication and install it on the QUIC server's node:
    Address remoteAddrQuic(InetSocketAddress(ueIpIface.GetAddress(0), dlPortQuic));
    BulkSendHelper bulkSendHelperQuic("ns3::QuicSocketFactory", remoteAddrQuic); // muask: a bit different than the 'tcp-bulk-send.cc' file, double-check it.
    bulkSendHelperQuic.SetAttribute("MaxBytes", UintegerValue(0)); // Zero is unlimited.
    bulkSendHelperQuic.SetAttribute("SendSize", UintegerValue(512)); // QUIC packet size in bytes
    // muask: Do we need to set the send interval for the bulksend application? 
    ApplicationContainer sourceAppsQuic = bulkSendHelperQuic.Install(quicRemoteHost);
    sourceAppsQuic.Start(Seconds(2));//muask
    sourceAppsQuic.Stop(Seconds(simulationDuration));

    // Create and configure a QUIC PacketSinkApplication and install it on 'UE-0':
    PacketSinkHelper PacketSinkHelperQuic("ns3::QuicSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), dlPortQuic));
    PacketSinkHelperQuic.SetAttribute("Protocol", TypeIdValue(QuicSocketFactory::GetTypeId()));
    ApplicationContainer sinkAppsQuic = PacketSinkHelperQuic.Install(quicUeNodes.Get(0));
    sinkAppsQuic.Start(Seconds(0));
    sinkAppsQuic.Stop(Seconds(simulationDuration));


    lteHelper->EnableTraces();
    Simulator::Stop(Seconds(simulationDuration));
    Simulator::Run();
    /*GtkConfigStore config;
    config.ConfigureAttributes();*/
    Simulator::Destroy();


    Ptr<PacketSink> tcpSink1 = DynamicCast<PacketSink>(sinkApps1.Get(0));
    uint64_t tcpTotalBytesReceived1 = tcpSink1->GetTotalRx();
    double tcpThroughput1 = (tcpTotalBytesReceived1 * 8.0) / (simulationDuration * 1000 * 1000);
    std::cout << "TCP FLOW 1 THROUGHTPUT: " << tcpThroughput1 << std::endl;

    Ptr<PacketSink> tcpSink2 = DynamicCast<PacketSink>(sinkApps2.Get(0));
    uint64_t tcpTotalBytesReceived2 = tcpSink2->GetTotalRx();
    double tcpThroughput2 = (tcpTotalBytesReceived2 * 8.0) / (simulationDuration * 1000 * 1000);
    std::cout << "TCP FLOW 2 THROUGHTPUT: " << tcpThroughput2 << std::endl;

    Ptr<PacketSink> quicSink = DynamicCast<PacketSink>(sinkAppsQuic.Get(0));
    uint64_t quicTotalBytesReceived = quicSink->GetTotalRx();
    double quicThroughput = (quicTotalBytesReceived * 8.0) / (simulationDuration * 1000 * 1000);
    std::cout << "QUIC FLOW 2 THROUGHTPUT: " << quicThroughput << std::endl;
    return 0;
}
