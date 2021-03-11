#include "commands.h"

#if defined(unix) || defined(__unix__) || defined(__unix)
    #include <arpa/inet.h>
#elif defined(_WIN32)
    uint32_t htonl(uint32_t hostlong) {
        uint32_t result = 0;
        result = ((hostlong & 0x000000FF) << 24)
               | ((hostlong & 0x0000FF00) << 8)  
               | ((hostlong & 0x00FF0000) >> 8)  
               | ((hostlong & 0xFF000000) >> 24);
        return result;
    }
#endif

#include <stdlib.h>


#define MIUCHIZ_SCSI_OPCODE_READ            (0x28)
#define MIUCHIZ_SCSI_OPCODE_WRITE           (0x2A)
#define MIUCHIZ_SCSI_OPCODE_WRITE_FILEMARKS (0x80)
#define MIUCHIZ_SCSI_OPCODE_READ_REVERSE    (0x81)

struct SCSIReadCommand miuchiz_scsi_read_command(uint32_t source_page) {
    struct SCSIReadCommand result;
    result.opcode = MIUCHIZ_SCSI_OPCODE_READ;
    result.source_page_be = htonl(source_page);
    return result;
}

struct SCSIWriteCommand miuchiz_scsi_write_command(uint32_t destination_page, uint32_t payload_size) {
    struct SCSIWriteCommand result;
    result.opcode = MIUCHIZ_SCSI_OPCODE_WRITE;
    result.destination_page_be = htonl(destination_page);
    result.payload_size_be = htonl(payload_size);
    return result;
}

struct SCSIReadReverseCommand miuchiz_scsi_read_reverse_command() {
    struct SCSIReadReverseCommand result;
    result.opcode = MIUCHIZ_SCSI_OPCODE_READ_REVERSE;
    return result;
}

struct SCSIWriteFilemarksCommand miuchiz_scsi_write_filemarks_command() {
    struct SCSIWriteFilemarksCommand result;
    result.opcode = MIUCHIZ_SCSI_OPCODE_WRITE_FILEMARKS;
    return result;
}