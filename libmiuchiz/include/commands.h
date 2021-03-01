#include <stdlib.h>

struct __attribute__ ((packed)) SCSIReadCommand {
    char opcode;
    u_int32_t source_page_be;
};

struct __attribute__ ((packed)) SCSIWriteCommand {
    char opcode;
    u_int32_t destination_page_be;
    u_int32_t payload_size_be;
};

struct __attribute__ ((packed)) SCSIReadReverseCommand {
    char opcode;
};

struct __attribute__ ((packed)) SCSIWriteFilemarksCommand {
    char opcode;
};

struct SCSIReadCommand* miuchiz_scsi_read_command_create(u_int32_t source_page);
void miuchiz_scsi_read_command_destroy(struct SCSIReadCommand* cmd);

struct SCSIWriteCommand* miuchiz_scsi_write_command_create(u_int32_t destination_page, u_int32_t payload_size);
void miuchiz_scsi_write_command_destroy(struct SCSIWriteCommand* cmd);

struct SCSIReadReverseCommand* miuchiz_scsi_read_reverse_command_create();
void miuchiz_scsi_read_reverse_command_destroy(struct SCSIReadReverseCommand* cmd);

struct SCSIWriteFilemarksCommand* miuchiz_scsi_write_filemarks_command_create();
void miuchiz_scsi_write_filemarks_command_destroy(struct SCSIWriteFilemarksCommand* cmd);