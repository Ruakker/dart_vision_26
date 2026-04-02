#include "tool/drv_cmd_com.h"

#include "tool/drv_CRC.h"

//    串口通信协议实现：25 届 HERO 战队电控组
//    有问题请联系本人，在没看懂源码的情况下不建议自行修改！！！

#if defined _WIN32
// #include "drv_txt_operation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define rt_tick_get                                                            \
  clock // rt_tick_get()函数是stm32嵌入式RTT操作系统调用的毫秒级时间读取函数，clock()是windows中time.h库中包含的毫秒级时间读取函数

///   该函数是用来发送数据到某个地方（为了调试，我写了一个文件操作的函数，使其写入txt文件中，模拟串口发送出去），如果是在linux系统中，建议改为自己的串口发送函数
///   在windows系统由于没有串口（目前不会），建议只修改写入的文件名称即可（记得需要引用我写的drv_txt_operation.h来对txt文件进行操作）
///   在linux系统中，需要重写为使用自己的串口驱动发送数据，但是建议还是使用该函数名称和原型，方便后续全平台兼容
///   在电控使用的stm32嵌入式平台上，如果使用rtt操作系统，无需定义该函数，在rtt中自带该函数
/// <param name="serial"></param>串口设备名称（设备句柄，用来查找和使用设备）
/// <param
/// name="start_postion"></param>需要发送的内容相较于缓冲区地址的偏移字节数量
/// <param name="buffer"></param>需要发送的缓冲区地址
/// <param name="size"></param>需要发送的内容的字节数
/// <returns></returns>返回成功输出的字节数
int rt_device_write(rt_device_t serial, int start_postion, char *buffer,
                    int size) {
  append_to_file("test.txt", buffer + start_postion, size);
  static int count = 0;
  count++;
  printf("写入%d次\n", count);
}
#elif defined __linux__
// #include "drv_txt_operation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define rt_tick_get                                                            \
  clock // rt_tick_get()函数是stm32嵌入式RTT操作系统调用的毫秒级时间读取函数，clock()是windows中time.h库中包含的毫秒级时间读取函数

///   该函数是用来发送数据到某个地方（为了调试，我写了一个文件操作的函数，使其写入txt文件中，模拟串口发送出去），如果是在linux系统中，建议改为自己的串口发送函数
///   在windows系统由于没有串口（目前不会），建议只修改写入的文件名称即可（记得需要引用我写的drv_txt_operation.h来对txt文件进行操作）
///   在linux系统中，需要重写为使用自己的串口驱动发送数据，但是建议还是使用该函数名称和原型，方便后续全平台兼容
///   在电控使用的stm32嵌入式平台上，如果使用rtt操作系统，无需定义该函数，在rtt中自带该函数
/// <param name="serial"></param>串口设备名称（设备句柄，用来查找和使用设备）
/// <param
/// name="start_postion"></param>需要发送的内容相较于缓冲区地址的偏移字节数量
/// <param name="buffer"></param>需要发送的缓冲区地址
/// <param name="size"></param>需要发送的内容的字节数
/// <returns></returns>返回成功输出的字节数
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h> // 补充缺失的头文件

// Linux串口写入实现
int rt_device_write(rt_device_t serial, int start_postion, char *buffer,
                    int size) {
  if (!serial || start_postion < 0 || size <= 0)
    return -1;
  // printf("Trying to write the data...");
  // // int fd = *(int*)serial; // 假设设备句柄是文件描述符
  //  int fd = reinterpret_cast<intptr_t>(serial); // C++ 标准方式
  // return write(fd, buffer + start_postion, size);
  // 安全地将 void* 转换为文件描述符
  int fd = (int)(intptr_t)serial;

  // 实际写入串口
  ssize_t bytes_written = write(fd, buffer + start_postion, size);
  // printf("Trying to write the data...");
  if (bytes_written < 0) {
    perror("rt_device_write failed");
    return -1;
  }
  return (int)bytes_written;
}

#elif defined __arm__

#endif
typedef struct {
  rt_device_t serial;      // 设备句柄（串口）
  unsigned int time_count; // 计数器
} Device_Time_Counter;

static unsigned char receive_fault_count = 0; // 收到数据后解析校验失败计数

#define MAX_SEND_CMD_NUM 10    // 最大发送命令数量
#define MAX_RECEIVE_CMD_NUM 10 // 最大接收命令数量
#define MAX_DEVICE_NUM 10 // 最大同时注册设备数量（一般这么多应该足够了吧  囧 ）
static ComSendCtrl_t
    sendCtrl[MAX_SEND_CMD_NUM]; // 发送控制结构体（与发送命令对应）
static ComReceiveCtrl_t
    receiveCtrl[MAX_RECEIVE_CMD_NUM]; // 接收控制结构体（与接收命令对应）
static Device_Time_Counter device_counter
    [MAX_DEVICE_NUM]; // 设备运行计数器（不同的串口不会发生干扰，可以同时调用发送函数，它们使用不同的计数器）

static int Attach_Device_auto(rt_device_t serial) {
  if (serial == NULL) {
    return 0;
  }
  for (int i = 0; i < MAX_DEVICE_NUM; i++) {
    if (serial == device_counter[i].serial) {
      break; // 设备已经存在,直接退出
    } else if (device_counter[i].serial != NULL) {
      continue; // 设备不同，继续寻找
    } else {
      device_counter[i].serial = serial; // 将新设备注册
      break;
    }
  }
  return 1;
}

static int Get_Device_Runcount(rt_device_t serial) {
  for (int i = 0; i < MAX_DEVICE_NUM; i++) {
    if (serial ==
        device_counter[i].serial) // 找到设备后返回设备运行次数，并自加1
    {
      return device_counter[i].time_count++;
    } else {
      continue; // 没有找到设备句柄，则继续寻找
    }
  }
  return -1; // 遍历整个设备列表都没有找到设备的话，就返回-1，表示没有找到设备
}

/*
 * @brief 添加新的发送命令ID到一个设备上 或者
 * 更新所发送命令的发送类型，发送周期和绑定的设备
 *
 * @param CmdID
 * @param TransType
 * @param send_period
 * @param DataLength
 * @param Data
 * @return unsigned char
 */
unsigned char Attach_SendCmd(rt_device_t serial, unsigned short CmdID,
                             TransType_t TransType, unsigned int send_period,
                             unsigned short DataLength, void *Data) {
  uint8_t ret = 0;
  if (serial == NULL || Data == NULL || DataLength <= 0) {
    return 0;
  }
  for (unsigned char i = 0; i < MAX_SEND_CMD_NUM; i++) // 检查命令ID是否已经存在
  {
    if (sendCtrl[i].CmdID == CmdID) // 如果命令ID已存在，则更新
    {
      sendCtrl[i].serial = serial; // 将发送的命令绑定到串口设备
      sendCtrl[i].TransType = TransType;
      sendCtrl[i].send_period = send_period;
    } else if (sendCtrl[i].CmdID != 0) {
      continue;
    } else {
      sendCtrl[i].serial = serial; // 将发送的命令绑定到串口设备
      sendCtrl[i].CmdID = CmdID; // 如果命令ID不存在，则添加新的命令
      sendCtrl[i].TransType = TransType;
      sendCtrl[i].send_period = send_period;
      sendCtrl[i].DataLength = DataLength;
      sendCtrl[i].Data = Data;
      ret = 1; // 添加成功
      break;
    }
  }
  if (Attach_Device_auto(serial))
    ret = 1; // 添加成功
  else
    ret = 0;
  return ret;
}

/*
 * @brief 删除指定的发送命令
 *
 * @param CmdID
 * @return unsigned char
 */
unsigned char Delete_SendCmd(unsigned short CmdID) {
  for (unsigned char i = 0; i < MAX_SEND_CMD_NUM; i++) {
    if (sendCtrl[i].CmdID == CmdID) {
      sendCtrl[i].CmdID = 0;       // 清空命令ID
      sendCtrl[i].TransType = 0;   // 清空发送类型
      sendCtrl[i].send_period = 0; // 清空发送周期
      sendCtrl[i].DataLength = 0;  // 清空数据长度
      sendCtrl[i].Data = NULL;     // 清空数据指针
      return 1;                    // 删除成功
    }
  }
  return 0; // 删除失败
}

/*
 * @brief 将数据写入缓冲区
 *
 * @param sendCtrl 发送控制结构体指针
 * @param buffer 缓冲区指针
 * @param start_index 起始写入索引
 * @param buff_available_size 缓冲区剩余容量（用缓冲区总容量减去开始位置得到）
 * @return uint32_t 缓冲区写入结束位置索引
 */
static uint32_t write_data_to_buffer(ComSendCtrl_t *sendCtrl,
                                     unsigned char *buffer,
                                     uint32_t start_index,
                                     uint32_t buff_available_size) {
  if (buffer == NULL || sendCtrl == NULL || start_index < 0 ||
      buff_available_size <= 0) {
    return 0; // 参数无效
  }

  ComSendPacket_t packet;
  packet.ComHeader.SOF = COM_HEADER_SOF;              // 设置帧头
  packet.ComHeader.DataLength = sendCtrl->DataLength; // 设置数据长度
  packet.ComHeader.Seq = 0x01;                        // 设置包序号
  packet.ComHeader.CRC8 = Get_CRC8_Check_Sum((unsigned char *)(&packet),
                                             sizeof(packet.ComHeader) -
                                                 sizeof(packet.ComHeader.CRC8),
                                             CRC8_INIT); // 设置CRC8校验
  packet.CmdID = sendCtrl->CmdID;                        // 设置命令ID
  packet.Data = sendCtrl->Data;                          // 设置数据指针
  if (sizeof(packet.ComHeader) + sizeof(packet.CmdID) + sendCtrl->DataLength >
      buff_available_size) {
    return start_index; // 缓冲区剩余容量不足以存放新的数据包
  }
  memcpy(buffer + start_index, &packet.ComHeader,
         sizeof(packet.ComHeader) + sizeof(packet.CmdID)); // 将帧头写入缓冲区
  start_index +=
      sizeof(packet.ComHeader) + sizeof(packet.CmdID); // 更新缓冲区索引
  memcpy(buffer + start_index, packet.Data,
         sendCtrl->DataLength); // 将数据包写入缓冲区
  // packet.CRC16 = Get_CRC16_Check_Sum((rt_unsigned char*)(&packet),
  // sizeof(packet)-sizeof(packet.CRC16),CRC16_INIT); //
  // 这里不设置CRC16校验，数据部分和帧头部分不在一起，比较麻烦
  return start_index + sendCtrl->DataLength; // 返回写入的结束位置
}

/// <summary>
/// 在发送线程中调用该函数实现轮询发送命令包
/// </summary>
/// <param name="serial"></param>发送设备（串口句柄）
/// <param name="tx_buff"></param>发送缓冲区指针
/// <param name="tx_buff_size"></param>缓冲区大小
/// <param
/// name="start_tx_index"></param>开始发送字节距离缓冲区开始位置的偏移字节量
/// <returns></returns>返回成功发送的字节数
uint32_t Send_Cmd_Packet(rt_device_t serial, void *tx_buff,
                         uint32_t tx_buff_size, uint32_t start_tx_index) {
  if (serial == NULL || tx_buff == NULL || start_tx_index >= tx_buff_size) {
    return 0; // 如果设备句柄为空，发送缓冲区为空，开始发送偏移字节量大于缓冲区大小，返回失败
  }
  int run_times = Get_Device_Runcount(serial); // 找到运行次数计数器
  if (run_times < 0) {
    return 0; // 如果没有找到设备的话，直接返回
  }
  uint32_t send_size = 0; // 发送数据字节大小
  memset(((char *)tx_buff + start_tx_index), 0,
         tx_buff_size - start_tx_index); // 清空从开始位置往后的发送缓冲区为0
  int index =
      start_tx_index; // 当前发送缓冲区位置索引，索引位置设置为偏移字节量
  for (unsigned char i = 0; i < MAX_SEND_CMD_NUM; i++) // 遍历所有发送命令
  {
    if (sendCtrl[i].CmdID != 0 &&
        sendCtrl[i].serial ==
            serial) // 如果命令ID不为0，且命令ID被绑定的串口和此次运行的串口是一致的，表示该命令有效
    {
      if (sendCtrl[i].TransType == TRANS_FOREVER) {
        // 永久发送
        if (sendCtrl[i].send_period == 0 ||
            (sendCtrl[i].send_period > 0 &&
             (run_times % sendCtrl[i].send_period) == 0)) {
          // 发送数据包
          int index_new = write_data_to_buffer(&sendCtrl[i], tx_buff, index,
                                               tx_buff_size - index);
          if (index_new == index) {
            send_size += rt_device_write(
                serial, start_tx_index, tx_buff,
                index -
                    start_tx_index); // 如果发送缓冲区快满了，先发送数据包到设备
            index = start_tx_index; // 重置索引位置
            // 发送数据包
            int index_new = write_data_to_buffer(&sendCtrl[i], tx_buff, index,
                                                 tx_buff_size - index);
            if (index_new == index) {
              continue; // 这种情况一般是缓冲区设置太小，数据都无法放下，跳过该放不下的大数据包（也可能是因为缓冲区较小）
            }
            index = index_new; // 更新索引位置
          } else {
            index = index_new; // 更新索引位置
          }
        }
      } else if (sendCtrl[i].TransType > 0) {
        // 有限次发送
        if (sendCtrl[i].send_period == 0 ||
            (sendCtrl[i].send_period > 0 &&
             (run_times % sendCtrl[i].send_period) == 0)) {
          // 发送数据包
          int index_new = write_data_to_buffer(&sendCtrl[i], tx_buff, index,
                                               tx_buff_size - index);
          if (index_new == index) {
            send_size += rt_device_write(
                serial, start_tx_index, tx_buff,
                index -
                    start_tx_index); // 如果发送缓冲区快满了，先发送数据包到设备
            index = start_tx_index; // 重置索引位置
            // 发送数据包
            int index_new = write_data_to_buffer(&sendCtrl[i], tx_buff, index,
                                                 tx_buff_size - index);
            if (index_new == index) {
              return 0; // 这种情况一般是缓冲区设置太小，数据都无法放下，返回0表示发送失败
            }
            index = index_new; // 更新索引位置
          } else {
            index = index_new;       // 更新索引位置
            sendCtrl[i].TransType--; // 减少剩余发送次数
          }
        }
      }
    } else if (sendCtrl[i].CmdID == 0) {
      break; // 如果命令ID为0，表示后续没有有效命令，退出循环
    }
  }
  if (index > start_tx_index) // 如果缓冲区有数据需要发送
  {
    send_size += rt_device_write(serial, start_tx_index, tx_buff,
                                 index - start_tx_index); // 发送数据包到设备
  }
  return send_size;
}

//      *******************************接收部分*********************
/*
 * @brief 添加接收命令ID,设置回调解析数据函数和数据大小
 *
 * @param CmdID 命令ID
 * @param DataLength 数据长度
 * @param Data 数据指针（需要刷新到的数据的地址）
 * @param Callback_func 接收数据回调函数指针
 * @return unsigned char 返回1表示添加成功，0表示添加失败
 */
unsigned char Attach_Receive_callback(unsigned short CmdID,
                                      unsigned short DataLength, void *Data,
                                      ComReceiveCallback_func_p Callback_func) {
  for (unsigned char i = 0; i < MAX_RECEIVE_CMD_NUM; i++) {
    if (receiveCtrl[i].CmdID == CmdID) // 如果命令ID已存在，则更新
    {
      receiveCtrl[i].DataLength = DataLength;
      receiveCtrl[i].Data = Data;
      receiveCtrl[i].last_rec_time = rt_tick_get(); // 更新上次接收时间
      receiveCtrl[i].Callback_func = Callback_func;
      return 1;                           // 更新成功
    } else if (receiveCtrl[i].CmdID == 0) // 如果命令ID不存在，则添加新的命令
    {
      receiveCtrl[i].CmdID = CmdID;
      receiveCtrl[i].DataLength = DataLength;
      receiveCtrl[i].Data = Data;
      receiveCtrl[i].Callback_func = Callback_func;
      return 1; // 添加成功
    }
  }
  return 0; // 添加失败
}

/*
 * @brief 删除指定的接收命令ID
 *
 * @param CmdID 命令ID
 * @return unsigned char 返回1表示删除成功，0表示删除失败
 */
unsigned char Delete_Receive_callback(unsigned short CmdID) {
  for (unsigned char i = 0; i < MAX_RECEIVE_CMD_NUM; i++) {
    if (receiveCtrl[i].CmdID == CmdID) {
      receiveCtrl[i].CmdID = 0;            // 清空命令ID
      receiveCtrl[i].DataLength = 0;       // 清空数据长度
      receiveCtrl[i].Data = NULL;          // 清空数据指针
      receiveCtrl[i].Callback_func = NULL; // 清空回调函数指针
      return 1;                            // 删除成功
    }
  }
  return 0; // 删除失败
}

/*
 * @brief 处理接收到的数据包，根据命令ID调用相应的回调函数
 *
 * @param CmdID 命令ID
 * @param pData 数据指针
 * @return unsigned char
 * 返回1表示处理成功，0表示处理失败(未找到对应的命令ID或者回调函数)
 */
static unsigned char Process_Receive_Packet(unsigned short CmdID,
                                            unsigned char *pData) {
  for (unsigned char i = 0; i < MAX_RECEIVE_CMD_NUM; i++) {
    if (receiveCtrl[i].CmdID == CmdID) {
      // 调用注册的回调函数
      if (receiveCtrl[i].Data) {
        memcpy(
            receiveCtrl[i].Data, pData,
            receiveCtrl[i]
                .DataLength); // 将数据复制到接收控制结构体的Data指针指向的地址
        receiveCtrl[i].frequency =
            1000.0f /
            (rt_tick_get() - receiveCtrl[i].last_rec_time); // 计算接收频率
        receiveCtrl[i].last_rec_time = rt_tick_get(); // 更新上次接收时间
        if (receiveCtrl[i].Callback_func) // 如果回调函数已注册
        {
          receiveCtrl[i].Callback_func(
              receiveCtrl[i].Data); // 调用回调函数，传递数据指针
        } else {
          return -1; // 回调函数未注册，处理失败
        }
        return 1; // 处理成功
      } else {
        return -1; // 回调函数未注册，处理失败
      }
    } else if (receiveCtrl[i].CmdID == 0) {
      // 如果命令ID为0，表示后续没有有效命令，退出循环
      break;
    }
  }
  return 0; // 处理失败
}

/// <summary>
/// 该函数是用来解析接收到的数据至对应的注册地址中
/// </summary>
/// <param name="SrcData"></param>
/// <param name="srcSize"></param>
/// <returns></returns>
unsigned char Receive_Handle(unsigned char *SrcData, unsigned short srcSize) {
  int data_pocket_num = 0; // 正常解析的数据包个数
  // 指向帧头
  unsigned char *pSrcData = SrcData;
  unsigned char *const pSrcData_const = SrcData;
  int8_t ret_error = 0;             // 接收数据包解析错误标志,
  ComReceivePacket_t pPacket = {0}; // 指向接收数据包的指针
  while (pSrcData - pSrcData_const < srcSize) {
    // 处理接收到的数据包
    if (*pSrcData != COM_HEADER_SOF) // 如果不是帧头，跳过
    {
      pSrcData++;
      continue;
    } else if (*pSrcData == COM_HEADER_SOF) {
      if (pSrcData - pSrcData_const + sizeof(pPacket.ComHeader) >
          srcSize) // 如果剩余数据不足以解析一个完整的数据包头
      {
        printf("Receive_Handle 1\n");
        return data_pocket_num; // 返回0表示数据不完整，无法处理
      }
      pSrcData += sizeof(pPacket.ComHeader.SOF); // 跳过帧头
      pPacket.ComHeader.SOF = COM_HEADER_SOF;    // 设置帧头
      pPacket.ComHeader.DataLength =
          *(unsigned short *)pSrcData;                     // 获取数据长度
      pSrcData += sizeof(pPacket.ComHeader.DataLength);    // 跳过数据长度
      pPacket.ComHeader.Seq = *(unsigned char *)pSrcData;  // 获取包序号
      pSrcData += sizeof(pPacket.ComHeader.Seq);           // 跳过包序号
      pPacket.ComHeader.CRC8 = *(unsigned char *)pSrcData; // 获取CRC8校验
      pSrcData += sizeof(pPacket.ComHeader.CRC8);          // 跳过CRC8校验
      if (pPacket.ComHeader.DataLength >
          srcSize -
              (pSrcData -
               pSrcData_const)) // 如果剩余数据不足以解析除了帧头以外的数据，那就返回，不做粘包处理
      {
        printf("Receive_Handle 2\n");
        return data_pocket_num; // 返回0表示数据不完整，无法处理
      }
      if (Get_CRC8_Check_Sum((unsigned char *)&pPacket,
                             sizeof(pPacket.ComHeader) -
                                 sizeof(pPacket.ComHeader.CRC8),
                             CRC8_INIT) != pPacket.ComHeader.CRC8) {
        ret_error = -1;        // 返回-1表示校验错误
        receive_fault_count++; // 增加故障计数
        pSrcData = pSrcData - sizeof(pPacket.ComHeader) +
                   sizeof(pPacket.ComHeader.SOF); // 回退到帧头之后继续寻找帧头
        continue;                                 // 继续往后寻找帧头
      } else                                      // 校验通过
      {
        // 处理接收到的数据包
        pPacket.CmdID = *(unsigned short *)pSrcData; // 获取命令ID
        pSrcData += sizeof(pPacket.CmdID);           // 跳过命令ID
        ret_error = Process_Receive_Packet(pPacket.CmdID, pSrcData);
        pSrcData += pPacket.ComHeader.DataLength; // 跳过数据部分
        data_pocket_num++;
      }
    }
  }
  return data_pocket_num; // 返回1表示处理成功
}

/*
 * @brief 处理接收到的数据包，根据命令ID调用相应的回调函数，并返回消耗的字节数
 *
 * @param CmdID 命令ID
 * @param pData 数据指针
 * @return int
 * 返回消耗的字节数
 */
int Receive_Handle_Consumed(unsigned char *SrcData, unsigned short srcSize) {
  // 指向当前正在查看的字节
  unsigned char *pSrcData = SrcData;
  // 记录缓冲区的起始位置，用于计算偏移量
  unsigned char *const pSrcData_const = SrcData;
  
  ComReceivePacket_t pPacket = {0}; 

  while (pSrcData - pSrcData_const < srcSize) {
    
    // 1. 寻找帧头 (SOF)
    if (*pSrcData != COM_HEADER_SOF) {
      pSrcData++;
      continue;
    } 
    
    // 标记帧头位置
    unsigned char *pCurrentSOF = pSrcData;

    // 2. 检查：剩余数据是否足够解析“包头”
    // 包头大小 = SOF + Len + Seq + CRC8
    if (srcSize - (pCurrentSOF - pSrcData_const) < sizeof(pPacket.ComHeader)) {
      // 这里的 printf 建议在生产环境去掉，否则会刷屏
      // printf("Receive_Handle 1: Partial Header\n");
      
      // 返回当前 SOF 之前的字节数，保留从 SOF 开始的所有数据
      return pCurrentSOF - pSrcData_const; 
    }

    // --- 开始预解析头部 (使用临时指针 pTemp，不移动主指针 pSrcData，方便回退) ---
    unsigned char *pTemp = pCurrentSOF;
    pTemp += sizeof(pPacket.ComHeader.SOF); // 跳过SOF
    
    // 获取数据长度
    unsigned short dataLen = *(unsigned short *)pTemp;
    pTemp += sizeof(dataLen);

    // 3. 检查：剩余数据是否足够解析“完整包” (头 + 体)
    int packetTotalSize = sizeof(pPacket.ComHeader) + dataLen;
    
    // 增加安全性检查：如果长度非法（比如超大），直接视为错包
    if (dataLen > 1024) { 
         // 长度异常，这不是一个正经包。
         // 移动主指针到 SOF 后面，重新找。
         pSrcData++; 
         continue;
    }

    if (srcSize - (pCurrentSOF - pSrcData_const) < packetTotalSize) {
      // printf("Receive_Handle 2: Partial Body\n");
      // 数据不够，是半包。保留从 SOF 开始的所有数据。
      return pCurrentSOF - pSrcData_const; 
    }

    pPacket.ComHeader.SOF = COM_HEADER_SOF;
    pPacket.ComHeader.DataLength = dataLen;
    pPacket.ComHeader.Seq = *pTemp++;
    pPacket.ComHeader.CRC8 = *pTemp++;

    // 4. CRC 校验
    if (Get_CRC8_Check_Sum((unsigned char *)&pPacket,
                           sizeof(pPacket.ComHeader) - sizeof(pPacket.ComHeader.CRC8),
                           CRC8_INIT) != pPacket.ComHeader.CRC8) {
      // 校验失败！说明这个 SOF 是假的，或者是坏包。
      // 我们只消耗这一个 SOF 字节，继续往后找。
      // 这样可以防止“错位”导致的漏包。
      pSrcData = pCurrentSOF + 1; 
      continue;
    } 
    
    // 5. 校验成功，处理业务
    {
       // pTemp 现在正好指向 Header 之后的数据 (Payload)
       // 注意：CmdID 是 Payload 的前两个字节
       pPacket.CmdID = *(unsigned short *)pTemp;
       
       // 跳过 CmdID (2字节) 指向实际参数
       unsigned char *pPayloadParams = pTemp + sizeof(pPacket.CmdID);
       
       Process_Receive_Packet(pPacket.CmdID, pPayloadParams);
       
       // 成功处理完一个包。
       // 将主指针 pSrcData 直接移到这个包的末尾，准备找下一个包
       pSrcData = pCurrentSOF + packetTotalSize;
    }
  }

  // 循环结束（缓冲区遍历完了），返回总共消耗的字节数
  return pSrcData - pSrcData_const;
}