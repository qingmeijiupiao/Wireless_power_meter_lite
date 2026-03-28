/*
 * @version: 2.0
 * @LastEditors: qingmeijiupiao
 * @Description: HXC ESP32 twai封装类
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-03-28 16:12:39
 */
#ifndef HXC_TWAI_HPP
#define HXC_TWAI_HPP
#include <cstring>
#include <map>
#include <functional>
#include "esp_twai.h"
//CAN消息结构体
struct HXC_CAN_message_t{
  bool extd=0;           /**< 扩展帧格式标志（29位ID） */
  bool rtr=0;            /**< 远程帧标志 */
  bool self=0;           /**< 自我接收请求。接收时无效。 */
  uint32_t identifier;                /**< 11或29位标识符 */
  uint8_t data_length_code;           /**< 数据长度代码 */
  uint8_t data[8];    /**< 数据字节（在RTR帧中无关） */
};

//CAN速率枚举
enum CAN_RATE{
  CAN_RATE_1MBIT,
  CAN_RATE_800KBIT,
  CAN_RATE_500KBIT,
  CAN_RATE_250KBIT,
  CAN_RATE_125KBIT,
  CAN_RATE_100KBIT
};

//CAN消息接收回调函数
using HXC_can_feedback_func= std::function<void(HXC_CAN_message_t* can_message)>;
// TWAI封装类
class HXC_TWAI {
public:
    // 防止值传递TWAI对象
    HXC_TWAI(const HXC_TWAI&) = delete;               /**< 删除拷贝构造函数 */
    HXC_TWAI& operator=(const HXC_TWAI&) = delete;    /**< 删除拷贝赋值函数 */
    HXC_TWAI(HXC_TWAI&&) = delete;                    /**< 删除移动构造函数 */
    HXC_TWAI& operator=(HXC_TWAI&&) = delete;         /**< 删除移动赋值函数 */

    /**
     * @description: TWAI构造函数, 默认为HXC开发板A的引脚
     * @return {*}
     * @Author: qingmeijiupiao
     * @param {uint8_t} tx 连接can收发芯片TX引脚的IO号
     * @param {uint8_t} rx 连接can收发芯片RX引脚的IO号
     * @param {CAN_RATE} rate CAN速率
     */
    HXC_TWAI(uint8_t tx = 8, uint8_t rx = 18, CAN_RATE rate = CAN_RATE_1MBIT);

    /**
     * @description: 析构函数
     */
    ~HXC_TWAI();

    /**
     * @description: TWAI初始化
     * @param {twai_mode_t} twai_mode TWAI模式默认为
     * TWAI_MODE_NORMAL 该模式下如果CAN总线无设备时发送消息函数会阻塞 
     * TWAI_MODE_NO_ACK 该模式下如果CAN总线发送消息不会检测ACK
     * TWAI_MODE_LISTEN_ONLY 该模式下只能接收消息
     * @return {esp_err_t} 成功返回ESP_OK
     * @Author: qingmeijiupiao
     */
    esp_err_t setup(twai_mode_t twai_mode=TWAI_MODE_NORMAL);

    /**
     * @description: 发送CAN消息
     * @return {esp_err_t}
     * @Author: qingmeijiupiao
     * @param {HXC_CAN_message_t*} message CAN消息
     */
    esp_err_t send(HXC_CAN_message_t* message);

    /**
     * @description: 发送CAN消息
     * @return {esp_err_t}
     * @Author: qingmeijiupiao
     * @param {HXC_CAN_message_t} message CAN消息
     */
    esp_err_t send(HXC_CAN_message_t message);

    /**
     * @description: 添加CAN消息接收回调,收到对应地址的消息时运行回调函数
     * @return {*}
     * @Author: qingmeijiupiao
     * @param {int} addr CAN消息地址
     * @param {HXC_can_feedback_func} func 回调函数
     */
    void add_can_receive_callback_func(int addr, HXC_can_feedback_func func);

    /**
     * @description: 移除CAN消息接收回调函数
     * @return {*}
     * @Author: qingmeijiupiao
     * @param {int} addr CAN消息地址
     */
    void remove_can_receive_callback_func(int addr);

    /**
     * @description: 停止接收CAN消息
     * @return {*}
     * @Author: qingmeijiupiao
     */
    void stop_receive();

    /**
     * @description: 停止之后使用该函数恢复接收CAN消息
     * @return {*}
     * @Author: qingmeijiupiao
     */
    void resume_receive();

    /**
     * @description: 判断此时是否在持续接收CAN消息
     * @return {bool} true:持续接收CAN消息 false:停止接收CAN消息
     * @Author: qingmeijiupiao
     */
    bool get_receive_status();

    /**
     * @description: 获取初始化状态
     * @return {bool} true:已初始化 false:未初始化
     * @Author: qingmeijiupiao
     */
    bool get_setup_flag();

protected:
    /**
     * @description: 判断CAN消息接收回调函数是否存在
     * @return {bool} true:存在 false:不存在
     * @Author: qingmeijiupiao
     * @param {int} addr CAN消息地址
     */
    bool exist_can_receive_callback_func(int addr);

    TaskHandle_t twai_fb_handle = nullptr;  /**< TWAI反馈任务句柄 */
    uint8_t TX_PIN, RX_PIN;  /**< 连接CAN收发芯片的TX和RX引脚IO号 */
    HXC_CAN_message_t RX_message_buf;  /**< 接收CAN消息缓存 */
    CAN_RATE can_rate;  /**< CAN速率 */
    bool is_setup = false;  /**< 初始化标志 */  
    std::map<int, HXC_can_feedback_func>* func_map=nullptr;  /**< 回调函数映射表 */
    /**
     * @description: TWAI接受数据的线程，接收到数据后转换成HXC_CAN_message，然后调用回调函数
     */
    static void twai_feedback_update_task(void* n);
};
#endif
/*
                                              .=%@#=.                                               
                                            -*@@@@@@@#=.                                            
                                         .+%@@@@@@@@@@@@#=                                          
                                       -#@@@@@@@* =@@@@@@@@*:                                       
                                     =%@@@@@@@@=   -@@@@@@@@@#-                                     
                                  .+@@@@@@@@@@-     .@@@@@@@@@@%=                                   
                                .+@@@@@@@@@@@@-     +@@@@@@@@@@@@@+.                                
                               +@@@@@@@@@@@@@@@    .@@@@@@@@@@@@@@@@+.                              
                             =@@@@@@@@@@@@@@@%-     =%@@%@@@@@@@@@@@@@=                             
                           -%@@@@@@@@@@@@+..     .       -@@@@@@@@@@@@@%-                           
                         .#@@@@@@@@@@@@@#       -@+       +@@@@@@@@@@@@@@#:                         
                        +@@@@@@@@@@@@@@@@+     +@@@+     =@@@@@@@@@@@@@@@@@+                        
                      :%@@@@@@@@@@@@@@@@@+    *@@@@*     =@@@@@@@@@@@@@@@@@@%-                      
                     +@@@@@@@@@@@@@@#+*+-   .#@@@@+       :+*+*@@@@@@@@@@@@@@@*                     
                   :%@@@@@@@@@@@@@@+       :%@@@@-    .-       -@@@@@@@@@@@@@@@%:                   
                  =@@@@@@@@@@@@@@@@-      -@@@@%:    .%@+      =@@@@@@@@@@@@@@@@@=                  
                 *@@@@@@@@@@@@@@@@@@.    =@@@@#.    -@@@@+    =@@@@@@@@@@@@@@@@@@@#                 
               .%@@@@@@@@@@@@@@@@@@+    +@@@@*     =@@@@%:    .#@@@@@@@@@@@@@@@@@@@%.               
              :@@@@@@@@@@@@@@@%:::.    #@@@@+     +@@@@#        .::.*@@@@@@@@@@@@@@@@-              
             -@@@@@@@@@@@@@@@%       .%@@@@=     *@@@@*     +-       *@@@@@@@@@@@@@@@@=             
            =@@@@@@@@@@@@@@@@@#.    -@@@@@-    :%@@@@=    .#@@+     +@@@@@@@@@@@@@@@@@@=            
           =@@@@@@@@@@@@@@@@@@@:    =====.     -+===:     :====     @@@@@@@@@@@@@@@@@@@@+           
          +@@@@@@@@@@@@@@@#%%#-                                     :*%%#%@@@@@@@@@@@@@@@+          
         =@@@@@@@@@@@@@@%.       ...........................              *@@@@@@@@@@@@@@@=         
        =@@@@@@@@@@@@@@@+      .#@@@@@@@@@@@@@@@@@@@@@@@@@@#     .*:      =@@@@@@@@@@@@@@@@-        
       -@@@@@@@@@@@@@@@@@=    .%@@@@@@@@@@@@@@@@@@@@@@@@@@#     :@@@-    =@@@@@@@@@@@@@@@@@@:       
      :@@@@@@@@@@@@@@@@@%.   -@@@@%+=====================:     -@@@@%    :%@@@@@@@@@@@@@@@@@@.      
      %@@@@@@@@@@@@@=-=:    =@@@@#.                           +@@@@#.      -=--%@@@@@@@@@@@@@%      
     #@@@@@@@@@@@@@:       +@@@@*      ............. .       *@@@@*             %@@@@@@@@@@@@@+     
    =@@@@@@@@@@@@@@#.     #@@@@+     +@@@@@@@@@@@@@@@#.    .#@@@@+     +#.     +@@@@@@@@@@@@@@@:    
   .@@@@@@@@@@@@@@@@-   .%@@@@=     *@@@@@@@@@@@@@@@#     :%@@@@-     *@@%:    @@@@@@@@@@@@@@@@%    
   %@@@@@@@@@@@%%%#=   :@@@@@:    .#@@@@+-----------     -@@@@@:     #@@@@=    :#%%%@@@@@@@@@@@@*   
  =@@@@@@@@@@@=       -@@@@%.    :%@@@@-                =@@@@%.    .%@@@@=          :%@@@@@@@@@@@:  
  @@@@@@@@@@@%.      =@@@@#     -@@@@%:    .:::-:      +@@@@#     :@@@@@:    .       +@@@@@@@@@@@#  
 +@@@@@@@@@@@@@.    *@@@@*     =@@@@#.    -@@@@@:     #@@@@+     =@@@@%.    -@#     +@@@@@@@@@@@@@- 
.@@@@@@@@@@@@@#    *@%@%=     +@@@@*     =@@@@#.    .#@@@%=     +@@@@#     =@@@%.   =@@@@@@@@@@@@@% 
+@@@@@@@@*-==-                .          .           . ..       .....      .....     .=+=+@@@@@@@@@-
%@@@@@@@+                                                                                 -@@@@@@@@#
@@@@@@@-       =#%#=     -#%%#-     -#%%*.     +%%%*.    .*%%#=     :#%%#-     =%%%*.      .#@@@@@@@
@@@@@@=.::::::*@@@@@*:::-@@@@@@-:::=@@@@@%::::*@@@@@#::::%@@@@@+:---@@@@@@=---+@@@@@%------:=@@@@@@@
=@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@+
 *@@@@@@@@@@@@@@@@@@@@@@@@@@@%%##**++===----:::::------===++***##%%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@* 
  -#@@@@@@@@@@@@@@@@%#*+=-:.                                        ..::-=+*##%@@@@@@@@@@@@@@@@@#-  
    :=*%@@@@@%#*=-:                                                             .:-=+*#%%%%##+-.    
                                                                                        
        K####      #####     ###    ###  ######.   ##########     K##    ### ###    ##W    ####W    
       #######    #######    ###    ###  ########  ##########     ###    ### ###   ###   W######    
      W###G####  ###W ####   ###    ###  ######### ##########     ###    ###  ##   ###   ###W####   
      ###   ###  ###   ###   ###    ##  ###    ###    ###         ###    ###  ### t##   ###   ###   
     G##    #   ###    ###   ##     ##  ###    ###    ###         ###    ###  ### ###   ##W         
     ###        ###    ###   ##    ###  ###    ###    ###         ##L    ##   ### ##   ###          
     ###        ###    ###  K##    ###  ###    ###    ###         ##     ##    #####   ###          
     ###       ,##     ###  ###    ###  ###   ###,    ##         G##    ###    ####    ###          
    W##        ###     ###  ###    ###  #########     ##         ##########    ####    ###          
    ###        ###     ###  ###    ###  ########     ###         ##########    ###i   K##           
    ###        ###     ###  ###    ##  #######       ###         ###    ###    ####   ###           
    ###        ###     ###  ##     ##  ###           ###         ###    ###   ##W##   ###           
    ###        ###     ##i  ##    ###  ###           ###         ###    ##    ## ##   ###           
    ###        ###    ###  ,##    ###  ###           ###         ##     ##   ### ##   ###           
    ###    ### ###    ###  K##    ###  ###           ##         t##    ###   ##  ###  ###    ###    
    ###   G##i ###   ###   .##   ###.  ##t           ##         ###    ###  ###  ###  W##,   ###    
     ########  W##W#####    ########   ##           ###         ###    ###  ##    ##   ####W###     
     #######    #######     #######   ###           ###         ###    ### ###    ##.  #######      
      #####      #####       #####    ###           ###         ###    ### ##W    ###   #####       
                   ###                                                                              
                   ###                                                                              
                   #####                                                                            
                    ####                                                                            
                      K                                                                             
*/