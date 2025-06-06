from datetime import datetime
import uuid
import framework
import os
import os.path
from unittest import skip
from random import getrandbits
from scapy.utils import hexdump
from scapy.contrib.pfcp import CauseValues, IE_ApplyAction, IE_Cause, \
    IE_CreateFAR, IE_CreatePDR, IE_CreateURR, IE_DestinationInterface, \
    IE_DurationMeasurement, IE_EndTime, IE_EnterpriseSpecific, IE_FAR_Id, \
    IE_ForwardingParameters, IE_FSEID, IE_MeasurementMethod, IE_CreatedPDR, \
    IE_NetworkInstance, IE_NodeId, IE_PDI, IE_PDR_Id, IE_Precedence, \
    IE_QueryURR, IE_RecoveryTimeStamp, IE_RedirectInformation, IE_ReportType, \
    IE_ReportingTriggers, IE_SDF_Filter, IE_SourceInterface, IE_StartTime, \
    IE_TimeQuota, IE_UE_IP_Address, IE_URR_Id, IE_UR_SEQN, \
    IE_FTEID, IE_OuterHeaderCreation, IE_OuterHeaderRemoval, \
    IE_UsageReportTrigger, IE_VolumeMeasurement, IE_ApplicationId, PFCP, \
    PFCPAssociationSetupRequest, PFCPAssociationSetupResponse, \
    PFCPHeartbeatRequest, PFCPHeartbeatResponse, PFCPSessionDeletionRequest, \
    PFCPSessionDeletionResponse, PFCPSessionEstablishmentRequest, \
    PFCPSessionEstablishmentResponse, PFCPSessionModificationRequest, \
    PFCPSessionModificationResponse, PFCPSessionReportRequest, \
    IE_UsageReport_SMR, IE_UsageReport_SRR, IE_UsageReport_SDR, \
    IE_ErrorIndicationReport
from scapy.contrib.gtp import GTP_U_Header, GTPEchoRequest, GTPEchoResponse, \
    GTP_UDPPort_ExtensionHeader, GTP_PDCP_PDU_ExtensionHeader, \
    IE_Recovery, IE_SelectionMode, GTPErrorIndication, \
    IE_GSNAddress, IE_TEIDI
from scapy.layers.l2 import Ether
from scapy.layers.inet import IP, UDP, TCP
from scapy.layers.inet6 import IPv6
from scapy.layers.tls.all import TLS, TLSClientHello, TLS_Ext_ServerName, \
    ServerName, TLS_Ext_SupportedPointFormat, TLS_Ext_SupportedGroups, \
    TLS_Ext_SignatureAlgorithms, TLS_Ext_ALPN, ProtocolName
from scapy.packet import Raw


def seid():
    return uuid.uuid4().int & (1 << 64) - 1


def filter_ies(ies):
    return [ie for ie in ies if ie]


DROP_IP_V4 = "192.0.2.99"
DROP_IP_V6 = "2001:db8::5"
REDIR_IP_V4 = "192.0.2.100"
REDIR_IP_V6 = "2001:db8::6"
REDIR_TARGET_IP_V4 = "198.51.100.42"
REDIR_TARGET_IP_V6 = "2001:db8::1:7"
APP_RULE_IP_V4 = "192.0.2.101"
APP_RULE_IP_V6 = "2001:db8::2:1"
NON_APP_RULE_IP_V4 = "192.0.9.201"
NON_APP_RULE_IP_V6 = "2001:db8::2:2"
NON_APP_RULE_IP_2_V4 = "192.0.9.202"
NON_APP_RULE_IP_2_V6 = "2001:db8::2:3"
NON_APP_RULE_IP_3_V4 = "192.0.9.203"
NON_APP_RULE_IP_3_V6 = "2001:db8::2:4"
EXTRA_SDF_IP_V4 = "192.0.9.204"
EXTRA_SDF_IP_V6 = "2001:db8::3:1"


class IPv4Mixin(object):
    drop_ip = DROP_IP_V4
    redir_ip = REDIR_IP_V4
    redir_target_ip = REDIR_TARGET_IP_V4
    non_app_rule_ip = NON_APP_RULE_IP_V4
    non_app_rule_ip_2 = NON_APP_RULE_IP_2_V4
    non_app_rule_ip_3 = NON_APP_RULE_IP_3_V4
    extra_sdf_ip = EXTRA_SDF_IP_V4
    app_rule_ip = APP_RULE_IP_V4

    @classmethod
    def setup_tables(cls):
        cls.vapi.cli("ip table add 100")
        cls.vapi.cli("ip table add 200")

    @classmethod
    def config_interface(self, test_cls, iface, n):
        test_cls.vapi.cli("set interface ip table %s %d" %
                          (iface.name, n * 100))
        iface.config_ip4()
        iface.resolve_arp()

    @classmethod
    def tdf_setup_cmds(cls):
        return [
            "upf nwi name cp vrf 0",
            "upf nwi name access vrf 100",
            "upf nwi name sgi vrf 200",
            "upf node-id ip4 %s" % cls.if_cp.local_ip4,
            "upf pfcp endpoint ip %s vrf 0" % cls.if_cp.local_ip4,
            "upf tdf ul table vrf 100 ip4 table-id 1001",
            "upf tdf ul enable ip4 %s" % cls.if_access.name,
            "ip route add 0.0.0.0/0 table 200 via %s %s" %
            (cls.if_sgi.remote_ip4, cls.if_sgi.name),
            "create upf application proxy name TST",
            "upf application TST rule 3000 add l7 regex " +
            r"^https?://(.*\\.)*(example)\\.com/",
            "upf application TST rule 3001 add ipfilter " +
            "permit out ip from %s to assigned" % APP_RULE_IP_V4,
            "nat44 enable sessions 1024 endpoint-dependent",
            "upf nat pool 78.32.0.2 - 78.32.0.25 block_size 512 nwi sgi name testing",
            "upf nat pool 78.32.20.2 - 78.32.20.25 block_size 512 nwi sgi name not-testing",
            "upf ueip pool nwi sgi id mypool",
        ]

    @classmethod
    def pgw_setup_cmds(cls):
        return [
            "upf nwi name cp vrf 0",
            "upf nwi name epc vrf 100",
            "upf nwi name sgi vrf 200",
            "upf specification release 16",
            "upf node-id fqdn upg",
            "upf pfcp endpoint ip %s vrf 0" % cls.if_cp.local_ip4,
            "ip route add 0.0.0.0/0 table 200 via %s %s" %
            (cls.if_sgi.remote_ip4, cls.if_sgi.name),
            "upf gtpu endpoint ip %s nwi cp teid 0x80000000/2" % cls.if_cp.local_ip4,
            "upf gtpu endpoint ip %s nwi epc teid 0x80000000/2" % cls.if_grx.local_ip4,
            "nat44 enable sessions 1024 endpoint-dependent",
            "upf nat pool 78.32.0.2 - 78.32.0.25 block_size 512 nwi sgi name testing",
            "upf nat pool 78.32.20.2 - 78.32.20.25 block_size 512 nwi sgi name not-testing",
            "upf ueip pool nwi sgi id mypool",
        ]

    @property
    def pfcp_cp_ip(self):
        return self.if_cp.remote_ip4

    @property
    def pfcp_up_ip(self):
        return self.if_cp.local_ip4

    @property
    def ue_ip(self):
        return self.if_access.remote_ip4

    @property
    def sgi_remote_ip(self):
        return self.if_sgi.remote_ip4

    @property
    def grx_local_ip(self):
        return self.if_grx.local_ip4

    @property
    def grx_remote_ip(self):
        return self.if_grx.remote_ip4

    def outer_header_creation(self):
        return IE_OuterHeaderCreation(
            GTPUUDPIPV4=1, TEID=self.CLIENT_TEID, ipv4=self.if_grx.remote_ip4)

    def ie_fteid(self):
        return IE_FTEID(V4=1, TEID=self.UNODE_TEID, ipv4=self.if_grx.local_ip4)

    def ie_fteid_ch(self, chid):
        return IE_FTEID(CH=1, CHID=1, choose_id=chid, V4=1)

    def ie_ue_ip_address(self, SD=0):
        return IE_UE_IP_Address(ipv4=self.ue_ip, V4=1, SD=SD)

    def ie_redirect_information(self):
        return IE_RedirectInformation(
            type="IPv4 address",
            address=REDIR_TARGET_IP_V4)

    def ie_sdf_filter_for_redirect(self):
        return IE_SDF_Filter(
            FD=1,
            flow_description="permit out ip from %s to assigned" %
            REDIR_IP_V4)

    def ie_sdf_filter_extra_for_app_pdi(self):
        return IE_SDF_Filter(
            FD=1,
            flow_description="permit out ip from %s to assigned" %
            EXTRA_SDF_IP_V4)

    def ie_fseid(self):
        return IE_FSEID(ipv4=self.pfcp_cp_ip, v4=1, seid=self.cur_seid)

    def ie_outer_header_removal(self):
        return IE_OuterHeaderRemoval(header="GTP-U/UDP/IPv4")

    def gtp_ie_gsn_address(self):
        return IE_GSNAddress(length=4, ipv4_address=self.grx_local_ip)

    def verify_ie_fteid(self, ie_fteid):
        self.assertEqual(ie_fteid.TEID, self.teid)
        self.assertTrue(ie_fteid.V4)
        self.assertFalse(ie_fteid.V6)
        self.assertEqual(ie_fteid.ipv4, self.grx_local_ip)

    def verify_ie_fseid(self, ie_fseid):
        self.assertTrue(ie_fseid.v4)
        self.assertFalse(ie_fseid.v6)
        self.assertEqual(ie_fseid.ipv4, self.pfcp_up_ip)

    def verify_ie_ue_ip_address(self, ie_ue_ip):
        self.assertEqual(ie_ue_ip.V4, 1)
        self.assertEqual(ie_ue_ip.V6, 0)
        self.assertEqual(ie_ue_ip.ipv4, self.ue_ip)

    @property
    def IP(self):
        return IP


class IPv6Mixin(object):
    drop_ip = DROP_IP_V6
    redir_ip = REDIR_IP_V6
    redir_target_ip = REDIR_TARGET_IP_V6
    non_app_rule_ip = NON_APP_RULE_IP_V6
    non_app_rule_ip_2 = NON_APP_RULE_IP_2_V6
    non_app_rule_ip_3 = NON_APP_RULE_IP_3_V6
    extra_sdf_ip = EXTRA_SDF_IP_V6
    app_rule_ip = APP_RULE_IP_V6

    @classmethod
    def setup_tables(cls):
        cls.vapi.cli("ip6 table add 100")
        cls.vapi.cli("ip6 table add 200")

    @classmethod
    def config_interface(self, test_cls, iface, n):
        test_cls.vapi.cli("set interface ip6 table %s %d" %
                          (iface.name, n * 100))
        iface.config_ip6()
        iface.resolve_ndp()

    @classmethod
    def tdf_setup_cmds(cls):
        return [
            "upf nwi name cp vrf 0",
            "upf nwi name access vrf 100",
            "upf nwi name sgi vrf 200",
            "upf node-id ip6 %s" % cls.if_cp.local_ip6,
            "upf pfcp endpoint ip %s vrf 0" % cls.if_cp.local_ip6,
            "upf tdf ul table vrf 100 ip6 table-id 1001",
            "upf tdf ul enable ip6 %s" % cls.if_access.name,
            "ip route add ::/0 table 200 via %s %s" %
            (cls.if_sgi.remote_ip6, cls.if_sgi.name),
            "create upf application proxy name TST",
            "upf application TST rule 3000 add l7 regex " +
            r"^https?://(.*\\.)*(example)\\.com/",
            "upf application TST rule 3001 add ipfilter " +
            "permit out ip from %s to assigned" % APP_RULE_IP_V6,
        ]

    @classmethod
    def pgw_setup_cmds(cls):
        return [
            "upf nwi name cp vrf 0",
            "upf nwi name epc vrf 100",
            "upf nwi name sgi vrf 200",
            "upf specification release 16",
            "upf node-id ip6 %s" % cls.if_cp.local_ip6,
            "upf pfcp endpoint ip %s vrf 0" % cls.if_cp.local_ip6,
            "ip route add ::/0 table 200 via %s %s" %
            (cls.if_sgi.remote_ip6, cls.if_sgi.name),
            "upf gtpu endpoint ip6 %s nwi cp teid 0x80000000/2" % cls.if_cp.local_ip6,
            "upf gtpu endpoint ip6 %s nwi epc teid 0x80000000/2" % cls.if_grx.local_ip6,
        ]

    @property
    def pfcp_cp_ip(self):
        return self.if_cp.remote_ip6

    @property
    def pfcp_up_ip(self):
        return self.if_cp.local_ip6

    @property
    def ue_ip(self):
        return self.if_access.remote_ip6

    @property
    def sgi_remote_ip(self):
        return self.if_sgi.remote_ip6

    @property
    def grx_local_ip(self):
        return self.if_grx.local_ip6

    @property
    def grx_remote_ip(self):
        return self.if_grx.remote_ip6

    def outer_header_creation(self):
        return IE_OuterHeaderCreation(
            GTPUUDPIPV6=1, TEID=self.CLIENT_TEID, ipv6=self.if_grx.remote_ip6)

    def ie_fteid(self):
        return IE_FTEID(V6=1, TEID=self.UNODE_TEID, ipv6=self.if_grx.local_ip6)

    def ie_fteid_ch(self, chid):
        return IE_FTEID(CH=1, CHID=1, choose_id=chid, V6=1)

    def ie_ue_ip_address(self, SD=0):
        return IE_UE_IP_Address(ipv6=self.ue_ip, V6=1, SD=SD)

    def ie_redirect_information(self):
        return IE_RedirectInformation(
            type="IPv6 address",
            address=REDIR_TARGET_IP_V6)

    def ie_sdf_filter_for_redirect(self):
        return IE_SDF_Filter(
            FD=1,
            flow_description="permit out ip from %s to assigned" %
            REDIR_IP_V6)

    def ie_sdf_filter_extra_for_app_pdi(self):
        return IE_SDF_Filter(
            FD=1,
            flow_description="permit out ip from %s to assigned" %
            EXTRA_SDF_IP_V6)

    def ie_fseid(self):
        return IE_FSEID(ipv6=self.pfcp_cp_ip, v6=1, seid=self.cur_seid)

    def ie_outer_header_removal(self):
        return IE_OuterHeaderRemoval(header="GTP-U/UDP/IPv6")

    def gtp_ie_gsn_address(self):
        return IE_GSNAddress(length=16, ipv6_address=self.grx_local_ip)

    def verify_ie_fteid(self, ie_fteid):
        self.assertEqual(ie_fteid.TEID, self.teid)
        self.assertFalse(ie_fteid.V4)
        self.assertTrue(ie_fteid.V6)
        self.assertEqual(ie_fteid.ipv6, self.grx_local_ip)

    def verify_ie_fseid(self, ie_fseid):
        self.assertFalse(ie_fseid.v4)
        self.assertTrue(ie_fseid.v6)
        self.assertEqual(ie_fseid.ipv6, self.pfcp_up_ip)

    def verify_ie_ue_ip_address(self, ie_ue_ip):
        self.assertEqual(ie_ue_ip.V4, 0)
        self.assertEqual(ie_ue_ip.V6, 1)
        self.assertEqual(ie_ue_ip.ipv6, self.ue_ip)

    @property
    def IP(self):
        return IPv6

global_seq = 1

class PFCPHelper(object):
    """PFCP Helper class"""

    @classmethod
    def setUpClass(cls):
        super(PFCPHelper, cls).setUpClass()
        cls.ts = int((datetime.now() - datetime(1900, 1, 1)).total_seconds())

    def setUp(self):
        super(PFCPHelper, self).setUp()
        # avoid reusing sequence numbers so as not to receive cached responses
        global global_seq
        self.seq = global_seq
        global_seq += 10000

    def chat(self, pkt, expectedResponse, seid=None):
        self.logger.info("REQ: %r" % pkt)
        self.if_cp.add_stream(
            Ether(src=self.if_cp.remote_mac, dst=self.if_cp.local_mac) /
            self.IP(src=self.pfcp_cp_ip, dst=self.pfcp_up_ip) /
            UDP(sport=8805, dport=8805) /
            PFCP(
                version=1, seq=self.seq,
                S=0 if seid is None else 1,
                seid=0 if seid is None else seid) /
            pkt)
        self.pg_enable_capture(self.pg_interfaces)
        self.pg_start()
        resp = self.if_cp.get_capture(1)[0][PFCP]
        self.logger.info("RESP: %r" % resp)
        self.assertEqual(resp.seq, self.seq)
        self.seq += 1
        return resp[expectedResponse]

    def associate(self):
        resp = self.chat(PFCPAssociationSetupRequest(IE_list=[
            IE_RecoveryTimeStamp(timestamp=self.ts),
            IE_NodeId(id_type="FQDN", id="ergw")
        ]), PFCPAssociationSetupResponse)
        self.assertEqual(CauseValues[resp[IE_Cause].cause], "Request accepted")
        #FIXME: There are new 3GPP and BBF PFCP IEs introduced, need changes to
        #       scapy pfcp module since decoding is done wrong for TP/BBF IEs
        #FIXME: Proper build ID handling required
        #self.assertIn(b"vpp", resp[IE_EnterpriseSpecific].data)
        if IE_NodeId in resp:
            if resp[IE_NodeId].id_type is 2:
                self.assertEqual(resp[IE_NodeId].id, b"upg")
            elif resp[IE_NodeId].id_type is 0:
                self.assertEqual(resp[IE_NodeId].ipv4, self.if_cp.local_ip4)
            elif resp[IE_NodeId].id_type is 1:
                self.assertEqual(resp[IE_NodeId].ipv6, self.if_cp.local_ip6)

    def heartbeat(self):
        resp = self.chat(PFCPHeartbeatRequest(IE_list=[
            IE_RecoveryTimeStamp(timestamp=self.ts)
        ]), PFCPHeartbeatResponse)
        self.assertIn(IE_RecoveryTimeStamp, resp)

    def reverse_far(self):
        return IE_CreateFAR(IE_list=[
            IE_ApplyAction(FORW=1),
            IE_FAR_Id(id=2),
            IE_ForwardingParameters(IE_list=[
                IE_DestinationInterface(interface="Access"),
                IE_NetworkInstance(instance="access")
            ])
        ])

    def forward_pdr(self):
        return IE_CreatePDR(IE_list=[
            IE_FAR_Id(id=1),
            IE_PDI(IE_list=[
                IE_NetworkInstance(instance="access"),
                IE_SDF_Filter(
                    FD=1,
                    flow_description="permit out ip from any to assigned"),
                IE_SourceInterface(interface="Access"),
                self.ie_ue_ip_address(),
            ]),
            IE_PDR_Id(id=1),
            IE_Precedence(precedence=200),
        ])

    def establish_session(self):
        self.cur_seid = seid()
        resp = self.chat(PFCPSessionEstablishmentRequest(IE_list=[
            IE_CreateFAR(IE_list=[
                IE_ApplyAction(FORW=1),
                IE_FAR_Id(id=1),
                IE_ForwardingParameters(IE_list=[
                    IE_DestinationInterface(interface="SGi-LAN/N6-LAN"),
                    IE_NetworkInstance(instance="sgi")
                ])
            ]),
            self.reverse_far(),
            # FIXME: this is not handled properly :(
            IE_CreateFAR(IE_list=[
                IE_ApplyAction(FORW=1),
                IE_FAR_Id(id=3),
                IE_ForwardingParameters(IE_list=[
                    IE_DestinationInterface(interface="SGi-LAN/N6-LAN"),
                    IE_NetworkInstance(instance="sgi"),
                    self.ie_redirect_information(),
                ])
            ]),
            IE_CreateFAR(IE_list=[
                IE_ApplyAction(DROP=1),
                IE_FAR_Id(id=4),
            ]),
            self.forward_pdr(),
            IE_CreatePDR(IE_list=[
                IE_FAR_Id(id=2),
                IE_PDI(IE_list=[
                    IE_NetworkInstance(instance="sgi"),
                    IE_SDF_Filter(
                        FD=1,
                        flow_description="permit out ip from any to assigned"),
                    IE_SourceInterface(interface="SGi-LAN/N6-LAN"),
                    self.ie_ue_ip_address(SD=1)
                ]),
                IE_PDR_Id(id=2),
                IE_Precedence(precedence=200),
            ])
        ] + self.extra_pdrs() + [
            self.ie_fseid(),
            IE_NodeId(id_type=2, id="ergw")
        ]), PFCPSessionEstablishmentResponse, seid=self.cur_seid)
        self.assertEqual(CauseValues[resp[IE_Cause].cause], "Request accepted")
        self.verify_ie_fseid(resp[IE_FSEID])
        self.assertEqual(resp[IE_FSEID].seid, self.cur_seid)
        if IE_CreatedPDR in resp:
            self.teid = resp[IE_CreatedPDR][IE_FTEID].TEID
            self.logger.info(self.teid)
        if IE_NodeId in resp:
            if resp[IE_NodeId].id_type is 2:
                self.assertEqual(resp[IE_NodeId].id, b"upg")
            elif resp[IE_NodeId].id_type is 0:
                self.assertEqual(resp[IE_NodeId].ipv4, self.if_cp.local_ip4)
            elif resp[IE_NodeId].id_type is 1:
                self.assertEqual(resp[IE_NodeId].ipv6, self.if_cp.local_ip6)

    def extra_pdrs(self):
        return []

    def delete_session(self):
        resp = self.chat(PFCPSessionDeletionRequest(IE_list=[
            self.ie_fseid(),
            IE_NodeId(id_type=2, id="ergw")
        ]), PFCPSessionDeletionResponse, seid=self.cur_seid)
        self.assertEqual(CauseValues[resp[IE_Cause].cause], "Request accepted")

    def build_from_ue_to_sgi(self, payload=None, l4proto=UDP, ue_port=12345,
                                remote_port=23456, remote_ip=None, **kwargs):
        if remote_ip is None:
            remote_ip = self.sgi_remote_ip
        to_send = self.IP(src=self.ue_ip, dst=remote_ip) / \
            l4proto(sport=ue_port, dport=remote_port, **kwargs)
        if payload is not None:
            to_send /= Raw(payload)
        return to_send

    def send_from_access_to_sgi(self, payload=None, l4proto=UDP, ue_port=12345,
                                remote_port=23456, remote_ip=None, **kwargs):
        if remote_ip is None:
            remote_ip = self.sgi_remote_ip
        to_send = Ether(src=self.if_access.remote_mac,
                        dst=self.if_access.local_mac) / \
            self.build_from_ue_to_sgi(payload,
                        ue_port=ue_port, remote_port=remote_port,
                        remote_ip=remote_ip, **kwargs)
        self.if_access.add_stream(to_send)
        self.pg_enable_capture(self.pg_interfaces)
        self.pg_start()
        return len(to_send[self.IP])

    def assert_packet_sent_to_sgi(self, payload=None, l4proto=UDP,
                                  ue_port=12345, remote_port=23456,
                                  remote_ip=None):
        if remote_ip is None:
            remote_ip = self.sgi_remote_ip
        pkt = self.if_sgi.get_capture(1)[0]
        self.assertEqual(pkt[self.IP].src, self.ue_ip)
        self.assertEqual(pkt[self.IP].dst, remote_ip)
        self.assertEqual(pkt[l4proto].sport, ue_port)
        self.assertEqual(pkt[l4proto].dport, remote_port)
        if payload is not None:
            self.assertEqual(pkt[Raw].load, payload)

    def assert_packet_not_sent_to_sgi(self):
        self.if_sgi.assert_nothing_captured()

    def send_from_sgi_to_ue(self, payload=None, l4proto=UDP, ue_port=12345,
                                remote_port=23456, remote_ip=None, **kwargs):
        if remote_ip is None:
            remote_ip = self.sgi_remote_ip
        to_send = Ether(src=self.if_sgi.remote_mac,
                        dst=self.if_sgi.local_mac) / \
            self.IP(src=remote_ip, dst=self.ue_ip) / \
            l4proto(sport=remote_port, dport=ue_port, **kwargs)
        if payload is not None:
            if isinstance(payload, bytes):
                payload = Raw(payload)
            to_send /= payload
        self.if_sgi.add_stream(to_send)
        self.pg_enable_capture(self.pg_interfaces)
        self.pg_start()
        return len(to_send[self.IP])

    def verify_upg_counters(self, expected_assoc=0, expected_sessions=0,
                            expected_flows=0):
        flow_counter = self.statistics['/upf/total_flows']
        session_counter = self.statistics['/upf/total_sessions']
        association_counter = self.statistics['/upf/total_assoc']
        # Counter dir is implemented as a per-cpu vector of vectors
        self.assertEqual(flow_counter[0][0], expected_flows)
        self.assertEqual(session_counter[0][0], expected_sessions)
        self.assertEqual(association_counter[0][0], expected_assoc)

class TestTDFBase(PFCPHelper):
    """Base TDF Test"""

    @classmethod
    def setUpClass(cls):
        super(TestTDFBase, cls).setUpClass()
        try:
            cls.create_pg_interfaces(range(3))
            cls.interfaces = list(cls.pg_interfaces)

            cls.setup_tables()
            # separate assignments are easier to understand for some
            # tools like elpy than this:
            # cls.if_cp, cls.if_access, cls.if_sgi = cls.interfaces
            cls.if_cp = cls.interfaces[0]
            cls.if_access = cls.interfaces[1]
            cls.if_sgi = cls.interfaces[2]
            for n, iface in enumerate(cls.interfaces):
                iface.admin_up()
                cls.config_interface(cls, iface, n)
            for cmd in cls.upf_setup_cmds():
                cls.vapi.cli(cmd)
        except Exception:
            super(TestTDFBase, cls).tearDownClass()
            raise

    @classmethod
    def upf_setup_cmds(cls):
        return cls.tdf_setup_cmds()

    def extra_pdrs(self):
        return [
            IE_CreatePDR(IE_list=[
                IE_FAR_Id(id=3),
                IE_PDI(IE_list=[
                    IE_NetworkInstance(instance="access"),
                    self.ie_sdf_filter_for_redirect(),
                    IE_SourceInterface(interface="Access"),
                    self.ie_ue_ip_address(),
                ]),
                IE_PDR_Id(id=3),
                IE_Precedence(precedence=100),
            ]),
            IE_CreatePDR(IE_list=[
                IE_FAR_Id(id=4),
                IE_PDI(IE_list=[
                    IE_NetworkInstance(instance="access"),
                    IE_SDF_Filter(
                        FD=1,
                        flow_description="permit out ip from %s to assigned" %
                        self.drop_ip),
                    IE_SourceInterface(interface="Access"),
                    self.ie_ue_ip_address(),
                ]),
                IE_PDR_Id(id=4),
                IE_Precedence(precedence=100),
            ]),
        ]

    def test_upf(self):
        try:
            self.associate()
            self.heartbeat()
            self.verify_no_forwarding()
            self.establish_session()
            self.verify_forwarding()
            self.verify_drop()
            # FIXME: the IP redirect is currently also handled by the proxy
            # self.verify_redirect()
            self.verify_upg_counters(expected_flows=3, expected_assoc=1, expected_sessions=1)
            self.delete_session()
            self.verify_no_forwarding()
        finally:
            self.vapi.cli("show error")

    def test_reporting(self):
        try:
            self.associate()
            self.heartbeat()
            self.establish_reporting_session(report_app=False)
            self.verify_reporting()
            self.verify_session_modification()
            self.delete_session()
        finally:
            self.vapi.cli("show error")

    @skip("app reporting is disabled for now")
    def test_app_reporting(self):
        try:
            self.associate()
            self.heartbeat()
            self.establish_reporting_session(report_app=True)
            self.verify_app_reporting()
            self.vapi.cli("show upf flows")
            self.delete_session()
        finally:
            self.vapi.cli("show error")

    def show_commands_at_teardown(self):
        self.logger.info(self.vapi.cli("show upf flows"))
        self.logger.info(self.vapi.cli("show hardware"))

    def establish_reporting_session(self, report_app=False):
        self.cur_seid = seid()
        resp = self.chat(PFCPSessionEstablishmentRequest(IE_list=filter_ies([
            IE_CreateFAR(IE_list=[
                IE_ApplyAction(FORW=1),
                IE_FAR_Id(id=1),
                IE_ForwardingParameters(IE_list=[
                    IE_DestinationInterface(interface="SGi-LAN/N6-LAN"),
                    IE_NetworkInstance(instance="sgi")
                ])
            ]),
            IE_CreateFAR(IE_list=[
                IE_ApplyAction(FORW=1),
                IE_FAR_Id(id=2),
                IE_ForwardingParameters(IE_list=[
                    IE_DestinationInterface(interface="Access"),
                    IE_NetworkInstance(instance="access")
                ])
            ]),
            IE_CreateURR(IE_list=[
                IE_MeasurementMethod(EVENT=1, VOLUM=1, DURAT=1),
                IE_ReportingTriggers(start_of_traffic=1),
                IE_TimeQuota(quota=60),
                IE_URR_Id(id=1)
            ]) if not report_app else None,
            IE_CreatePDR(IE_list=filter_ies([
                IE_FAR_Id(id=1),
                IE_PDI(IE_list=[
                    IE_NetworkInstance(instance="access"),
                    IE_SDF_Filter(
                        FD=1,
                        flow_description="permit out ip from any to assigned"),
                    IE_SourceInterface(interface="Access"),
                    self.ie_ue_ip_address()
                ]),
                IE_PDR_Id(id=1),
                IE_Precedence(precedence=200),
                IE_URR_Id(id=1) if not report_app else None
            ])),
            IE_CreatePDR(IE_list=[
                IE_FAR_Id(id=2),
                IE_PDI(IE_list=[
                    IE_NetworkInstance(instance="sgi"),
                    IE_SDF_Filter(
                        FD=1,
                        flow_description="permit out ip from any to assigned"),
                    IE_SourceInterface(interface="SGi-LAN/N6-LAN"),
                    self.ie_ue_ip_address(SD=1)
                ]),
                IE_PDR_Id(id=2),
                IE_Precedence(precedence=200),
            ]),
            IE_CreateURR(IE_list=[
                IE_MeasurementMethod(VOLUM=1, DURAT=1),
                IE_ReportingTriggers(),
                IE_TimeQuota(quota=60),
                IE_URR_Id(id=2)
            ]) if report_app else None,
            IE_CreatePDR(IE_list=[
                IE_FAR_Id(id=1),
                IE_PDI(IE_list=[
                    self.ie_sdf_filter_extra_for_app_pdi(),
                    IE_ApplicationId(id="TST"),
                    IE_NetworkInstance(instance="access"),
                    IE_SourceInterface(interface="Access"),
                    self.ie_ue_ip_address(),
                ]),
                IE_PDR_Id(id=3),
                IE_Precedence(precedence=100),
                IE_URR_Id(id=2)
            ]) if report_app else None,
            IE_CreatePDR(IE_list=[
                IE_FAR_Id(id=2),
                IE_PDI(IE_list=[
                    IE_SDF_Filter(
                         FD=1,
                         flow_description="permit out ip from %s to assigned"
                         % self.extra_sdf_ip),
                    IE_ApplicationId(id="TST"),
                    IE_NetworkInstance(instance="sgi"),
                    IE_SourceInterface(interface="SGi-LAN/N6-LAN"),
                    self.ie_ue_ip_address(SD=1),
                ]),
                IE_PDR_Id(id=4),
                # FIXME: likely a bug in 20.01 branch: PDR for DL
                # traffic must have lower precedence
                IE_Precedence(precedence=10),
                IE_URR_Id(id=2)
            ]) if report_app else None,
            self.ie_fseid(),
            IE_NodeId(id_type=2, id="ergw")
        ])), PFCPSessionEstablishmentResponse, seid=self.cur_seid)
        self.assertEqual(CauseValues[resp[IE_Cause].cause], "Request accepted")
        self.verify_ie_fseid(resp[IE_FSEID])
        self.assertEqual(resp[IE_FSEID].seid, self.cur_seid)

    def assert_packet_sent_to_access(self, payload=None, l4proto=UDP,
                                     ue_port=12345, remote_port=23456,
                                     remote_ip=None):
        if remote_ip is None:
            remote_ip = self.sgi_remote_ip
        pkt = self.if_access.get_capture(1)[0]
        self.assertEqual(pkt[self.IP].src, remote_ip)
        self.assertEqual(pkt[self.IP].dst, self.ue_ip)
        self.assertEqual(pkt[l4proto].sport, remote_port)
        self.assertEqual(pkt[l4proto].dport, ue_port)
        if payload is not None:
            self.assertEqual(pkt[Raw].load, payload)

    def assert_packet_not_sent_to_access(self):
        self.if_access.assert_nothing_captured()

    def verify_no_forwarding(self):
        # Access -> SGi
        self.send_from_access_to_sgi(b"42")
        self.assert_packet_not_sent_to_sgi()
        # SGi -> Access
        self.send_from_sgi_to_ue(b"42")
        self.assert_packet_not_sent_to_access()

    def verify_forwarding(self):
        # Access -> SGi
        self.send_from_access_to_sgi(b"42")
        self.assert_packet_sent_to_sgi(b"42")
        # SGi -> Access
        self.send_from_sgi_to_ue(b"4242")
        self.assert_packet_sent_to_access(b"4242")

    def verify_drop(self):
        # Access -> SGi
        self.send_from_access_to_sgi(b"42", remote_ip=self.drop_ip)
        self.assert_packet_not_sent_to_sgi()

    def verify_redirect(self):
        # FIXME: the IP redirect is currently also handled by the proxy
        self.send_from_access_to_sgi(b"42", remote_ip=self.redir_ip)
        self.assert_packet_sent_to_sgi(b"42", remote_ip=self.redir_target_ip)

    def verify_reporting(self):
        # Access -> SGi
        self.send_from_access_to_sgi(b"42")
        self.assert_packet_sent_to_sgi(b"42")
        sr = self.if_cp.get_capture(1)[0][PFCPSessionReportRequest]
        self.assertEqual(sr[IE_ReportType].UPIR, 0)
        self.assertEqual(sr[IE_ReportType].ERIR, 0)
        self.assertEqual(sr[IE_ReportType].USAR, 1)
        self.assertEqual(sr[IE_ReportType].DLDR, 0)
        self.assertEqual(sr[IE_URR_Id].id, 1)
        self.assertEqual(sr[IE_UR_SEQN].number, 0)
        rt = sr[IE_UsageReportTrigger]
        self.assertEqual(rt.IMMER, 0)
        self.assertEqual(rt.DROTH, 0)
        self.assertEqual(rt.STOPT, 0)
        self.assertEqual(rt.START, 1)
        self.assertEqual(rt.QUHTI, 0)
        self.assertEqual(rt.TIMTH, 0)
        self.assertEqual(rt.VOLTH, 0)
        self.assertEqual(rt.PERIO, 0)
        self.assertEqual(rt.EVETH, 0)
        self.assertEqual(rt.MACAR, 0)
        self.assertEqual(rt.ENVCL, 0)
        self.assertEqual(rt.MONIT, 0)
        self.assertEqual(rt.TERMR, 0)
        self.assertEqual(rt.LIUSA, 0)
        self.assertEqual(rt.TIMQU, 0)
        self.assertEqual(rt.VOLQU, 0)
        self.verify_ie_ue_ip_address(sr[IE_UE_IP_Address])

    def verify_session_modification(self):
        send_len = 0
        for i in range(0, 3):
            send_len += self.send_from_access_to_sgi(b"42 foo bar baz")
            self.assert_packet_sent_to_sgi(b"42 foo bar baz")
        resp = self.chat(PFCPSessionModificationRequest(IE_list=[
            IE_QueryURR(IE_list=[IE_URR_Id(id=1)])
        ]), PFCPSessionModificationResponse, seid=self.cur_seid)
        self.assertEqual(CauseValues[resp[IE_Cause].cause], "Request accepted")
        # TODO: check timestamps & duration
        self.assertIn(IE_StartTime, resp)
        self.assertIn(IE_EndTime, resp)
        self.assertIn(IE_DurationMeasurement, resp)
        self.assertIn(IE_UR_SEQN, resp)
        rt = resp[IE_UsageReportTrigger]
        self.assertEqual(rt.IMMER, 1)
        self.assertEqual(rt.DROTH, 0)
        self.assertEqual(rt.STOPT, 0)
        self.assertEqual(rt.START, 0)
        self.assertEqual(rt.QUHTI, 0)
        self.assertEqual(rt.TIMTH, 0)
        self.assertEqual(rt.VOLTH, 0)
        self.assertEqual(rt.PERIO, 0)
        self.assertEqual(rt.EVETH, 0)
        self.assertEqual(rt.MACAR, 0)
        self.assertEqual(rt.ENVCL, 0)
        self.assertEqual(rt.MONIT, 0)
        self.assertEqual(rt.TERMR, 0)
        self.assertEqual(rt.LIUSA, 0)
        self.assertEqual(rt.TIMQU, 0)
        self.assertEqual(rt.VOLQU, 0)
        vm = resp[IE_VolumeMeasurement]
        self.assertTrue(vm.DLVOL)
        self.assertTrue(vm.ULVOL)
        self.assertTrue(vm.TOVOL)
        self.assertEqual(vm.total, send_len)
        self.assertEqual(vm.uplink, send_len)
        self.assertEqual(vm.downlink, 0)
        # TODO: verify more packets in both directions

    def verify_traffic_reporting(self, up_len, down_len):
        resp = self.chat(PFCPSessionModificationRequest(IE_list=[
            IE_QueryURR(IE_list=[IE_URR_Id(id=2)])
        ]), PFCPSessionModificationResponse, seid=self.cur_seid)
        vm = resp[IE_VolumeMeasurement]
        self.assertTrue(vm.DLVOL)
        self.assertTrue(vm.ULVOL)
        self.assertTrue(vm.TOVOL)
        self.assertEqual(vm.total, up_len + down_len)
        self.assertEqual(vm.uplink, up_len)
        self.assertEqual(vm.downlink, down_len)

    def tcp_handshake(self, remote_ip, remote_port):
        # 31 instead of 32 to make it a little bit easier to deal
        # with integer overflows
        s1, s2 = getrandbits(31), getrandbits(31)

        self.send_from_access_to_sgi(
            l4proto=TCP, flags="S",
            remote_ip=remote_ip, remote_port=remote_port, seq=s1, ack=0)
        # self.assert_packet_sent_to_sgi(
        #     l4proto=TCP,
        #     remote_ip=remote_ip, remote_port=remote_port)

        # self.send_from_sgi_to_ue(
        #     l4proto=TCP,
        #     flags="SA",
        #     remote_ip=remote_ip, remote_port=remote_port,
        #     seq=s2, ack=s1+1)
        self.assert_packet_sent_to_access(
            l4proto=TCP,
            remote_ip=remote_ip, remote_port=remote_port)

        self.send_from_access_to_sgi(
            l4proto=TCP,
            flags="A",
            remote_ip=remote_ip, remote_port=remote_port,
            seq=s1+1, ack=s2+1)
        self.assert_packet_sent_to_sgi(
            l4proto=TCP,
            remote_ip=remote_ip, remote_port=remote_port)

        return s1, s2

    def verify_app_reporting_http(self):
        s1, s2 = self.tcp_handshake(remote_ip=self.non_app_rule_ip,
                                    remote_port=80)
        http_get = b"GET / HTTP/1.1\r\nHost: example.com/\r\n\r\n"
        up_len = self.send_from_access_to_sgi(
            http_get, flags="P", l4proto=TCP,
            remote_port=80, remote_ip=self.non_app_rule_ip,
            seq=s1+1, ack=s2+1)
        self.assert_packet_sent_to_sgi(
            http_get, l4proto=TCP,
            remote_port=80, remote_ip=self.non_app_rule_ip)

        http_resp = b"HTTP/1.1 200 OK\nContent-Type: text/plain\r\n\r\nfoo"
        down_len = self.send_from_sgi_to_ue(
            http_resp, l4proto=TCP, flags="A",
            remote_port=80, remote_ip=self.non_app_rule_ip,
            seq=s2+1, ack=s1+1+len(http_get))
        self.assert_packet_sent_to_access(
            http_resp, l4proto=TCP,
            remote_port=80, remote_ip=self.non_app_rule_ip)

        self.verify_traffic_reporting(up_len, down_len)

    def verify_app_reporting_tls(self):
        s1, s2 = self.tcp_handshake(
            remote_ip=self.non_app_rule_ip_2, remote_port=443)
        tls = TLS(msg=[
            TLSClientHello(
                version=771,
                gmt_unix_time=2183047903,
                random_bytes=b"\x9f\x98\x0b\x13\x05\xa5O\xd0?\x8d\xc8\xdc)" +
                "\x86\xb0W\xd7\xc1\x7f'\x19sg\x84%\xa8\xc4\xc4",
                ciphers=[
                    49200, 49196, 49192, 49188, 49172, 49162, 159, 107,
                    57, 52393, 52392, 52394, 65413, 196, 136, 129,
                    157, 61, 53, 192, 132, 49199, 49195, 49191,
                    49187, 49171, 49161, 158, 103, 51, 190, 69,
                    156, 60, 47, 186, 65, 49169, 49159, 5,
                    4, 49170, 49160, 22, 10, 255
                ],
                comp=[0],
                ext=[
                    TLS_Ext_ServerName(servernames=[
                        ServerName(nametype=0, servername=b'example.com')
                    ]),
                    TLS_Ext_SupportedPointFormat(ecpl=[0]),
                    TLS_Ext_SupportedGroups(groups=[29, 23, 24]),
                    TLS_Ext_SignatureAlgorithms(sig_algs=[
                        1537, 1539, 61423, 1281, 1283, 1025, 1027, 61166,
                        60909, 769, 771, 513, 515
                    ]),
                    TLS_Ext_ALPN(protocols=[
                        ProtocolName(protocol=b'h2'),
                        ProtocolName(protocol=b'http/1.1')
                    ])
                ])
        ], version=769)

        up_len = self.send_from_access_to_sgi(
            tls, flags="P", l4proto=TCP,
            remote_port=443, remote_ip=self.non_app_rule_ip_2,
            seq=s1+1, ack=s2+1)
        self.assert_packet_sent_to_sgi(
            l4proto=TCP, remote_port=443, remote_ip=self.non_app_rule_ip_2)

        down_len = self.send_from_sgi_to_ue(
            l4proto=TCP, flags="A",
            remote_port=443, remote_ip=self.non_app_rule_ip_2,
            seq=s2+1, ack=s1+1+len(tls))
        self.assert_packet_sent_to_access(
            l4proto=TCP,
            remote_port=443, remote_ip=self.non_app_rule_ip_2)

        self.verify_traffic_reporting(up_len, down_len)

    def verify_app_reporting_ip(self, ip):
        # Access -> SGi (IP rule)
        up_len = self.send_from_access_to_sgi(b"42", remote_ip=ip)
        self.assert_packet_sent_to_sgi(b"42", remote_ip=ip)

        # SGi -> Access (IP rule)
        down_len = self.send_from_sgi_to_ue(b"4242", remote_ip=ip)
        self.assert_packet_sent_to_access(b"4242", remote_ip=ip)

        # the following packets aren't counted
        self.send_from_access_to_sgi(
            b"1234567", remote_ip=self.non_app_rule_ip_3)
        self.assert_packet_sent_to_sgi(
            b"1234567", remote_ip=self.non_app_rule_ip_3)
        self.send_from_sgi_to_ue(
            b"foobarbaz", remote_ip=self.non_app_rule_ip_3)
        self.assert_packet_sent_to_access(
            b"foobarbaz", remote_ip=self.non_app_rule_ip_3)

        self.verify_traffic_reporting(up_len, down_len)

    def verify_app_reporting(self):
        self.verify_app_reporting_http()
        self.verify_app_reporting_tls()
        self.verify_app_reporting_ip(ip=self.app_rule_ip)
        self.verify_app_reporting_ip(ip=self.extra_sdf_ip)


class TestPGWBase(PFCPHelper):
    """UPF GTP-U test"""

    CLIENT_TEID = 1
    UNODE_TEID = 1127415596

    @classmethod
    def setUpClass(cls):
        super(TestPGWBase, cls).setUpClass()
        try:
            cls.create_pg_interfaces(range(3))
            cls.interfaces = list(cls.pg_interfaces)

            cls.setup_tables()
            # separate assignments are easier to understand for some
            # tools like elpy than this:
            # cls.if_cp, cls.if_grx, cls.if_sgi = cls.interfaces
            cls.if_cp = cls.interfaces[0]
            cls.if_grx = cls.interfaces[1]
            cls.if_sgi = cls.interfaces[2]
            cls.teid = 1127415596
            for n, iface in enumerate(cls.interfaces):
                iface.admin_up()
                cls.config_interface(cls, iface, n)
            for cmd in cls.upf_setup_cmds():
                cls.vapi.cli(cmd)
        except Exception:
            super(TestPGWBase, cls).tearDownClass()
            raise

    @classmethod
    def upf_setup_cmds(cls):
        return cls.pgw_setup_cmds()

    def reverse_far(self):
        return IE_CreateFAR(IE_list=[
            IE_ApplyAction(FORW=1),
            IE_FAR_Id(id=2),
            IE_ForwardingParameters(IE_list=[
                IE_DestinationInterface(interface="Access"),
                IE_NetworkInstance(instance="epc"),
                self.outer_header_creation(),
            ])
        ])

    def forward_pdr(self):
        return IE_CreatePDR(IE_list=[
            IE_FAR_Id(id=1),
            self.ie_outer_header_removal(),
            IE_PDI(IE_list=[
                IE_NetworkInstance(instance="epc"),
                IE_SDF_Filter(
                    FD=1,
                    flow_description="permit out ip from " +
                    "any to assigned"),
                IE_SourceInterface(interface="Access"),
                self.ie_ue_ip_address(),
                self.ie_fteid(),
            ]),
            IE_PDR_Id(id=1),
            IE_Precedence(precedence=200),
            IE_URR_Id(id=1),
        ])

    def send_from_grx(self, pkt):
        to_send = Ether(src=self.if_grx.remote_mac,
                        dst=self.if_grx.local_mac) / \
                        self.IP(src=self.grx_remote_ip,
                                dst=self.grx_local_ip) / \
                        UDP(sport=2152, dport=2152) / \
                        pkt
        self.if_grx.add_stream(to_send)
        self.pg_enable_capture(self.pg_interfaces)
        self.pg_start()

    def verify_gtp_echo(self, to_send, expected_seq=42):
        self.send_from_grx(to_send)
        pkt = self.if_grx.get_capture(1)[0]
        self.assertTrue(GTP_U_Header in pkt)
        # Expected response:
        # GTP_U_Header(version=1, PT=1, reserved=0, E=0, S=1,
        #              PN=0, gtp_type=2, length=6, teid=0, seq=42,
        #              npdu=0, next_ex=0) /
        # GTPEchoResponse(IE_list=[IE_Recovery(ietype=14, restart_counter=0)])
        hdr = pkt[GTP_U_Header]
        # GTPv1
        self.assertEqual(hdr.version, 1)
        # TS 29.281 5.1: PT == 1 means GTP (not GTP')
        self.assertTrue(hdr.PT)
        self.assertEqual(hdr.length, 6)
        self.assertFalse(hdr.E)
        # sequence number is present
        self.assertTrue(hdr.S)
        # PN must be set to 0 in the echo response
        self.assertFalse(hdr.PN)
        self.assertEqual(hdr.gtp_type, 2) # echo_response
        # TS 29.281 5.1: for Echo Request/Response, 0 is always used
        self.assertEqual(hdr.teid, 0)
        # seq matches the value from request
        self.assertEqual(hdr.seq, expected_seq)
        self.assertEqual(hdr.next_ex, 0) # "No more extension headers"
        self.assertTrue(IE_Recovery in hdr)
        self.assertEqual(hdr[IE_Recovery].restart_counter, 0)

    def verify_gtp_no_echo(self, to_send):
        self.send_from_grx(to_send)
        self.if_grx.assert_nothing_captured()

    def assert_packet_sent_to_ue(self, payload=None, l4proto=UDP,
                                  ue_port=12345, remote_port=23456,
                                  remote_ip=None, expected_seq=42):
        pkt = self.if_grx.get_capture(1)[0]
        self.assertTrue(GTP_U_Header in pkt)
        self.assertEqual(pkt[self.IP].src, self.grx_local_ip)
        self.assertEqual(pkt[self.IP].dst, self.grx_remote_ip)
        hdr = pkt[GTP_U_Header]
        self.assertEqual(hdr.teid, self.CLIENT_TEID)
        self.assertEqual(hdr.version, 1)
        self.assertTrue(hdr.PT)
        self.assertFalse(hdr.E)
        self.assertFalse(hdr.PN)
        self.assertEqual(hdr.length, self.expected_length)
        self.assertEqual(hdr.gtp_type, 0xff) # GTP-encapsulated Plane Data Unit
        self.assertFalse(hdr.S) # no Sequence number is expected fot GPDU
        innerPacket = hdr.payload
        self.assertEqual(innerPacket[self.IP].src, remote_ip)
        self.assertEqual(innerPacket[self.IP].dst, self.ue_ip)
        self.assertEqual(innerPacket[l4proto].sport, remote_port)
        self.assertEqual(innerPacket[l4proto].dport, ue_port)
        if payload is not None:
            self.assertEqual(innerPacket[Raw].load, payload)

    def assert_error_indication_sent_from_upf(self):
        pkt = self.if_grx.get_capture(1)[0]
        self.logger.info(pkt.summary())
        self.logger.info(pkt.show(dump = True))
        self.assertTrue(GTP_U_Header in pkt)
        self.assertEqual(pkt[self.IP].src, self.grx_local_ip)
        self.assertEqual(pkt[self.IP].dst, self.grx_remote_ip)
        hdr = pkt[GTP_U_Header]
        self.assertEqual(hdr.version, 1)
        self.assertTrue(hdr.PT)
        self.assertTrue(hdr.E)
        self.assertFalse(hdr.PN)
        self.assertEqual(hdr.gtp_type, 0x1A) # GTP-U Error Indication
        self.assertTrue(hdr.S)
        extHeader = pkt[GTP_UDPPort_ExtensionHeader]
        self.assertEqual(extHeader.length, 1)
        self.assertEqual(extHeader.udp_port, 2152)
        self.assertEqual(extHeader.next_ex, 0)
        ##TODO: For some reasons, if ExtensionHeader is present in a pkt, scapy can't parse IE_List after extension header
        ##TODO: while wireshark parses it's perfectly. Check if we can somehow convert Raw into IE_List
        #errInd = pkt[GTPErrorIndication]
        #ies = errInd.IE_list
        #self.logger.info(ies)
        #iesToCheck = 3 #IE_TEIDI, IE_Recovery, IE_GSNAddress
        #for ie in ies:
        #    if (ie.ietype == 14): #recovery
        #        iesToCheck -= 1
        #        self.assertEqual(ie.restart_counter, 0)
        #    if (ie.ietype == 16): #TEID I
        #        iesToCheck -= 1
        #        self.assertEqual(ie.TEIDI, teid)
        #    if (ie.ietype == 133): #IE_GSNAddress
        #        iesToCheck -= 1
        #        if self.expected_length == 32: #ipv4
        #            self.assertEqual(ie.length, 4)
        #            self.assertEqual(ie.ipv4_address, self.grx_remote_ip)
        #        else:
        #            self.assertEqual(ie.length, 16)
        #            self.assertEqual(ie.ipv6_address, self.grx_remote_ip)
        #self.assertEqual(iesToCheck, 0) # We met all IEs

    def verify_gtp_forwarding(self):
        payload = self.build_from_ue_to_sgi(b"42", remote_ip = self.remote_ip)
        # UE -> remote_ip
        self.send_from_grx(GTP_U_Header(seq=42, teid=self.teid) / payload)
        self.assert_packet_sent_to_sgi(b"42", remote_ip = self.remote_ip)
        # remote_ip -> UE
        self.send_from_sgi_to_ue(b"4242", remote_ip = self.remote_ip)
        self.assert_packet_sent_to_ue(b"4242", remote_ip = self.remote_ip)
        # UE-> remote_ip, but no such TEID exists on UPF
        self.send_from_grx(GTP_U_Header(seq=42, teid=(424242)) / payload)
        self.assert_error_indication_sent_from_upf()

    def verify_gtp_error_indication(self):
        self.send_from_grx(
            GTP_U_Header(seq=43, teid=self.teid) / GTPErrorIndication() /
            IE_TEIDI(TEIDI=self.teid) /
            IE_Recovery(restart_counter=0) /
            self.gtp_ie_gsn_address())
        sr = self.if_cp.get_capture(1)[0][PFCPSessionReportRequest]
        self.assertIn(IE_ErrorIndicationReport, sr)
        error_ind_report = sr[IE_ErrorIndicationReport]
        self.assertIn(IE_FTEID, error_ind_report)
        self.verify_ie_fteid(error_ind_report[IE_FTEID])

    def test_gtp_error_indication(self):
        try:
            self.associate()
            self.heartbeat()
            self.establish_session()
            self.verify_gtp_error_indication()
            self.delete_session()
        finally:
            self.vapi.cli("show error")

    def test_gtp_echo(self):
        try:
            self.associate()
            self.heartbeat()
            self.establish_session()
            self.verify_gtp_echo(GTP_U_Header(seq=42) / GTPEchoRequest())
            # no sequence number => drop
            self.verify_gtp_no_echo(GTP_U_Header(S=0) / GTPEchoRequest())
            self.verify_gtp_echo(GTP_U_Header(seq=42) / GTPEchoRequest())
            self.verify_gtp_echo(
                GTP_U_Header(seq=42, teid=self.UNODE_TEID) / GTPEchoRequest())
            # Verify stripping extensions (these extensions are
            # used here just b/c they're available in Scapy GTP module)
            self.verify_gtp_echo(
                GTP_U_Header(seq=42, PN=1, npdu=123) /
                GTPEchoRequest() /
                GTP_UDPPort_ExtensionHeader(udp_port=1234) /
                GTP_PDCP_PDU_ExtensionHeader(pdcp_pdu=123))
            # Verify stripping IE (the IE is not relevant here)
            self.verify_gtp_echo(GTP_U_Header(seq=42) / GTPEchoRequest() /
                                 IE_SelectionMode())
            self.verify_gtp_forwarding()
            self.delete_session()
        finally:
            self.vapi.cli("show error")


class TestUPFIPv4(IPv4Mixin, TestTDFBase, framework.VppTestCase):
    """IPv4 UPF Test"""


class TestUPFIPv6(IPv6Mixin, TestTDFBase, framework.VppTestCase):
    """IPv6 UPF Test"""


class TestPGWIPv4(IPv4Mixin, TestPGWBase, framework.VppTestCase):
    """IPv4 PGW Test"""

    @property
    def ue_ip(self):
        return self.UE_IP

class TestPGWIPv4(IPv4Mixin, TestPGWBase, framework.VppTestCase):
    """IPv4 PGW Test"""
    UE_IP = "198.20.0.2"
    expected_length = 32

    @property
    def ue_ip(self):
        return self.UE_IP

    @property
    def remote_ip(self):
        return APP_RULE_IP_V4

class TestPGWIPv6(IPv6Mixin, TestPGWBase, framework.VppTestCase):
    """IPv6 PGW Test"""
    UE_IP = "2001:db8:13::2"
    expected_length = 52

    @property
    def ue_ip(self):
        return self.UE_IP

    @property
    def remote_ip(self):
        return APP_RULE_IP_V6


class TestPGWFTUPIP4(IPv4Mixin, TestPGWBase, framework.VppTestCase):
    """IPv4 PGW Test"""
    UE_IP = "198.20.0.2"
    expected_length = 32

    @property
    def ue_ip(self):
        return self.UE_IP

    @property
    def remote_ip(self):
        return APP_RULE_IP_V4

    def establish_session(self):
        self.cur_seid = seid()
        resp = self.chat(PFCPSessionEstablishmentRequest(IE_list=[
            IE_CreateFAR(IE_list=[
                IE_ApplyAction(FORW=1),
                IE_FAR_Id(id=1),
                IE_ForwardingParameters(IE_list=[
                    IE_DestinationInterface(interface="SGi-LAN/N6-LAN"),
                    IE_NetworkInstance(instance="sgi")
                ])
            ]),
            self.reverse_far(),
            # FIXME: this is not handled properly :(
            IE_CreateFAR(IE_list=[
                IE_ApplyAction(FORW=1),
                IE_FAR_Id(id=3),
                IE_ForwardingParameters(IE_list=[
                    IE_DestinationInterface(interface="SGi-LAN/N6-LAN"),
                    IE_NetworkInstance(instance="sgi"),
                    self.ie_redirect_information(),
                ])
            ]),
            IE_CreateFAR(IE_list=[
                IE_ApplyAction(DROP=1),
                IE_FAR_Id(id=4),
            ]),
            self.forward_pdr(pdr_id=1),
            self.forward_pdr(pdr_id=2),
            IE_CreatePDR(IE_list=[
                IE_FAR_Id(id=2),
                IE_PDI(IE_list=[
                    IE_NetworkInstance(instance="sgi"),
                    IE_SDF_Filter(
                        FD=1,
                        flow_description="permit out ip from any to assigned"),
                    IE_SourceInterface(interface="SGi-LAN/N6-LAN"),
                    self.ie_ue_ip_address(SD=1)
                ]),
                IE_PDR_Id(id=2),
                IE_Precedence(precedence=200),
            ])
        ] + self.extra_pdrs() + [
            self.ie_fseid(),
            IE_NodeId(id_type=2, id="ergw")
        ]), PFCPSessionEstablishmentResponse, seid=self.cur_seid)
        self.assertEqual(CauseValues[resp[IE_Cause].cause], "Request accepted")
        self.verify_ie_fseid(resp[IE_FSEID])
        self.assertEqual(resp[IE_FSEID].seid, self.cur_seid)
        if IE_CreatedPDR in resp:
            self.teid = resp[IE_CreatedPDR][IE_FTEID].TEID
            self.logger.info(self.teid)


    def forward_pdr(self, pdr_id=1):
        chid = pdr_id + 200
        return IE_CreatePDR(IE_list=[
            IE_FAR_Id(id=pdr_id),
            self.ie_outer_header_removal(),
            IE_PDI(IE_list=[
                IE_NetworkInstance(instance="epc"),
                IE_SDF_Filter(
                    FD=1,
                    flow_description="permit out ip from " +
                    "any to assigned"),
                IE_SourceInterface(interface="Access"),
                self.ie_ue_ip_address(),
                self.ie_fteid_ch(chid),
            ]),
            IE_PDR_Id(id=pdr_id),
            IE_Precedence(precedence=200),
            IE_URR_Id(id=pdr_id),
        ])

class TestPGWFTUPIP6(IPv6Mixin, TestPGWBase, framework.VppTestCase):
    """IPv6 PGW Test"""
    UE_IP = "2001:db8:13::2"
    expected_length = 52

    @property
    def ue_ip(self):
        return self.UE_IP

    @property
    def remote_ip(self):
        return APP_RULE_IP_V6

    def forward_pdr(self, pdr_id=1):
        chid = pdr_id + 200
        return IE_CreatePDR(IE_list=[
            IE_FAR_Id(id=pdr_id),
            self.ie_outer_header_removal(),
            IE_PDI(IE_list=[
                IE_NetworkInstance(instance="epc"),
                IE_SDF_Filter(
                    FD=1,
                    flow_description="permit out ip from " +
                    "any to assigned"),
                IE_SourceInterface(interface="Access"),
                self.ie_ue_ip_address(),
                self.ie_fteid_ch(chid),
            ]),
            IE_PDR_Id(id=pdr_id),
            IE_Precedence(precedence=200),
            IE_URR_Id(id=pdr_id),
        ])

    def establish_session(self):
        self.cur_seid = seid()
        resp = self.chat(PFCPSessionEstablishmentRequest(IE_list=[
            IE_CreateFAR(IE_list=[
                IE_ApplyAction(FORW=1),
                IE_FAR_Id(id=1),
                IE_ForwardingParameters(IE_list=[
                    IE_DestinationInterface(interface="SGi-LAN/N6-LAN"),
                    IE_NetworkInstance(instance="sgi")
                ])
            ]),
            self.reverse_far(),
            # FIXME: this is not handled properly :(
            IE_CreateFAR(IE_list=[
                IE_ApplyAction(FORW=1),
                IE_FAR_Id(id=3),
                IE_ForwardingParameters(IE_list=[
                    IE_DestinationInterface(interface="SGi-LAN/N6-LAN"),
                    IE_NetworkInstance(instance="sgi"),
                    self.ie_redirect_information(),
                ])
            ]),
            IE_CreateFAR(IE_list=[
                IE_ApplyAction(DROP=1),
                IE_FAR_Id(id=4),
            ]),
            self.forward_pdr(pdr_id=1),
            self.forward_pdr(pdr_id=2),
            IE_CreatePDR(IE_list=[
                IE_FAR_Id(id=2),
                IE_PDI(IE_list=[
                    IE_NetworkInstance(instance="sgi"),
                    IE_SDF_Filter(
                        FD=1,
                        flow_description="permit out ip from any to assigned"),
                    IE_SourceInterface(interface="SGi-LAN/N6-LAN"),
                    self.ie_ue_ip_address(SD=1)
                ]),
                IE_PDR_Id(id=2),
                IE_Precedence(precedence=200),
            ])
        ] + self.extra_pdrs() + [
            self.ie_fseid(),
            IE_NodeId(id_type=2, id="ergw")
        ]), PFCPSessionEstablishmentResponse, seid=self.cur_seid)
        self.assertEqual(CauseValues[resp[IE_Cause].cause], "Request accepted")
        self.verify_ie_fseid(resp[IE_FSEID])
        self.assertEqual(resp[IE_FSEID].seid, self.cur_seid)
        if IE_CreatedPDR in resp:
            self.teid = resp[IE_CreatedPDR][IE_FTEID].TEID
            self.logger.info(self.teid)

class TestUPFUnit(framework.VppTestCase):
    """ UPF Unit Test Cases """

    def test_unit(self):
        r = self.vapi.cli_return_response("test upf debug")
        self.assertEqual(r.retval, 0)

class TestPFCPReencode(framework.VppTestCase):
    """PFCP Reencode Test"""

    def setUp(self):
        self.have_diffs = False

    def tearDown(self):
        self.assertFalse(self.have_diffs, "should not have diffs")

    def verify_text(self, name, text):
        ws_root = os.getenv("WS_ROOT")
        text_filename = os.path.join(ws_root, "src/plugins/upf/test", name + ".txt")
        out_filename = text_filename + ".out"
        if os.path.exists(text_filename):
            with open(text_filename) as f:
                orig_text = f.read()
            if orig_text == text:
                if os.path.exists(out_filename):
                    os.remove(out_filename)
                return
            print("DIFF in %s, please review %s" % (text_filename, out_filename))
        else:
            print("File not found: %s, please review and rename %s" %
                  (text_filename, out_filename))
        self.have_diffs = True
        with open(out_filename, "w") as f_out:
            f_out.write(text)

    def verify_reencode(self, name, data):
        data = bytes(data)
        r = self.vapi.upf_pfcp_reencode(len(data), data)
        self.assertEqual(len(r.packet), r.packet_len)
        hexdump(data)
        PFCP(data).show()
        print("----")
        hexdump(r.packet)
        print("----")
        PFCP(r.packet).show()
        # first reencoding rearranges the IEs, second reencoding
        # should keep them intact
        r1 = self.vapi.upf_pfcp_reencode(len(r.packet), r.packet)
        self.assertEqual(len(r1.packet), r1.packet_len)
        hexdump(r1.packet)
        print("----")
        PFCP(r1.packet).show()
        self.assertEqual(r1.packet, r.packet)
        r2 = self.vapi.upf_pfcp_format(len(data), data)
        self.verify_text(name, r2.text.decode("utf-8"))

    def test_reencode(self):
        ts = int((datetime(2020, 3, 4) - datetime(1900, 1, 1)).total_seconds())
        req = PFCP(version=1, seq=1, S=0) / \
            PFCPAssociationSetupRequest(IE_list=[
                IE_RecoveryTimeStamp(timestamp=ts),
                IE_NodeId(id_type="FQDN", id="ergw")
            ])
        self.verify_reencode("association_setup_request", req)

        req = PFCP(version=1, seq=6, seid=0xffde72125aeb00a3) / \
            PFCPSessionModificationRequest(IE_list=[
                IE_QueryURR(IE_list=[IE_URR_Id(id=1)])
            ])
        self.verify_reencode("session_modification_request", req)

        req = PFCP(version=1, seq=6, seid=0xffde72125aeb00a3) / \
            PFCPSessionModificationResponse(IE_list=[
                IE_Cause(cause=1),
                IE_UsageReport_SMR(IE_list=[
                    IE_URR_Id(id=1),
                    IE_UR_SEQN(number=0),
                    IE_UsageReportTrigger(IMMER=1),
                    IE_StartTime(timestamp=3782115066),
                    IE_EndTime(timestamp=3782115066),
                    IE_VolumeMeasurement(DLVOL=1, ULVOL=1, TOVOL=1),
                    IE_DurationMeasurement(),
                    IE_EnterpriseSpecific(ietype=32771, enterprise_id=18681, data=b'\xe1n~\xfa\x05Vl\x00'),
                    IE_EnterpriseSpecific(ietype=32772, enterprise_id=18681, data=b'\xe1n~\xfa\x02\x7f\x08\x00'),
                    IE_EnterpriseSpecific(ietype=32773, enterprise_id=18681, data=b'\xe1n~\xfa\x02\x7f\x08\x00')
                ])
            ])
        self.verify_reencode("session_modification_response", req)

        req = PFCP(seq=49, seid=1152331208797536256) / \
            PFCPSessionReportRequest(IE_list=[
                IE_ReportType(USAR=1),
                IE_UsageReport_SRR(IE_list=[
                    IE_URR_Id(id=3),
                    IE_UR_SEQN(number=24),
                    IE_UsageReportTrigger(PERIO=1),
                    IE_StartTime(timestamp=3786688391),
                    IE_EndTime(timestamp=3786688401),
                    IE_VolumeMeasurement(
                        DLVOL=1, ULVOL=1, TOVOL=1, total=0, uplink=0, downlink=0),
                    IE_DurationMeasurement(duration=10),
                    IE_EnterpriseSpecific(
                        ietype=32771,
                        enterprise_id=18681,
                        data=b'\xe1\xb4G\x917\xcb\xd8\x00'),
                    IE_EnterpriseSpecific(
                        ietype=32772,
                        enterprise_id=18681,
                        data=b'\xe1\xb4G\x877\xcb\xd8\x00'),
                    IE_EnterpriseSpecific(
                        ietype=32773,
                        enterprise_id=18681,
                        data=b'\xe1\xb4G\x917\xcb\xd8\x00')
                ])
            ])
        self.verify_reencode("session_report_request", req)

        req = PFCP(seq=3) / \
            PFCPSessionEstablishmentRequest(IE_list=[
                IE_CreateFAR(IE_list=[
                    IE_ApplyAction(FORW=1),
                    IE_FAR_Id(id=6),
                    IE_ForwardingParameters(IE_list=[
                        IE_DestinationInterface(interface="SGi-LAN/N6-LAN"),
                        IE_NetworkInstance(instance="sgi")
                    ])
                ]),
                IE_CreateFAR(IE_list=[
                    IE_ApplyAction(FORW=1),
                    IE_FAR_Id(id=4),
                    IE_ForwardingParameters(IE_list=[
                        IE_DestinationInterface(interface="SGi-LAN/N6-LAN"),
                        IE_NetworkInstance(instance="sgi"),
                        IE_RedirectInformation(type="URL", address="http://example.com")
                    ])
                ]),
                IE_CreateFAR(IE_list=[
                    IE_ApplyAction(FORW=1),
                    IE_FAR_Id(id=2),
                    IE_ForwardingParameters(IE_list=[
                        IE_DestinationInterface(interface="SGi-LAN/N6-LAN"),
                        IE_NetworkInstance(instance="sgi")
                    ])
                ]),
                IE_CreateFAR(IE_list=[
                    IE_ApplyAction(FORW=1),
                    IE_FAR_Id(id=5),
                    IE_ForwardingParameters(IE_list=[
                        IE_DestinationInterface(interface="Access"),
                        IE_NetworkInstance(instance="access")
                    ])
                ]),
                IE_CreateFAR(IE_list=[
                    IE_ApplyAction(FORW=1),
                    IE_FAR_Id(id=3),
                    IE_ForwardingParameters(IE_list=[
                        IE_DestinationInterface(interface="Access"),
                        IE_NetworkInstance(instance="access")
                    ])
                ]),
                IE_CreateFAR(IE_list=[
                    IE_ApplyAction(FORW=1),
                    IE_FAR_Id(id=1),
                    IE_ForwardingParameters(IE_list=[
                        IE_DestinationInterface(interface="Access"),
                        IE_NetworkInstance(instance="access")
                    ])
                ]),
                IE_CreatePDR(IE_list=[
                    IE_FAR_Id(id=6),
                    IE_PDI(IE_list=[
                        IE_ApplicationId(id="TST"),
                        IE_NetworkInstance(instance="access"),
                        IE_SourceInterface(interface="Access"),
                        IE_UE_IP_Address(ipv4='10.192.0.0', V4=1)
                    ]),
                    IE_PDR_Id(id=6),
                    IE_Precedence(precedence=150),
                    IE_URR_Id(id=3)
                ]),
                IE_CreatePDR(IE_list=[
                    IE_FAR_Id(id=4),
                    IE_PDI(IE_list=[
                        IE_NetworkInstance(instance="access"),
                        IE_SDF_Filter(FD=1, flow_description="permit out ip from 198.19.65.4 to assigned"),
                        IE_SourceInterface(interface="Access"),
                        IE_UE_IP_Address(ipv4='10.192.0.0', V4=1)
                    ]),
                    IE_PDR_Id(id=4),
                    IE_Precedence(precedence=100),
                    IE_URR_Id(id=2)
                ]),
                IE_CreatePDR(IE_list=[
                    IE_FAR_Id(id=2),
                    IE_PDI(IE_list=[
                        IE_NetworkInstance(instance="access"),
                        IE_SDF_Filter(FD=1, flow_description="permit out ip from 198.19.65.2 to assigned"),
                        IE_SourceInterface(interface="Access"),
                        IE_UE_IP_Address(ipv4='10.192.0.0', V4=1)
                    ]),
                    IE_PDR_Id(id=2),
                    IE_Precedence(precedence=200),
                    IE_URR_Id(id=1)
                ]),
                IE_CreatePDR(IE_list=[
                    IE_FAR_Id(id=5),
                    IE_PDI(IE_list=[
                        IE_ApplicationId(id="TST"),
                        IE_NetworkInstance(instance="sgi"),
                        IE_SourceInterface(interface="SGi-LAN/N6-LAN"),
                        IE_UE_IP_Address(ipv4='10.192.0.0', SD=1, V4=1)
                    ]),
                    IE_PDR_Id(id=5),
                    IE_Precedence(precedence=150),
                    IE_URR_Id(id=3)
                ]),
                IE_CreatePDR(IE_list=[
                    IE_FAR_Id(id=3),
                    IE_PDI(IE_list=[
                        IE_NetworkInstance(instance="sgi"),
                        IE_SDF_Filter(FD=1, flow_description="permit out ip from 198.19.65.4 to assigned"),
                        IE_SourceInterface(interface="SGi-LAN/N6-LAN"),
                        IE_UE_IP_Address(ipv4='10.192.0.0', SD=1, V4=1)
                    ]),
                    IE_PDR_Id(id=3),
                    IE_Precedence(precedence=100),
                    IE_URR_Id(id=2)
                ]),
                IE_CreatePDR(IE_list=[
                    IE_FAR_Id(id=1),
                    IE_PDI(IE_list=[
                        IE_NetworkInstance(instance="sgi"),
                        IE_SDF_Filter(FD=1, flow_description="permit out ip from 198.19.65.2 to assigned"),
                        IE_SourceInterface(interface="SGi-LAN/N6-LAN"),
                        IE_UE_IP_Address(ipv4='10.192.0.0', SD=1, V4=1)
                    ]),
                    IE_PDR_Id(id=1),
                    IE_Precedence(precedence=200),
                    IE_URR_Id(id=1)
                ]),
                IE_CreateURR(IE_list=[
                    IE_MeasurementMethod(VOLUM=1),
                    IE_ReportingTriggers(),
                    IE_URR_Id(id=2)
                ]),
                IE_CreateURR(IE_list=[
                    IE_MeasurementMethod(VOLUM=1, DURAT=1),
                    IE_ReportingTriggers(),
                    IE_URR_Id(id=3)
                ]),
                IE_CreateURR(IE_list=[
                    IE_MeasurementMethod(VOLUM=1),
                    IE_ReportingTriggers(),
                    IE_URR_Id(id=1)
                ]),
                IE_FSEID(ipv4='172.18.1.1', v4=1, seid=0xffde7211a5ab800a),
                IE_NodeId(id_type="FQDN", id="ergw")
            ])
        self.verify_reencode("session_establishment_request", req)

        report_ies = [
            IE_UsageReport_SDR(IE_list=[
                IE_URR_Id(id=n, extra_data=b''),
                IE_UR_SEQN(number=0),
                IE_UsageReportTrigger(TERMR=1, extra_data=b'\x00'),
                IE_StartTime(timestamp=3824381293),
                IE_EndTime(timestamp=3824381293),
                IE_VolumeMeasurement(DLVOL=1, ULVOL=1, TOVOL=1, total=0, uplink=0, downlink=0,
                                     extra_data=b'\x00'*24),
                IE_DurationMeasurement(duration=0),
                IE_EnterpriseSpecific(enterprise_id=18681, data=b'\xe3\xf3mm\xb0A\xe0\x00'),
                IE_EnterpriseSpecific(enterprise_id=18681, data=b'\xe3\xf3mm\xae\xee\xf4\x00'),
                IE_EnterpriseSpecific(enterprise_id=18681, data=b'\xe3\xf3mm\xae\xee\xf4\x00')
            ]) for n in range(1, 21)
        ]
        req = PFCP(version=1, seq=3, seid=5577006791947779410) / \
            PFCPSessionDeletionResponse(IE_list=[ IE_Cause(cause=1) ] + report_ies)
        self.verify_reencode("session_deletion_response", req)

# TODO: send session report response
# TODO: check for heartbeat requests from UPF
# TODO: check redirects (perhaps IPv4 type redirect) -- currently broken
# TODO: upstream the scapy changes
