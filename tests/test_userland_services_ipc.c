/**
 * @file    test_userland_services_ipc.c
 * @brief   用户态服务 IPC 集成测试
 * @version 1.0
 *
 * @details 测试 AISafeOS64 所有用户态服务的 IPC 通信
 *          - fs: 文件系统服务
 *          - proc: 进程管理服务
 *          - mem: 内存服务
 *          - net: 网络协议栈服务
 *          - path: 路径服务
 *          - init: 初始化服务
 *          - security: 安全服务
 *          - vmm: 虚拟内存管理服务
 *
 * @note MISRA-C:2012 合规
 * @note 使用 IPC 桩函数进行测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/* ========================================================================
 * IPC 桩函数
 * ======================================================================== */

/* IPC 消息类型定义 */
typedef enum
{
    IPC_MSG_REQ = 0,
    IPC_MSG_REP = 1,
    IPC_MSG_PULSE = 2
} ipc_msg_type_t;

/* 服务类型 */
typedef enum
{
    SVC_FS = 0,
    SVC_PROC = 1,
    SVC_MEM = 2,
    SVC_NET = 3,
    SVC_PATH = 4,
    SVC_INIT = 5,
    SVC_SECURITY = 6,
    SVC_VMM = 7
} service_type_t;

/* 文件系统请求类型 */
typedef enum
{
    FS_REQ_OPEN = 0,
    FS_REQ_CLOSE,
    FS_REQ_READ,
    FS_REQ_WRITE,
    FS_REQ_MKDIR,
    FS_REQ_UNLINK,
    FS_REQ_LSTAT
} fs_req_type_t;

/* 进程管理请求类型 */
typedef enum
{
    PROC_REQ_CREATE = 0,
    PROC_REQ_DESTROY,
    PROC_REQ_STATUS,
    PROC_REQ_SET_PRIORITY
} proc_req_type_t;

/* 内存服务请求类型 */
typedef enum
{
    MEM_REQ_STATS = 0,
    MEM_REQ_MAP_QUERY,
    MEM_REQ_SET_LIMITS
} mem_req_type_t;

/* 网络服务请求类型 */
typedef enum
{
    NET_REQ_SOCKET_CREATE = 0,
    NET_REQ_TCP_CONNECT,
    NET_REQ_UDP_SEND_RECV
} net_req_type_t;

/* 路径服务请求类型 */
typedef enum
{
    PATH_REQ_REGISTER = 0,
    PATH_REQ_DISCOVER,
    PATH_REQ_RESOLVE
} path_req_type_t;

/* 初始化服务请求类型 */
typedef enum
{
    INIT_REQ_STARTUP = 0,
    INIT_REQ_DEPENDENCY_GRAPH,
    INIT_REQ_AUTO_RESTART
} init_req_type_t;

/* 安全服务请求类型 */
typedef enum
{
    SECURITY_REQ_CAP_VERIFY = 0,
    SECURITY_REQ_ACCESS_CONTROL,
    SECURITY_REQ_AUDIT_LOG
} security_req_type_t;

/* 虚拟内存管理请求类型 */
typedef enum
{
    VMM_REQ_CREATE = 0,
    VMM_REQ_MAP,
    VMM_REQ_VIRTUALIZATION
} vmm_req_type_t;

/* IPC 消息结构 */
typedef struct
{
    uint32_t type;
    uint32_t service;
    uint32_t req_type;
    uint32_t length;
    uint8_t data[256];
} ipc_msg_t;

/* IPC 桩函数声明 */
extern int ipc_stubs_init(void);
extern int ipc_stubs_send(service_type_t service,
                         uint32_t req_type,
                         const void *data,
                         size_t data_len,
                         void *reply,
                         size_t reply_len);
extern void ipc_stubs_set_service_available(service_type_t service,
                                          bool available);
extern int ipc_stubs_cleanup(void);

/* ========================================================================
 * 测试框架
 * ======================================================================== */

static int g_test_passed = 0;
static int g_test_failed = 0;

static const char *g_service_names[] =
{
    "FS",
    "PROC",
    "MEM",
    "NET",
    "PATH",
    "INIT",
    "SECURITY",
    "VMM"
};

static void test_start(service_type_t type, const char *name)
{
    printf("[%s] %s ... ", g_service_names[type], name);
}

static void test_pass(void)
{
    printf("✓ PASSED\n");
    g_test_passed++;
}

static void test_fail(const char *reason)
{
    printf("✗ FAILED (%s)\n", reason);
    g_test_failed++;
}

/* ========================================================================
 * 文件系统服务 IPC 测试
 * ======================================================================== */

static void test_fs_ipc_open(void)
{
    int ret;
    const char *filename = "test.txt";

    test_start(SVC_FS, "IPC 调用 - open");

    ret = ipc_stubs_send(SVC_FS, FS_REQ_OPEN,
                          filename, strlen(filename) + 1,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_fs_ipc_read(void)
{
    int ret;
    uint8_t buf[64];

    test_start(SVC_FS, "IPC 调用 - read");

    ret = ipc_stubs_send(SVC_FS, FS_REQ_READ,
                          NULL, 0,
                          buf, sizeof(buf));

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_fs_ipc_write(void)
{
    int ret;
    const char *data = "Hello, World!";

    test_start(SVC_FS, "IPC 调用 - write");

    ret = ipc_stubs_send(SVC_FS, FS_REQ_WRITE,
                          data, strlen(data) + 1,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

/* ========================================================================
 * 进程管理服务 IPC 测试
 * ======================================================================== */

static void test_proc_ipc_create(void)
{
    int ret;

    test_start(SVC_PROC, "IPC 调用 - create");

    ret = ipc_stubs_send(SVC_PROC, PROC_REQ_CREATE,
                          NULL, 0,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_proc_ipc_status(void)
{
    int ret;

    test_start(SVC_PROC, "IPC 调用 - status");

    ret = ipc_stubs_send(SVC_PROC, PROC_REQ_STATUS,
                          NULL, 0,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_proc_ipc_set_priority(void)
{
    int ret;
    uint32_t priority = 10;

    test_start(SVC_PROC, "IPC 调用 - set_priority");

    ret = ipc_stubs_send(SVC_PROC, PROC_REQ_SET_PRIORITY,
                          &priority, sizeof(priority),
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

/* ========================================================================
 * 内存服务 IPC 测试
 * ======================================================================== */

static void test_mem_ipc_stats(void)
{
    int ret;

    test_start(SVC_MEM, "IPC 调用 - stats");

    ret = ipc_stubs_send(SVC_MEM, MEM_REQ_STATS,
                          NULL, 0,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_mem_ipc_map_query(void)
{
    int ret;

    test_start(SVC_MEM, "IPC 调用 - map_query");

    ret = ipc_stubs_send(SVC_MEM, MEM_REQ_MAP_QUERY,
                          NULL, 0,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_mem_ipc_set_limits(void)
{
    int ret;
    uint64_t limit = 1024 * 1024;

    test_start(SVC_MEM, "IPC 调用 - set_limits");

    ret = ipc_stubs_send(SVC_MEM, MEM_REQ_SET_LIMITS,
                          &limit, sizeof(limit),
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

/* ========================================================================
 * 网络服务 IPC 测试
 * ======================================================================== */

static void test_net_ipc_socket_create(void)
{
    int ret;

    test_start(SVC_NET, "IPC 调用 - socket_create");

    ret = ipc_stubs_send(SVC_NET, NET_REQ_SOCKET_CREATE,
                          NULL, 0,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_net_ipc_tcp_connect(void)
{
    int ret;
    const char *addr = "127.0.0.1";
    uint16_t port = 8080;

    struct
    {
        char addr[16];
        uint16_t port;
    } conn_data;

    strncpy(conn_data.addr, addr, sizeof(conn_data.addr));
    conn_data.port = port;

    test_start(SVC_NET, "IPC 调用 - tcp_connect");

    ret = ipc_stubs_send(SVC_NET, NET_REQ_TCP_CONNECT,
                          &conn_data, sizeof(conn_data),
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_net_ipc_udp_send_recv(void)
{
    int ret;
    const char *data = "UDP test data";

    test_start(SVC_NET, "IPC 调用 - udp_send_recv");

    ret = ipc_stubs_send(SVC_NET, NET_REQ_UDP_SEND_RECV,
                          data, strlen(data) + 1,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

/* ========================================================================
 * 路径服务 IPC 测试
 * ======================================================================== */

static void test_path_ipc_register(void)
{
    int ret;
    const char *service_name = "test_service";

    test_start(SVC_PATH, "IPC 调用 - register");

    ret = ipc_stubs_send(SVC_PATH, PATH_REQ_REGISTER,
                          service_name, strlen(service_name) + 1,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_path_ipc_discover(void)
{
    int ret;
    const char *service_name = "fs";

    test_start(SVC_PATH, "IPC 调用 - discover");

    ret = ipc_stubs_send(SVC_PATH, PATH_REQ_DISCOVER,
                          service_name, strlen(service_name) + 1,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_path_ipc_resolve(void)
{
    int ret;
    const char *path = "/fs/test.txt";

    test_start(SVC_PATH, "IPC 调用 - resolve");

    ret = ipc_stubs_send(SVC_PATH, PATH_REQ_RESOLVE,
                          path, strlen(path) + 1,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

/* ========================================================================
 * 初始化服务 IPC 测试
 * ======================================================================== */

static void test_init_ipc_startup(void)
{
    int ret;

    test_start(SVC_INIT, "IPC 调用 - startup");

    ret = ipc_stubs_send(SVC_INIT, INIT_REQ_STARTUP,
                          NULL, 0,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_init_ipc_dependency_graph(void)
{
    int ret;

    test_start(SVC_INIT, "IPC 调用 - dependency_graph");

    ret = ipc_stubs_send(SVC_INIT, INIT_REQ_DEPENDENCY_GRAPH,
                          NULL, 0,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_init_ipc_auto_restart(void)
{
    int ret;

    test_start(SVC_INIT, "IPC 调用 - auto_restart");

    ret = ipc_stubs_send(SVC_INIT, INIT_REQ_AUTO_RESTART,
                          NULL, 0,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

/* ========================================================================
 * 安全服务 IPC 测试
 * ======================================================================== */

static void test_security_ipc_cap_verify(void)
{
    int ret;
    uint64_t cap = 0x123456789ABCDEF0ULL;

    test_start(SVC_SECURITY, "IPC 调用 - cap_verify");

    ret = ipc_stubs_send(SVC_SECURITY, SECURITY_REQ_CAP_VERIFY,
                          &cap, sizeof(cap),
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_security_ipc_access_control(void)
{
    int ret;
    uint32_t resource_id = 100;

    test_start(SVC_SECURITY, "IPC 调用 - access_control");

    ret = ipc_stubs_send(SVC_SECURITY, SECURITY_REQ_ACCESS_CONTROL,
                          &resource_id, sizeof(resource_id),
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_security_ipc_audit_log(void)
{
    int ret;
    const char *event = "access_denied";

    test_start(SVC_SECURITY, "IPC 调用 - audit_log");

    ret = ipc_stubs_send(SVC_SECURITY, SECURITY_REQ_AUDIT_LOG,
                          event, strlen(event) + 1,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

/* ========================================================================
 * 虚拟内存管理服务 IPC 测试
 * ======================================================================== */

static void test_vmm_ipc_create(void)
{
    int ret;

    test_start(SVC_VMM, "IPC 调用 - create");

    ret = ipc_stubs_send(SVC_VMM, VMM_REQ_CREATE,
                          NULL, 0,
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_vmm_ipc_map(void)
{
    int ret;
    uint64_t phys_addr = 0x1000;
    uint64_t virt_addr = 0x8000;

    struct
    {
        uint64_t phys_addr;
        uint64_t virt_addr;
    } map_data = {phys_addr, virt_addr};

    test_start(SVC_VMM, "IPC 调用 - map");

    ret = ipc_stubs_send(SVC_VMM, VMM_REQ_MAP,
                          &map_data, sizeof(map_data),
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

static void test_vmm_ipc_virtualization(void)
{
    int ret;
    uint32_t vm_id = 1;

    test_start(SVC_VMM, "IPC 调用 - virtualization");

    ret = ipc_stubs_send(SVC_VMM, VMM_REQ_VIRTUALIZATION,
                          &vm_id, sizeof(vm_id),
                          NULL, 0);

    if (ret == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("IPC 发送失败");
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    int ret;

    printf("\n");
    printf("========================================\n");
    printf("  用户态服务 IPC 集成测试\n");
    printf("========================================\n");
    printf("\n");

    /* 初始化 IPC 桩函数 */
    ret = ipc_stubs_init();
    if (ret != 0)
    {
        printf("IPC 桩函数初始化失败: %d\n", ret);
        return 1;
    }

    /* 文件系统服务测试 */
    printf("--- 文件系统服务 (fs) ---\n");
    test_fs_ipc_open();
    test_fs_ipc_read();
    test_fs_ipc_write();

    /* 进程管理服务测试 */
    printf("\n--- 进程管理服务 (proc) ---\n");
    test_proc_ipc_create();
    test_proc_ipc_status();
    test_proc_ipc_set_priority();

    /* 内存服务测试 */
    printf("\n--- 内存服务 (mem) ---\n");
    test_mem_ipc_stats();
    test_mem_ipc_map_query();
    test_mem_ipc_set_limits();

    /* 网络协议栈服务测试 */
    printf("\n--- 网络协议栈服务 (net) ---\n");
    test_net_ipc_socket_create();
    test_net_ipc_tcp_connect();
    test_net_ipc_udp_send_recv();

    /* 路径服务测试 */
    printf("\n--- 路径服务 (path) ---\n");
    test_path_ipc_register();
    test_path_ipc_discover();
    test_path_ipc_resolve();

    /* 初始化服务测试 */
    printf("\n--- 初始化服务 (init) ---\n");
    test_init_ipc_startup();
    test_init_ipc_dependency_graph();
    test_init_ipc_auto_restart();

    /* 安全服务测试 */
    printf("\n--- 安全服务 (security) ---\n");
    test_security_ipc_cap_verify();
    test_security_ipc_access_control();
    test_security_ipc_audit_log();

    /* 虚拟内存管理服务测试 */
    printf("\n--- 虚拟内存管理服务 (vmm) ---\n");
    test_vmm_ipc_create();
    test_vmm_ipc_map();
    test_vmm_ipc_virtualization();

    /* 清理 IPC 桩函数 */
    ipc_stubs_cleanup();

    /* 测试总结 */
    printf("\n");
    printf("========================================\n");
    printf("  测试结果\n");
    printf("========================================\n");
    printf("Total:    %d\n", g_test_passed + g_test_failed);
    printf("Passed:   %d ✓\n", g_test_passed);
    printf("Failed:   %d ✗\n", g_test_failed);
    printf("========================================\n");
    printf("\n");

    if (g_test_failed == 0) {
        printf("✓ 所有 IPC 测试通过\n");
        return 0;
    } else {
        printf("✗ 部分测试失败\n");
        return 1;
    }
}
