#ifndef RENTAL_SYSTEM_H
#define RENTAL_SYSTEM_H

/*
 * 文件: rental_system.h
 * 定义内容: 系统核心领域模型、状态常量与主流程入口声明。
 * 后续用途: 作为全项目共享的业务契约头文件，供存储层/业务层/工具层共同依赖。
 */

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

/* ==================== 看房-合同关联状态 ==================== */
#define VIEWING_CONTRACT_NONE     0  /* 未发起合同 */
#define VIEWING_CONTRACT_PENDING  1  /* 已发起合同待确认 */
#define VIEWING_CONTRACT_DONE     2  /* 合同已完成 */

/* ==================== 租约状态 ==================== */
#define RENTAL_ACTIVE   0   /* 有效 */
#define RENTAL_EXPIRED  1   /* 已到期 */
#define RENTAL_EARLY    2   /* 提前退租 */

/* ==================== 签约流程状态 ==================== */
#define RENTAL_SIGN_PENDING    0  /* 待租客确认 */
#define RENTAL_SIGN_CONFIRMED  1  /* 已签订 */
#define RENTAL_SIGN_REJECTED   2  /* 已拒签 */
#define RENTAL_SIGN_CANCELLED  3  /* 已撤销 */

/*
 * 分类列表结构
 * 作用: 统一承载区域/朝向/装修等可配置字典项。
 * 后续用途:
 * 1) 房源录入与修改时作为可选值来源，减少脏数据。
 * 2) 组合查询与统计时用于枚举和合法性校验。
 * 3) 管理员分类维护菜单直接操作该结构。
 */
typedef struct {
    /* 分类条目二维数组: 最多 MAX_CATEGORY_ITEMS 条，每条最长 MAX_STR-1 字符 */
    char items[MAX_CATEGORY_ITEMS][MAX_STR];
    /* 当前有效条目数量 */
    int count;
} CategoryList;

/*
 * 中介基础信息结构
 * 作用: 表示单个中介账号及身份信息，是中介业务权限的主体。
 * 后续用途:
 * 1) 登录认证与密码管理。
 * 2) 看房/租约记录中的 agentId 外键关联。
 * 3) 管理员与统计模块按中介维度查询业务数据。
 */
typedef struct {
    int id;
    char name[MAX_STR];
    char gender[8];  /* 性别: "男" / "女" */
    char phone[20];
    char idCard[20];
    char password[32];
} Agent;

typedef struct AgentNode {
    /* 当前节点保存的中介数据 */
    Agent data;
    /* 指向下一个节点 */
    struct AgentNode *next;
} AgentNode;

/*
 * 租客基础信息结构
 * 作用: 表示单个租客账号及身份信息，是租客端业务的主体。
 * 后续用途:
 * 1) 登录认证、资料维护、找回密码校验。
 * 2) 看房/租约记录中的 tenantId 外键关联。
 * 3) 管理员与统计模块按租客维度聚合分析。
 */
typedef struct {
    int id;
    char name[MAX_STR];
    char gender[8];  /* 性别: "男" / "女" */
    char phone[20];
    char idCard[20];
    char password[32];
} Tenant;

typedef struct TenantNode {
    /* 当前节点保存的租客数据 */
    Tenant data;
    /* 指向下一个节点 */
    struct TenantNode *next;
} TenantNode;

/*
 * 房源实体结构
 * 作用: 描述可出租房源的静态属性与上架状态。
 * 后续用途:
 * 1) 查询/排序/组合筛选的核心数据源。
 * 2) 预约冲突校验与租约签订时的合法性判定。
 * 3) 审核流转与出租状态统计（空闲/已租/待审/下架）。
 */
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
    /* 当前节点保存的房源数据 */
    House data;
    /* 指向下一个节点 */
    struct HouseNode *next;
} HouseNode;

/*
 * 看房预约结构
 * 作用: 记录一次租客看房申请或安排，覆盖时间、参与方与处理状态。
 * 后续用途:
 * 1) 中介待处理预约工作流（同意/拒绝/完成）。
 * 2) 管理员分配中介与查询审计。
 * 3) 与租约通过 appointmentId 建立业务闭环，计算转化率。
 */
typedef struct {
    int id;
    char datetime[20];    /* YYYY-MM-DD HH:MM */
    int houseId;
    int tenantId;
    int agentId;          /* 0 表示待分配 */
    int durationMinutes;
    int status;           /* VIEWING_xxx */
    int contractStatus;   /* VIEWING_CONTRACT_xxx */
    char tenantFeedback[MAX_BIG_STR];
    char agentFeedback[MAX_BIG_STR];
} Viewing;

typedef struct ViewingNode {
    /* 当前节点保存的预约数据 */
    Viewing data;
    /* 指向下一个节点 */
    struct ViewingNode *next;
} ViewingNode;

/*
 * 租约合同结构
 * 作用: 记录签约结果、租期与履约状态，是租房业务的最终凭据。
 * 后续用途:
 * 1) 租客确认/拒签流程与合同状态维护。
 * 2) 房源状态联动刷新（已租/可租）。
 * 3) 管理端和中介端按时间、状态、金额做统计分析。
 */
typedef struct {
    int id;
    int houseId;
    int tenantId;
    int agentId;
    int appointmentId;      /* 关联看房ID，0 表示无关联 */
    char contractDate[11]; /* YYYY-MM-DD */
    char startDate[11];    /* YYYY-MM-DD */
    char endDate[11];      /* YYYY-MM-DD */
    int leaseTerm;         /* 租期（月） */
    double monthlyRent;
    double deposit;        /* 押金 */
    char otherTerms[500];  /* 其他条款 */
    char rejectReason[MAX_BIG_STR]; /* 拒签原因 */
    int status;            /* RENTAL_xxx */
    int signStatus;        /* RENTAL_SIGN_xxx */
} Rental;

typedef struct RentalNode {
    /* 当前节点保存的租约数据 */
    Rental data;
    /* 指向下一个节点 */
    struct RentalNode *next;
} RentalNode;

/*
 * 全局数据库结构
 * 作用: 运行期内存数据库，聚合系统全部业务链表和计数器。
 * 后续用途:
 * 1) 作为查询、排序、统计与业务校验的统一数据源。
 * 2) 序列化/反序列化时的顶层容器。
 * 3) 支撑多角色菜单在一次会话内共享状态。
 */
typedef struct {
    /* 管理员密码（程序内部可能为明文或哈希串） */
    char adminPassword[32];

    /* 房源分类字典（用于录入/筛选） */
    CategoryList regions;
    CategoryList floorNotes;
    CategoryList orientations;
    CategoryList houseTypes;
    CategoryList decorations;

    /* 中介链表与数量 */
    AgentNode   *agents;
    int          agentCount;

    /* 租客链表与数量 */
    TenantNode  *tenants;
    int          tenantCount;

    /* 房源链表与数量 */
    HouseNode   *houses;
    int          houseCount;

    /* 看房预约链表与数量 */
    ViewingNode *viewings;
    int          viewingCount;

    /* 租约链表与数量 */
    RentalNode  *rentals;
    int          rentalCount;
} Database;

/* ==================== 入口 ==================== */
/*
 * 功能: 启动租房中介管理系统主流程
 * 输入: argv0 可执行程序路径（用于推导默认数据文件路径）
 * 输出: 无
 */
void rental_system_run(const char *argv0);

#endif
