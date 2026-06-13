// pages/control/control.js
Page({

  /**
   * 页面的初始数据
   */
  data: {
    power: 1,
    brightness: 80,
    mode: 'breathing',
    speed: 3,
    connected: false,
    topicControl: 'esp01s/led/control',
    topicStatus: 'esp01s/led/status'
  },

  /**
   * 生命周期函数--监听页面加载
   */
  onLoad(options) {
    const app = getApp();

    // 从 globalData 同步初始状态
    const state = app.globalData.ledState;
    this.setData({
      power: state.power,
      brightness: state.brightness,
      mode: state.mode,
      speed: state.speed,
      connected: app.globalData.mqttConnected
    });

    // 注册消息回调（不注册会遗漏设备上报）
    // 注意：用 bind(this) 绑定页面上下文，否则回调中 this.setData 会失效
    this._onConnectChange = this._onConnectChange.bind(this);
    this._onDeviceStatus = this._onDeviceStatus.bind(this);
    app.registerMessageCallback('__connect__', this._onConnectChange);
    app.registerMessageCallback(app.globalData.topicStatus, this._onDeviceStatus);

    // 确保全局 MQTT 已连接（幂等，不会重复创建 socket）
    app.ensureMqttConnected();

    // 连接后请求一次设备状态
    if (app.globalData.mqttConnected) {
      this.requestStatus();
    }
  },

  /**
   * 生命周期函数--监听页面卸载
   */
  onUnload() {
    const app = getApp();
    app.unregisterMessageCallback('__connect__', this._onConnectChange);
    app.unregisterMessageCallback(app.globalData.topicStatus, this._onDeviceStatus);
  },

  /**
   * onHide: 不做任何事 — MQTT 连接是全局的，不因切后台断开
   */
  onHide() {
    // 空实现：全局连接生命周期独立于页面
  },

  /**
   * onShow: 只同步连接状态，不再创建新连接
   */
  onShow() {
    const app = getApp();
    // 同步当前连接状态
    if (this.data.connected !== app.globalData.mqttConnected) {
      this.setData({ connected: app.globalData.mqttConnected });
    }
    // 同步设备状态（期间可能有变化）
    const state = app.globalData.ledState;
    this.setData({
      power: state.power,
      brightness: state.brightness,
      mode: state.mode,
      speed: state.speed
    });
  },

  // ── 消息回调（由 app.js 的事件分发器调用） ──

  /**
   * ArrayBuffer → 字符串（兼容真机与模拟器）
   */
  _arrayBufferToString(buf) {
    if (typeof TextDecoder !== 'undefined') {
      return new TextDecoder().decode(buf);
    }
    let str = '';
    const arr = new Uint8Array(buf);
    for (let i = 0; i < arr.length; i++) {
      str += String.fromCharCode(arr[i]);
    }
    return str;
  },

  /**
   * 连接状态变更回调
   * @param {{ connected: boolean }} data
   */
  _onConnectChange: function(data) {
    this.setData({ connected: data.connected });
  },

  /**
   * 设备状态上报回调
   * @param {Buffer|ArrayBuffer} payload — MQTT 原始消息体
   */
  _onDeviceStatus: function(payload) {
    try {
      const msg = JSON.parse(this._arrayBufferToString(payload));
      console.log('[MQTT] 收到状态:', msg);

      const update = {};
      if (msg.power !== undefined) update.power = msg.power;
      if (msg.brightness !== undefined) update.brightness = msg.brightness;
      if (msg.mode !== undefined) update.mode = msg.mode;
      if (msg.speed !== undefined) update.speed = msg.speed;

      if (Object.keys(update).length > 0) {
        this.setData(update);
        // 同步到 globalData
        const app = getApp();
        Object.assign(app.globalData.ledState, update);
      }
    } catch (e) {
      console.warn('[MQTT] 解析消息失败:', e);
    }
  },

  // ── 发送命令 ──────────────────────────────

  /**
   * 主动请求设备上报状态
   */
  requestStatus() {
    this.sendCommandRaw({ cmd: 'status' });
  },

  /**
   * 发送控制指令
   * 设备端协议：{"cmd": "<key>", "value": <value>}
   */
  sendCommand(key, value) {
    this.sendCommandRaw({ cmd: key, value: value });
  },

  /**
   * 将 JSON 对象发布到控制主题
   */
  sendCommandRaw(data) {
    const app = getApp();
    if (!app.globalData.mqttConnected) {
      wx.showToast({ title: '设备未连接', icon: 'none', duration: 2000 });
      return;
    }

    const payload = JSON.stringify(data);
    const client = app.globalData.mqttClient;
    if (client && client.connected) {
      client.publish(this.data.topicControl, payload, { qos: 1 }, (err) => {
        if (err) {
          console.error('[MQTT] 发送失败:', err);
          wx.showToast({ title: '发送失败', icon: 'none', duration: 2000 });
        } else {
          console.log('[MQTT] 已发送:', payload);
        }
      });
    }
  },

  // ── Vant UI 事件绑定 ───────────────────────

  /**
   * 电源开关切换
   * Vant van-switch 的 change 事件：e.detail 是 boolean
   */
  onPowerChange(e) {
    const val = e.detail ? 1 : 0;
    this.setData({ power: val });
    this.sendCommand('power', val);
  },

  /**
   * 工作模式切换
   * Vant van-radio-group 的 change 事件：e.detail 就是 name 值
   */
  onModeChange(e) {
    const mode = e.detail;
    this.setData({ mode });
    this.sendCommand('mode', mode);
  },

  /**
   * 亮度调节
   * Vant van-slider 的 change 事件：e.detail 是数字
   */
  onBrightnessChange(e) {
    const brightness = e.detail;
    this.setData({ brightness });
    this.sendCommand('brightness', brightness);
  },

  /**
   * 呼吸速度调节
   * Vant van-slider 的 change 事件：e.detail 是数字
   */
  onSpeedChange(e) {
    const speed = e.detail;
    this.setData({ speed });
    this.sendCommand('speed', speed);
  }
});
