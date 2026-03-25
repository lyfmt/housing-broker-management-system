#ifndef DEMO_DATA_H
#define DEMO_DATA_H

#include <stddef.h>

typedef struct {
    int id;
    const char *name;
    const char *gender;
    const char *phone;
    const char *idCard;
    const char *password;
} DemoAgentEntry;

typedef struct {
    int id;
    const char *name;
    const char *gender;
    const char *phone;
    const char *idCard;
    const char *password;
} DemoTenantEntry;

typedef struct {
    int id;
    const char *city;
    const char *region;
    const char *community;
    const char *address;
    const char *building;
    int floor;
    const char *unitNo;
    const char *floorNote;
    const char *orientation;
    const char *houseType;
    double area;
    const char *decoration;
    double price;
    const char *ownerName;
    const char *ownerPhone;
    int createdByAgentId;
    int status;
} DemoHouseEntry;

typedef struct {
    int id;
    const char *datetime;
    int houseId;
    int tenantId;
    int agentId;
    int durationMinutes;
    int status;
    int contractStatus;
    const char *tenantFeedback;
    const char *agentFeedback;
} DemoViewingEntry;

typedef struct {
    int id;
    int houseId;
    int tenantId;
    int agentId;
    int appointmentId;
    const char *contractDate;
    const char *startDate;
    const char *endDate;
    int leaseTerm;
    double monthlyRent;
    double deposit;
    const char *otherTerms;
    const char *rejectReason;
    int status;
    int signStatus;
} DemoRentalEntry;

const char *const *demo_regions(size_t *count);
const char *const *demo_floor_notes(size_t *count);
const char *const *demo_orientations(size_t *count);
const char *const *demo_house_types(size_t *count);
const char *const *demo_decorations(size_t *count);

const DemoAgentEntry *demo_agent_entries(size_t *count);
const DemoTenantEntry *demo_tenant_entries(size_t *count);
const DemoHouseEntry *demo_house_entries(size_t *count);
const DemoViewingEntry *demo_viewing_entries(size_t *count);
const DemoRentalEntry *demo_rental_entries(size_t *count);

#endif
