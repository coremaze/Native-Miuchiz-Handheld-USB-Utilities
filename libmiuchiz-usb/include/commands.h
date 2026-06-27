#ifndef MIUCHIZ_LIBMIUCHIZ_COMMANDS_H
#define MIUCHIZ_LIBMIUCHIZ_COMMANDS_H
#include <inttypes.h>

#define MIUCHIZ_SCSI_READ_COMMAND_SIZE            (5) /* opcode + 4-byte BE page */
#define MIUCHIZ_SCSI_WRITE_COMMAND_SIZE           (9) /* opcode + 4-byte BE page + 4-byte BE size */
#define MIUCHIZ_SCSI_READ_REVERSE_COMMAND_SIZE    (1) /* opcode */
#define MIUCHIZ_SCSI_WRITE_FILEMARKS_COMMAND_SIZE (1) /* opcode */

struct SCSIReadCommand {
    unsigned char bytes[MIUCHIZ_SCSI_READ_COMMAND_SIZE];
};

struct SCSIWriteCommand {
    unsigned char bytes[MIUCHIZ_SCSI_WRITE_COMMAND_SIZE];
};

struct SCSIReadReverseCommand {
    unsigned char bytes[MIUCHIZ_SCSI_READ_REVERSE_COMMAND_SIZE];
};

struct SCSIWriteFilemarksCommand {
    unsigned char bytes[MIUCHIZ_SCSI_WRITE_FILEMARKS_COMMAND_SIZE];
};

struct SCSIReadCommand miuchiz_scsi_read_command(uint32_t source_page);
struct SCSIWriteCommand miuchiz_scsi_write_command(uint32_t destination_page, uint32_t payload_size);
struct SCSIReadReverseCommand miuchiz_scsi_read_reverse_command(void);
struct SCSIWriteFilemarksCommand miuchiz_scsi_write_filemarks_command(void);

#endif
