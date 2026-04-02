#ifndef __DRV_CMD_COM_H
#define __DRV_CMD_COM_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined __arm__
#include <rtdevice.h>
#include <rtthread.h>
#elif defined _WIN32
#include <stdint.h>
#define rt_device_t \
    void*  //  该函数是stm32
           //  RTT操作系统特有的结构体，主要是用来传递设备（串口）指针，来定向到输出和输入的数据流，
//  建议在看懂数据输出位置后，重新设置该类型和其调用的函数

#elif defined __linux__
#include <stdint.h>
#define rt_device_t \
    void*  //  该函数是stm32
           //  RTT操作系统特有的结构体，主要是用来传递设备（串口）指针，来定向到输出和输入的数据流
//  建议在看懂数据输出位置后，重新设置该类型和其调用的函数

#endif

#define COM_HEADER_SOF 0xAA  // 设置帧头的数据为0xAA

// 这里需要处理结构体字节对齐问题，按照如下方式处理可以设置为不需要字节对齐（一字节对齐），保证数据是对齐的（如果设置不对其，需要发送方和接收方同时设置为不对齐方式）
#if defined _WIN32 || defined __linux__
#pragma pack(push, 1)  // 保存当前对齐状态，并设置为1字节对齐
// 帧头
typedef struct {
    unsigned char SOF;          // 数据帧起始字节
    unsigned short DataLength;  // 数据帧中data的长度
    unsigned char Seq;          // 包序号
    unsigned char CRC8;         // CRC8校验
} ComHeader_t;

// 发送包结构体
typedef struct {
    ComHeader_t ComHeader;  // 帧头
    unsigned short CmdID;   // 命令ID
    void* Data;             // 数据指针
    // unsigned short CRC16; // CRC16校验
} ComSendPacket_t;

// 接收包结构体
typedef struct {
    ComHeader_t ComHeader;
    unsigned short CmdID;
    void* Data;  // 数据指针
    // unsigned short CRC16;
} ComReceivePacket_t;
#pragma pack(pop)  // 恢复之前的对齐状态

struct LinuxDevice {
    int fd;  // 文件描述符
};

#elif defined __arm__
// 帧头
typedef __packed struct {
    unsigned char SOF;          // 数据帧起始字节
    unsigned short DataLength;  // 数据帧中data的长度
    unsigned char Seq;          // 包序号
    unsigned char CRC8;         // CRC8校验
} ComHeader_t;

// 发送包结构体
typedef __packed struct {
    ComHeader_t ComHeader;  // 帧头
    unsigned short CmdID;   // 命令ID
    void* Data;             // 数据指针
    // unsigned short CRC16; // CRC16校验
} ComSendPacket_t;

// 接收包结构体
typedef __packed struct {
    ComHeader_t ComHeader;
    unsigned short CmdID;
    void* Data;  // 数据指针
    // unsigned short CRC16;
} ComReceivePacket_t;

#endif

//    ******************接收部分************
// 接收数据包结构

typedef void (*ComReceiveCallback_func_p)(
    void*
        data_p);  // 接收数据回调函数指针类型，参数为所设置的解析地址（可以调用解析后的数据来进行相关操作）

typedef struct {
    unsigned short CmdID;                     // 命令ID
    unsigned short DataLength;                // 数据长度
    unsigned int last_rec_time;               // 上次接收时间
    float frequency;                          // 接收频率
    void* Data;                               // 数据指针(指向接收数据的地址)
    ComReceiveCallback_func_p Callback_func;  // 接收数据回调函数指针
} ComReceiveCtrl_t;

//  ******************接收部分************

//   ************发送部分****************
typedef enum {
    TRANS_FOREVER = -1,  // 永久发送
    TRANS_ONCE = 1,      // 单次发送，可以发送多次，发送的次数强制类型转换即可
                     // 这里写为枚举是为了清楚显示永久发送写-1即可（用枚举表示）
} TransType_t;

typedef struct {
    rt_device_t serial;
    unsigned short CmdID;      // 命令ID
    TransType_t TransType;     // 发送类型
    unsigned int send_period;  // 发送周期(每轮询几次发送一次)
    // unsigned char index; // 发送索引
    unsigned short DataLength;  // 数据长度
    void* Data;                 // 数据指针(指向发送数据的地址)
} ComSendCtrl_t;

unsigned char Attach_SendCmd(rt_device_t serial, unsigned short CmdID,
                             TransType_t TransType, unsigned int send_period,
                             unsigned short DataLength, void* Data);
unsigned char Delete_SendCmd(unsigned short CmdID);
uint32_t Send_Cmd_Packet(rt_device_t serial, void* tx_buff,
                         uint32_t tx_buff_size, uint32_t start_tx_index);

unsigned char Attach_Receive_callback(unsigned short CmdID,
                                      unsigned short DataLength, void* Data,
                                      ComReceiveCallback_func_p Callback_func);
unsigned char Delete_Receive_callback(unsigned short CmdID);
unsigned char Receive_Handle(unsigned char* SrcData, unsigned short srcSize);
int Receive_Handle_Consumed(unsigned char* SrcData, unsigned short srcSize);
int rt_device_write(rt_device_t serial, int start_postion, char* buffer,
                    int size);
void Callback_func_data3(void* data);

//     ************发送部分****************

#ifdef __cplusplus
}
#endif

#endif
