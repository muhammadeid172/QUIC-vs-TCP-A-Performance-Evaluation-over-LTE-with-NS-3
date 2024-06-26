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
 * and starts a QUIC flow from a remote host to the UE over the LTE RAN.
 */

void PacketArrivalCallback(Ptr<const Packet> packet, const Address& from);
int calcFileSize(std::string sizeStr);
double lastArrivalTime = -1;

int
main(int argc, char* argv[])
{
    double distance = 250; // Default distance value.
    double simulationDuration = 40.0; // Default simulation duration in seconds.
    std::string fileSize = "1MB";  // Default file size

    CommandLine cmd(__FILE__);
    cmd.AddValue("fileSize", "In the format of 10B, 10KB, 10MB", fileSize);
    cmd.Parse(argc, argv);

    int calculatedFileSize = calcFileSize(fileSize);

    // Set the RNG seed and run number
    RngSeedManager::SetSeed(time(NULL)); // Sets the seed to the current time
    RngSeedManager::SetRun(rand()); // Sets a random run number

    uint16_t numOfEnbNodes = 1;
    uint16_t numOfUeNodes = 1; // muask(QUIC): change to 2.

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

    // Create a single RemoteHost for the QUIC server. //muask(QUIC): Create another one for QUIC.
    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);
    QuicHelper stack;
    stack.InstallQuic(remoteHostContainer);

    // Create the Internet
    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", StringValue("1Gbps"));  // muask: check the 'lena-simple-epc.cc' file for syntax.
    // p2ph.SetDeviceAttribute("Mtu", UintegerValue(1500));     // muask: check what is the default and if it needs to be modified.
    p2ph.SetChannelAttribute("Delay", StringValue("12ms"));
    NetDeviceContainer internetDevices = p2ph.Install(pgw, remoteHost);
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
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(epcHelper->GetUeDefaultGatewayAddress(), Ipv4Mask("255.0.0.0"), 1); // muask: what is the 1?

    // Create LTE nodes:
    NodeContainer ueNodes;
    NodeContainer enbNodes;
    enbNodes.Create(numOfEnbNodes);
    ueNodes.Create(numOfUeNodes); // muask(QUIC): this will be affected.

    // Install Mobility Model:
    // Setup the LTE node's positions:
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0)); // The position of the eNB node
    positionAlloc->Add(Vector(distance, 0.0, 0.0)); // The position of the "QUIC UE"
    //positionAlloc->Add(Vector(0.0, distance, 0.0)); // The position of the "QUIC UE" // muask(QUIC): uncomment this.
    // Create and configure the MobilityHelper
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.SetPositionAllocator(positionAlloc);
    mobility.Install(enbNodes);
    mobility.Install(ueNodes);

    // Install LTE Devices to the nodes:
    NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice(ueNodes);

    // Set transmission power of the eNb to 46 dBm:
    Ptr<LteEnbNetDevice> lteEnbDev = enbLteDevs.Get(0)->GetObject<LteEnbNetDevice>();    
    lteEnbDev->GetPhy()->SetTxPower(46);
    // Set transmission power of the UEs to 23 dBm:
    Ptr<LteUeNetDevice> lteUeDev = ueLteDevs.Get(0)->GetObject<LteUeNetDevice>();
    lteUeDev->GetPhy()->SetTxPower(23);
    // Ptr<LteUeNetDevice> lteUeDev = ueLteDevs.Get(1)->GetObject<LteUeNetDevice>(); // muask(QUIC): uncomment this.
    // lteUeDev->GetPhy()->SetTxPower(23); // muask(QUIC): uncomment this.

    // Install the IP stack on the UEs
    stack.InstallQuic(ueNodes);
    Ipv4InterfaceContainer ueIpIface;
    ueIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueLteDevs));//muask(array?)
    // Assign IP address to UEs, and install applications
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        Ptr<Node> ueNode = ueNodes.Get(i);
        // Set the default gateway for the UE
        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(ueNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1); // muask: what is the 1?
    }
    // lteHelper->ActivateEpsBearer (ueLteDevs, EpsBearer (EpsBearer::NGBR_VIDEO_TCP_DEFAULT), EpcTft::Default ()); // muask: this is used in the 'lena-simple-epc.cc' file. is it needed?

    // Attach the UEs to the eNodeB:
    for (uint16_t i = 0; i < ueNodes.GetN(); i++) // muask(QUIC): This will be affected.
    {
        lteHelper->Attach(ueLteDevs.Get(i), enbLteDevs.Get(0));
        // Side effect: the default EPS bearer will be activated.
    }

    // Setup the applications needed for the QUIC traffic from the 'QUIC server' to 'UE-0':
    uint16_t dlPort = 1100;
    // muask: here there were also an 'ulPort' and 'otherPort', check if they are needed.

    // Create and configure a QUIC BulkSendApplication and install it on the QUIC server's node:
    Address remoteAddr(InetSocketAddress(ueIpIface.GetAddress(0), dlPort));
    BulkSendHelper bulkSendHelper("ns3::QuicSocketFactory", remoteAddr); // muask: a bit different than the 'tcp-bulk-send.cc' file, double-check it.
    bulkSendHelper.SetAttribute("MaxBytes", UintegerValue(calculatedFileSize));
    bulkSendHelper.SetAttribute("SendSize", UintegerValue(512)); // QUIC packet size in bytes
    // muask: Do we need to set the send interval for the bulksend application? 
    ApplicationContainer sourceApps = bulkSendHelper.Install(remoteHost);
    sourceApps.Start(Seconds(0.01));
    sourceApps.Stop(Seconds(simulationDuration));

    // Create and configure a QUIC PacketSinkApplication and install it on 'UE-0':
    PacketSinkHelper PacketSinkHelper("ns3::QuicSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), dlPort));
    PacketSinkHelper.SetAttribute("Protocol", TypeIdValue(QuicSocketFactory::GetTypeId()));
    ApplicationContainer sinkApps = PacketSinkHelper.Install(ueNodes.Get(0));
    sinkApps.Start(Seconds(0));
    sinkApps.Stop(Seconds(simulationDuration));
    
    // Setup tracing for received packets
    Config::ConnectWithoutContext("/NodeList/*/ApplicationList/*/$ns3::PacketSink/Rx", MakeCallback(&PacketArrivalCallback));
    //Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/PhyRxEnd", MakeCallback(&PacketArrivalCallback));

    lteHelper->EnableTraces();
    Simulator::Stop(Seconds(simulationDuration));
    Simulator::Run();

    /*GtkConfigStore config;
    config.ConfigureAttributes();*/

    Simulator::Destroy();
    if(lastArrivalTime == -1) {
        std::cout << "ERROR: Failed to track arrival times. [lastArrivalTime = " << lastArrivalTime << "]" << std::endl;
        return -1;
    }
    std::cout << lastArrivalTime << std::endl;
    return 0;
}

void PacketArrivalCallback(Ptr<const Packet> packet, const Address& from) {
    Time now = Simulator::Now();
    lastArrivalTime = now.GetSeconds();
}

int calcFileSize(std::string sizeStr) {
    sizeStr.pop_back();
    char unit = sizeStr.back();
    if(unit >= '0' && unit <= '9') {
        return stoi(sizeStr);
    }

    sizeStr.pop_back();
    int to_multiply = 1;
    switch(unit) {
        case 'K':
            to_multiply = 1024;
            break;
        case 'M':
            to_multiply = 1024*1024;
            break;
        default:
            std::cout << "ERROR: File size unit (" << unit << ") is not supported." << std::endl;
            exit(1);
    }
    return (stoi(sizeStr) * to_multiply);
}