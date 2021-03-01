#include "commands.h"

#include <arpa/inet.h>

#define MIUCHIZ_SCSI_OPCODE_READ            (0x28)
#define MIUCHIZ_SCSI_OPCODE_WRITE           (0x2A)
#define MIUCHIZ_SCSI_OPCODE_WRITE_FILEMARKS (0x80)
#define MIUCHIZ_SCSI_OPCODE_READ_REVERSE    (0x81)

struct SCSIReadCommand* miuchiz_scsi_read_command_create(u_int32_t source_page) {
    struct SCSIReadCommand* result = malloc(sizeof(struct SCSIReadCommand));
    result->opcode = MIUCHIZ_SCSI_OPCODE_READ;
    result->source_page_be = htonl(source_page);
    return result;
}

void miuchiz_scsi_read_command_destroy(struct SCSIReadCommand* cmd) {
    free(cmd);
}

struct SCSIWriteCommand* miuchiz_scsi_write_command_create(u_int32_t destination_page, u_int32_t payload_size) {
    struct SCSIWriteCommand* result = malloc(sizeof(struct SCSIWriteCommand));
    result->opcode = MIUCHIZ_SCSI_OPCODE_WRITE;
    result->destination_page_be = htonl(destination_page);
    result->payload_size_be = htonl(payload_size);
    return result;
}

void miuchiz_scsi_write_command_destroy(struct SCSIWriteCommand* cmd) {
    free(cmd);
}

struct SCSIReadReverseCommand* miuchiz_scsi_read_reverse_command_create() {
    struct SCSIReadReverseCommand* result = malloc(sizeof(struct SCSIReadReverseCommand));
    result->opcode = MIUCHIZ_SCSI_OPCODE_READ_REVERSE;
    return result;
}

void miuchiz_scsi_read_reverse_command_destroy(struct SCSIReadReverseCommand* cmd) {
    free(cmd);
}

struct SCSIWriteFilemarksCommand* miuchiz_scsi_write_filemarks_command_create() {
    struct SCSIWriteFilemarksCommand* result = malloc(sizeof(struct SCSIWriteFilemarksCommand));
    result->opcode = MIUCHIZ_SCSI_OPCODE_WRITE_FILEMARKS;
    return result;
}

void miuchiz_scsi_write_filemarks_command_destroy(struct SCSIWriteFilemarksCommand* cmd) {
    free(cmd);
}