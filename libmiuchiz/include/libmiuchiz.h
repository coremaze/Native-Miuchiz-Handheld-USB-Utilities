#include <unistd.h>

#define MIUCHIZ_SECTOR_SIZE (512)
#define MIUCHIZ_SECTOR_SCSI_WRITE (0x31)
#define MIUCHIZ_SECTOR_DATA_READ (0x58)
#define MIUCHIZ_SECTOR_DATA_WRITE (0x33)
#define MIUCHIZ_PAGE_SIZE (0x1000)

struct Handheld {
    char* device;
    int fd;
};

/** 
 *Opens a device as a Miuchiz handheld.
 *@param device The device.
 *@return A Handheld* with .fd set to the opened object.
 *@note Free and close device with miuchiz_handheld_destroy.
 *@warning This does not verify that the device is a valid handheld.
 *         Use miuchiz_handheld_is_handheld for that.
 */
struct Handheld* miuchiz_handheld_create(const char* device);

/** 
 *Closes and frees a Miuchiz handheld.
 *@param handheld Pointer to a Handheld.
 *@note Compliment to miuchiz_handheld_create
 */
void miuchiz_handheld_destroy(struct Handheld* handheld);

int miuchiz_handheld_open(struct Handheld* handheld);

void miuchiz_handheld_close(struct Handheld* handheld);

/** 
 *Opens all valid Miuchiz devices on the system.
 *@param handhelds A Handheld***, that is, a pointer to which an array of Handheld pointers will be placed.
 *@return The number of handhelds found and verified; the number of elements in handhelds.
 *@note Free and close all with miuchiz_handheld_destroy_all.
 */
int miuchiz_handheld_create_all(struct Handheld*** handhelds);

/** 
 *Closes and frees an array of handhelds from miuchiz_handheld_create_all.
 *@param handhelds The Handheld** filled by miuchiz_handheld_create_all.
 *@return The number of handhelds found and verified; the number of elements in handhelds.
 *@note Compliment to miuchiz_handheld_create_all.
 */
void miuchiz_handheld_destroy_all(struct Handheld** handhelds);

/** 
 *Verifies that a Handheld* actually refers to a Miuchiz handheld device.
 *@param handheld A Handheld* to be verified.
 *@return 1 if the device is a Miuchiz handheld, 0 otherwise.
 */
int miuchiz_handheld_is_handheld(struct Handheld* handheld);

/** 
 *Writes data to a sector of a Miuchiz handheld.
 *@param handheld A Handheld* to be written to.
 *@param sector The sector to write to.
 *@param data A pointer to data to write.
 *@param ndata The size of data to write.
 *@return -2 if data to be written was not at least 1 sector large. 
 *        -1 if data failed to be written; errno will be filled.
 *        Otherwise, the number of bytes written.
 */
int miuchiz_handheld_write_sector(struct Handheld* handheld, int sector, const char* data, size_t ndata);

/** 
 *Reads data from a sector of a Miuchiz handheld.
 *@param handheld A Handheld* to be read from.
 *@param sector The sector to read from.
 *@param buf A pointer to a buffer to fill with read data.
 *@param nbuf The size of the buffer to be filled.
 *@return -2 if buffer to be filled was not at least 1 sector large. 
 *        -1 if data failed to be read; errno will be filled.
 *        Otherwise, the number of bytes read.
 */
int miuchiz_handheld_read_sector(struct Handheld* handheld, int sector, char* buf, size_t nbuf);

/** 
 *Writes a (SCSI) command to the command interface of a Miuchiz handheld.
 *@param handheld A Handheld* to which to send the command.
 *@param data The command to send.
 *@param ndata The size of the command.
 *@return miuchiz_handheld_write_sector error code or bytes written.
 */
int miuchiz_handheld_send_scsi(struct Handheld* handheld, const char* data, size_t ndata);

/** 
 *Reads a page (0x1000 bytes) from a Miuchiz handheld's flash memory.
 *@param handheld A Handheld* to be read from.
 *@param page The page to be read (0x0000 ~ 0x01FF normally).
 *@param buf A pointer to a buffer to fill with read data.
 *@param nbuf The size of the buffer to be filled.
 *@return miuchiz_handheld_read_sector error code or bytes read.
 */
int miuchiz_handheld_read_page(struct Handheld* handheld, int page, char* buf, size_t nbuf);

/** 
 *Writes a page (0x1000 bytes) to a Miuchiz handheld's flash memory.
 *@param handheld A Handheld* to write to.
 *@param page The page to be read (0x0000 ~ 0x01FF normally).
 *@param buf A pointer to data to write.
 *@param nbuf The size of the data.
 *@return -3 if buf is not exactly 1 page large, miuchiz_handheld_read_sector error code, or bytes written.
 */
int miuchiz_handheld_write_page(struct Handheld* handheld, int page, const char* buf, size_t nbuf);

/** 
 *Rounds n up to the nearest multiple of alignment.
 *@param n Number to be rounded.
 *@param alignment Number to which n will be rounded to a multiple of.
 *@return Rounded number.
 */
size_t miuchiz_round_size_up(size_t n, int alignment);

/** 
 *Prints a hex dump to the screen.
 *@param buffer data to be dumped.
 *@param n size of data.
 */
void miuchiz_hex_dump(const void* buffer, size_t n);