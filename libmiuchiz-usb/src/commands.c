#include "commands.h"

#define MIUCHIZ_SCSI_OPCODE_READ            (0x28)
#define MIUCHIZ_SCSI_OPCODE_WRITE           (0x2A)
#define MIUCHIZ_SCSI_OPCODE_WRITE_FILEMARKS (0x80)
#define MIUCHIZ_SCSI_OPCODE_READ_REVERSE    (0x81)

static void miuchiz_store_be32(unsigned char* dst, uint32_t value) {
    dst[0] = (unsigned char)((value >> 24) & 0xFF);
    dst[1] = (unsigned char)((value >> 16) & 0xFF);
    dst[2] = (unsigned char)((value >>  8) & 0xFF);
    dst[3] = (unsigned char)( value        & 0xFF);
}

struct SCSIReadCommand miuchiz_scsi_read_command(uint32_t source_page) {
    struct SCSIReadCommand result;
    result.bytes[0] = MIUCHIZ_SCSI_OPCODE_READ;
    miuchiz_store_be32(&result.bytes[1], source_page);
    return result;
}

struct SCSIWriteCommand miuchiz_scsi_write_command(uint32_t destination_page, uint32_t payload_size) {
    struct SCSIWriteCommand result;
    result.bytes[0] = MIUCHIZ_SCSI_OPCODE_WRITE;
    miuchiz_store_be32(&result.bytes[1], destination_page);
    miuchiz_store_be32(&result.bytes[5], payload_size);
    return result;
}

struct SCSIReadReverseCommand miuchiz_scsi_read_reverse_command(void) {
    struct SCSIReadReverseCommand result;
    result.bytes[0] = MIUCHIZ_SCSI_OPCODE_READ_REVERSE;
    return result;
}

struct SCSIWriteFilemarksCommand miuchiz_scsi_write_filemarks_command(void) {
    struct SCSIWriteFilemarksCommand result;
    result.bytes[0] = MIUCHIZ_SCSI_OPCODE_WRITE_FILEMARKS;
    return result;
}
