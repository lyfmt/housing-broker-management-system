/*
 * storage.c — 逐记录读写链表数据到文件
 * v4 开始使用稳定的逐字段小端序格式，避免结构体整体写入带来的填充差异
 */
#include "storage.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STORAGE_MAGIC "RMS2"
#define STORAGE_VERSION 4

typedef struct {
    int id;
    char name[MAX_STR];
    char phone[20];
    char password[32];
} LegacyAgent;

typedef struct {
    int id;
    int houseId;
    int tenantId;
    int agentId;
    char contractDate[11];
    char startDate[11];
    char endDate[11];
    double monthlyRent;
    int status;
} LegacyRentalV2;

/* ---------- 辅助宏：写/读固定大小数据块 ---------- */
#define WRITE_FIELD(fp, ptr, sz) \
    do { if (fwrite((ptr), (sz), 1, (fp)) != 1) return 0; } while (0)
#define READ_FIELD(fp, ptr, sz) \
    do { if (fread((ptr), (sz), 1, (fp)) != 1) return 0; } while (0)

static void sanitize_string_field(char *field, size_t size) {
    if (!field || size == 0) return;
    field[size - 1] = '\0';
}

static void sanitize_category_list(CategoryList *list) {
    int i;
    if (!list) return;
    if (list->count < 0) list->count = 0;
    if (list->count > MAX_CATEGORY_ITEMS) list->count = MAX_CATEGORY_ITEMS;
    for (i = 0; i < list->count; ++i) {
        sanitize_string_field(list->items[i], sizeof(list->items[i]));
    }
}

static void sanitize_agent(Agent *agent) {
    if (!agent) return;
    sanitize_string_field(agent->name, sizeof(agent->name));
    sanitize_string_field(agent->phone, sizeof(agent->phone));
    sanitize_string_field(agent->idCard, sizeof(agent->idCard));
    sanitize_string_field(agent->password, sizeof(agent->password));
}

static void sanitize_tenant(Tenant *tenant) {
    if (!tenant) return;
    sanitize_string_field(tenant->name, sizeof(tenant->name));
    sanitize_string_field(tenant->phone, sizeof(tenant->phone));
    sanitize_string_field(tenant->idCard, sizeof(tenant->idCard));
    sanitize_string_field(tenant->password, sizeof(tenant->password));
}

static void sanitize_house(House *house) {
    if (!house) return;
    sanitize_string_field(house->city, sizeof(house->city));
    sanitize_string_field(house->region, sizeof(house->region));
    sanitize_string_field(house->community, sizeof(house->community));
    sanitize_string_field(house->address, sizeof(house->address));
    sanitize_string_field(house->building, sizeof(house->building));
    sanitize_string_field(house->unitNo, sizeof(house->unitNo));
    sanitize_string_field(house->floorNote, sizeof(house->floorNote));
    sanitize_string_field(house->orientation, sizeof(house->orientation));
    sanitize_string_field(house->houseType, sizeof(house->houseType));
    sanitize_string_field(house->decoration, sizeof(house->decoration));
    sanitize_string_field(house->ownerName, sizeof(house->ownerName));
    sanitize_string_field(house->ownerPhone, sizeof(house->ownerPhone));
    sanitize_string_field(house->rejectReason, sizeof(house->rejectReason));
}

static void sanitize_viewing(Viewing *viewing) {
    if (!viewing) return;
    sanitize_string_field(viewing->datetime, sizeof(viewing->datetime));
    sanitize_string_field(viewing->tenantFeedback, sizeof(viewing->tenantFeedback));
    sanitize_string_field(viewing->agentFeedback, sizeof(viewing->agentFeedback));
}

static void sanitize_rental(Rental *rental) {
    if (!rental) return;
    sanitize_string_field(rental->contractDate, sizeof(rental->contractDate));
    sanitize_string_field(rental->startDate, sizeof(rental->startDate));
    sanitize_string_field(rental->endDate, sizeof(rental->endDate));
    if (rental->signStatus < RENTAL_SIGN_PENDING || rental->signStatus > RENTAL_SIGN_CANCELLED) {
        rental->signStatus = RENTAL_SIGN_CONFIRMED;
    }
}

static void sanitize_database(Database *db) {
    AgentNode *agentCur;
    TenantNode *tenantCur;
    HouseNode *houseCur;
    ViewingNode *viewingCur;
    RentalNode *rentalCur;

    if (!db) return;

    sanitize_string_field(db->adminPassword, sizeof(db->adminPassword));
    sanitize_category_list(&db->regions);
    sanitize_category_list(&db->floorNotes);
    sanitize_category_list(&db->orientations);
    sanitize_category_list(&db->houseTypes);
    sanitize_category_list(&db->decorations);

    for (agentCur = db->agents; agentCur; agentCur = agentCur->next) sanitize_agent(&agentCur->data);
    for (tenantCur = db->tenants; tenantCur; tenantCur = tenantCur->next) sanitize_tenant(&tenantCur->data);
    for (houseCur = db->houses; houseCur; houseCur = houseCur->next) sanitize_house(&houseCur->data);
    for (viewingCur = db->viewings; viewingCur; viewingCur = viewingCur->next) sanitize_viewing(&viewingCur->data);
    for (rentalCur = db->rentals; rentalCur; rentalCur = rentalCur->next) sanitize_rental(&rentalCur->data);
}

static int write_u32_le(FILE *fp, uint32_t value) {
    unsigned char bytes[4];
    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
    bytes[2] = (unsigned char)((value >> 16) & 0xFFu);
    bytes[3] = (unsigned char)((value >> 24) & 0xFFu);
    WRITE_FIELD(fp, bytes, sizeof(bytes));
    return 1;
}

static int read_u32_le(FILE *fp, uint32_t *value) {
    unsigned char bytes[4];
    if (!value) return 0;
    READ_FIELD(fp, bytes, sizeof(bytes));
    *value = (uint32_t)bytes[0]
           | ((uint32_t)bytes[1] << 8)
           | ((uint32_t)bytes[2] << 16)
           | ((uint32_t)bytes[3] << 24);
    return 1;
}

static int write_i32_le(FILE *fp, int value) {
    return write_u32_le(fp, (uint32_t)(int32_t)value);
}

static int read_i32_le(FILE *fp, int *value) {
    uint32_t raw;
    if (!value) return 0;
    if (!read_u32_le(fp, &raw)) return 0;
    *value = (int)(int32_t)raw;
    return 1;
}

static int read_i32_native(FILE *fp, int *value) {
    if (!value) return 0;
    READ_FIELD(fp, value, sizeof(*value));
    return 1;
}

static int write_u64_le(FILE *fp, uint64_t value) {
    unsigned char bytes[8];
    int i;
    for (i = 0; i < 8; ++i) {
        bytes[i] = (unsigned char)((value >> (i * 8)) & 0xFFu);
    }
    WRITE_FIELD(fp, bytes, sizeof(bytes));
    return 1;
}

static int read_u64_le(FILE *fp, uint64_t *value) {
    unsigned char bytes[8];
    int i;
    if (!value) return 0;
    READ_FIELD(fp, bytes, sizeof(bytes));
    *value = 0;
    for (i = 0; i < 8; ++i) {
        *value |= ((uint64_t)bytes[i]) << (i * 8);
    }
    return 1;
}

static int write_double_le(FILE *fp, double value) {
    uint64_t bits = 0;
    if (sizeof(double) != sizeof(bits)) return 0;
    memcpy(&bits, &value, sizeof(bits));
    return write_u64_le(fp, bits);
}

static int read_double_le(FILE *fp, double *value) {
    uint64_t bits = 0;
    if (!value || sizeof(double) != sizeof(bits)) return 0;
    if (!read_u64_le(fp, &bits)) return 0;
    memcpy(value, &bits, sizeof(bits));
    return 1;
}

static int read_double_native(FILE *fp, double *value) {
    if (!value) return 0;
    READ_FIELD(fp, value, sizeof(*value));
    return 1;
}

static int write_fixed_text(FILE *fp, const char *text, size_t size) {
    WRITE_FIELD(fp, text, size);
    return 1;
}

static int read_fixed_text(FILE *fp, char *text, size_t size) {
    READ_FIELD(fp, text, size);
    sanitize_string_field(text, size);
    return 1;
}

static int decode_i32_le_bytes(const unsigned char bytes[4], int *value) {
    uint32_t raw;
    if (!value) return 0;
    raw = (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16)
        | ((uint32_t)bytes[3] << 24);
    *value = (int)(int32_t)raw;
    return 1;
}

static int write_header(FILE *fp) {
    WRITE_FIELD(fp, STORAGE_MAGIC, 4);
    return write_i32_le(fp, STORAGE_VERSION);
}

static int read_header(FILE *fp, int *version) {
    char magic[4];
    unsigned char versionBytes[4];
    int nativeVersion = 0;
    int littleVersion = 0;
    if (fread(magic, sizeof(magic), 1, fp) != 1) return 0;
    if (memcmp(magic, STORAGE_MAGIC, 4) != 0) return 0;
    if (fread(versionBytes, sizeof(versionBytes), 1, fp) != 1) return 0;
    if (sizeof(nativeVersion) == sizeof(versionBytes)) {
        memcpy(&nativeVersion, versionBytes, sizeof(versionBytes));
    }
    if (!decode_i32_le_bytes(versionBytes, &littleVersion)) return 0;
    if (nativeVersion == 2 || nativeVersion == 3) {
        *version = nativeVersion;
        return 1;
    }
    if (littleVersion == STORAGE_VERSION) {
        *version = littleVersion;
        return 1;
    }
    if (nativeVersion == STORAGE_VERSION) {
        *version = nativeVersion;
        return 1;
    }
    return 0;
}

static int write_category_list_record(FILE *fp, const CategoryList *list) {
    int i;
    int count = 0;
    if (list) count = list->count;
    if (count < 0) count = 0;
    if (count > MAX_CATEGORY_ITEMS) count = MAX_CATEGORY_ITEMS;
    if (!write_i32_le(fp, count)) return 0;
    for (i = 0; i < count; ++i) {
        if (!write_fixed_text(fp, list->items[i], sizeof(list->items[i]))) return 0;
    }
    return 1;
}

static int read_category_list_record(FILE *fp, CategoryList *list) {
    int count, i;
    char discard[MAX_STR];
    if (!list) return 0;
    memset(list, 0, sizeof(*list));
    if (!read_i32_le(fp, &count) || count < 0) return 0;
    for (i = 0; i < count; ++i) {
        char *slot = (i < MAX_CATEGORY_ITEMS) ? list->items[i] : discard;
        if (!read_fixed_text(fp, slot, MAX_STR)) return 0;
    }
    list->count = (count > MAX_CATEGORY_ITEMS) ? MAX_CATEGORY_ITEMS : count;
    sanitize_category_list(list);
    return 1;
}

static int read_legacy_category_list(FILE *fp, CategoryList *list) {
    if (!list) return 0;
    READ_FIELD(fp, list, sizeof(*list));
    sanitize_category_list(list);
    return 1;
}

static int write_agent_record(FILE *fp, const Agent *agent) {
    if (!agent) return 0;
    if (!write_i32_le(fp, agent->id)) return 0;
    if (!write_fixed_text(fp, agent->name, sizeof(agent->name))) return 0;
    if (!write_fixed_text(fp, agent->phone, sizeof(agent->phone))) return 0;
    if (!write_fixed_text(fp, agent->idCard, sizeof(agent->idCard))) return 0;
    if (!write_fixed_text(fp, agent->password, sizeof(agent->password))) return 0;
    return 1;
}

static int read_agent_record(FILE *fp, Agent *agent) {
    if (!agent) return 0;
    if (!read_i32_le(fp, &agent->id)) return 0;
    if (!read_fixed_text(fp, agent->name, sizeof(agent->name))) return 0;
    if (!read_fixed_text(fp, agent->phone, sizeof(agent->phone))) return 0;
    if (!read_fixed_text(fp, agent->idCard, sizeof(agent->idCard))) return 0;
    if (!read_fixed_text(fp, agent->password, sizeof(agent->password))) return 0;
    return 1;
}

static int read_agent_record_native(FILE *fp, Agent *agent) {
    if (!agent) return 0;
    READ_FIELD(fp, &agent->id, sizeof(agent->id));
    if (!read_fixed_text(fp, agent->name, sizeof(agent->name))) return 0;
    if (!read_fixed_text(fp, agent->phone, sizeof(agent->phone))) return 0;
    if (!read_fixed_text(fp, agent->idCard, sizeof(agent->idCard))) return 0;
    if (!read_fixed_text(fp, agent->password, sizeof(agent->password))) return 0;
    return 1;
}

static int write_tenant_record(FILE *fp, const Tenant *tenant) {
    if (!tenant) return 0;
    if (!write_i32_le(fp, tenant->id)) return 0;
    if (!write_fixed_text(fp, tenant->name, sizeof(tenant->name))) return 0;
    if (!write_fixed_text(fp, tenant->phone, sizeof(tenant->phone))) return 0;
    if (!write_fixed_text(fp, tenant->idCard, sizeof(tenant->idCard))) return 0;
    if (!write_fixed_text(fp, tenant->password, sizeof(tenant->password))) return 0;
    return 1;
}

static int read_tenant_record(FILE *fp, Tenant *tenant) {
    if (!tenant) return 0;
    if (!read_i32_le(fp, &tenant->id)) return 0;
    if (!read_fixed_text(fp, tenant->name, sizeof(tenant->name))) return 0;
    if (!read_fixed_text(fp, tenant->phone, sizeof(tenant->phone))) return 0;
    if (!read_fixed_text(fp, tenant->idCard, sizeof(tenant->idCard))) return 0;
    if (!read_fixed_text(fp, tenant->password, sizeof(tenant->password))) return 0;
    return 1;
}

static int write_house_record(FILE *fp, const House *house) {
    if (!house) return 0;
    if (!write_i32_le(fp, house->id)) return 0;
    if (!write_fixed_text(fp, house->city, sizeof(house->city))) return 0;
    if (!write_fixed_text(fp, house->region, sizeof(house->region))) return 0;
    if (!write_fixed_text(fp, house->community, sizeof(house->community))) return 0;
    if (!write_fixed_text(fp, house->address, sizeof(house->address))) return 0;
    if (!write_fixed_text(fp, house->building, sizeof(house->building))) return 0;
    if (!write_i32_le(fp, house->floor)) return 0;
    if (!write_fixed_text(fp, house->unitNo, sizeof(house->unitNo))) return 0;
    if (!write_fixed_text(fp, house->floorNote, sizeof(house->floorNote))) return 0;
    if (!write_fixed_text(fp, house->orientation, sizeof(house->orientation))) return 0;
    if (!write_fixed_text(fp, house->houseType, sizeof(house->houseType))) return 0;
    if (!write_double_le(fp, house->area)) return 0;
    if (!write_fixed_text(fp, house->decoration, sizeof(house->decoration))) return 0;
    if (!write_double_le(fp, house->price)) return 0;
    if (!write_fixed_text(fp, house->ownerName, sizeof(house->ownerName))) return 0;
    if (!write_fixed_text(fp, house->ownerPhone, sizeof(house->ownerPhone))) return 0;
    if (!write_i32_le(fp, house->createdByAgentId)) return 0;
    if (!write_i32_le(fp, house->status)) return 0;
    if (!write_fixed_text(fp, house->rejectReason, sizeof(house->rejectReason))) return 0;
    return 1;
}

static int read_house_record(FILE *fp, House *house) {
    if (!house) return 0;
    if (!read_i32_le(fp, &house->id)) return 0;
    if (!read_fixed_text(fp, house->city, sizeof(house->city))) return 0;
    if (!read_fixed_text(fp, house->region, sizeof(house->region))) return 0;
    if (!read_fixed_text(fp, house->community, sizeof(house->community))) return 0;
    if (!read_fixed_text(fp, house->address, sizeof(house->address))) return 0;
    if (!read_fixed_text(fp, house->building, sizeof(house->building))) return 0;
    if (!read_i32_le(fp, &house->floor)) return 0;
    if (!read_fixed_text(fp, house->unitNo, sizeof(house->unitNo))) return 0;
    if (!read_fixed_text(fp, house->floorNote, sizeof(house->floorNote))) return 0;
    if (!read_fixed_text(fp, house->orientation, sizeof(house->orientation))) return 0;
    if (!read_fixed_text(fp, house->houseType, sizeof(house->houseType))) return 0;
    if (!read_double_le(fp, &house->area)) return 0;
    if (!read_fixed_text(fp, house->decoration, sizeof(house->decoration))) return 0;
    if (!read_double_le(fp, &house->price)) return 0;
    if (!read_fixed_text(fp, house->ownerName, sizeof(house->ownerName))) return 0;
    if (!read_fixed_text(fp, house->ownerPhone, sizeof(house->ownerPhone))) return 0;
    if (!read_i32_le(fp, &house->createdByAgentId)) return 0;
    if (!read_i32_le(fp, &house->status)) return 0;
    if (!read_fixed_text(fp, house->rejectReason, sizeof(house->rejectReason))) return 0;
    return 1;
}

static int write_viewing_record(FILE *fp, const Viewing *viewing) {
    if (!viewing) return 0;
    if (!write_i32_le(fp, viewing->id)) return 0;
    if (!write_fixed_text(fp, viewing->datetime, sizeof(viewing->datetime))) return 0;
    if (!write_i32_le(fp, viewing->houseId)) return 0;
    if (!write_i32_le(fp, viewing->tenantId)) return 0;
    if (!write_i32_le(fp, viewing->agentId)) return 0;
    if (!write_i32_le(fp, viewing->durationMinutes)) return 0;
    if (!write_i32_le(fp, viewing->status)) return 0;
    if (!write_fixed_text(fp, viewing->tenantFeedback, sizeof(viewing->tenantFeedback))) return 0;
    if (!write_fixed_text(fp, viewing->agentFeedback, sizeof(viewing->agentFeedback))) return 0;
    return 1;
}

static int read_viewing_record(FILE *fp, Viewing *viewing) {
    if (!viewing) return 0;
    if (!read_i32_le(fp, &viewing->id)) return 0;
    if (!read_fixed_text(fp, viewing->datetime, sizeof(viewing->datetime))) return 0;
    if (!read_i32_le(fp, &viewing->houseId)) return 0;
    if (!read_i32_le(fp, &viewing->tenantId)) return 0;
    if (!read_i32_le(fp, &viewing->agentId)) return 0;
    if (!read_i32_le(fp, &viewing->durationMinutes)) return 0;
    if (!read_i32_le(fp, &viewing->status)) return 0;
    if (!read_fixed_text(fp, viewing->tenantFeedback, sizeof(viewing->tenantFeedback))) return 0;
    if (!read_fixed_text(fp, viewing->agentFeedback, sizeof(viewing->agentFeedback))) return 0;
    return 1;
}

static int write_rental_record(FILE *fp, const Rental *rental) {
    if (!rental) return 0;
    if (!write_i32_le(fp, rental->id)) return 0;
    if (!write_i32_le(fp, rental->houseId)) return 0;
    if (!write_i32_le(fp, rental->tenantId)) return 0;
    if (!write_i32_le(fp, rental->agentId)) return 0;
    if (!write_fixed_text(fp, rental->contractDate, sizeof(rental->contractDate))) return 0;
    if (!write_fixed_text(fp, rental->startDate, sizeof(rental->startDate))) return 0;
    if (!write_fixed_text(fp, rental->endDate, sizeof(rental->endDate))) return 0;
    if (!write_double_le(fp, rental->monthlyRent)) return 0;
    if (!write_i32_le(fp, rental->status)) return 0;
    if (!write_i32_le(fp, rental->signStatus)) return 0;
    return 1;
}

static int read_rental_record(FILE *fp, Rental *rental) {
    if (!rental) return 0;
    if (!read_i32_le(fp, &rental->id)) return 0;
    if (!read_i32_le(fp, &rental->houseId)) return 0;
    if (!read_i32_le(fp, &rental->tenantId)) return 0;
    if (!read_i32_le(fp, &rental->agentId)) return 0;
    if (!read_fixed_text(fp, rental->contractDate, sizeof(rental->contractDate))) return 0;
    if (!read_fixed_text(fp, rental->startDate, sizeof(rental->startDate))) return 0;
    if (!read_fixed_text(fp, rental->endDate, sizeof(rental->endDate))) return 0;
    if (!read_double_le(fp, &rental->monthlyRent)) return 0;
    if (!read_i32_le(fp, &rental->status)) return 0;
    if (!read_i32_le(fp, &rental->signStatus)) return 0;
    return 1;
}

static int read_rental_record_native(FILE *fp, Rental *rental) {
    if (!rental) return 0;
    READ_FIELD(fp, &rental->id, sizeof(rental->id));
    READ_FIELD(fp, &rental->houseId, sizeof(rental->houseId));
    READ_FIELD(fp, &rental->tenantId, sizeof(rental->tenantId));
    READ_FIELD(fp, &rental->agentId, sizeof(rental->agentId));
    if (!read_fixed_text(fp, rental->contractDate, sizeof(rental->contractDate))) return 0;
    if (!read_fixed_text(fp, rental->startDate, sizeof(rental->startDate))) return 0;
    if (!read_fixed_text(fp, rental->endDate, sizeof(rental->endDate))) return 0;
    if (!read_double_native(fp, &rental->monthlyRent)) return 0;
    READ_FIELD(fp, &rental->status, sizeof(rental->status));
    READ_FIELD(fp, &rental->signStatus, sizeof(rental->signStatus));
    return 1;
}

/* =================================================================
 *  保存
 * ================================================================= */
int storage_save(const char *filename, const Database *db) {
    FILE *fp = fopen(filename, "wb");
    if (!fp || !db) return 0;

    if (!write_header(fp)) {
        fclose(fp);
        return 0;
    }

    if (!write_fixed_text(fp, db->adminPassword, sizeof(db->adminPassword)) ||
        !write_category_list_record(fp, &db->regions) ||
        !write_category_list_record(fp, &db->floorNotes) ||
        !write_category_list_record(fp, &db->orientations) ||
        !write_category_list_record(fp, &db->houseTypes) ||
        !write_category_list_record(fp, &db->decorations)) {
        fclose(fp);
        return 0;
    }

    {
        AgentNode *cur;
        if (!write_i32_le(fp, db->agentCount)) { fclose(fp); return 0; }
        for (cur = db->agents; cur; cur = cur->next) {
            if (!write_agent_record(fp, &cur->data)) {
                fclose(fp);
                return 0;
            }
        }
    }

    {
        TenantNode *cur;
        if (!write_i32_le(fp, db->tenantCount)) { fclose(fp); return 0; }
        for (cur = db->tenants; cur; cur = cur->next) {
            if (!write_tenant_record(fp, &cur->data)) {
                fclose(fp);
                return 0;
            }
        }
    }

    {
        HouseNode *cur;
        if (!write_i32_le(fp, db->houseCount)) { fclose(fp); return 0; }
        for (cur = db->houses; cur; cur = cur->next) {
            if (!write_house_record(fp, &cur->data)) {
                fclose(fp);
                return 0;
            }
        }
    }

    {
        ViewingNode *cur;
        if (!write_i32_le(fp, db->viewingCount)) { fclose(fp); return 0; }
        for (cur = db->viewings; cur; cur = cur->next) {
            if (!write_viewing_record(fp, &cur->data)) {
                fclose(fp);
                return 0;
            }
        }
    }

    {
        RentalNode *cur;
        if (!write_i32_le(fp, db->rentalCount)) { fclose(fp); return 0; }
        for (cur = db->rentals; cur; cur = cur->next) {
            if (!write_rental_record(fp, &cur->data)) {
                fclose(fp);
                return 0;
            }
        }
    }

    fclose(fp);
    return 1;
}

/* =================================================================
 *  加载
 * ================================================================= */

static void free_agent_list(AgentNode **head) {
    AgentNode *cur = *head;
    while (cur) {
        AgentNode *nx = cur->next;
        free(cur);
        cur = nx;
    }
    *head = NULL;
}

static void free_tenant_list(TenantNode **head) {
    TenantNode *cur = *head;
    while (cur) {
        TenantNode *nx = cur->next;
        free(cur);
        cur = nx;
    }
    *head = NULL;
}

static void free_house_list(HouseNode **head) {
    HouseNode *cur = *head;
    while (cur) {
        HouseNode *nx = cur->next;
        free(cur);
        cur = nx;
    }
    *head = NULL;
}

static void free_viewing_list(ViewingNode **head) {
    ViewingNode *cur = *head;
    while (cur) {
        ViewingNode *nx = cur->next;
        free(cur);
        cur = nx;
    }
    *head = NULL;
}

static void free_rental_list(RentalNode **head) {
    RentalNode *cur = *head;
    while (cur) {
        RentalNode *nx = cur->next;
        free(cur);
        cur = nx;
    }
    *head = NULL;
}

static void free_loaded_lists(Database *db) {
    if (!db) return;
    free_agent_list(&db->agents);
    free_tenant_list(&db->tenants);
    free_house_list(&db->houses);
    free_viewing_list(&db->viewings);
    free_rental_list(&db->rentals);
}

static int load_agent_list_v4(FILE *fp, AgentNode **head, int *count) {
    int i, cnt;
    AgentNode *tail = NULL;
    *head = NULL;
    *count = 0;
    if (!read_i32_le(fp, &cnt) || cnt < 0) return 0;
    for (i = 0; i < cnt; ++i) {
        AgentNode *n = (AgentNode *)malloc(sizeof(AgentNode));
        if (!n) {
            free_agent_list(head);
            return 0;
        }
        if (!read_agent_record(fp, &n->data)) {
            free(n);
            free_agent_list(head);
            return 0;
        }
        sanitize_agent(&n->data);
        n->next = NULL;
        if (!*head) *head = tail = n;
        else {
            tail->next = n;
            tail = n;
        }
    }
    *count = cnt;
    return 1;
}

static int load_agent_list_v3(FILE *fp, AgentNode **head, int *count) {
    int i, cnt;
    AgentNode *tail = NULL;
    *head = NULL;
    *count = 0;
    if (!read_i32_native(fp, &cnt) || cnt < 0) return 0;
    for (i = 0; i < cnt; ++i) {
        AgentNode *n = (AgentNode *)malloc(sizeof(AgentNode));
        if (!n) {
            free_agent_list(head);
            return 0;
        }
        if (!read_agent_record_native(fp, &n->data)) {
            free(n);
            free_agent_list(head);
            return 0;
        }
        sanitize_agent(&n->data);
        n->next = NULL;
        if (!*head) *head = tail = n;
        else {
            tail->next = n;
            tail = n;
        }
    }
    *count = cnt;
    return 1;
}

static int load_legacy_agent_list(FILE *fp, AgentNode **head, int *count) {
    int i, cnt;
    AgentNode *tail = NULL;
    *head = NULL;
    *count = 0;
    if (!read_i32_native(fp, &cnt) || cnt < 0) return 0;
    for (i = 0; i < cnt; ++i) {
        LegacyAgent legacy;
        AgentNode *n = (AgentNode *)malloc(sizeof(AgentNode));
        if (!n) {
            free_agent_list(head);
            return 0;
        }
        if (fread(&legacy, sizeof(LegacyAgent), 1, fp) != 1) {
            free(n);
            free_agent_list(head);
            return 0;
        }
        memset(&n->data, 0, sizeof(n->data));
        n->data.id = legacy.id;
        memcpy(n->data.name, legacy.name, sizeof(legacy.name));
        memcpy(n->data.phone, legacy.phone, sizeof(legacy.phone));
        memcpy(n->data.password, legacy.password, sizeof(legacy.password));
        sanitize_agent(&n->data);
        n->next = NULL;
        if (!*head) *head = tail = n;
        else {
            tail->next = n;
            tail = n;
        }
    }
    *count = cnt;
    return 1;
}

static int load_tenant_list_v3(FILE *fp, TenantNode **head, int *count) {
    int i, cnt;
    TenantNode *tail = NULL;
    *head = NULL;
    *count = 0;
    if (!read_i32_native(fp, &cnt) || cnt < 0) return 0;
    for (i = 0; i < cnt; ++i) {
        TenantNode *n = (TenantNode *)malloc(sizeof(TenantNode));
        if (!n) {
            free_tenant_list(head);
            return 0;
        }
        if (fread(&n->data, sizeof(Tenant), 1, fp) != 1) {
            free(n);
            free_tenant_list(head);
            return 0;
        }
        sanitize_tenant(&n->data);
        n->next = NULL;
        if (!*head) *head = tail = n;
        else {
            tail->next = n;
            tail = n;
        }
    }
    *count = cnt;
    return 1;
}

static int load_tenant_list_v4(FILE *fp, TenantNode **head, int *count) {
    int i, cnt;
    TenantNode *tail = NULL;
    *head = NULL;
    *count = 0;
    if (!read_i32_le(fp, &cnt) || cnt < 0) return 0;
    for (i = 0; i < cnt; ++i) {
        TenantNode *n = (TenantNode *)malloc(sizeof(TenantNode));
        if (!n) {
            free_tenant_list(head);
            return 0;
        }
        if (!read_tenant_record(fp, &n->data)) {
            free(n);
            free_tenant_list(head);
            return 0;
        }
        sanitize_tenant(&n->data);
        n->next = NULL;
        if (!*head) *head = tail = n;
        else {
            tail->next = n;
            tail = n;
        }
    }
    *count = cnt;
    return 1;
}

static int load_house_list_v3(FILE *fp, HouseNode **head, int *count) {
    int i, cnt;
    HouseNode *tail = NULL;
    *head = NULL;
    *count = 0;
    if (!read_i32_native(fp, &cnt) || cnt < 0) return 0;
    for (i = 0; i < cnt; ++i) {
        HouseNode *n = (HouseNode *)malloc(sizeof(HouseNode));
        if (!n) {
            free_house_list(head);
            return 0;
        }
        if (fread(&n->data, sizeof(House), 1, fp) != 1) {
            free(n);
            free_house_list(head);
            return 0;
        }
        sanitize_house(&n->data);
        n->next = NULL;
        if (!*head) *head = tail = n;
        else {
            tail->next = n;
            tail = n;
        }
    }
    *count = cnt;
    return 1;
}

static int load_house_list_v4(FILE *fp, HouseNode **head, int *count) {
    int i, cnt;
    HouseNode *tail = NULL;
    *head = NULL;
    *count = 0;
    if (!read_i32_le(fp, &cnt) || cnt < 0) return 0;
    for (i = 0; i < cnt; ++i) {
        HouseNode *n = (HouseNode *)malloc(sizeof(HouseNode));
        if (!n) {
            free_house_list(head);
            return 0;
        }
        if (!read_house_record(fp, &n->data)) {
            free(n);
            free_house_list(head);
            return 0;
        }
        sanitize_house(&n->data);
        n->next = NULL;
        if (!*head) *head = tail = n;
        else {
            tail->next = n;
            tail = n;
        }
    }
    *count = cnt;
    return 1;
}

static int load_viewing_list_v3(FILE *fp, ViewingNode **head, int *count) {
    int i, cnt;
    ViewingNode *tail = NULL;
    *head = NULL;
    *count = 0;
    if (!read_i32_native(fp, &cnt) || cnt < 0) return 0;
    for (i = 0; i < cnt; ++i) {
        ViewingNode *n = (ViewingNode *)malloc(sizeof(ViewingNode));
        if (!n) {
            free_viewing_list(head);
            return 0;
        }
        if (fread(&n->data, sizeof(Viewing), 1, fp) != 1) {
            free(n);
            free_viewing_list(head);
            return 0;
        }
        sanitize_viewing(&n->data);
        n->next = NULL;
        if (!*head) *head = tail = n;
        else {
            tail->next = n;
            tail = n;
        }
    }
    *count = cnt;
    return 1;
}

static int load_viewing_list_v4(FILE *fp, ViewingNode **head, int *count) {
    int i, cnt;
    ViewingNode *tail = NULL;
    *head = NULL;
    *count = 0;
    if (!read_i32_le(fp, &cnt) || cnt < 0) return 0;
    for (i = 0; i < cnt; ++i) {
        ViewingNode *n = (ViewingNode *)malloc(sizeof(ViewingNode));
        if (!n) {
            free_viewing_list(head);
            return 0;
        }
        if (!read_viewing_record(fp, &n->data)) {
            free(n);
            free_viewing_list(head);
            return 0;
        }
        sanitize_viewing(&n->data);
        n->next = NULL;
        if (!*head) *head = tail = n;
        else {
            tail->next = n;
            tail = n;
        }
    }
    *count = cnt;
    return 1;
}

static int load_rental_list_v4(FILE *fp, RentalNode **head, int *count) {
    int i, cnt;
    RentalNode *tail = NULL;
    *head = NULL;
    *count = 0;
    if (!read_i32_le(fp, &cnt) || cnt < 0) return 0;
    for (i = 0; i < cnt; ++i) {
        RentalNode *n = (RentalNode *)malloc(sizeof(RentalNode));
        if (!n) {
            free_rental_list(head);
            return 0;
        }
        if (!read_rental_record(fp, &n->data)) {
            free(n);
            free_rental_list(head);
            return 0;
        }
        sanitize_rental(&n->data);
        n->next = NULL;
        if (!*head) *head = tail = n;
        else {
            tail->next = n;
            tail = n;
        }
    }
    *count = cnt;
    return 1;
}

static int load_rental_list_v3(FILE *fp, RentalNode **head, int *count) {
    int i, cnt;
    RentalNode *tail = NULL;
    *head = NULL;
    *count = 0;
    if (!read_i32_native(fp, &cnt) || cnt < 0) return 0;
    for (i = 0; i < cnt; ++i) {
        RentalNode *n = (RentalNode *)malloc(sizeof(RentalNode));
        if (!n) {
            free_rental_list(head);
            return 0;
        }
        if (!read_rental_record_native(fp, &n->data)) {
            free(n);
            free_rental_list(head);
            return 0;
        }
        sanitize_rental(&n->data);
        n->next = NULL;
        if (!*head) *head = tail = n;
        else {
            tail->next = n;
            tail = n;
        }
    }
    *count = cnt;
    return 1;
}

static int load_legacy_rental_list_v2(FILE *fp, RentalNode **head, int *count) {
    int i, cnt;
    RentalNode *tail = NULL;
    *head = NULL;
    *count = 0;
    if (!read_i32_native(fp, &cnt) || cnt < 0) return 0;
    for (i = 0; i < cnt; ++i) {
        LegacyRentalV2 legacy;
        RentalNode *n = (RentalNode *)malloc(sizeof(RentalNode));
        if (!n) {
            free_rental_list(head);
            return 0;
        }
        if (fread(&legacy, sizeof(LegacyRentalV2), 1, fp) != 1) {
            free(n);
            free_rental_list(head);
            return 0;
        }
        memset(&n->data, 0, sizeof(n->data));
        n->data.id = legacy.id;
        n->data.houseId = legacy.houseId;
        n->data.tenantId = legacy.tenantId;
        n->data.agentId = legacy.agentId;
        memcpy(n->data.contractDate, legacy.contractDate, sizeof(legacy.contractDate));
        memcpy(n->data.startDate, legacy.startDate, sizeof(legacy.startDate));
        memcpy(n->data.endDate, legacy.endDate, sizeof(legacy.endDate));
        n->data.monthlyRent = legacy.monthlyRent;
        n->data.status = legacy.status;
        n->data.signStatus = RENTAL_SIGN_CONFIRMED;
        sanitize_rental(&n->data);
        n->next = NULL;
        if (!*head) *head = tail = n;
        else {
            tail->next = n;
            tail = n;
        }
    }
    *count = cnt;
    return 1;
}

int storage_load(const char *filename, Database *db) {
    FILE *fp = fopen(filename, "rb");
    Database tmp;
    int version = 0;
    int hasHeader = 0;
    if (!fp || !db) return 0;

    memset(&tmp, 0, sizeof(tmp));

    if (read_header(fp, &version)) {
        if (version != 2 && version != 3 && version != STORAGE_VERSION) {
            fclose(fp);
            return 0;
        }
        hasHeader = 1;
    } else {
        rewind(fp);
    }

    if (!read_fixed_text(fp, tmp.adminPassword, sizeof(tmp.adminPassword))) {
        fclose(fp);
        return 0;
    }

    if (version >= STORAGE_VERSION) {
        if (!read_category_list_record(fp, &tmp.regions) ||
            !read_category_list_record(fp, &tmp.floorNotes) ||
            !read_category_list_record(fp, &tmp.orientations) ||
            !read_category_list_record(fp, &tmp.houseTypes) ||
            !read_category_list_record(fp, &tmp.decorations)) {
            fclose(fp);
            return 0;
        }
    } else {
        if (!read_legacy_category_list(fp, &tmp.regions) ||
            !read_legacy_category_list(fp, &tmp.floorNotes) ||
            !read_legacy_category_list(fp, &tmp.orientations) ||
            !read_legacy_category_list(fp, &tmp.houseTypes) ||
            !read_legacy_category_list(fp, &tmp.decorations)) {
            fclose(fp);
            return 0;
        }
    }

    if (!((version >= STORAGE_VERSION && load_agent_list_v4(fp, &tmp.agents, &tmp.agentCount)) ||
          ((hasHeader && version < STORAGE_VERSION) && load_agent_list_v3(fp, &tmp.agents, &tmp.agentCount)) ||
          (!hasHeader && load_legacy_agent_list(fp, &tmp.agents, &tmp.agentCount))) ||
        !((version >= STORAGE_VERSION && load_tenant_list_v4(fp, &tmp.tenants, &tmp.tenantCount)) ||
          (version < STORAGE_VERSION && load_tenant_list_v3(fp, &tmp.tenants, &tmp.tenantCount))) ||
        !((version >= STORAGE_VERSION && load_house_list_v4(fp, &tmp.houses, &tmp.houseCount)) ||
          (version < STORAGE_VERSION && load_house_list_v3(fp, &tmp.houses, &tmp.houseCount))) ||
        !((version >= STORAGE_VERSION && load_viewing_list_v4(fp, &tmp.viewings, &tmp.viewingCount)) ||
          (version < STORAGE_VERSION && load_viewing_list_v3(fp, &tmp.viewings, &tmp.viewingCount))) ||
        !((version == 2 && load_legacy_rental_list_v2(fp, &tmp.rentals, &tmp.rentalCount)) ||
          (version >= STORAGE_VERSION && load_rental_list_v4(fp, &tmp.rentals, &tmp.rentalCount)) ||
          ((version != 2 && version < STORAGE_VERSION) && load_rental_list_v3(fp, &tmp.rentals, &tmp.rentalCount)))) {
        free_loaded_lists(&tmp);
        fclose(fp);
        return 0;
    }

    fclose(fp);
    sanitize_database(&tmp);

    free_loaded_lists(db);

    memcpy(db->adminPassword, tmp.adminPassword, sizeof(db->adminPassword));
    db->regions = tmp.regions;
    db->floorNotes = tmp.floorNotes;
    db->orientations = tmp.orientations;
    db->houseTypes = tmp.houseTypes;
    db->decorations = tmp.decorations;
    db->agents = tmp.agents;
    db->agentCount = tmp.agentCount;
    db->tenants = tmp.tenants;
    db->tenantCount = tmp.tenantCount;
    db->houses = tmp.houses;
    db->houseCount = tmp.houseCount;
    db->viewings = tmp.viewings;
    db->viewingCount = tmp.viewingCount;
    db->rentals = tmp.rentals;
    db->rentalCount = tmp.rentalCount;

    return 1;
}
