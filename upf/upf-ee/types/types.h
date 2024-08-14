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
    bool packetFilterUsage;
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
    cvector(DomainInformation) domainInfoList;

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
    FlowInformation flowInfo;
    VolumeMeasurement volumeMeasurement;
    ThroughputMeasurement throughputMeasurement;
    ApplicationRelatedInformation applicationRelatedInformation;
    ThroughputStatisticsMeasurement throughputStatisticsMeasurement;
} UserDataUsageMeasurements;

typedef struct {
    EventType type;
    char* ueIpv4Addr;
    char* ueIpv6Prefix;
    char* ueMacAddr;
    char* dnn;
    Snssai snssai;
    char* gpsi;
    char* supi;
    time_t timeStamp;
    time_t startTime;
    //qosMonitoringMeasurement
    //tscMngtInfo
    cvector(UserDataUsageMeasurements) userDataUsageMeasurements;
}NotificationItem;

typedef struct
{
    uint64_t seid;
    ip46_address_t src_ip;
    ip46_address_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  proto;
    uint32_t src_pkts;
    uint64_t src_bytes;
    uint32_t dst_pkts;
    uint64_t dst_bytes;

} usage_report_per_flow_t;



static inline UpfEventTrigger getUpfEventTrigger(const char *s);
static inline PartitioningCriteria getPartitioningCriteria(const char *s);
static inline NotificationFlag getNotificationFlag(const char *s);
static inline BufferedNotificationsAction getBufferedNotificationsAction(const char *s);
static inline SubscriptionAction getSubscriptionAction(const char *s);
static inline EventType getEventType(const char *s);
static inline MeasurementType getMeasurementType(const char *s);
static inline FlowDirection getFlowDirection(const char *s);
static inline GranularityOfMeasurement getGranularityOfMeasurement(const char *s);
static inline ReportingUrgency getReportingUrgency(const char *s);
static inline UeIpAddressVersion getUeIpAddressVersion(const char *s);
static inline DnProtocol getDnProtocol(const char *s);
static inline const char* getUpfEventTriggerString(UpfEventTrigger trigger);
static inline const char* getPartitioningCriteriaString(PartitioningCriteria criteria);
static inline const char* getNotificationFlagString(NotificationFlag flag);
static inline const char* getBufferedNotificationsActionString(BufferedNotificationsAction action);
static inline const char* getSubscriptionActionString(SubscriptionAction action);
static inline const char* getEventTypeString(EventType type);
static inline const char* getMeasurementTypeString(MeasurementType type);
static inline const char* getFlowDirectionString(FlowDirection direction);
static inline const char* getGranularityOfMeasurementString(GranularityOfMeasurement granularity);
static inline const char* getReportingUrgencyString(ReportingUrgency urgency);
static inline const char* getDnProtocolString(DnProtocol protocol);

static inline UeIpAddressVersion getUeIpAddressVersion(const char *s) {
  if (strcmp(s, "V4") == 0) return V4;
  if (strcmp(s, "V6") == 0) return V6;
  if (strcmp(s, "V6Prefix") == 0) return V6Prefix;
  return V4;
}

static inline UpfEventTrigger getUpfEventTrigger(const char *s) {
  if (strcmp(s, "ONE_TIME") == 0) return ONE_TIME;
  if (strcmp(s, "PERIODIC") == 0) return PERIODIC;
  return ONE_TIME;
}

static inline PartitioningCriteria getPartitioningCriteria(const char *s) {
  if (strcmp(s, "TAC") == 0) return TAC;
  if (strcmp(s, "SUBPLMN") == 0) return SUBPLMN;
  if (strcmp(s, "GEOAREA") == 0) return GEOAREA;
  if (strcmp(s, "SNSSAI") == 0) return SNSSAI;
  if (strcmp(s, "DNN") == 0) return DNN;
  return TAC;
}

static inline NotificationFlag getNotificationFlag(const char *s) {
  if (strcmp(s, "ACTIVATE") == 0) return ACTIVATE;
  if (strcmp(s, "DEACTIVATE") == 0) return DEACTIVATE;
  if (strcmp(s, "RETRIEVAL") == 0) return RETRIEVAL;
  return ACTIVATE;
}

static inline BufferedNotificationsAction getBufferedNotificationsAction(const char *s) {
  if (strcmp(s, "SEND_ALL") == 0) return SEND_ALL;
  if (strcmp(s, "DISCARD_ALL") == 0) return DISCARD_ALL;
  if (strcmp(s, "DROP_OLD") == 0) return DROP_OLD;
  return SEND_ALL;
}

static inline SubscriptionAction getSubscriptionAction(const char *s) {
  if (strcmp(s, "CLOSE") == 0) return CLOSE;
  if (strcmp(s, "CONTINUE_WITH_MUTING") == 0) return CONTINUE_WITH_MUTING;
  if (strcmp(s, "CONTINUE_WITHOUT_MUTING") == 0) return CONTINUE_WITHOUT_MUTING;
  return CLOSE;
}

static inline EventType getEventType(const char *s) {
  if (strcmp(s, "QOS_MONITORING") == 0) return QOS_MONITORING;
  if (strcmp(s, "USER_DATA_USAGE_MEASURES") == 0) return USER_DATA_USAGE_MEASURES;
  if (strcmp(s, "USER_DATA_USAGE_TRENDS") == 0) return USER_DATA_USAGE_TRENDS;
  if (strcmp(s, "TSC_MNGT_INFO") == 0) return TSC_MNGT_INFO;
  return QOS_MONITORING;
}

static inline MeasurementType getMeasurementType(const char *s) {
  if (strcmp(s, "VOLUME_MEASUREMENT") == 0) return VOLUME_MEASUREMENT;
  if (strcmp(s, "THROUGHPUT_MEASUREMENT") == 0) return THROUGHPUT_MEASUREMENT;
  if (strcmp(s, "APPLICATION_RELATED_INFO") == 0) return APPLICATION_RELATED_INFO;
  return VOLUME_MEASUREMENT;
}

static inline FlowDirection getFlowDirection(const char *s) {
  if (strcmp(s, "DOWNLINK") == 0) return DOWNLINK;
  if (strcmp(s, "UPLINK") == 0) return UPLINK;
  if (strcmp(s, "BIDIRECTIONAL") == 0) return BIDIRECTIONAL;
  if (strcmp(s, "UNSPECIFIED") == 0) return UNSPECIFIED;
  return UNSPECIFIED;
}

static inline GranularityOfMeasurement getGranularityOfMeasurement(const char *s) {
  if (strcmp(s, "PER_APPLICATION") == 0) return PER_APPLICATION;
  if (strcmp(s, "PER_SESSION") == 0) return PER_SESSION;
  if (strcmp(s, "PER_FLOW") == 0) return PER_FLOW;
  return PER_APPLICATION;
}

static inline ReportingUrgency getReportingUrgency(const char *s) {
  if (strcmp(s, "DELAY_TOLERANT") == 0) return DELAY_TOLERANT;
  if (strcmp(s, "NON_DELAY_TOLERANT") == 0) return NON_DELAY_TOLERANT;
  return DELAY_TOLERANT;
}

static inline const char* getUpfEventTriggerString(UpfEventTrigger trigger) {
  switch (trigger) {
    case ONE_TIME: return "ONE_TIME";
    case PERIODIC: return "PERIODIC";
    default: return "ONE_TIME";
  }
}

static inline const char* getPartitioningCriteriaString(PartitioningCriteria criteria) {
  switch (criteria) {
    case TAC: return "TAC";
    case SUBPLMN: return "SUBPLMN";
    case GEOAREA: return "GEOAREA";
    case SNSSAI: return "SNSSAI";
    case DNN: return "DNN";
    default: return "TAC";
  }
}

static inline const char* getNotificationFlagString(NotificationFlag flag) {
  switch (flag) {
    case ACTIVATE: return "ACTIVATE";
    case DEACTIVATE: return "DEACTIVATE";
    case RETRIEVAL: return "RETRIEVAL";
    default: return "ACTIVATE";
  }
}

static inline const char* getBufferedNotificationsActionString(BufferedNotificationsAction action) {
  switch (action) {
    case SEND_ALL: return "SEND_ALL";
    case DISCARD_ALL: return "DISCARD_ALL";
    case DROP_OLD: return "DROP_OLD";
    default: return "SEND_ALL";
  }
}

static inline const char* getSubscriptionActionString(SubscriptionAction action) {
  switch (action) {
    case CLOSE: return "CLOSE";
    case CONTINUE_WITH_MUTING: return "CONTINUE_WITH_MUTING";
    case CONTINUE_WITHOUT_MUTING: return "CONTINUE_WITHOUT_MUTING";
    default: return "CLOSE";
  }
}

static inline const char* getEventTypeString(EventType type) {
  switch (type) {
    case QOS_MONITORING: return "QOS_MONITORING";
    case USER_DATA_USAGE_MEASURES: return "USER_DATA_USAGE_MEASURES";
    case USER_DATA_USAGE_TRENDS: return "USER_DATA_USAGE_TRENDS";
    case TSC_MNGT_INFO: return "TSC_MNGT_INFO";
    default: return "QOS_MONITORING";
  }
}

static inline const char* getMeasurementTypeString(MeasurementType type) {
  switch (type) {
    case VOLUME_MEASUREMENT: return "VOLUME_MEASUREMENT";
    case THROUGHPUT_MEASUREMENT: return "THROUGHPUT_MEASUREMENT";
    case APPLICATION_RELATED_INFO: return "APPLICATION_RELATED_INFO";
    default: return "VOLUME_MEASUREMENT";
  }
}

static inline const char* getFlowDirectionString(FlowDirection direction) {
  switch (direction) {
    case DOWNLINK: return "DOWNLINK";
    case UPLINK: return "UPLINK";
    case BIDIRECTIONAL: return "BIDIRECTIONAL";
    case UNSPECIFIED: return "UNSPECIFIED";
    default: return "UNSPECIFIED";
  }
}

static inline const char* getGranularityOfMeasurementString(GranularityOfMeasurement granularity) {
  switch (granularity) {
    case PER_APPLICATION: return "PER_APPLICATION";
    case PER_SESSION: return "PER_SESSION";
    case PER_FLOW: return "PER_FLOW";
    default: return "PER_APPLICATION";
  }
}

static inline const char* getReportingUrgencyString(ReportingUrgency urgency) {
  switch (urgency) {
    case DELAY_TOLERANT: return "DELAY_TOLERANT";
    case NON_DELAY_TOLERANT: return "NON_DELAY_TOLERANT";
    default: return "DELAY_TOLERANT";
  }
}


static inline DnProtocol getDnProtocol(const char *s) {
  if (strcmp(s, "DNS_QNAME") == 0) return DNS_QNAME;
  if (strcmp(s, "TLS_SNI") == 0) return TLS_SNI;
  if (strcmp(s, "TLS_SAN") == 0) return TLS_SAN;
  if (strcmp(s, "TLS_SCN") == 0) return TLS_SCN;
  return DNS_QNAME; // Default value if no match is found
}

static inline const char* getDnProtocolString(DnProtocol protocol) {
  switch (protocol) {
      case DNS_QNAME:
          return "DNS_QNAME";
      case TLS_SNI:
          return "TLS_SNI";
      case TLS_SAN:
          return "TLS_SAN";
      case TLS_SCN:
          return "TLS_SCN";
      default:
          return "DNS_QNAME"; // Default string if no match is found
  }
}

#endif
