#ifndef RENTAL_SYSTEM_H
#define RENTAL_SYSTEM_H

#include <time.h>

/* ==================== 常量定义 ==================== */
#define MAX_CATEGORY_ITEMS 100
#define MAX_STR            64
#define MAX_BIG_STR        128
#define DEFAULT_DATA_FILE  "rental_data.dat"
#define DEFAULT_AGENT_PASSWORD  "123456"
#define DEFAULT_TENANT_PASSWORD "123456"
#define MAX_LOGIN_ATTEMPTS 5
#define MIN_PASSWORD_LEN   6

/* ==================== 房源状态 ==================== */
#define HOUSE_VACANT    0   /* 空闲 */
#define HOUSE_RENTED    1   /* 已出租 */
#define HOUSE_PENDING   2   /* 待审核 */
#define HOUSE_OFFLINE   3   /* 已下架 */

/* ==================== 看房状态 ==================== */
#define VIEWING_UNCONFIRMED 0  /* 待确认 */
#define VIEWING_CONFIRMED   1  /* 已确认 */
#define VIEWING_COMPLETED   2  /* 已完成 */
#define VIEWING_CANCELLED   3  /* 已取消 */
#define VIEWING_MISSED      4  /* 未赴约 */

/* ==================== 租约状态 ==================== */
#define RENTAL_ACTIVE   0   /* 有效 */
#define RENTAL_EXPIRED  1   /* 已到期 */
#define RENTAL_EARLY    2   /* 提前退租 */

/* ==================== 签约流程状态 ==================== */
#define RENTAL_SIGN_PENDING    0  /* 待租客确认 */
#define RENTAL_SIGN_CONFIRMED  1  /* 已签订 */
#define RENTAL_SIGN_REJECTED   2  /* 已拒签 */
#define RENTAL_SIGN_CANCELLED  3  /* 已撤销 */

/* ==================== 分类列表 ==================== */
typedef struct {
    char items[MAX_CATEGORY_ITEMS][MAX_STR];
    int count;
} CategoryList;

/* ==================== 中介 ==================== */
typedef struct {
    int id;
    char name[MAX_STR];
    char phone[20];
    char idCard[20];
    char password[32];
} Agent;

typedef struct AgentNode {
    Agent data;
    struct AgentNode *next;
} AgentNode;

/* ==================== 租客 ==================== */
typedef struct {
    int id;
    char name[MAX_STR];
    char phone[20];
    char idCard[20];
    char password[32];
} Tenant;

typedef struct TenantNode {
    Tenant data;
    struct TenantNode *next;
} TenantNode;

/* ==================== 房源 ==================== */
typedef struct {
    int id;
    char city[MAX_STR];
    char region[MAX_STR];
    char community[MAX_STR];
    char address[MAX_BIG_STR];  /* 路/街道地址 */
    char building[MAX_STR];     /* 楼栋，如 "1栋" "A座" */
    int floor;                  /* 第几层 */
    char unitNo[MAX_STR];       /* 室号，如 "1202室" */
    char floorNote[MAX_STR];
    char orientation[MAX_STR];
    char houseType[MAX_STR];
    double area;
    char decoration[MAX_STR];
    double price;
    char ownerName[MAX_STR];
    char ownerPhone[20];
    int createdByAgentId;    /* 录入中介ID，管理员录入为0 */
    int status;  /* HOUSE_VACANT / HOUSE_RENTED / HOUSE_PENDING / HOUSE_OFFLINE */
    char rejectReason[MAX_BIG_STR];  /* 驳回原因，非驳回状态可为空 */
} House;

typedef struct HouseNode {
    House data;
    struct HouseNode *next;
} HouseNode;

/* ==================== 看房预约 ==================== */
typedef struct {
    int id;
    char datetime[20];    /* YYYY-MM-DD HH:MM */
    int houseId;
    int tenantId;
    int agentId;          /* 0 表示待分配 */
    int durationMinutes;
    int status;           /* VIEWING_xxx */
    char tenantFeedback[MAX_BIG_STR];
    char agentFeedback[MAX_BIG_STR];
} Viewing;

typedef struct ViewingNode {
    Viewing data;
    struct ViewingNode *next;
} ViewingNode;

/* ==================== 租房合同 ==================== */
typedef struct {
    int id;
    int houseId;
    int tenantId;
    int agentId;
    char contractDate[11]; /* YYYY-MM-DD */
    char startDate[11];    /* YYYY-MM-DD */
    char endDate[11];      /* YYYY-MM-DD */
    double monthlyRent;
    int status;            /* RENTAL_xxx */
    int signStatus;        /* RENTAL_SIGN_xxx */
} Rental;

typedef struct RentalNode {
    Rental data;
    struct RentalNode *next;
} RentalNode;

/* ==================== 全局数据库 ==================== */
typedef struct {
    char adminPassword[32];

    CategoryList regions;
    CategoryList floorNotes;
    CategoryList orientations;
    CategoryList houseTypes;
    CategoryList decorations;

    AgentNode   *agents;
    int          agentCount;

    TenantNode  *tenants;
    int          tenantCount;

    HouseNode   *houses;
    int          houseCount;

    ViewingNode *viewings;
    int          viewingCount;

    RentalNode  *rentals;
    int          rentalCount;
} Database;

/* ==================== 入口 ==================== */
void rental_system_run(const char *argv0);

#endif
