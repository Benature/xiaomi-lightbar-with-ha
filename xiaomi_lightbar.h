#pragma once

#include "esphome.h"

#define NRF_CMD_W_REGISTER 0x20
#define NRF_CMD_W_TX_PAYLOAD 0xA0
#define NRF_CMD_FLUSH_TX 0xE1
#define NRF_CMD_FLUSH_RX 0xE2
#define NRF_CMD_R_RX_PAYLOAD 0x61
#define NRF_REG_CONFIG 0x00
#define NRF_REG_EN_AA 0x01
#define NRF_REG_SETUP_AW 0x03
#define NRF_REG_SETUP_RETR 0x04
#define NRF_REG_RF_CH 0x05
#define NRF_REG_RF_SETUP 0x06
#define NRF_REG_STATUS 0x07
#define NRF_REG_RX_ADDR_P1 0x0B
#define NRF_REG_TX_ADDR 0x10
#define NRF_REG_RX_PW_P1 0x12

class XiaomiLightbar {
private:
  uint8_t ce_pin;
  uint8_t csn_pin;
  uint8_t sck_pin;
  uint8_t mosi_pin;
  uint8_t miso_pin;
  uint32_t remote_id;
  uint8_t tx_counter = 0;
  uint8_t last_rx_counter = 0xFF;
  float cold_val = 0.0f;
  float warm_val = 0.0f;
  int current_brightness = -1;
  int current_temp = -1;
  bool sync_flag = false;
  bool is_on = false; // 跟踪挂灯物理开关状态

  uint8_t spi_transfer(uint8_t data) {
    uint8_t in = 0;
    for (int i = 0; i < 8; i++) {
      digitalWrite(mosi_pin, (data & 0x80) ? HIGH : LOW);
      data <<= 1;
      digitalWrite(sck_pin, HIGH);
      delayMicroseconds(1);
      in = (in << 1) | (digitalRead(miso_pin) ? 1 : 0);
      digitalWrite(sck_pin, LOW);
      delayMicroseconds(1);
    }
    return in;
  }

  void write_register(uint8_t reg, uint8_t value) {
    digitalWrite(csn_pin, LOW);
    spi_transfer(NRF_CMD_W_REGISTER | (reg & 0x1F));
    spi_transfer(value);
    digitalWrite(csn_pin, HIGH);
  }

  uint8_t read_register(uint8_t reg) {
    digitalWrite(csn_pin, LOW);
    spi_transfer(reg & 0x1F);
    uint8_t val = spi_transfer(0xFF);
    digitalWrite(csn_pin, HIGH);
    return val;
  }

  void write_register_bytes(uint8_t reg, const uint8_t *data, size_t len) {
    digitalWrite(csn_pin, LOW);
    spi_transfer(NRF_CMD_W_REGISTER | (reg & 0x1F));
    for (size_t i = 0; i < len; i++)
      spi_transfer(data[i]);
    digitalWrite(csn_pin, HIGH);
  }

  void write_payload(const uint8_t *data, size_t len) {
    digitalWrite(csn_pin, LOW);
    spi_transfer(NRF_CMD_W_TX_PAYLOAD);
    for (size_t i = 0; i < len; i++)
      spi_transfer(data[i]);
    digitalWrite(csn_pin, HIGH);
  }

  void read_payload(uint8_t *data, size_t len) {
    digitalWrite(csn_pin, LOW);
    spi_transfer(NRF_CMD_R_RX_PAYLOAD);
    for (size_t i = 0; i < len; i++)
      data[i] = spi_transfer(0xFF);
    digitalWrite(csn_pin, HIGH);
  }

  uint16_t compute_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFE;
    for (size_t i = 0; i < len; i++) {
      crc ^= (uint16_t)data[i] << 8;
      for (int j = 0; j < 8; j++) {
        if (crc & 0x8000)
          crc = ((crc << 1) ^ 0x1021) & 0xFFFF;
        else
          crc = (crc << 1) & 0xFFFF;
      }
    }
    return crc;
  }

  void enter_rx_mode(uint8_t ch = 6) {
    digitalWrite(ce_pin, LOW);
    write_register(NRF_REG_RF_CH, ch);
    write_register(NRF_REG_CONFIG, 0x03); // PWR_UP=1, PRIM_RX=1
    digitalWrite(ce_pin, HIGH);           // 开启监听
  }

  void send_packet(uint16_t command) {
    digitalWrite(ce_pin, LOW);
    write_register(NRF_REG_CONFIG, 0x02); // 切换为发射模式

    uint8_t pkt[17];
    pkt[0] = 0x53;
    pkt[1] = 0x39;
    pkt[2] = 0x14;
    pkt[3] = 0xDD;
    pkt[4] = 0x1C;
    pkt[5] = 0x49;
    pkt[6] = 0x34;
    pkt[7] = 0x12;
    pkt[8] = (remote_id >> 16) & 0xFF;
    pkt[9] = (remote_id >> 8) & 0xFF;
    pkt[10] = remote_id & 0xFF;
    pkt[11] = 0xFF;
    pkt[12] = tx_counter++;
    pkt[13] = (command >> 8) & 0xFF;
    pkt[14] = command & 0xFF;
    uint16_t crc = compute_crc16(pkt, 15);
    pkt[15] = (crc >> 8) & 0xFF;
    pkt[16] = crc & 0xFF;

    for (int i = 0; i < 20; i++) {
      digitalWrite(csn_pin, LOW);
      spi_transfer(NRF_CMD_FLUSH_TX);
      digitalWrite(csn_pin, HIGH);
      write_register(NRF_REG_STATUS, 0x70);
      write_payload(pkt, 17);
      digitalWrite(ce_pin, HIGH);
      delayMicroseconds(15);
      digitalWrite(ce_pin, LOW);
      delay(10);
    }

    // 发送完毕立即恢复监听
    enter_rx_mode();
  }

public:
  XiaomiLightbar(uint8_t ce, uint8_t csn, uint8_t sck, uint8_t mosi,
                 uint8_t miso, uint32_t id)
      : ce_pin(ce), csn_pin(csn), sck_pin(sck), mosi_pin(mosi), miso_pin(miso),
        remote_id(id) {}

  void begin() {
    pinMode(ce_pin, OUTPUT);
    pinMode(csn_pin, OUTPUT);
    pinMode(sck_pin, OUTPUT);
    pinMode(mosi_pin, OUTPUT);
    pinMode(miso_pin, INPUT);

    digitalWrite(ce_pin, LOW);
    digitalWrite(csn_pin, HIGH);
    digitalWrite(sck_pin, LOW);
    digitalWrite(mosi_pin, LOW);

    delay(100);

    write_register(NRF_REG_EN_AA, 0x00);
    write_register(0x02, 0x02);             // 启用通道 1
    write_register(NRF_REG_SETUP_AW, 0x03); // 5 字节地址
    write_register(NRF_REG_SETUP_RETR, 0x00);
    write_register(NRF_REG_RF_CH, 6);       // 默认信道 6
    write_register(NRF_REG_RF_SETUP, 0x08); // 2Mbps

    uint8_t sync_addr[5] = {0x55, 0x55, 0x55, 0x55, 0x55};
    write_register_bytes(NRF_REG_TX_ADDR, sync_addr, 5);

    uint8_t rx_addr[5] = {0x1C, 0xDD, 0x14, 0x39, 0x53};
    write_register_bytes(NRF_REG_RX_ADDR_P1, rx_addr, 5);
    write_register(NRF_REG_RX_PW_P1, 12);

    enter_rx_mode();
    ESP_LOGI("xiaomi_lightbar", "初始化就绪 (Remote ID: 0x%06X)", (unsigned int)remote_id);
  }

  // 方法 1：配对对码指令（0x0600 长按重置）
  void pair() {
    send_packet(0x0600);
    ESP_LOGI("xiaomi_lightbar", "已发送配对/重置广播指令 (0x0600)");
  }

  // 方法 2：嗅探捕获实体遥控器真实 ID
  void start_sniffing() {
    ESP_LOGW("sniffer", "========================================");
    ESP_LOGW("sniffer",
             "🔍 嗅探已启动！请在 20 秒内连续快速旋转/按压实体遥控器...");
    ESP_LOGW("sniffer", "========================================");

    const uint8_t channels[] = {6, 15, 43, 68};
    int ch_idx = 0;
    enter_rx_mode(channels[ch_idx]);

    uint32_t start_ms = millis();
    uint32_t last_hop = millis();
    bool found = false;

    while (millis() - start_ms < 20000) {
      uint8_t status = read_register(NRF_REG_STATUS);
      if (status & 0x40) {
        uint8_t raw[12];
        read_payload(raw, 12);
        write_register(NRF_REG_STATUS, 0x40);

        digitalWrite(csn_pin, LOW);
        spi_transfer(NRF_CMD_FLUSH_RX);
        digitalWrite(csn_pin, HIGH);

        for (int sep_pos = 3; sep_pos <= 6; sep_pos++) {
          if (raw[sep_pos] == 0xFF && (sep_pos + 5 <= 11)) {
            uint32_t cand_id = ((uint32_t)raw[sep_pos - 3] << 16) |
                               ((uint32_t)raw[sep_pos - 2] << 8) |
                               raw[sep_pos - 1];
            uint8_t cnt = raw[sep_pos + 1];
            uint16_t cmd = ((uint16_t)raw[sep_pos + 2] << 8) | raw[sep_pos + 3];
            uint16_t crc = ((uint16_t)raw[sep_pos + 4] << 8) | raw[sep_pos + 5];

            uint8_t chk[15] = {0x53,
                               0x39,
                               0x14,
                               0xDD,
                               0x1C,
                               0x49,
                               0x34,
                               0x12,
                               (uint8_t)((cand_id >> 16) & 0xFF),
                               (uint8_t)((cand_id >> 8) & 0xFF),
                               (uint8_t)(cand_id & 0xFF),
                               0xFF,
                               cnt,
                               (uint8_t)((cmd >> 8) & 0xFF),
                               (uint8_t)(cmd & 0xFF)};

            if (compute_crc16(chk, 15) == crc) {
              ESP_LOGW("sniffer", "****************************************");
              ESP_LOGW("sniffer", "🎉 成功捕获实体遥控器！");
              ESP_LOGW("sniffer", "🎯 你的真实 Remote ID: 0x%06X", (unsigned int)cand_id);
              ESP_LOGW("sniffer", "****************************************");
              found = true;
              break;
            }
          }
        }
        if (found)
          break;
      }

      // 每 150ms 轮换信道
      if (millis() - last_hop > 150) {
        last_hop = millis();
        ch_idx = (ch_idx + 1) % 4;
        enter_rx_mode(channels[ch_idx]);
      }
      delay(3);
    }

    if (!found) {
      ESP_LOGW("sniffer", "⚠️ 未捕获到信号，请检查 MISO(GPIO19) 接线并重试。");
    }

    enter_rx_mode(6); // 恢复信道 6 常驻监听
  }

  void set_sync_flag(bool val) { sync_flag = val; }

  // 后台监听旋钮按键
  uint16_t poll_remote() {
    uint8_t status = read_register(NRF_REG_STATUS);
    if (!(status & 0x40))
      return 0;

    uint8_t raw[12];
    read_payload(raw, 12);
    write_register(NRF_REG_STATUS, 0x40);

    digitalWrite(csn_pin, LOW);
    spi_transfer(NRF_CMD_FLUSH_RX);
    digitalWrite(csn_pin, HIGH);

    for (int sep_pos = 3; sep_pos <= 6; sep_pos++) {
      if (raw[sep_pos] == 0xFF && (sep_pos + 5 <= 11)) {
        uint32_t cand_id = ((uint32_t)raw[sep_pos - 3] << 16) |
                           ((uint32_t)raw[sep_pos - 2] << 8) | raw[sep_pos - 1];
        if (cand_id != remote_id)
          continue;

        uint8_t cnt = raw[sep_pos + 1];
        uint16_t cmd = ((uint16_t)raw[sep_pos + 2] << 8) | raw[sep_pos + 3];
        uint16_t crc = ((uint16_t)raw[sep_pos + 4] << 8) | raw[sep_pos + 5];

        uint8_t chk[15] = {0x53,
                           0x39,
                           0x14,
                           0xDD,
                           0x1C,
                           0x49,
                           0x34,
                           0x12,
                           (uint8_t)((cand_id >> 16) & 0xFF),
                           (uint8_t)((cand_id >> 8) & 0xFF),
                           (uint8_t)(cand_id & 0xFF),
                           0xFF,
                           cnt,
                           (uint8_t)((cmd >> 8) & 0xFF),
                           (uint8_t)(cmd & 0xFF)};

        if (compute_crc16(chk, 15) == crc) {
          if (cnt == last_rx_counter)
            return 0;
          last_rx_counter = cnt;
          tx_counter = cnt + 1; // 保持发送计数器递增超前，避免计数冲突
          return cmd;
        }
      }
    }
    return 0;
  }

  void set_c(float c) { cold_val = c; }
  void set_w(float w) { warm_val = w; }
  bool get_is_on() const { return is_on; }
  void set_is_on(bool val) { is_on = val; }

  // 手动/实体翻转电源状态
  void toggle_power() {
    send_packet(0x0100);
    is_on = !is_on;
    sync_flag = true;
    if (!is_on) {
      current_brightness = -1;
      current_temp = -1;
    }
    ESP_LOGI("xiaomi_lightbar", "翻转挂灯电源状态，当前 is_on: %d", is_on);
  }

  void apply() {
    // 若由实体旋钮按键触发 HA 状态同步，挂灯硬件本身已动作，无需重复发包
    if (sync_flag) {
      sync_flag = false;
      is_on = (cold_val + warm_val > 0.005f);
      if (!is_on) {
        current_brightness = -1;
        current_temp = -1;
      }
      return;
    }

    float total = cold_val + warm_val;
    bool target_on = (total > 0.005f);

    // 1. 关灯逻辑：当前处于开启状态，但目标要关灯
    if (!target_on) {
      if (is_on) {
        ESP_LOGI("xiaomi_lightbar", "执行关灯 (发送 0x0100)");
        send_packet(0x0100);
        is_on = false;
        current_brightness = -1;
        current_temp = -1;
      }
      return;
    }

    // 2. 开灯逻辑：当前处于关闭状态，目标是要开灯
    if (!is_on) {
      ESP_LOGI("xiaomi_lightbar", "执行开灯唤醒 (发送 0x0100)");
      send_packet(0x0100);
      is_on = true;
      delay(80); // 等待挂灯单片机完成开机唤醒
    }

    // 3. 计算目标亮度与色温档位 (0 - 15)
    int b_step = round(std::min(1.0f, total) * 15.0f);
    float c_ratio = cold_val / total;
    int t_step = round(c_ratio * 15.0f);

    // 4. 亮度调节 (先打底 0x04F0，再必发 0x0400 + b_step 激活)
    if (b_step != current_brightness) {
      send_packet(0x04F0);
      delay(30);
      send_packet(0x0400 + b_step);
      current_brightness = b_step;
    }

    // 5. 色温调节 (先打底 0x02F0，再必发 0x0200 + t_step 激活)
    if (t_step != current_temp) {
      send_packet(0x02F0);
      delay(30);
      send_packet(0x0200 + t_step);
      current_temp = t_step;
    }
  }
};
// ⚠️ 请确认填入你抓到的真实 Remote ID：
inline XiaomiLightbar &get_lightbar() {
  // 0xABCDEF 替换为真实 Remote ID
  static XiaomiLightbar bar(4, 5, 18, 23, 19, 0xABCDEF);
  return bar;
}
