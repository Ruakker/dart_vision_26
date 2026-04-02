#ifndef __DRV_CRC_H__
#define __DRV_CRC_H__

#ifdef __cplusplus
extern "C" {
#endif

extern const unsigned char CRC8_INIT;
extern const unsigned short CRC16_INIT;
extern const unsigned char CRC8_TAB[256];
extern const unsigned short wCRC_Table[256];

/*
** Descriptions: CRC8 checksum function
** Input: Data to check,Stream length, initialized checksum
** Output: CRC checksum
*/
unsigned char Get_CRC8_Check_Sum(unsigned char *pchMessage,
                                 unsigned int dwLength, unsigned char ucCRC8);

/*
** Descriptions: CRC8 Verify function
** Input: Data to Verify,Stream length = Data + checksum
** Output: True or False (CRC Verify Result)
*/
unsigned int Verify_CRC8_Check_Sum(unsigned char *pchMessage,
                                   unsigned int dwLength);

/*
** Descriptions: append CRC8 to the end of data
** Input: Data to CRC and append,Stream length = Data + checksum
** Output: True or False (CRC Verify Result)
*/
void Append_CRC8_Check_Sum(unsigned char *pchMessage, unsigned int dwLength);

/*
** Descriptions: CRC16 checksum function
** Input: Data to check,Stream length, initialized checksum
** Output: CRC checksum
*/
unsigned short Get_CRC16_Check_Sum(unsigned char *pchMessage,
                                   unsigned int dwLength, unsigned short wCRC);

/*
** Descriptions: CRC16 Verify function
** Input: Data to Verify,Stream length = Data + checksum
** Output: True or False (CRC Verify Result)
*/
unsigned int Verify_CRC16_Check_Sum(unsigned char *pchMessage,
                                    unsigned int dwLength);

/*
** Descriptions: append CRC16 to the end of data
** Input: Data to CRC and append,Stream length = Data + checksum
** Output: True or False (CRC Verify Result)
*/
void Append_CRC16_Check_Sum(unsigned char *pchMessage, unsigned int dwLength);

#ifdef __cplusplus
}
#endif

#endif
