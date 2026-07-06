/*
 * Emulator backend: reaches a running emiu2 emulator instance over a local
 * socket and speaks its USB transaction protocol - the software analogue of
 * plugging in a handheld. Compiled on every platform, alongside the platform
 * backend, and reached through the backend.c dispatch layer for handhelds
 * whose device string is "emu:" + endpoint file path.
 *
 * Discovery mirrors enumerating the USB bus: each emulator publishes an
 * endpoint file in a well-known runtime directory (a Unix socket per instance;
 * on Windows a loopback TCP port in a "<pid>.port" file). A stale file whose
 * emulator crashed refuses connections and is pruned here.
 *
 * Wire protocol (defined by emiu2's src/usb_socket.rs):
 *   hello    : "EMIU2USB"  version:u16le  flags:u8 (bit0 = cable plugged)
 *              identity_len:u32le  identity[..]
 *   request  : endpoint:u8  token:u8(0=Setup,1=In,2=Out)  len:u32le  data[len]
 *   response : kind:u8(0=Ack,1=Nak,2=Stall,3=Data) then, for Data,
 *              len:u32le data[len]
 *
 * On top of those raw transactions this file implements the same USB Mass
 * Storage Bulk-Only Transport + SCSI the libusb backend implements on real
 * hardware, so everything above the backend seam behaves identically.
 */

#include "libmiuchiz-usb.h"
#include "backend-internal.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    typedef SOCKET emu_sock_t;
    #define EMU_INVALID_SOCKET INVALID_SOCKET
    #define emu_close_socket closesocket
    static void emu_sleep_us(unsigned int usecs) {
        Sleep((usecs + 999) / 1000);
    }
#else
    #include <sys/socket.h>
    #include <sys/un.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
    #include <dirent.h>
    typedef int emu_sock_t;
    #define EMU_INVALID_SOCKET (-1)
    #define emu_close_socket close
    static void emu_sleep_us(unsigned int usecs) {
        usleep(usecs);
    }
#endif

#define EMU_DEVICE_PREFIX "emu:"

#define EMU_HELLO_MAGIC "EMIU2USB"
#define EMU_PROTOCOL_VERSION (2)

/* Hello flags bit: the emulator's USB cable is plugged in. The emulator owns
 * that state (the player toggles it); when it is clear, nothing is on the bus
 * and the endpoint closes right after the hello. */
#define EMU_HELLO_FLAG_PLUGGED (0x01)

/* Transaction tokens. */
#define EMU_TOKEN_SETUP (0)
#define EMU_TOKEN_IN    (1)
#define EMU_TOKEN_OUT   (2)

/* Response kinds. */
#define EMU_RESP_ACK   (0)
#define EMU_RESP_NAK   (1)
#define EMU_RESP_STALL (2)
#define EMU_RESP_DATA  (3)
#define EMU_RESP_ERROR (-1) /* transport failure */

/* Endpoint numbers on the emulated device (0 = control, 1 = bulk). */
#define EMU_ENDPOINT_BULK (1)

/* The device services one transaction per ~1 ms; a NAK means "not staged yet,
 * ask again". The budget bounds how long one logical transfer step may stall
 * (also how long probing a non-USB-mode emulator takes before giving up). */
#define EMU_NAK_RETRIES (2000)
#define EMU_NAK_WAIT_US (500)

/* Bulk packets on this device are at most 64 bytes; responses larger than a
 * sector mean the peer is not speaking our protocol. */
#define EMU_BULK_MAX (64)
#define EMU_MAX_RESPONSE (512)

/* Socket receive/send timeout. Generous: a live emulator answers every
 * transaction within a few ms; only a stopped one runs into this. */
#define EMU_IO_TIMEOUT_MS (5000)

#define CBW_SIZE (31)
#define CSW_SIZE (13)

struct EmuHandheld {
    emu_sock_t sock;
    uint32_t current_sector;
    uint32_t cbw_tag;
};

int miuchiz_emu_is(const struct Handheld* handheld) {
    return handheld->device != NULL
        && strncmp(handheld->device, EMU_DEVICE_PREFIX, strlen(EMU_DEVICE_PREFIX)) == 0;
}

static void ensure_sockets_init(void) {
#if defined(_WIN32)
    static int initialized = 0;
    if (!initialized) {
        WSADATA wsadata;
        WSAStartup(MAKEWORD(2, 2), &wsadata);
        initialized = 1;
    }
#endif
}

static void emu_set_timeouts(emu_sock_t sock) {
#if defined(_WIN32)
    DWORD ms = EMU_IO_TIMEOUT_MS;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof(ms));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof(ms));
#else
    struct timeval tv;
    tv.tv_sec = EMU_IO_TIMEOUT_MS / 1000;
    tv.tv_usec = (EMU_IO_TIMEOUT_MS % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

static int emu_send_all(emu_sock_t sock, const void* buf, size_t n) {
    const char* p = buf;
    while (n > 0) {
        ssize_t sent = send(sock, p, n, 0);
        if (sent <= 0) {
            return -1;
        }
        p += sent;
        n -= sent;
    }
    return 0;
}

static int emu_recv_all(emu_sock_t sock, void* buf, size_t n) {
    char* p = buf;
    while (n > 0) {
        ssize_t got = recv(sock, p, n, 0);
        if (got <= 0) {
            return -1;
        }
        p += got;
        n -= got;
    }
    return 0;
}

/* Reads the endpoint's hello. Returns 0 when it identifies a compatible
 * emulator (setting *plugged from the cable flag), -1 otherwise. */
static int emu_read_hello(emu_sock_t sock, int* plugged) {
    unsigned char header[11];
    if (emu_recv_all(sock, header, sizeof(header)) < 0) {
        return -1;
    }
    if (memcmp(header, EMU_HELLO_MAGIC, 8) != 0) {
        miuchiz_log("libmiuchiz: endpoint is not an emiu2 emulator (bad hello)\n");
        return -1;
    }
    uint16_t version = miuchiz_le16_read(header + 8);
    if (version != EMU_PROTOCOL_VERSION) {
        miuchiz_log("libmiuchiz: emulator endpoint protocol version %u not supported\n", version);
        return -1;
    }
    if (plugged != NULL) {
        *plugged = (header[10] & EMU_HELLO_FLAG_PLUGGED) != 0;
    }
    unsigned char lenbuf[4];
    if (emu_recv_all(sock, lenbuf, sizeof(lenbuf)) < 0) {
        return -1;
    }
    uint32_t identity_len = miuchiz_le32_read(lenbuf);
    if (identity_len > 4096) {
        return -1;
    }
    char identity[4097];
    if (emu_recv_all(sock, identity, identity_len) < 0) {
        return -1;
    }
    identity[identity_len] = '\0';
    miuchiz_log("libmiuchiz: emulator endpoint identity: \"%s\"\n", identity);
    return 0;
}

/* Connects to the endpoint behind a discovery file. Returns the connected
 * socket, or EMU_INVALID_SOCKET. `refused` (optional) is set to 1 when the
 * endpoint actively refused - the mark of a stale file. */
static emu_sock_t emu_connect_path(const char* path, int* refused) {
    if (refused != NULL) {
        *refused = 0;
    }
    ensure_sockets_init();

    const char* ext = strrchr(path, '.');
    if (ext == NULL) {
        return EMU_INVALID_SOCKET;
    }

#if !defined(_WIN32)
    if (strcmp(ext, ".sock") == 0) {
        struct sockaddr_un addr;
        if (strlen(path) >= sizeof(addr.sun_path)) {
            return EMU_INVALID_SOCKET;
        }
        emu_sock_t sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock == EMU_INVALID_SOCKET) {
            return EMU_INVALID_SOCKET;
        }
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strcpy(addr.sun_path, path);
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            if (refused != NULL && errno == ECONNREFUSED) {
                *refused = 1;
            }
            emu_close_socket(sock);
            return EMU_INVALID_SOCKET;
        }
        emu_set_timeouts(sock);
        return sock;
    }
#endif

    if (strcmp(ext, ".port") == 0) {
        FILE* f = fopen(path, "r");
        if (f == NULL) {
            return EMU_INVALID_SOCKET;
        }
        unsigned int port = 0;
        int parsed = fscanf(f, "%u", &port);
        fclose(f);
        if (parsed != 1 || port == 0 || port > 65535) {
            return EMU_INVALID_SOCKET;
        }

        emu_sock_t sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == EMU_INVALID_SOCKET) {
            return EMU_INVALID_SOCKET;
        }
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((unsigned short)port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            if (refused != NULL) {
                /* A dead port on loopback answers RST, i.e. refused. */
                *refused = 1;
            }
            emu_close_socket(sock);
            return EMU_INVALID_SOCKET;
        }
        int nodelay = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
        emu_set_timeouts(sock);
        return sock;
    }

    return EMU_INVALID_SOCKET;
}

/* ---------------------------------------------------------------------------
 * Raw transactions.
 * ------------------------------------------------------------------------ */

/* Issues one transaction. For EMU_RESP_DATA, up to `nresp` bytes are stored in
 * `resp` and `*resp_len` receives the payload length. Returns the response
 * kind, or EMU_RESP_ERROR on a transport failure. */
static int emu_transact(struct EmuHandheld* emu,
                        unsigned char endpoint,
                        unsigned char token,
                        const void* data,
                        size_t ndata,
                        void* resp,
                        size_t nresp,
                        size_t* resp_len) {
    unsigned char header[6];
    header[0] = endpoint;
    header[1] = token;
    miuchiz_le32_write(header + 2, (uint32_t)ndata);
    if (emu_send_all(emu->sock, header, sizeof(header)) < 0) {
        return EMU_RESP_ERROR;
    }
    if (ndata > 0 && emu_send_all(emu->sock, data, ndata) < 0) {
        return EMU_RESP_ERROR;
    }

    unsigned char kind;
    if (emu_recv_all(emu->sock, &kind, 1) < 0) {
        return EMU_RESP_ERROR;
    }
    if (kind != EMU_RESP_DATA) {
        return kind <= EMU_RESP_STALL ? kind : EMU_RESP_ERROR;
    }

    unsigned char lenbuf[4];
    if (emu_recv_all(emu->sock, lenbuf, sizeof(lenbuf)) < 0) {
        return EMU_RESP_ERROR;
    }
    uint32_t len = miuchiz_le32_read(lenbuf);
    if (len > EMU_MAX_RESPONSE) {
        miuchiz_log("libmiuchiz: oversized emulator response (%u bytes)\n", len);
        return EMU_RESP_ERROR;
    }
    unsigned char payload[EMU_MAX_RESPONSE];
    if (emu_recv_all(emu->sock, payload, len) < 0) {
        return EMU_RESP_ERROR;
    }
    size_t copy = len;
    if (copy > nresp) {
        copy = nresp;
    }
    if (resp != NULL) {
        memcpy(resp, payload, copy);
    }
    if (resp_len != NULL) {
        *resp_len = len;
    }
    return EMU_RESP_DATA;
}

/* Like emu_transact, but retries while the device NAKs (the firmware hasn't
 * staged the buffer yet - real hosts poll exactly the same way). */
static int emu_transact_retry(struct EmuHandheld* emu,
                              unsigned char endpoint,
                              unsigned char token,
                              const void* data,
                              size_t ndata,
                              void* resp,
                              size_t nresp,
                              size_t* resp_len) {
    for (int attempt = 0; attempt < EMU_NAK_RETRIES; attempt++) {
        int kind = emu_transact(emu, endpoint, token, data, ndata, resp, nresp, resp_len);
        if (kind != EMU_RESP_NAK) {
            return kind;
        }
        emu_sleep_us(EMU_NAK_WAIT_US);
    }
    miuchiz_log("libmiuchiz: emulator NAK retry budget exhausted\n");
    return EMU_RESP_NAK;
}

/* ---------------------------------------------------------------------------
 * USB Mass Storage Bulk-Only Transport over the transaction seam. Mirrors the
 * libusb backend's scsi_bulk_read/scsi_bulk_write, with libusb bulk transfers
 * replaced by sequences of <=64-byte transactions.
 * ------------------------------------------------------------------------ */

static int emu_bulk_out(struct EmuHandheld* emu, const void* buf, size_t n) {
    const unsigned char* p = buf;
    while (n > 0) {
        size_t chunk = n < EMU_BULK_MAX ? n : EMU_BULK_MAX;
        int kind = emu_transact_retry(emu, EMU_ENDPOINT_BULK, EMU_TOKEN_OUT,
                                      p, chunk, NULL, 0, NULL);
        if (kind != EMU_RESP_ACK) {
            miuchiz_log("libmiuchiz: bulk OUT not accepted (kind %d)\n", kind);
            return -1;
        }
        p += chunk;
        n -= chunk;
    }
    return 0;
}

/* Reads up to `n` bytes from the bulk-IN endpoint. A short packet ends the
 * transfer (the device may supply less than asked). Returns the byte count,
 * or -1. */
static ssize_t emu_bulk_in(struct EmuHandheld* emu, void* buf, size_t n) {
    unsigned char* p = buf;
    size_t total = 0;
    while (total < n) {
        unsigned char packet[EMU_MAX_RESPONSE];
        size_t packet_len = 0;
        int kind = emu_transact_retry(emu, EMU_ENDPOINT_BULK, EMU_TOKEN_IN,
                                      NULL, 0, packet, sizeof(packet), &packet_len);
        if (kind != EMU_RESP_DATA) {
            miuchiz_log("libmiuchiz: bulk IN failed (kind %d)\n", kind);
            return -1;
        }
        size_t copy = packet_len;
        if (copy > n - total) {
            copy = n - total;
        }
        memcpy(p + total, packet, copy);
        total += copy;
        if (packet_len < EMU_BULK_MAX) {
            break;
        }
    }
    return (ssize_t)total;
}

static void emu_build_cbw(unsigned char* cbw,
                          uint32_t tag,
                          uint32_t data_len,
                          int dir_in,
                          const unsigned char* cdb,
                          size_t ncdb) {
    memset(cbw, 0, CBW_SIZE);
    memcpy(cbw, "USBC", 4);
    miuchiz_le32_write(cbw + 4, tag);
    miuchiz_le32_write(cbw + 8, data_len);
    cbw[12] = dir_in ? 0x80 : 0x00;
    cbw[13] = 0x00; /* LUN */
    cbw[14] = (unsigned char)ncdb;
    memcpy(cbw + 15, cdb, ncdb);
}

/* Reads and validates the Command Status Wrapper. Returns 0 when the command
 * passed, -1 otherwise. */
static int emu_check_csw(struct EmuHandheld* emu, uint32_t expected_tag) {
    unsigned char csw[CSW_SIZE];
    ssize_t got = emu_bulk_in(emu, csw, sizeof(csw));
    if (got != CSW_SIZE) {
        miuchiz_log("libmiuchiz: short CSW (%zd bytes)\n", got);
        return -1;
    }
    if (memcmp(csw, "USBS", 4) != 0) {
        miuchiz_log("libmiuchiz: bad CSW signature\n");
        return -1;
    }
    if (miuchiz_le32_read(csw + 4) != expected_tag) {
        miuchiz_log("libmiuchiz: CSW tag mismatch\n");
        return -1;
    }
    if (csw[12] != 0x00) {
        miuchiz_log("libmiuchiz: CSW reports command failed (status %u)\n", csw[12]);
        return -1;
    }
    return 0;
}

static ssize_t emu_scsi_read(struct EmuHandheld* emu, uint32_t sector, void* buf, size_t n) {
    uint32_t tag = ++emu->cbw_tag;
    uint16_t blocks = (uint16_t)((n + (MIUCHIZ_SECTOR_SIZE - 1)) / MIUCHIZ_SECTOR_SIZE);
    unsigned char cdb[10] = {
        0x28, 0x00, /* READ(10) */
        (sector >> 24) & 0xFF, (sector >> 16) & 0xFF, (sector >> 8) & 0xFF, sector & 0xFF,
        0x00,
        (blocks >> 8) & 0xFF, blocks & 0xFF,
        0x00,
    };
    unsigned char cbw[CBW_SIZE];
    emu_build_cbw(cbw, tag, (uint32_t)n, 1, cdb, sizeof(cdb));

    if (emu_bulk_out(emu, cbw, sizeof(cbw)) < 0) {
        return -1;
    }
    ssize_t got = emu_bulk_in(emu, buf, n);
    if (got < 0) {
        return -1;
    }
    if (emu_check_csw(emu, tag) < 0) {
        return -1;
    }
    return got;
}

static ssize_t emu_scsi_write(struct EmuHandheld* emu, uint32_t sector, const void* buf, size_t n) {
    uint32_t tag = ++emu->cbw_tag;
    uint16_t blocks = (uint16_t)((n + (MIUCHIZ_SECTOR_SIZE - 1)) / MIUCHIZ_SECTOR_SIZE);
    unsigned char cdb[10] = {
        0x2A, 0x00, /* WRITE(10) */
        (sector >> 24) & 0xFF, (sector >> 16) & 0xFF, (sector >> 8) & 0xFF, sector & 0xFF,
        0x00,
        (blocks >> 8) & 0xFF, blocks & 0xFF,
        0x00,
    };
    unsigned char cbw[CBW_SIZE];
    emu_build_cbw(cbw, tag, (uint32_t)n, 0, cdb, sizeof(cdb));

    if (emu_bulk_out(emu, cbw, sizeof(cbw)) < 0) {
        return -1;
    }
    if (emu_bulk_out(emu, buf, n) < 0) {
        return -1;
    }
    if (emu_check_csw(emu, tag) < 0) {
        return -1;
    }
    return (ssize_t)n;
}

/* ---------------------------------------------------------------------------
 * The backend interface.
 * ------------------------------------------------------------------------ */

void miuchiz_emu_open(struct Handheld* handheld) {
    handheld->emu = NULL;

    const char* path = handheld->device + strlen(EMU_DEVICE_PREFIX);
    emu_sock_t sock = emu_connect_path(path, NULL);
    if (sock == EMU_INVALID_SOCKET) {
        return;
    }
    int plugged = 0;
    if (emu_read_hello(sock, &plugged) < 0) {
        emu_close_socket(sock);
        return;
    }
    if (!plugged) {
        /* The emulator is running but its USB cable is unplugged - like a
         * real handheld sitting next to the PC. The endpoint has already
         * closed; do not treat it as an attached device. */
        miuchiz_log("libmiuchiz: emulator at %s has its USB cable unplugged\n",
                    handheld->device);
        emu_close_socket(sock);
        return;
    }

    struct EmuHandheld* emu = malloc(sizeof(struct EmuHandheld));
    emu->sock = sock;
    emu->current_sector = 0;
    emu->cbw_tag = 0;
    handheld->emu = emu;
}

void miuchiz_emu_close(struct Handheld* handheld) {
    struct EmuHandheld* emu = handheld->emu;
    if (emu != NULL) {
        emu_close_socket(emu->sock);
        free(emu);
        handheld->emu = NULL;
    }
}

ssize_t miuchiz_emu_read(struct Handheld* handheld, void* buf, size_t n) {
    struct EmuHandheld* emu = handheld->emu;
    if (emu == NULL) {
        return -1;
    }
    return emu_scsi_read(emu, emu->current_sector, buf, n);
}

ssize_t miuchiz_emu_write(struct Handheld* handheld, const void* buf, size_t n) {
    struct EmuHandheld* emu = handheld->emu;
    if (emu == NULL) {
        return -1;
    }
    /* No pacing sleep here: the misbehave-when-rushed workaround in the
     * platform backends is a physical-device quirk; the emulated firmware is
     * already throttled by the transaction seam itself. */
    return emu_scsi_write(emu, emu->current_sector, buf, n);
}

off_t miuchiz_emu_seek(struct Handheld* handheld, off_t offset) {
    struct EmuHandheld* emu = handheld->emu;
    if (emu == NULL) {
        return -1;
    }
    emu->current_sector = (uint32_t)(offset / MIUCHIZ_SECTOR_SIZE);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Discovery.
 * ------------------------------------------------------------------------ */

/* The runtime directories emulators publish endpoints in. Must mirror
 * emiu2's endpoint_dir(): EMIU2_USB_DIR overrides everything; otherwise
 * $XDG_RUNTIME_DIR/emiu2-usb (with /tmp/emiu2-usb as the no-XDG fallback) on
 * Unix, %LOCALAPPDATA%\emiu2-usb on Windows. Both Unix candidates are scanned
 * so a tool and an emulator with different environments still meet. */
static int emu_endpoint_dirs(char dirs[][512], int max_dirs) {
    int count = 0;

    const char* override = getenv("EMIU2_USB_DIR");
    if (override != NULL && override[0] != '\0') {
        snprintf(dirs[count++], 512, "%s", override);
        return count;
    }

#if defined(_WIN32)
    const char* local = getenv("LOCALAPPDATA");
    if (local != NULL && local[0] != '\0' && count < max_dirs) {
        snprintf(dirs[count++], 512, "%s\\emiu2-usb", local);
    }
    const char* temp = getenv("TEMP");
    if (temp != NULL && temp[0] != '\0' && count < max_dirs) {
        snprintf(dirs[count++], 512, "%s\\emiu2-usb", temp);
    }
#else
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime != NULL && runtime[0] != '\0' && count < max_dirs) {
        snprintf(dirs[count++], 512, "%s/emiu2-usb", runtime);
    }
    if (count < max_dirs) {
        snprintf(dirs[count++], 512, "/tmp/emiu2-usb");
    }
#endif

    return count;
}

static int emu_is_endpoint_file(const char* name) {
    const char* ext = strrchr(name, '.');
    if (ext == NULL) {
        return 0;
    }
#if defined(_WIN32)
    return strcmp(ext, ".port") == 0;
#else
    return strcmp(ext, ".sock") == 0 || strcmp(ext, ".port") == 0;
#endif
}

/* Considers one endpoint file: verifies the emulator behind it, appends it to
 * the result list or prunes/skips it. */
static void emu_consider_endpoint(const char* dir_path,
                                  const char* name,
                                  struct Handheld*** handhelds,
                                  int* count,
                                  int* capacity) {
    if (!emu_is_endpoint_file(name)) {
        return;
    }

    char device[2048];
    snprintf(device, sizeof(device), "%s%s/%s", EMU_DEVICE_PREFIX, dir_path, name);
    const char* path = device + strlen(EMU_DEVICE_PREFIX);

    struct Handheld* candidate = miuchiz_handheld_create(device);
    if (candidate->emu == NULL) {
        /* Could not attach. If the endpoint actively refuses, its emulator is
         * gone - prune the corpse. (A busy cable or a hello failure just gets
         * skipped.) */
        int refused = 0;
        emu_sock_t probe = emu_connect_path(path, &refused);
        if (probe != EMU_INVALID_SOCKET) {
            emu_close_socket(probe);
        }
        else if (refused) {
            remove(path);
        }
        miuchiz_handheld_destroy(candidate);
        return;
    }

    if (miuchiz_handheld_is_handheld(candidate)) {
        if (*count + 1 >= *capacity) {
            *capacity = *capacity == 0 ? 8 : *capacity * 2;
            *handhelds = realloc(*handhelds, *capacity * sizeof(struct Handheld*));
        }
        (*handhelds)[(*count)++] = candidate;
        (*handhelds)[*count] = NULL;
    }
    else {
        /* Present but not answering as a handheld - an emulator whose device
         * is not in its USB ("Please Connect to PC") mode. */
        miuchiz_log("libmiuchiz: emulator at %s is not in USB mode; skipping\n", device);
        miuchiz_handheld_destroy(candidate);
    }
}

int miuchiz_emu_enumerate(struct Handheld*** handhelds) {
    int count = 0;
    int capacity = 0;
    *handhelds = NULL;

    char dirs[2][512];
    int ndirs = emu_endpoint_dirs(dirs, 2);

    for (int d = 0; d < ndirs; d++) {
#if defined(_WIN32)
        char pattern[560];
        snprintf(pattern, sizeof(pattern), "%s\\*", dirs[d]);
        WIN32_FIND_DATAA find;
        HANDLE search = FindFirstFileA(pattern, &find);
        if (search == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            emu_consider_endpoint(dirs[d], find.cFileName, handhelds, &count, &capacity);
        } while (FindNextFileA(search, &find));
        FindClose(search);
#else
        DIR* dir = opendir(dirs[d]);
        if (dir == NULL) {
            continue;
        }
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            emu_consider_endpoint(dirs[d], entry->d_name, handhelds, &count, &capacity);
        }
        closedir(dir);
#endif
    }

    return count;
}
