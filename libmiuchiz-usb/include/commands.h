#ifndef MIUCHIZ_LIBMIUCHIZ_COMMANDS_H
#define MIUCHIZ_LIBMIUCHIZ_COMMANDS_H
#include <inttypes.h>

struct __attribute__ ((packed)) SCSIReadCommand {
    char opcode;
    uint32_t source_page_be;
};

struct __attribute__ ((packed)) SCSIWriteCommand {
    char opcode;
    uint32_t destination_page_be;
    uint32_t payload_size_be;
};

struct __attribute__ ((packed)) SCSIReadReverseCommand {
    char opcode;
};

struct __attribute__ ((packed)) SCSIWriteFilemarksCommand {
    char opcode;
};

struct SCSIReadCommand miuchiz_scsi_read_command(uint32_t source_page);
struct SCSIWriteCommand miuchiz_scsi_write_command(uint32_t destination_page, uint32_t payload_size);
struct SCSIReadReverseCommand miuchiz_scsi_read_reverse_command();
struct SCSIWriteFilemarksCommand miuchiz_scsi_write_filemarks_command();

#endif