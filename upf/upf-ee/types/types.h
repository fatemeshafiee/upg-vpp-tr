//
// Created by Fatemeh Shafiei Ardestani on 2024-07-02.
//

#ifndef REST_API_C_TYPES_H
#define REST_API_C_TYPES_H

#include <stdbool.h>
#include "../lib/cvector.h"
#include "stdio.h"
#include "../lib/stb_ds.h"
#include <time.h>
#include <stdint.h>
#include <pthread.h>

typedef enum {
    ONE_TIME,
    PERIODIC
} UpfEventTrigger;

typedef enum{
    TAC,
    SUBPLMN,
    GEOAREA,
    SNSSAI,
    DNN
}PartitioningCriteria;

typedef enum {
    ACTIVATE,
    DEACTIVATE,
    RETRIEVAL
} NotificationFlag;

typedef enum {
    SEND_ALL,
    DISCARD_ALL,
    DROP_OLD
} BufferedNotificationsAction;

typedef enum {
    CLOSE,
    CONTINUE_WITH_MUTING,
    CONTINUE_WITHOUT_MUTING
} SubscriptionAction;


typedef struct {
    SubscriptionAction Subscriptioninstructions;
    BufferedNotificationsAction BufferedNotificationinstructions;
} MutingExcInstructions;

typedef struct {
    UpfEventTrigger trigger;
    int maxReports;
    int sent_reports;
    char* expiry;
    int repPeriod;
    int sampRatio;
    cvector_vector_type(PartitioningCriteria)partitioningCriteria;
    NotificationFlag notifFlag;
    MutingExcInstructions mutingExcInstructions;
    time_t TimeOfLastReport;
    time_t TimeOfSubscription;

} EventReportingMode;

typedef enum {
    QOS_MONITORING,
    USER_DATA_USAGE_MEASURES,
    USER_DATA_USAGE_TRENDS,
    TSC_MNGT_INFO
} EventType;

typedef enum {
    VOLUME_MEASUREMENT,
    THROUGHPUT_MEASUREMENT,
    APPLICATION_RELATED_INFO
} MeasurementType;

typedef enum {
    DOWNLINK,
    UPLINK,
    BIDIRECTIONAL,
    UNSPECIFIED
} FlowDirection;

typedef enum {
    PER_APPLICATION,
    PER_SESSION,
    PER_FLOW
} GranularityOfMeasurement;

typedef enum {
    DELAY_TOLERANT,
    NON_DELAY_TOLERANT
} ReportingUrgency;

typedef struct {
    const char* destMacAddr;
    const char* ethType;
    const char* fDesc;
    FlowDirection fDir;
    const char* sourceMacAddr;
    cvector_vector_type(char*) vlanTags;
    const char* srcMacAddrEnd;
    const char* destMacAddrEnd;

} EthFlowDescription;

typedef struct {
    const char* flowDescription;
    EthFlowDescription* ethFlowDescription; // Done
    const char* packFiltId;
    bool packetFilterUsage; // should be bool* ? default:false
    const char* tosTrafficClass;
    const char* spi;
    const char* flowLabel;
    FlowDirection fDir;
} FlowInformation;


typedef struct {
    ReportingUrgency reportingUrgency;
    int reportingTimeInfo;
}ReportingSuggestionInformation;

typedef struct {
    EventType type;
    bool immediateFlag;
    cvector_vector_type(MeasurementType) measurementTypes;
    cvector_vector_type(char *) appIds; // Done
    cvector_vector_type(FlowInformation) TrafficFilters; //Done
    GranularityOfMeasurement granularityOfMeasurement;
    ReportingSuggestionInformation reportingSuggestionInfo;
} UpfEvent;

typedef struct {
    unsigned int sst;
    char sd[7];
} Snssai;
typedef enum {
    V4,
    V6,
    V6Prefix

} UeIpAddressVersion;
typedef struct {
    cvector_vector_type(UpfEvent) EventList;
    const char* eventNotifyUri;
    const char*  notifyCorrelationId;
    EventReportingMode eventReportingMode;
    const char* nfId;
    UeIpAddressVersion ueIpAddressVersion;
    const char* ueIpAddress;
    const char* supi;
    const char* gpsi;
    const char* pei;
    bool anyUe;
    const char* dnn;
    Snssai snssai;

} UpfEventSubscription;

typedef struct {
    UpfEventSubscription subscription;
    char * SupportedFeatures;
} SubscriptionRequest;

typedef struct {
    char *ulAverageThroughput;
    char *dlAverageThroughput;
    char *ulPeakThroughput;
    char *dlPeakThroughput;
    char *ulAveragePacketThroughput;
    char *dlAveragePacketThroughput;
    char *ulPeakPacketThroughput;
    char *dlPeakPacketThroughput;
} ThroughputStatisticsMeasurement;

typedef struct {
    char *ulThroughput;
    char *dlThroughput;
    char *ulPacketThroughput;
    char *dlPacketThroughput;
} ThroughputMeasurement;

typedef enum {
    DNS_QNAME,
    TLS_SNI,
    TLS_SAN,
    TLS_SCN
} DnProtocol;

typedef struct {
    char* domainName;
    DnProtocol domainNameProtocol;
}DomainInformation;

typedef struct {
    cvector(char *) urls;
    cvector(DomainInformation *) domainInfoList;

}ApplicationRelatedInformation;

typedef struct {
    char *totalVolume;
    char *ulVolume;
    char *dlVolume;
    uint64_t totalNbOfPackets;
    uint64_t ulNbOfPackets;
    uint64_t dlNbOfPackets;
} VolumeMeasurement;

typedef struct {
    char *appID;
    FlowInformation *flowInfo;
    VolumeMeasurement *volumeMeasurement;
    ThroughputMeasurement *throughputMeasurement;
    ApplicationRelatedInformation *applicationRelatedInformation;
    ThroughputStatisticsMeasurement *throughputStatisticsMeasurement;
} UserDataUsageMeasurements;

typedef struct {
    EventType type;
    const char* ueIpv4Addr;
    const char* ueIpv6Prefix;
    const char* ueMacAddr;
    const char* dnn;
    Snssai snssai;
    const char* gpsi;
    const char* supi;
    time_t timeStamp;
    time_t startTime;
    //qosMonitoringMeasurement
    //tscMngtInfo
    cvector(UserDataUsageMeasurements *) userDataUsageMeasurements;
}NotificationItem;

typedef struct
{
    uint64_t seid;
    const char* src_ip;
    const char* dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  proto;
    uint32_t src_pkts;
    uint64_t src_bytes;
    uint32_t dst_pkts;
    uint64_t dst_bytes;
} usage_report_per_flow_t;

typedef struct {
    u64 seid;
    const char* src_ip;
    const char* dst_ip;
    u16 src_port;
    u16 dst_port;
    u16  proto;

} flow_key;

typedef struct
{
    flow_key * key;
    time_t packet_time;
    u16 packet_length;
    char * highest_layer;
    int ip_flags;
    int tcp_length;
    int tcp_ack;
    int tcp_flags;
    int tcp_window_size;
    int udp_length;
    int ICMP_type;
    bool is_reverse;

} usage_report_per_packet_t;


UpfEventTrigger getUpfEventTrigger(const char *s);
PartitioningCriteria getPartitioningCriteria(const char *s);
NotificationFlag getNotificationFlag(const char *s);
BufferedNotificationsAction getBufferedNotificationsAction(const char *s);
SubscriptionAction getSubscriptionAction(const char *s);
EventType getEventType(const char *s);
MeasurementType getMeasurementType(const char *s);
FlowDirection getFlowDirection(const char *s);
GranularityOfMeasurement getGranularityOfMeasurement(const char *s);
ReportingUrgency getReportingUrgency(const char *s);
UeIpAddressVersion getUeIpAddressVersion(const char *s);
DnProtocol getDnProtocol(const char *s);
const char* getUpfEventTriggerString(UpfEventTrigger trigger);
const char* getPartitioningCriteriaString(PartitioningCriteria criteria);
const char* getNotificationFlagString(NotificationFlag flag);
const char* getBufferedNotificationsActionString(BufferedNotificationsAction action);
const char* getSubscriptionActionString(SubscriptionAction action);
const char* getEventTypeString(EventType type);
const char* getMeasurementTypeString(MeasurementType type);
const char* getFlowDirectionString(FlowDirection direction);
const char* getGranularityOfMeasurementString(GranularityOfMeasurement granularity);
const char* getReportingUrgencyString(ReportingUrgency urgency);
const char* getDnProtocolString(DnProtocol protocol);
#endif
